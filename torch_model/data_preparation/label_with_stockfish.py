"""Label every position of a .chessarray dataset with Stockfish's best move
and evaluation, for supervised training on engine moves instead of human
ones (see torch_model/rl_training.md section 9).

Why: the human games (Lichess, Elo >= 1800) end in resignation long before
mate and are full of human mistakes, so a network imitating them never
learns tactics or how to deliver mate. Stockfish's choice in the same
positions is a much stronger, consistent teacher, and its evaluation gives
a value target ("how good is this position for the side to move").

Reads the "fen_array" that generate_chess_tensor.py stores (the
canonicalized board tensor alone drops side to move and en passant, so it
can't be turned back into an exact position). Writes a sidecar file next
to each dataset, `<name>.chessarray` -> `<name>.sflabels.npz`, holding
per-position arrays aligned with the dataset:

    from_array, to_array  (N,) int64  Stockfish's best move, canonicalized
                                      square indices (same frame as the
                                      human labels)
    cp_array              (N,) int32  evaluation in centipawns from the side
                                      to move's view, mates as +-10000
    value_array           (N,) float32 tanh(cp / 400), in [-1, 1]
    depth                 ()   int32  search depth used
    fen_sha1              ()   str    fingerprint of the dataset's fen_array,
                                      so labels of a since-regenerated
                                      dataset are detected (fen_fingerprint())

A dataset whose sidecar already has the same depth and fingerprint is
skipped, so rerunning (e.g. create_all.sh after regenerating identical
data) doesn't relabel for hours.

Positions are analysed in parallel (one Stockfish process per worker) and
in shards of --shard-size positions, saved under `<name>.sflabels.parts/`
as they finish - an interrupted run resumes where it stopped, and the
parts are merged and removed at the end.

Usage:
    python3 label_with_stockfish.py --config chess_tensor_config_10k.yaml
    python3 label_with_stockfish.py path/to/x_train.chessarray [...] --depth 8
"""

import argparse
import hashlib
import json
import math
import multiprocessing
import os
import pickle
import shutil
import time
from multiprocessing import util
from typing import List, Optional

import chess
import chess.engine
import numpy as np

from generate_chess_tensor import load_config, square_to_canonical_index, tensor_path, validate_config

MATE_CP = 10000
VALUE_SCALE_CP = 400.0

# Per-worker-process engine, opened by _init_worker().
_engine: Optional[chess.engine.SimpleEngine] = None
_limit: Optional[chess.engine.Limit] = None


def labels_path(chessarray_path: str) -> str:
    """Sidecar file of a dataset - also what chess_preparator.py loads."""
    return os.path.splitext(chessarray_path)[0] + ".sflabels.npz"


def fen_fingerprint(fen_array: np.ndarray) -> str:
    """SHA-1 of a dataset's fen_array - also checked by chess_preparator.py."""
    return hashlib.sha1(np.ascontiguousarray(fen_array).tobytes()).hexdigest()


def _init_worker(engine_command: str, depth: int, hash_mb: int) -> None:
    global _engine, _limit
    _engine = chess.engine.SimpleEngine.popen_uci(engine_command)
    _engine.configure({"Threads": 1, "Hash": hash_mb})
    _limit = chess.engine.Limit(depth=depth)
    # Quit Stockfish cleanly when the pool shuts this worker down.
    util.Finalize(None, _engine.quit, exitpriority=10)


def _label_fens(fens: List[bytes]) -> np.ndarray:
    """(len(fens), 3) int64 array of (from_idx, to_idx, cp) per FEN."""
    out = np.empty((len(fens), 3), dtype=np.int64)
    for i, fen in enumerate(fens):
        board = chess.Board(fen.decode())
        info = _engine.analyse(board, _limit)
        move = info["pv"][0] if info.get("pv") else _engine.play(board, _limit).move
        mover_is_white = board.turn == chess.WHITE
        cp = info["score"].pov(board.turn).score(mate_score=MATE_CP)
        out[i] = (
            square_to_canonical_index(move.from_square, mover_is_white),
            square_to_canonical_index(move.to_square, mover_is_white),
            max(-MATE_CP, min(MATE_CP, cp)),
        )
    return out


