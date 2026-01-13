import sys
import numpy as np
import torch
import chess
from typing import Tuple

from fdq.ui_functions import getIntInput

BOARD_SIZE = (8, 8, 6)
PIECE_TO_INDEX = {"P": 0, "R": 1, "N": 2, "B": 3, "Q": 4, "K": 5}
INDEX_TO_PIECE = {0: "P", 1: "R", 2: "N", 3: "B", 4: "Q", 5: "K"}


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


def fdq_test(experiment):
    experiment.models["simpleNet"].eval()
    test_loader = experiment.data["CHESS"].test_data_loader

    accuracy = None

    if experiment.mode.op_mode.unittest or experiment.cfg.mode.run_test_auto:
        # no interactive for test experiments
        tmode = 1

    else:
        tmode = getIntInput(
            "\nSelect Testmode:\n1: Automatic with predefined data.\n2: Automatic with"
            " predefined data - verbose.\n3: kings pawn E4 (expect C5 or E5 response)",
            [1, 3],
        )

    if tmode == 1:
        max_samples_to_print = (
            500000  # getIntInput("How many random samples do you want to show?\n")
        )

        correct_positions = 0
        correct_moves = 0

        nb_evaluated_moved = 0

        for i, batch in enumerate(test_loader):
            if i + 1 > max_samples_to_print:
                print("done testing..")
                sys.exit()

            nb_evaluated_moved += 1

            # print("--------------------------------------")

            inputs = batch["inputs"]
            targets = batch["targets"]

            # print(array_to_board(torch.squeeze(inputs).numpy()))

            pred = experiment.models["simpleNet"](inputs.to(experiment.device))

            pred_minmax = torch.zeros(64)
            pred_minmax[torch.argmin(pred)] = -1
            pred_minmax[torch.argmax(pred)] = 1

            if (
                torch.max(targets - pred_minmax) == 0
                or torch.min(targets - pred_minmax) == 0
            ):
                correct_positions += 1

            if (
                torch.max(targets - pred_minmax) == 0
                and torch.min(targets - pred_minmax) == 0
            ):
                correct_moves += 1
            else:
                pass
                # createSubplots([pred.view(8,8).detach(),F.softmax(pred,dim=1).view(8,8).detach()
                # ,targets.view(8,8).detach(),pred_minmax.view(8,8).detach()])

            print(
                f"correct moves: {correct_moves}/{i + 1}, correct positions: {correct_positions}"
            )

            accuracy = correct_moves / nb_evaluated_moved

        return accuracy

    if tmode == 2:
        max_samples_to_print = (
            500000  # getIntInput("How many random samples do you want to show?\n")
        )

        correct_positions = 0
        correct_moves = 0

        nb_evaluated_moved = 0

        for i, batch in enumerate(test_loader):
            if i + 1 > max_samples_to_print:
                print("done testing..")
                sys.exit()

            nb_evaluated_moved += 1

            inputs = batch["inputs"]
            targets = batch["targets"]

            print("------------------------------------------------")
            print("current state:")
            print(array_to_board(torch.squeeze(inputs).numpy()))

            print("\nTarget:")
            print(torch.squeeze(targets).reshape(8, 8).numpy())

            pred = experiment.networkModel(inputs.to(experiment.device))
            pred_minmax = torch.zeros(64)
            pred_minmax[torch.argmin(pred)] = -1
            pred_minmax[torch.argmax(pred)] = 1

            print("\nPrediction:")
            print(torch.squeeze(pred_minmax).reshape(8, 8).numpy())

            input("Press Enter to continue...")

            if (
                torch.max(targets - pred_minmax) == 0
                or torch.min(targets - pred_minmax) == 0
            ):
                correct_positions += 1

            if (
                torch.max(targets - pred_minmax) == 0
                and torch.min(targets - pred_minmax) == 0
            ):
                correct_moves += 1
            else:
                pass
                # createSubplots([pred.view(8,8).detach(),F.softmax(pred,dim=1).view(8,8).detach(),
                # targets.view(8,8).detach(),pred_minmax.view(8,8).detach()])

            print(
                f"correct moves: {correct_moves}/{i + 1}, correct positions: {correct_positions}"
            )

            accuracy = correct_moves / nb_evaluated_moved

        return accuracy

    if tmode == 3:
        # empty field
        infield = torch.zeros(1, 6, 8, 8)
        infield[:, 0, 1, :] = -1  # black pawns
        infield[:, 0, -2, :] = 1  # white pawns

        infield[:, 1, 0, 0] = -1  # black rock
        infield[:, 1, 0, -1] = -1  # black rock
        infield[:, 1, -1, 0] = 1  # white rock
        infield[:, 1, -1, -1] = 1  # white rock

        infield[:, 2, 0, 1] = -1  # black knight
        infield[:, 2, 0, -2] = -1  # black knight
        infield[:, 2, -1, 1] = 1  # white knight
        infield[:, 2, -1, -2] = 1  # white knight

        infield[:, 3, 0, 2] = -1  # black bishop
        infield[:, 3, 0, -3] = -1  # black bishop
        infield[:, 3, -1, 2] = 1  # white bishop
        infield[:, 3, -1, -3] = 1  # white bishop

        infield[:, 4, 0, 3] = -1  # black queen
        infield[:, 5, 0, -4] = -1  # black king
        infield[:, 4, -1, 3] = 1  # white queen
        infield[:, 5, -1, -4] = 1  # white king

        # E2 -> E4
        infield[:, 0, -2, 4] = 0  # white pawns
        infield[:, 0, -4, 4] = 1  # white pawns

        pred = experiment.networkModel(infield.to(experiment.device))

        pred_minmax = torch.zeros(64)
        pred_minmax[torch.argmin(pred)] = -1
        pred_minmax[torch.argmax(pred)] = 1

        print("------------------------------------------------")
        print("current state:")
        print(array_to_board(torch.squeeze(infield).numpy()))

        print("\nPrediction:")
        print(torch.squeeze(pred_minmax).reshape(8, 8).numpy())

        input("Press Enter to continue...")

        print("raw prediction")
        print(pred.reshape(8, 8))
        print("minmax prediction")
        print(pred_minmax.reshape(8, 8))

    return 1
