import io
import os
import pickle
import json
import pathlib
import argparse
from itertools import islice
from typing import Tuple, Optional

try:
    import yaml  # type: ignore
except Exception:  # pragma: no cover - handled at runtime if missing
    yaml = None

import chess
import chess.pgn
import numpy as np
import pandas as pd


BOARD_SIZE = (8, 8, 6)
PIECE_TO_INDEX = {"P": 0, "R": 1, "N": 2, "B": 3, "Q": 4, "K": 5}
INDEX_TO_PIECE = {0: "P", 1: "R", 2: "N", 3: "B", 4: "Q", 5: "K"}


# t,date,result,welo,belo,len,date_c,resu_c,welo_c,belo_c,edate_c,setup,fen,resu2_c,oyrange,bad_len,game
# --------------------------------------------------------------------------------------------------------
# Position of the game in the original PGN file.
# Date at which the game was played (the format is year.month.day).
# result: 1, 0 or -1 corresponding to white win, draw or loose
# ELO of withe player
# ELO of black player
# Number of moves in the game
# date_c = date is corrupted or missing? true = corrup!
# resu_c = result is corrupted or missing?
# welo_c = withe ELO is corrupted or missing?
# belo_c = black ELO is corrupted or missing?
# edate_c = event date is corrupted or missing?
# setup = setup_true or setup_false. If true then the game initial position is specified
# fen = fen_true and fen_false. It is related to column 12.
# In the original file the result is provided in two places. At the end of each sequence of moves and in the
# attributes part. This flag indicates if the result is (is not) properly provided after the sequence of
# moves (just for checking consistency in the PGN file).
# oyrange may be oyrange_true or oyrange_false. This flag is false only for games with dates in the range
# of years [1998,2007]. The oyrange means out of year range.
# bad_len  indicates, when blen_true (blen_false), if the length of the game is (is not) good.
# moves


def flatten_coord2d(coord2d: Tuple[int, int]) -> int:
    return (8 * coord2d[0]) + coord2d[1]


def array_to_board(in_array: np.ndarray) -> chess.Board:
    """Convert a (6, 8, 8) array to a chess.Board."""
    board = chess.Board()
    board.clear()

    in_array = in_array.transpose(1, 2, 0)

    for i in range(BOARD_SIZE[0]):
        for j in range(BOARD_SIZE[1]):
            index_piece = np.where(in_array[(i, j)] != 0)[0]
            new_coords = flatten_coord2d((7 - i, j))
            if index_piece.size:
                piece = INDEX_TO_PIECE[index_piece[0]]
                if in_array[(i, j, index_piece[0])] == -1:
                    piece = piece.lower()
                board.set_piece_at(new_coords, chess.Piece.from_symbol(piece))

    return board


