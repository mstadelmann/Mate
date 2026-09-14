from typing import Tuple

import chess
import numpy as np
import torch

from fdq.ui_functions import getIntInput

# Kept in sync with torch_model/data_preparation/generate_chess_tensor.py -
# see that module's docstring for the full channel/orientation convention.
NB_CHANNELS = 16
PIECE_TO_INDEX = {"P": 0, "R": 1, "N": 2, "B": 3, "Q": 4, "K": 5}
INDEX_TO_PIECE = {v: k for k, v in PIECE_TO_INDEX.items()}


def board_to_array(board: chess.Board) -> np.ndarray:
    """Encode `board` as a (16, 8, 8) canonicalized tensor from the
    perspective of the side to move (mirrors generate_chess_tensor.py)."""
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


def canonical_index_to_coord(idx: int, mover_is_white: bool) -> str:
    """Map a canonical flat index [0, 63] back to a real algebraic square,
    for readable printing only."""
    row, col = idx // 8, idx % 8
    if not mover_is_white:
        row, col = 7 - row, 7 - col
    return chess.square_name(chess.square(col, 7 - row))


def _evaluate_batch(experiment, batch, verbose: bool = False) -> Tuple[int, int]:
    """Evaluate a single (batch-size-1) example and return
    (correct_position, correct_move).

    - ``correct_move`` is 1 only if both the predicted from- and to-square
      exactly match the played move.
    - ``correct_position`` is 1 if at least one of the two squares matches.
    """

    model = experiment.models["chessCNN"]

    inputs = batch["inputs"]
    from_label = batch["from_label"]
    to_label = batch["to_label"]

    if verbose:
        print("------------------------------------------------")
        print("current state (mover always shown as White):")
        print(array_to_board(torch.squeeze(inputs, 0).numpy()))
        print(f"\nTarget: from={int(from_label.item())}, to={int(to_label.item())}")

    from_logits, to_logits = model(inputs.to(experiment.device))
    pred_from = torch.argmax(from_logits, dim=1).cpu()
    pred_to = torch.argmax(to_logits, dim=1).cpu()

    if verbose:
        print(f"Prediction: from={int(pred_from.item())}, to={int(pred_to.item())}")
        input("Press Enter to continue...")

    correct_from = pred_from == from_label
    correct_to = pred_to == to_label
    correct_move = int((correct_from & correct_to).item())
    correct_position = int((correct_from | correct_to).item())

    return correct_position, correct_move


def fdq_test(experiment):
    print(
        "Scoring metrics:\n"
        "- correct_move: predicted from- and to-square both match the played move.\n"
        "- correct_position: at least one of the two predicted squares matches.\n"
    )

    experiment.models["chessCNN"].eval()
    test_loader = experiment.data["CHESS"].test_data_loader

    accuracy = None

    if experiment.mode.op_mode.unittest or experiment.cfg.mode.run_test_auto:
        # no interactive for test experiments
        tmode = 1
    else:
        tmode = getIntInput(
            "\nSelect Testmode:\n1: Automatic with predefined data.\n2: Automatic with"
            " predefined data - verbose.\n3: Manual Test: Kings pawn E2-E4 (expect a"
            " sensible Black reply such as C5 or E5)",
            [1, 3],
        )

    if tmode in (1, 2):
        if tmode == 1:
            max_samples_to_print = 500000
        else:
            max_samples_to_print = getIntInput(
                "How many random samples do you want to show?\n", [1, 500000]
            )

        correct_positions = 0
        correct_moves = 0
        nb_evaluated_moved = 0

        for i, batch in enumerate(test_loader):
            if i + 1 > max_samples_to_print:
                print("done testing..")
                break

            nb_evaluated_moved += 1

            verbose = tmode == 2
            c_pos, c_move = _evaluate_batch(experiment, batch, verbose=verbose)
            correct_positions += c_pos
            correct_moves += c_move

            accuracy = correct_moves / nb_evaluated_moved

            print(
                f"analyzed moves: {nb_evaluated_moved}, correct moves: {correct_moves}, correct positions: {correct_positions} -> Accuracy: {accuracy:.4f}"
            )

        return accuracy

    if tmode == 3:
        board = chess.Board()
        board.push_san("e4")  # Black to move, mirrors the old manual smoke test

        infield = torch.from_numpy(board_to_array(board)).unsqueeze(0)

        model = experiment.models["chessCNN"]
        from_logits, to_logits = model(infield.to(experiment.device))

        top_from = torch.topk(from_logits.squeeze(0), k=3).indices.cpu().tolist()
        top_to = torch.topk(to_logits.squeeze(0), k=3).indices.cpu().tolist()

        print("------------------------------------------------")
        print("current state (mover always shown as White):")
        print(array_to_board(infield.squeeze(0).numpy()))

        print("\nTop-3 predicted from-squares:", [canonical_index_to_coord(i, False) for i in top_from])
        print("Top-3 predicted to-squares:", [canonical_index_to_coord(i, False) for i in top_to])

        input("Press Enter to continue...")

    return 1
