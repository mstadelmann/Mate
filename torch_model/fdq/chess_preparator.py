import os
import pickle
from typing import Any, Dict

import numpy as np
from torch.utils.data import DataLoader, Dataset, Subset


def find_game_starts(board_in_array: np.ndarray, chunk_size: int = 65536) -> np.ndarray:
    """(N,) bool mask of the positions that start a game, for .chessarray
    files written before generate_chess_tensor.py stored "game_array".

    Positions are stored game after game, ply by ply, and every game starts
    from the standard initial position - which is the dataset's very first
    row. A game that returns to the exact initial position mid-game (e.g.
    Nf3 Nf6 Ng1 Ng8) gets split in two there, which is harmless for a
    train/val split.
    """
    start = board_in_array[0].reshape(-1)
    flat = board_in_array.reshape(board_in_array.shape[0], -1)
    return np.concatenate(
        [np.all(flat[i : i + chunk_size] == start, axis=1) for i in range(0, flat.shape[0], chunk_size)]
    )


class ChessDataset(Dataset):
    """PyTorch dataset wrapping precomputed chess tensors from a pickle file.

    The pickle file is expected to contain three numpy arrays: "in_array"
    (N, 16, 8, 8) canonicalized board positions, and "from_array" / "to_array"
    (N,) integer square labels (0-63) for the move actually played from each
    position.
    """

    def __init__(self, pickle_path: str) -> None:
        with open(pickle_path, "rb") as fn:
            # trunk-ignore(bandit/B301)
            chess_tensor = pickle.load(fn)

        self.board_in_array = chess_tensor["in_array"]
        self.from_array = chess_tensor["from_array"]
        self.to_array = chess_tensor["to_array"]
        # (N,) index of the game each position comes from. Older
        # .chessarray files predate it - see find_game_starts().
        game_array = chess_tensor.get("game_array", None)
        if game_array is None:
            game_array = np.cumsum(find_game_starts(self.board_in_array)) - 1
        self.game_array = game_array

    def __len__(self) -> int:
        return self.board_in_array.shape[0]

    def __getitem__(self, i: int) -> Dict[str, np.ndarray]:
        return {
            "inputs": self.board_in_array[i, ...].astype(np.float32),
            "from_label": self.from_array[i].astype(np.int64),
            "to_label": self.to_array[i].astype(np.int64),
        }


def create_datasets(experiment, args) -> Dict[str, Any]:
    """Create train/validation/test dataloaders for the chess experiment.

    The ``experiment`` argument is kept for API compatibility with the fdq
    framework but is not used directly inside this function.
    """

    base_path = os.path.expanduser(args.base_path)

    train_set_all = ChessDataset(os.path.join(base_path, args.train_set))
    test_set = ChessDataset(os.path.join(base_path, args.test_set))

    # Hold out whole games (val_ratio of them) for validation, not random
    # positions: consecutive positions of one game are nearly identical, so
    # a position-level split would still leak. The validation games are
    # excluded from training - otherwise val_loss is just train loss and
    # the best_val checkpoint is simply the most memorized one. Fixed seed,
    # so every run (and resume) gets the same split.
    game_ids = np.unique(train_set_all.game_array)
    rng = np.random.default_rng(getattr(args, "val_split_seed", 0))
    n_val_games = int(len(game_ids) * args.val_ratio)
    val_games = rng.choice(game_ids, size=n_val_games, replace=False)
    is_val = np.isin(train_set_all.game_array, val_games)
    train_subset = Subset(train_set_all, np.flatnonzero(~is_val))
    val_subset = Subset(train_set_all, np.flatnonzero(is_val))
    n_train, n_val = len(train_subset), len(val_subset)
    print(f"Train/val split: {len(game_ids) - n_val_games}/{n_val_games} games, {n_train}/{n_val} positions")

    nb_ds_worker = getattr(args, "num_workers", 1)

    train_data_loader = DataLoader(
        train_subset,
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
        "n_train_samples": n_train,
        "n_val_samples": n_val,
        "n_test_samples": len(test_set),
        "n_train_batches": len(train_data_loader),
        "n_val_batches": len(val_data_loader) if val_data_loader is not None else 0,
        "n_test_batches": len(test_data_loader),
    }