def board_to_array(board: chess.Board) -> np.ndarray:
    """Convert a chess.Board to a (6, 8, 8) integer array."""
    im2d = np.array(list(str(board).replace("\n", "").replace(" ", ""))).reshape((8, 8))
    im = np.zeros(BOARD_SIZE)

    for i in range(BOARD_SIZE[0]):
        for j in range(BOARD_SIZE[1]):
            piece = im2d[i, j]
            if piece == ".":
                continue
            if piece.isupper():
                im[i, j, PIECE_TO_INDEX[piece.upper()]] = 1
            else:
                im[i, j, PIECE_TO_INDEX[piece.upper()]] = -1

    return im.transpose(2, 0, 1).astype(np.int32)


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

    Raises ValueError/FileNotFoundError with helpful messages on failure.
    Returns the config dict (possibly with expanded paths).
    """
    required_types = {
        "chess_db_base_path": str,
        "number_of_games": int,
        "stopAfterXMoves": (int, type(None)),
        "min_nb_moves": int,
        "minElo": int,
        "remove_duplicates": bool,
        "EXPORT_PICKLE": bool,
        "debug": bool,
    }

    for key, typ in required_types.items():
        if key not in cfg:
            raise ValueError(f"Missing config key '{key}'")
        if not isinstance(cfg[key], typ):
            # Render expected type(s) cleanly
            if isinstance(typ, tuple):
                expected = ", ".join(t.__name__ for t in typ)
            else:
                expected = typ.__name__
            raise ValueError(
                f"Invalid type for '{key}': expected {expected}, got {type(cfg[key]).__name__}"
            )

    if cfg["number_of_games"] <= 0:
        raise ValueError("'number_of_games' must be > 0")
    if cfg["min_nb_moves"] <= 0:
        raise ValueError("'min_nb_moves' must be > 0")
    if cfg["minElo"] <= 0:
        raise ValueError("'minElo' must be > 0")
    if cfg["stopAfterXMoves"] is not None and cfg["stopAfterXMoves"] <= 0:
        raise ValueError("'stopAfterXMoves' must be > 0 or null")

    # Validate DB path exists
    expanded_path = expand_path(cfg["chess_db_base_path"])
    if not os.path.exists(expanded_path):
        raise FileNotFoundError(f"chess_db_base_path not found: {expanded_path}")
    cfg["chess_db_base_path"] = expanded_path

    return cfg


def extract_head_lines(src_path: str, dst_path: str, n_lines: int) -> None:
    """Write the first n_lines from src_path into dst_path using pure Python."""
    src = expand_path(src_path)
    with (
        open(src, "r", encoding="utf8") as f_in,
        open(dst_path, "w", encoding="utf8") as f_out,
    ):
        for line in islice(f_in, n_lines):
            f_out.write(line)


def cleanChessDB(chess_db_base_path: str, chess_db_base_path_out: str) -> None:
    """Clean the raw DB into a CSV with headers and fields suitable for pandas."""
    with open(chess_db_base_path, "r", encoding="utf8") as f:
        with open(chess_db_base_path_out, "w", encoding="utf8") as fw:
            header = next(f)
            header = next(f)
            header = next(f)
            header = next(f)
            header = next(f)
            header = header.replace("# ", "").replace("...", "")
            headers_clean = [h.split(".")[1] for h in header.split(" ")]

            fw.write(f"{','.join(headers_clean)}\n")

            for i, line in enumerate(f):
                print(f"Cleaning line {i + 1}", end="\r")

                linefrag = line.split("###")
                a = linefrag[0].split(" ")
                a = [aa.replace(" ", "") for aa in a] + [linefrag[1]]
                if "" in a:
                    a.remove("")
                fw.write(",".join(a))

    print("\n\ndone cleaning.")


def load_and_filter_data(csv_path: str, min_nb_moves: int, minElo: int) -> pd.DataFrame:
    """Load cleaned CSV and apply all filters (valid ratings, result present, min moves, min ELO)."""
    print("Loading data.")
    data = pd.read_csv(csv_path, delimiter=",")
    print(f"Nb loaded games: {len(data)}")

    # find non-valid ratings
    data = data.loc[data["welo_c"] == "welo_false"]
    data = data.loc[data["belo_c"] == "belo_false"]

    data = data.astype({"welo": int, "belo": int})
    data = data.drop(["welo_c", "belo_c", "t"], axis=1)
    print(f"Nb games after removing those without rating: {len(data)}")

    # find games without result
    data = data.loc[data["resu_c"] == "result_false"]
    data = data.drop(["resu_c"], axis=1)
    print(f"Nb games after removing those without result: {len(data)}")

    # at least x moves
    data = data.loc[data["len"] >= min_nb_moves]
    print(
        f"Nb games after removing those without at least {min_nb_moves} moves: {len(data)}"
    )

    # at least minElo for both players
    data = data.loc[data["welo"] >= minElo]
    data = data.loc[data["belo"] >= minElo]
    print(f"Nb games after removing bad players: {len(data)}")

    return data


def generate_tensors(
    data: pd.DataFrame,
    stopAfterXMoves: Optional[int],
    debug: bool,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Iterate over games to produce input positions, move deltas, resulting boards, and ratings.

    Returns:
        board_in_array: (N, 6, 8, 8)
        board_out_array: (N, 1, 8, 8) move mask (-1 source, +1 destination; captures clipped)
        board_out_full_array: (N, 6, 8, 8) resulting board after the black move
        game_rating: (N,) black player's ELO
    """
    col = {True: "white", False: "black"}

    board_in = []
    board_out = []
    board_out_full = []
    game_rating = []

    tot_moves = 0
    nb_games = 0

    for i, pdgame in data.iterrows():
        nb_games += 1
        pgngame = pdgame["game"]

        if debug:
            print(pgngame)

        game = chess.pgn.read_game(io.StringIO(pgngame))
        board = game.board()

        posA = board_to_array(board)
        posB = board_to_array(board)

        for j, move in enumerate(game.mainline_moves()):
            if debug:
                print("-----------------------------------------")

            tot_moves += 1
            print(
                f"processing game NB {i}, move {j}, total games: {nb_games}, total moves: {tot_moves}.",
                end="\r",
            )

            board.push(move)

            posA = posB
            posB = board_to_array(board)

            if debug:
                print("\n")
                print(board)
                print(col[board.turn])

            # After pushing a black move, it's White's turn.
            if board.turn:
                board_in.append(posA)
                out_move = np.sum(posA - posB, axis=0)
                # Saturate to [-1, 1] to produce a clean move mask
                out_move = np.clip(out_move, -1, 1)

                board_out.append(out_move)
                game_rating.append(pdgame["belo"])  # black player's rating
                board_out_full.append(posB)

            if stopAfterXMoves is not None and j > stopAfterXMoves:
                break

    board_in_array = np.array(board_in)
    board_out_array = np.expand_dims(np.array(board_out), 1)
    board_out_full_array = np.array(board_out_full)
    game_rating = np.array(game_rating)

    print(
        f"\nGame array shape before filtering: {board_in_array.shape} (only black moves are considered)"
    )

    return board_in_array, board_out_array, board_out_full_array, game_rating


