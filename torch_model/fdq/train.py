"""Training loop for the CHESS experiment using the fdq framework.

Model-architecture-agnostic: `train.args.model_name` picks which entry of
`models:` to train (e.g. "chessCNN" for chess_cnn_p00.yaml, "chessFC" for
chess_fc_p00.yaml) - the loop itself only relies on the model returning
(from_logits, to_logits), not on any particular architecture.
"""

import torch
from fdq.experiment import fdqExperiment
from fdq.ui_functions import startProgBar, iprint

from rl_self_play import make_opponent, play_greedy_games


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

    # Optional per-epoch games against a fixed opponent (train.args.val),
    # played greedily like the RL pipeline's validation (see train_rl.py), so
    # wins/draws/losses are tracked for supervised models too - their
    # cross-entropy loss alone says nothing about actual playing strength.
    # Omit train.args.val (or set nb_games: 0) to skip.
    games_args = experiment.cfg.train.args.get("val", None) or {}
    games_nb: int = games_args.get("nb_games", 0)
    games_engine_command: str = games_args.get("engine_command", "random")
    games_max_plies: int = games_args.get("max_plies_per_game", 200)
    games_engine = None
    games_move_getter = None
    if games_nb > 0:
        games_move_getter, games_engine = make_opponent(
            games_engine_command,
            dict(games_args.get("engine_uci_options", {}) or {}),
            games_args.get("engine_movetime_ms", 50),
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
            log_scalars = {}
            if games_move_getter is not None:
                wins, draws, losses = play_greedy_games(
                    model=model,
                    opponent_move_getter=games_move_getter,
                    nb_games=games_nb,
                    device=experiment.device,
                    max_plies=games_max_plies,
                )
                log_scalars = {
                    "val_wins": wins,
                    "val_draws": draws,
                    "val_losses": losses,
                    "val_win_rate": wins / games_nb,
                    "val_draw_rate": draws / games_nb,
                    "val_loss_rate": losses / games_nb,
                }
                iprint(f"Epoch {epoch} games vs {games_engine_command}: {wins} wins, {draws} draws, {losses} losses")

            experiment.on_epoch_end(log_scalars=log_scalars)

            if experiment.check_early_stop():
                break
    finally:
        if games_engine is not None:
            games_engine.quit()
