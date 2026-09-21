from typing import Tuple

import chess
import torch

from fdq.ui_functions import getIntInput

from chess_encoding import array_to_board, board_to_array, canonical_index_to_coord


def _evaluate_batch(experiment, model_name: str, batch, verbose: bool = False) -> Tuple[int, int]:
    """Evaluate a single (batch-size-1) example and return
    (correct_position, correct_move).

    - ``correct_move`` is 1 only if both the predicted from- and to-square
      exactly match the played move.
    - ``correct_position`` is 1 if at least one of the two squares matches.
    """

    model = experiment.models[model_name]

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

    # Looked up rather than hardcoded, since this evaluator is shared by both
    # the supervised config (model key "chessCNN") and the RL one ("chessRL")
    # - each config defines exactly one model, so its name is whatever key
    # happens to be there.
    model_name = next(iter(experiment.models))
    experiment.models[model_name].eval()
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
            c_pos, c_move = _evaluate_batch(experiment, model_name, batch, verbose=verbose)
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

        model = experiment.models[model_name]
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