def dedupe_by_best_response(
    board_in_array: np.ndarray,
    board_out_array: np.ndarray,
    board_out_full_array: np.ndarray,
    game_rating: np.ndarray,
    debug: bool,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Remove duplicate input positions, keeping the black response favored by higher cumulative ELO.

    Returns filtered (board_in_array_filt, board_out_array_filt, board_out_full_array_filt).
    """
    board_in_array_filt, indices, inv_indices, counts = np.unique(
        board_in_array,
        axis=0,
        return_index=True,
        return_inverse=True,
        return_counts=True,
    )

    idx_label = []

    for i, idx in enumerate(indices):
        print(f"filtering duplicates {i} / {len(indices)}", end="\r")

        if counts[i] == 1:
            idx_label.append(idx)
        else:
            if debug:
                print("action")
                print(array_to_board(board_in_array[idx]))

            id_dup_in_orig = np.where(inv_indices == i)[0]

            _, id_out_ndp, id_inv_out_ndp = np.unique(
                board_out_full_array[id_dup_in_orig, ...],
                axis=0,
                return_index=True,
                return_inverse=True,
            )
            rating = np.zeros(len(id_out_ndp))
            for j, idback in enumerate(id_inv_out_ndp):
                rating[idback] += game_rating[id_dup_in_orig[j]]

            best_reactio_idx = id_dup_in_orig[id_out_ndp[np.argmax(rating)]]

            if debug:
                print("reaction")
                print(array_to_board(board_out_full_array[best_reactio_idx]))

            idx_label.append(best_reactio_idx)

    idx_label = np.array(idx_label)

    # Align inputs with selected representative indices (fixes ordering mismatch with np.unique)
    board_in_array_aligned = board_in_array[idx_label, ...]
    board_out_array_filt = board_out_array[idx_label, ...]
    board_out_full_array_filt = board_out_full_array[idx_label, ...]

    print("\nBoard array shape after duplicate filtering")
    print(board_out_array_filt.shape)

    return board_in_array_aligned, board_out_array_filt, board_out_full_array_filt


def save_tensor(
    chess_db_base_path: str,
    number_of_games: int,
    stopAfterXMoves: Optional[int],
    minElo: int,
    in_array: np.ndarray,
    out_array: np.ndarray,
    ext: str = "chesstensor",
) -> str:
    """Serialize the dataset to a pickle file and return the path."""
    chess_db_base_export = (
        f"{chess_db_base_path.split('.', maxsplit=1)[0]}_nbGames{number_of_games}"
        f"_stopAfter{stopAfterXMoves}_nbMoves{out_array.shape[0]}_minElo{minElo}.{ext}"
    )

    print(f"Saving tensor data to {chess_db_base_export}")
    with open(chess_db_base_export, "wb") as fn:
        pickle.dump({"in_array": in_array, "out_array": out_array}, fn)
    print("saving done")
    return chess_db_base_export


def main():
    parser = argparse.ArgumentParser(
        description="Generate chess tensors from PGN DB using YAML/JSON config."
    )
    parser.add_argument(
        "--config", required=True, help="Path to YAML or JSON config file"
    )
    args = parser.parse_args()

    cfg = validate_config(load_config(args.config))
    # -------------------------------------------------------------------------
    chess_db_base_path = cfg["chess_db_base_path"]
    number_of_games = int(cfg["number_of_games"])  # nb games to include (tot = 2.3mio)
    stopAfterXMoves: Optional[int] = (
        None
        if cfg.get("stopAfterXMoves") is None
        else int(cfg["stopAfterXMoves"])  # stop after x moves per game
    )
    min_nb_moves = int(cfg["min_nb_moves"])  # filter games with less moves
    minElo = int(cfg["minElo"])  # remove games with lower elo
    remove_duplicates = bool(
        cfg["remove_duplicates"]
    )  # keep only one response per duplicated position
    EXPORT_PICKLE = bool(cfg["EXPORT_PICKLE"])  # export file for later training
    debug = bool(cfg["debug"])  # extra logging
    # --------------------------------------------------------------------------

    base_path_expanded = expand_path(chess_db_base_path)
    processing_path = f"{base_path_expanded.split('.', maxsplit=1)[0]}_temp.txt"
    processing_path_clean = f"{base_path_expanded.split('.', maxsplit=1)[0]}_tempc.csv"

    # get lines from full file (include 5 header lines expected by cleanChessDB)
    extract_head_lines(base_path_expanded, processing_path, number_of_games + 5)

    # clean file
    cleanChessDB(processing_path, processing_path_clean)

    # load and filter
    data = load_and_filter_data(processing_path_clean, min_nb_moves, minElo)

    # tensors
    board_in_array, board_out_array, board_out_full_array, game_rating = (
        generate_tensors(data, stopAfterXMoves, debug)
    )

    # remove duplicates keeping best black reaction
    if remove_duplicates:
        board_in_array_filt, board_out_array, board_out_full_array = (
            dedupe_by_best_response(
                board_in_array,
                board_out_array,
                board_out_full_array,
                game_rating,
                debug,
            )
        )
    else:
        board_in_array_filt = board_in_array

    # (optional) demo prints
    if debug:
        nb_demo_moves = min(10, board_in_array_filt.shape[0])
        for i in range(nb_demo_moves):
            print("-------------------------------------")
            print(f"Show demo move {i}/{nb_demo_moves}:")
            print("\nwhite")
            print(array_to_board(board_in_array_filt[i]))
            print("\nblack response:")
            print(array_to_board(board_out_full_array[i]))

    # export
    if EXPORT_PICKLE:
        save_tensor(
            base_path_expanded,
            number_of_games,
            stopAfterXMoves,
            minElo,
            board_in_array_filt,
            board_out_array,
            ext="chessarray",
        )

    # cleanup
    try:
        os.remove(processing_path)
    except OSError:
        pass
    try:
        os.remove(processing_path_clean)
    except OSError:
        pass


if __name__ == "__main__":
    main()
