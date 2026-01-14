"""Training loop for the CHESS experiment using the fdq framework."""

import torch
from fdq.experiment import fdqExperiment
from fdq.ui_functions import startProgBar, iprint


def fdq_train(experiment: fdqExperiment) -> None:
    """Train the model using the provided experiment configuration.

    Args:
        experiment (fdqExperiment): The experiment object containing data loaders, models, and training configurations.
    """
    iprint("Default training")

    data = experiment.data["CHESS"]
    model = experiment.models["simpleNet"]

    # Determine the autocast device type from the experiment's device.
    device_type = getattr(getattr(experiment, "device", None), "type", "cpu")

    for epoch in range(experiment.start_epoch, experiment.nb_epochs):
        experiment.on_epoch_start(epoch=epoch)

        train_loss_sum = 0.0
        val_loss_sum = 0.0
        model.train()
        pbar = startProgBar(data.n_train_samples, "training...")

        for nb_batch, batch in enumerate(data.train_data_loader):
            pbar.update(nb_batch * experiment.cfg.data.CHESS.args.train_batch_size)

            inputs = batch["inputs"]
            targets = batch["targets"]
            inputs = inputs.to(experiment.device).type(torch.float32)
            targets = targets.to(experiment.device)

            with torch.autocast(device_type=device_type, enabled=experiment.useAMP):
                output = model(inputs)
                loss_tensor = (
                    experiment.losses["mse_loss"](output, targets)
                    / experiment.gradacc_iter
                )
                if experiment.useAMP and experiment.scaler is not None:
                    experiment.scaler.scale(loss_tensor).backward()
                else:
                    loss_tensor.backward()

            experiment.update_gradients(
                b_idx=nb_batch, loader_name="CHESS", model_name="simpleNet"
            )

            train_loss_sum += loss_tensor.detach().item()

        experiment.trainLoss = train_loss_sum / len(data.train_data_loader.dataset)
        pbar.finish()

        model.eval()
        pbar = startProgBar(data.n_val_samples, "validation...")

        for nb_batch, batch in enumerate(data.val_data_loader):
            pbar.update(nb_batch * experiment.cfg.data.CHESS.args.val_batch_size)

            inputs = batch["inputs"]
            targets = batch["targets"]

            with torch.no_grad():
                inputs = inputs.to(experiment.device)
                output = model(inputs)
                targets = targets.to(experiment.device)
                loss_tensor = experiment.losses["mse_loss"](output, targets)

            val_loss_sum += loss_tensor.detach().item()
        experiment.valLoss = val_loss_sum / len(data.val_data_loader.dataset)

        pbar.finish()

        experiment.on_epoch_end()

        if experiment.check_early_stop():
            break
