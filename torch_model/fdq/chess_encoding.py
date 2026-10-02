"""Board/move encoding shared by the supervised and RL training pipelines,
and by the evaluator - kept dependency-free (no fdq import) so it can be
used standalone (e.g. by rl_self_play.py without needing fdq installed).

Mirrors torch_model/data_preparation/generate_chess_tensor.py - see that
module's docstring for the full channel/orientation convention. Duplicated
rather than imported across the two directories since data_preparation/ and
fdq/ are set up to be installed/run independently (see their separate
requirements.txt files).
"""

from typing import List, Tuple

import chess
import numpy as np

NB_CHANNELS = 16
PIECE_TO_INDEX = {"P": 0, "R": 1, "N": 2, "B": 3, "Q": 4, "K": 5}
INDEX_TO_PIECE = {v: k for k, v in PIECE_TO_INDEX.items()}


def board_to_array(board: chess.Board) -> np.ndarray:
    """Encode `board` as a (16, 8, 8) canonicalized tensor from the
    perspective of the side to move."""
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
    channels (0-11) of a canonicalized tensor - castling rights and move
    history are not reconstructed."""
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


def square_to_canonical_index(square: int, mover_is_white: bool) -> int:
    """Map a python-chess square (0=a1 .. 63=h8) to a flat index in [0, 63]
    in the same canonicalized (row, col) frame as board_to_array()."""
    row = 7 - chess.square_rank(square)
    col = chess.square_file(square)
    if not mover_is_white:
        row = 7 - row
        col = 7 - col
    return row * 8 + col


def canonical_index_to_coord(idx: int, mover_is_white: bool) -> str:
    """Inverse of square_to_canonical_index(): flat index -> algebraic square."""
    row, col = idx // 8, idx % 8
    if not mover_is_white:
        row, col = 7 - row, 7 - col
    return chess.square_name(chess.square(col, 7 - row))


def legal_move_candidates(board: chess.Board) -> List[Tuple[chess.Move, int, int]]:
    """Return one (move, from_idx, to_idx) triple per distinct (from, to)
    square pair among the board's legal moves, in the canonicalized index
    frame of the side to move.

    Several legal moves can share the same (from, to) squares - the four
    under-promotion choices of one pawn push/capture are the only case in
    standard chess - since this project's move representation (like the
    engine's own ML move integration, see src/chess_ML.cpp) does not model
    promotion choice separately, only one candidate per square pair is kept,
    preferring queen promotion (matching chess::applyMove's own default) if
    one of the duplicates is a queen promotion.
    """
    mover_is_white = board.turn == chess.WHITE
    by_squares: dict[Tuple[int, int], chess.Move] = {}

    for move in board.legal_moves:
        key = (
            square_to_canonical_index(move.from_square, mover_is_white),
            square_to_canonical_index(move.to_square, mover_is_white),
        )
        if key not in by_squares or move.promotion == chess.QUEEN:
            by_squares[key] = move

    return [(move, from_idx, to_idx) for (from_idx, to_idx), move in by_squares.items()]
