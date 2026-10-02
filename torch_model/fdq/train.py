"""Training loop for the CHESS experiment using the fdq framework.

Model-architecture-agnostic: `train.args.model_name` picks which entry of
`models:` to train (e.g. "chessCNN" for chess_cnn_p00.yaml, "chessFC" for
chess_fc_p00.yaml) - the loop itself only relies on the model returning
(from_logits, to_logits), not on any particular architecture.
"""

import torch
from fdq.experiment import fdqExperiment
from fdq.ui_functions import startProgBar, iprint

from rl_self_play import close_engines, make_val_opponents, play_val_games


def fdq_train(experiment: fdqExperiment) -> None:
    """Train the model using the provided experiment configuration.

    Args:
        experiment (fdqExperiment): The experiment object containing data loaders, models, and training configurations.
    """
    iprint("Default training")

    data = experiment.data["CHESS"]
    model_name = experiment.cfg.train.args.model_name
    model = experiment.models[model_name]

    # Determine the autocast device type from the experiment's device.
    device_type = getattr(getattr(experiment, "device", None), "type", "cpu")

    # Optional per-epoch games against fixed opponents (train.args.val),
    # played greedily like the RL pipeline's validation (see train_rl.py), so
    # wins/draws/losses are tracked for supervised models too - their
    # cross-entropy loss alone says nothing about actual playing strength.
    # train.args.val is either a single opponent config or a list of them;
    # each one's `log_name` (default: engine_command's basename) prefixes its
    # wandb keys, e.g. "stockfish_lv_0/val_win_rate" - see
    # make_val_opponents() in rl_self_play.py. select_best is ignored here:
    # valLoss stays the cross-entropy loss. Omit train.args.val (or set
    # nb_games: 0 on an entry) to skip.
    games_opponents, games_engines = make_val_opponents(
        experiment.cfg.train.args.get("val", None),
        nb_engines=experiment.cfg.train.args.get("nb_engines", None),
    )

    # Optional train.args.lr_plateau: halve (factor) the learning rate once
    # val_loss hasn't improved for `patience` epochs. Stepped here rather
    # than via fdq's models.<name>.lr_scheduler, since fdq calls
    # scheduler.step() without the val loss ReduceLROnPlateau needs. Not
    # saved in fdq checkpoints - a resumed run starts it afresh.
    plateau_cfg = experiment.cfg.train.args.get("lr_plateau", None)
    plateau_scheduler = None
    if plateau_cfg is not None:
        plateau_scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
            experiment.optimizers[model_name],
            factor=plateau_cfg.get("factor", 0.5),
            patience=plateau_cfg.get("patience", 5),
            min_lr=plateau_cfg.get("min_lr", 0.0),
        )

    try:
        for epoch in range(experiment.start_epoch, experiment.nb_epochs):
            experiment.on_epoch_start(epoch=epoch)

            train_loss_sum = 0.0
            val_loss_sum = 0.0
            model.train()
            pbar = startProgBar(data.n_train_samples, "training...")

            for nb_batch, batch in enumerate(data.train_data_loader):
                pbar.update(nb_batch * experiment.cfg.data.CHESS.args.train_batch_size)

                inputs = batch["inputs"].to(experiment.device).type(torch.float32)
                from_label = batch["from_label"].to(experiment.device)
                to_label = batch["to_label"].to(experiment.device)

                with torch.autocast(device_type=device_type, enabled=experiment.useAMP):
                    from_logits, to_logits = model(inputs)
                    loss_tensor = (
                        experiment.losses["ce_from"](from_logits, from_label)
                        + experiment.losses["ce_to"](to_logits, to_label)
                    ) / experiment.gradacc_iter
                    if experiment.useAMP and experiment.scaler is not None:
                        experiment.scaler.scale(loss_tensor).backward()
                    else:
                        loss_tensor.backward()

                experiment.update_gradients(
                    b_idx=nb_batch, loader_name="CHESS", model_name=model_name
                )

                train_loss_sum += loss_tensor.detach().item()

            experiment.trainLoss = train_loss_sum / len(data.train_data_loader.dataset)
            pbar.finish()

            model.eval()
            pbar = startProgBar(data.n_val_samples, "validation...")

            for nb_batch, batch in enumerate(data.val_data_loader):
                pbar.update(nb_batch * experiment.cfg.data.CHESS.args.val_batch_size)

                inputs = batch["inputs"]
                from_label = batch["from_label"]
                to_label = batch["to_label"]

                with torch.no_grad():
                    inputs = inputs.to(experiment.device)
                    from_logits, to_logits = model(inputs)
                    from_label = from_label.to(experiment.device)
                    to_label = to_label.to(experiment.device)
                    loss_tensor = experiment.losses["ce_from"](from_logits, from_label) + (
                        experiment.losses["ce_to"](to_logits, to_label)
                    )

                val_loss_sum += loss_tensor.detach().item()
            experiment.valLoss = val_loss_sum / len(data.val_data_loader.dataset)

            pbar.finish()

            # Logged to wandb/tensorboard by on_epoch_end() below (fdq adds
            # train_loss / val_loss / epoch itself).
            log_scalars = {"lr": experiment.optimizers[model_name].param_groups[0]["lr"]}
            if plateau_scheduler is not None:
                plateau_scheduler.step(experiment.valLoss)

            for opp in games_opponents:
                opp_scalars, summary = play_val_games(model, opp, experiment.device)
                log_scalars.update(opp_scalars)
                iprint(f"Epoch {epoch} games {summary}")

            experiment.on_epoch_end(log_scalars=log_scalars)

            if experiment.check_early_stop():
                break
    finally:
        close_engines(games_engines)