def label_file(
    chessarray_path: str,
    engine_command: str,
    depth: int,
    nb_workers: int,
    shard_size: int,
    hash_mb: int,
) -> str:
    out_path = labels_path(chessarray_path)
    print(f"\n== {chessarray_path}\n-> {out_path} (depth {depth}, {nb_workers} workers)")

    with open(chessarray_path, "rb") as fn:
        # trunk-ignore(bandit/B301)
        fen_array = pickle.load(fn).get("fen_array", None)
    if fen_array is None:
        raise ValueError(
            f"{chessarray_path} has no fen_array - it was generated before FENs were stored."
            " Regenerate it (create_all.sh / generate_chess_tensor.py) first."
        )
    nb_positions = len(fen_array)
    fingerprint = fen_fingerprint(fen_array)

    if os.path.exists(out_path):
        existing = np.load(out_path)
        if "fen_sha1" in existing and str(existing["fen_sha1"]) == fingerprint and int(existing["depth"]) == depth:
            print("already labelled at this depth - skipping")
            return out_path

    # Shards of an interrupted run are only reused for the same dataset.
    parts_dir = os.path.splitext(out_path)[0] + ".parts"
    meta_path = os.path.join(parts_dir, "meta.json")
    meta = {"fen_sha1": fingerprint, "nb_positions": nb_positions}
    if os.path.exists(parts_dir):
        old_meta = None
        if os.path.exists(meta_path):
            with open(meta_path) as fn:
                old_meta = fn.read()
        if old_meta != json.dumps(meta):
            print("discarding shards of a different (since regenerated) dataset")
            shutil.rmtree(parts_dir)
    os.makedirs(parts_dir, exist_ok=True)
    with open(meta_path, "w") as fn:
        fn.write(json.dumps(meta))
    nb_shards = math.ceil(nb_positions / shard_size)
    # Small tasks for load balancing - some positions take far longer than others.
    task_size = 200

    start = time.time()
    nb_done_this_run = 0
    with multiprocessing.Pool(nb_workers, initializer=_init_worker, initargs=(engine_command, depth, hash_mb)) as pool:
        for shard in range(nb_shards):
            part_path = os.path.join(parts_dir, f"part_{shard:05d}_d{depth}.npy")
            if os.path.exists(part_path):
                continue
            lo, hi = shard * shard_size, min((shard + 1) * shard_size, nb_positions)
            tasks = [fen_array[i : min(i + task_size, hi)].tolist() for i in range(lo, hi, task_size)]
            labels = np.concatenate(pool.map(_label_fens, tasks))
            np.save(part_path + ".tmp.npy", labels)
            os.replace(part_path + ".tmp.npy", part_path)  # never leave a half-written part

            nb_done_this_run += hi - lo
            elapsed = time.time() - start
            remaining = (nb_positions - hi) * elapsed / nb_done_this_run
            print(
                f"shard {shard + 1}/{nb_shards}: {hi}/{nb_positions} positions,"
                f" {nb_done_this_run / elapsed:.0f} pos/s, ~{remaining / 60:.0f} min left",
                flush=True,
            )
        # Let workers exit normally (running the engine-quit Finalize);
        # leaving the `with` block alone would terminate() them.
        pool.close()
        pool.join()

    parts = [os.path.join(parts_dir, f"part_{shard:05d}_d{depth}.npy") for shard in range(nb_shards)]
    labels = np.concatenate([np.load(p) for p in parts])
    assert len(labels) == nb_positions
    cp_array = labels[:, 2].astype(np.int32)
    np.savez(
        out_path,
        from_array=labels[:, 0],
        to_array=labels[:, 1],
        cp_array=cp_array,
        value_array=np.tanh(cp_array / VALUE_SCALE_CP).astype(np.float32),
        depth=np.int32(depth),
        fen_sha1=np.str_(fingerprint),
    )
    shutil.rmtree(parts_dir)
    print(f"saved {out_path}")
    return out_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", help=".chessarray files to label")
    parser.add_argument(
        "--config", help="label the train (and test) files of this generate_chess_tensor.py config instead"
    )
    parser.add_argument("--engine", default="/usr/games/stockfish", help="UCI engine binary")
    parser.add_argument("--depth", type=int, default=10, help="search depth per position (default 10)")
    parser.add_argument("--workers", type=int, default=os.cpu_count(), help="parallel engine processes")
    parser.add_argument("--shard-size", type=int, default=50000, help="positions per resumable shard")
    parser.add_argument("--hash-mb", type=int, default=16, help="Stockfish hash table size per process")
    args = parser.parse_args()

    files = list(args.files)
    if args.config:
        cfg = validate_config(load_config(args.config))
        for split in ("train", "test"):
            path = tensor_path(
                cfg["output_dir"], cfg["hf_dataset_name"], int(cfg["number_of_games"]), int(cfg["min_elo"]), split
            )
            if split == "train" or os.path.exists(path):
                files.append(path)
    if not files:
        parser.error("give .chessarray files or --config")

    for path in files:
        label_file(path, args.engine, args.depth, args.workers, args.shard_size, args.hash_mb)


if __name__ == "__main__":
    main()
