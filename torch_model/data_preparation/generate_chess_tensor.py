import io
import json
import os
import pathlib
import pickle
import argparse
from typing import Iterator, List, Optional, Tuple

try:
    import yaml  # type: ignore
except Exception:  # pragma: no cover - handled at runtime if missing
    yaml = None

import chess
import chess.pgn
import numpy as np
from datasets import load_dataset


# Canonicalized board tensor: 16 channels.
#   0-5:   the mover's own P, R, N, B, Q, K (binary presence)
#   6-11:  the opponent's P, R, N, B, Q, K (binary presence)
#   12-13: the mover's own kingside / queenside castling rights (constant)
#   14-15: the opponent's kingside / queenside castling rights (constant)
# The board is always oriented as if the side to move were White: when
# Black is to move, both rank and file are mirrored (a 180 degree rotation)
# so a single model can be trained on - and used for - both colors. Row 0 is
# the mover's opponent's back rank, row 7 the mover's own back rank, before
# any such rotation; col 0 is file A, col 7 is file H.
NB_CHANNELS = 16
PIECE_TO_INDEX = {"P": 0, "R": 1, "N": 2, "B": 3, "Q": 4, "K": 5}
INDEX_TO_PIECE = {v: k for k, v in PIECE_TO_INDEX.items()}


def expand_path(path: str) -> str:
    """Expand '~' and return an absolute path."""
    return os.path.abspath(os.path.expanduser(path))


def load_config(config_path: str) -> dict:
    """Load configuration (YAML or JSON) from `config_path` based on file extension."""
    path = expand_path(config_path)
    suffix = pathlib.Path(path).suffix.lower()
    with open(path, "r", encoding="utf8") as f:
        if suffix in {".yaml", ".yml"}:
            if yaml is None:
                raise ImportError(
                    "PyYAML is required for YAML configs. Install 'pyyaml'."
                )
            return yaml.safe_load(f)
        # default to JSON
        return json.load(f)


def validate_config(cfg: dict) -> dict:
    """Validate required keys, types, and basic constraints.

    Raises ValueError with a helpful message on failure. Returns the config
    dict (possibly with expanded paths).
    """
    required_types = {
        "hf_dataset_name": str,
        "number_of_games": int,
        "min_elo": int,
        "allowed_terminations": list,
        "max_plies_per_game": (int, type(None)),
        "test_ratio": (int, float),
        "output_dir": str,
        "EXPORT_PICKLE": bool,
        "debug": bool,
    }

    for key, typ in required_types.items():
        if key not in cfg:
            raise ValueError(f"Missing config key '{key}'")
        if not isinstance(cfg[key], typ):
            expected = (
                ", ".join(t.__name__ for t in typ)
                if isinstance(typ, tuple)
                else typ.__name__
            )
            raise ValueError(
                f"Invalid type for '{key}': expected {expected}, got {type(cfg[key]).__name__}"
            )

    if cfg["number_of_games"] <= 0:
        raise ValueError("'number_of_games' must be > 0")
    if cfg["min_elo"] <= 0:
        raise ValueError("'min_elo' must be > 0")
    if cfg["max_plies_per_game"] is not None and cfg["max_plies_per_game"] <= 0:
        raise ValueError("'max_plies_per_game' must be > 0 or null")
    if not (0.0 <= cfg["test_ratio"] < 1.0):
        raise ValueError("'test_ratio' must be in [0, 1)")
    if not cfg["allowed_terminations"]:
        raise ValueError("'allowed_terminations' must be a non-empty list")

    cfg["output_dir"] = expand_path(cfg["output_dir"])
    return cfg


def square_to_canonical_index(square: int, mover_is_white: bool) -> int:
    """Map a python-chess square (0=a1 .. 63=h8) to a flat index in [0, 63]
    in the same row-major (row=file-independent rank order), canonicalized
    frame used by `board_to_array` - see its docstring for the convention."""
    file_idx = chess.square_file(square)
    rank_idx = chess.square_rank(square)
    row = 7 - rank_idx  # rank 8 -> row 0
    col = file_idx

    if not mover_is_white:
        row = 7 - row
        col = 7 - col

    return row * 8 + col


