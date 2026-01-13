import os
import pickle

import numpy as np
from torch.utils.data import DataLoader, Dataset, random_split


class ChessDataset(Dataset):
    def __init__(self, pickle_path, flatten_input):
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

    def __len__(self):
        return self.board_in_array.shape[0]

    def __getitem__(self, i):
        return {
            "inputs": self.board_in_array[i, ...].astype(np.float32),
            "targets": self.board_out_array[i, ...].astype(np.float32),
        }


def create_datasets(experiment, args) -> dict:
    flatten_input = args.get("flatten", False)

    train_set_all = ChessDataset(
        os.path.join(args.base_path, args.train_set), flatten_input
    )
    test_set = ChessDataset(os.path.join(args.base_path, args.test_set), flatten_input)

    n_val = int(len(train_set_all) * args.val_ratio)
    n_train = len(train_set_all) - n_val
    _, val = random_split(train_set_all, [n_train, n_val])

    nb_ds_worker = args.get("num_workers", 1)

    # use everything to train, but only a small subset for val - assume that we have all moves, we want it to overfit.
    train_data_loader = DataLoader(
        train_set_all,
        batch_size=args.train_batch_size,
        shuffle=args.shuffle_train,
        num_workers=nb_ds_worker,
        pin_memory=args.pin_memory,
    )
    val_data_loader = DataLoader(
        val,
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
