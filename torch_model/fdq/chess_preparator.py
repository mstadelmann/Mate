import os
import pickle
from typing import Any, Dict

import numpy as np
from torch.utils.data import DataLoader, Dataset, random_split


class ChessDataset(Dataset):
    """PyTorch dataset wrapping precomputed chess tensors from a pickle file.

    The pickle file is expected to contain two numpy arrays with the keys
    "in_array" and "out_array".
    """

    def __init__(self, pickle_path: str, flatten_input: bool) -> None:
        with open(pickle_path, "rb") as fn:
            # trunk-ignore(bandit/B301)
            chess_tensor = pickle.load(fn)

        self.board_in_array = chess_tensor["in_array"]
        if flatten_input:
            nb_samples = chess_tensor["in_array"].shape[0]
            self.board_out_array = np.reshape(
                chess_tensor["out_array"], (nb_samples, 64)
            )
        else:
            self.board_out_array = chess_tensor["out_array"]

    def __len__(self) -> int:
        return self.board_in_array.shape[0]

    def __getitem__(self, i: int) -> Dict[str, np.ndarray]:
        return {
            "inputs": self.board_in_array[i, ...].astype(np.float32),
            "targets": self.board_out_array[i, ...].astype(np.float32),
        }


def create_datasets(experiment, args) -> Dict[str, Any]:
    """Create train/validation/test dataloaders for the chess experiment.

    The ``experiment`` argument is kept for API compatibility with the fdq
    framework but is not used directly inside this function.
    """

    # ``args`` can be a config object or a mapping; prefer attribute access
    # and fall back to a sensible default when missing.
    flatten_input = getattr(args, "flatten", False)

    base_path = os.path.expanduser(args.base_path)

    train_set_all = ChessDataset(os.path.join(base_path, args.train_set), flatten_input)
    test_set = ChessDataset(os.path.join(base_path, args.test_set), flatten_input)

    n_val = int(len(train_set_all) * args.val_ratio)
    n_train = len(train_set_all) - n_val
    _, val_subset = random_split(train_set_all, [n_train, n_val])

    nb_ds_worker = getattr(args, "num_workers", 1)

    # use everything to train, but only a small subset for val - assume that we have all moves, we want it to overfit.
    train_data_loader = DataLoader(
        train_set_all,
        batch_size=args.train_batch_size,
        shuffle=args.shuffle_train,
        num_workers=nb_ds_worker,
        pin_memory=args.pin_memory,
    )
    val_data_loader = DataLoader(
        val_subset,
        batch_size=args.val_batch_size,
        shuffle=args.shuffle_val,
        num_workers=nb_ds_worker,
        pin_memory=args.pin_memory,
    )

    test_data_loader = DataLoader(
        test_set,
        batch_size=args.test_batch_size,
        shuffle=args.shuffle_test,
        num_workers=nb_ds_worker,
        pin_memory=args.pin_memory,
    )

    return {
        "train_data_loader": train_data_loader,
        "val_data_loader": val_data_loader,
        "test_data_loader": test_data_loader,
        "n_train_samples": len(train_set_all),
        "n_val_samples": n_val,
        "n_test_samples": len(test_set),
        "n_train_batches": len(train_data_loader),
        "n_val_batches": len(val_data_loader) if val_data_loader is not None else 0,
        "n_test_batches": len(test_data_loader),
    }