def board_to_array(board: chess.Board) -> np.ndarray:
    """Encode `board` as a (16, 8, 8) canonicalized tensor from the
    perspective of the side to move. See the module docstring above for the
    channel layout and orientation convention.
    """
    arr = np.zeros((NB_CHANNELS, 8, 8), dtype=np.float32)
    mover = board.turn
    opponent = not mover

    for square, piece in board.piece_map().items():
        row = 7 - chess.square_rank(square)
        col = chess.square_file(square)
        if mover == chess.BLACK:
            row = 7 - row
            col = 7 - col

        channel_offset = 0 if piece.color == mover else 6
        arr[channel_offset + PIECE_TO_INDEX[piece.symbol().upper()], row, col] = 1.0

    arr[12, :, :] = 1.0 if board.has_kingside_castling_rights(mover) else 0.0
    arr[13, :, :] = 1.0 if board.has_queenside_castling_rights(mover) else 0.0
    arr[14, :, :] = 1.0 if board.has_kingside_castling_rights(opponent) else 0.0
    arr[15, :, :] = 1.0 if board.has_queenside_castling_rights(opponent) else 0.0

    return arr


def array_to_board(arr: np.ndarray, mover_is_white: bool = True) -> chess.Board:
    """Debug helper: reconstruct an approximate board from the piece-placement
    channels (0-11) of a canonicalized tensor, for sanity-check printing only
    - castling rights and move history are not reconstructed."""
    board = chess.Board()
    board.clear()

    for row in range(8):
        for col in range(8):
            r, c = row, col
            if not mover_is_white:
                r, c = 7 - row, 7 - col
            square = chess.square(c, 7 - r)

            for ch in range(12):
                if arr[ch, row, col] != 0:
                    symbol = INDEX_TO_PIECE[ch % 6]
                    is_movers_piece = ch < 6
                    color = mover_is_white if is_movers_piece else (not mover_is_white)
                    if not color:
                        symbol = symbol.lower()
                    board.set_piece_at(square, chess.Piece.from_symbol(symbol))

    return board


def stream_filtered_games(
    hf_dataset_name: str,
    number_of_games: int,
    min_elo: int,
    allowed_terminations: List[str],
) -> Iterator[Tuple[str, int, int]]:
    """Stream rows from the Hugging Face dataset and yield
    (movetext, white_elo, black_elo) for games passing the Elo/termination
    filters, stopping once `number_of_games` have been yielded. The dataset
    is streamed, never downloaded in full.
    """
    allowed = set(allowed_terminations)
    ds = load_dataset(hf_dataset_name, split="train", streaming=True)

    yielded = 0
    for row in ds:
        if yielded >= number_of_games:
            break

        white_elo = row.get("WhiteElo")
        black_elo = row.get("BlackElo")
        termination = row.get("Termination")
        movetext = row.get("movetext")

        if white_elo is None or black_elo is None or not movetext:
            continue
        if termination not in allowed:
            continue
        if white_elo < min_elo or black_elo < min_elo:
            continue

        yielded += 1
        print(f"Collected {yielded}/{number_of_games} games matching filters", end="\r")
        yield movetext, int(white_elo), int(black_elo)

    print()


def generate_tensors(
    games: List[Tuple[str, int, int]],
    max_plies_per_game: Optional[int],
    debug: bool,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Parse each game's movetext and emit one training example per ply, for
    both White and Black moves.

    Returns:
        board_in_array: (N, 16, 8, 8)
        from_array: (N,) canonicalized "from" square index, 0-63
        to_array: (N,) canonicalized "to" square index, 0-63
    """
    board_in: List[np.ndarray] = []
    from_labels: List[int] = []
    to_labels: List[int] = []

    tot_moves = 0
    for game_idx, (movetext, white_elo, black_elo) in enumerate(games):
        try:
            game = chess.pgn.read_game(io.StringIO(movetext))
            if game is None:
                continue
        except Exception:  # noqa: BLE001 - skip any malformed game, data is external
            continue

        board = game.board()

        for ply, move in enumerate(game.mainline_moves()):
            mover_is_white = board.turn == chess.WHITE

            in_array = board_to_array(board)
            from_idx = square_to_canonical_index(move.from_square, mover_is_white)
            to_idx = square_to_canonical_index(move.to_square, mover_is_white)

            board_in.append(in_array)
            from_labels.append(from_idx)
            to_labels.append(to_idx)

            if debug:
                print("-----------------------------------------")
                print(board)
                print(f"mover: {'white' if mover_is_white else 'black'}, "
                      f"from={from_idx}, to={to_idx}")

            board.push(move)
            tot_moves += 1

            if max_plies_per_game is not None and ply + 1 >= max_plies_per_game:
                break

        print(
            f"processed game {game_idx + 1}, total positions so far: {len(board_in)}, "
            f"total moves: {tot_moves}.",
            end="\r",
        )

    print()

    board_in_array = np.array(board_in, dtype=np.float32)
    from_array = np.array(from_labels, dtype=np.int64)
    to_array = np.array(to_labels, dtype=np.int64)

    print(f"\nGenerated {board_in_array.shape[0]} positions from {len(games)} games "
          f"(both colors included).")

    return board_in_array, from_array, to_array


def save_tensor(
    output_dir: str,
    hf_dataset_name: str,
    number_of_games: int,
    min_elo: int,
    split_name: str,
    in_array: np.ndarray,
    from_array: np.ndarray,
    to_array: np.ndarray,
    ext: str = "chessarray",
) -> str:
    """Serialize one split to a pickle file and return the path."""
    dataset_tag = hf_dataset_name.split("/")[-1]
    filename = (
        f"{dataset_tag}_nbGames{number_of_games}_minElo{min_elo}_{split_name}.{ext}"
    )
    out_path = os.path.join(output_dir, filename)

    print(f"Saving {split_name} tensor data to {out_path}")
    os.makedirs(output_dir, exist_ok=True)
    with open(out_path, "wb") as fn:
        pickle.dump(
            {"in_array": in_array, "from_array": from_array, "to_array": to_array}, fn
        )
    print("saving done")
    return out_path


def main():
    parser = argparse.ArgumentParser(
        description="Generate chess tensors from a Hugging Face games dataset using a YAML/JSON config."
    )
    parser.add_argument(
        "--config", required=True, help="Path to YAML or JSON config file"
    )
    args = parser.parse_args()

    cfg = validate_config(load_config(args.config))

    hf_dataset_name = cfg["hf_dataset_name"]
    number_of_games = int(cfg["number_of_games"])
    min_elo = int(cfg["min_elo"])
    allowed_terminations = list(cfg["allowed_terminations"])
    max_plies_per_game: Optional[int] = (
        None if cfg.get("max_plies_per_game") is None else int(cfg["max_plies_per_game"])
    )
    test_ratio = float(cfg["test_ratio"])
    output_dir = cfg["output_dir"]
    EXPORT_PICKLE = bool(cfg["EXPORT_PICKLE"])
    debug = bool(cfg["debug"])

    games = list(
        stream_filtered_games(hf_dataset_name, number_of_games, min_elo, allowed_terminations)
    )
    if not games:
        raise RuntimeError(
            "No games matched the configured filters (min_elo / allowed_terminations); "
            "loosen the config and try again."
        )

    n_test_games = int(len(games) * test_ratio)
    n_train_games = len(games) - n_test_games
    train_games = games[:n_train_games]
    test_games = games[n_train_games:] if n_test_games > 0 else []

    print(f"Split: {len(train_games)} train games, {len(test_games)} test games")

    train_in, train_from, train_to = generate_tensors(train_games, max_plies_per_game, debug)

    if EXPORT_PICKLE:
        save_tensor(
            output_dir, hf_dataset_name, number_of_games, min_elo, "train",
            train_in, train_from, train_to,
        )

    if test_games:
        test_in, test_from, test_to = generate_tensors(test_games, max_plies_per_game, debug)
        if EXPORT_PICKLE:
            save_tensor(
                output_dir, hf_dataset_name, number_of_games, min_elo, "test",
                test_in, test_from, test_to,
            )

    if debug:
        nb_demo = min(5, train_in.shape[0])
        for i in range(nb_demo):
            print("-------------------------------------")
            print(f"Show demo position {i}/{nb_demo} (mover always shown as White):")
            print(array_to_board(train_in[i]))
            print(f"from={train_from[i]}, to={train_to[i]}")


if __name__ == "__main__":
    main()
