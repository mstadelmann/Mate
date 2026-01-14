// ML-based move integration for Mate chess engine
// Uses a PyTorch (libtorch) model to suggest a move based on the
// current board position.

#include "chess.h"

#include <torch/script.h>
#include <torch/torch.h>

#include <iostream>
#include <vector>

namespace
{
    // Global model instance, loaded on first use.
    torch::jit::script::Module g_ml_module;
    bool g_ml_loaded = false;

    // TODO: consider moving this to a config option
    const char *kDefaultModelPath =
        "/home/marc/dev/Mate/torch_model/trained_models/chessy_testmodel.vlfpt";

    bool load_ml_module_once()
    {
        if (g_ml_loaded)
        {
            return true;
        }

        try
        {
            g_ml_module = torch::jit::load(kDefaultModelPath);
            g_ml_loaded = true;
            std::cout << "[ML] Loaded model from: " << kDefaultModelPath << '\n';
        }
        catch (const c10::Error &e)
        {
            std::cerr << "[ML] Error loading model from " << kDefaultModelPath << "\n";
            std::cerr << e.what() << '\n';
            g_ml_loaded = false;
        }

        return g_ml_loaded;
    }

    // Convert the current board to a tensor of shape (1, 6, 8, 8).
    // Channels: 0=P, 1=R, 2=N, 3=B, 4=Q, 5=K.
    // Values: +1 for white pieces, -1 for black pieces.
    // Layout matches the training pipeline: tensor[0, c, row, col]
    // where row=0 is rank 8 and row=7 is rank 1, col=0 is file A.
    torch::Tensor board_to_tensor(chess &game)
    {
        auto tensor = torch::zeros({1, 6, 8, 8}, torch::dtype(torch::kFloat32));

        for (int fileIdx = 0; fileIdx < 8; ++fileIdx)
        {
            char fileChar = static_cast<char>('A' + fileIdx);
            for (int rank = 1; rank <= 8; ++rank)
            {
                boardCoordinateType coord{fileChar, rank};
                boardPositionType pos = game.query_position(coord);
                const pieceType &pc = pos.piece;

                if (pc.piece == pieceCode::empty || pc.color == playerColor::none)
                    continue;

                float sign = (pc.color == playerColor::white) ? 1.0f : -1.0f;
                int row = 8 - rank; // rank 8 -> row 0, rank 1 -> row 7
                int col = fileIdx;  // file A -> 0

                int channel = -1;
                switch (pc.piece)
                {
                case pieceCode::pawn:
                    channel = 0;
                    break;
                case pieceCode::rook:
                    channel = 1;
                    break;
                case pieceCode::knight:
                    channel = 2;
                    break;
                case pieceCode::bishop:
                    channel = 3;
                    break;
                case pieceCode::queen:
                    channel = 4;
                    break;
                case pieceCode::king:
                    channel = 5;
                    break;
                case pieceCode::empty:
                default:
                    channel = -1;
                    break;
                }

                if (channel >= 0)
                {
                    tensor.index_put_({0, channel, row, col}, sign);
                }
            }
        }

        return tensor;
    }

    // Map a flat index in [0, 63] to board coordinates (file, rank).
    // Index is assumed to be row-major over (row, col) where row=0 is rank 8.
    boardCoordinateType index_to_coord(int idx)
    {
        int row = idx / 8; // 0..7, 0 is top (rank 8)
        int col = idx % 8; // 0..7, 0 is file A
        char file = static_cast<char>('A' + col);
        int rank = 8 - row; // 8..1
        return {file, rank};
    }

    // Given the model's score tensor, try the best-scoring suggestions in
    // descending order until a legal move is found.
    bool scores_to_legal_move(chess &game, const torch::Tensor &scores, motionType &outMove)
    {
        // Flatten in case the model returns shape (1, 64) instead of (64,).
        torch::Tensor flat = scores.view(-1);
        if (flat.numel() != 64)
        {
            std::cerr << "[ML] Expected 64 outputs, got " << flat.numel() << '\n';
            return false;
        }

        // Sort indices for "from" (most negative first) and "to" (most positive first).
        auto from_order = torch::argsort(flat, /*dim=*/0, /*descending=*/false);
        auto to_order = torch::argsort(flat, /*dim=*/0, /*descending=*/true);

        motionVector legalMoves = game.findAllLegalMoves();

        for (int k = 0; k < flat.size(0); ++k)
        {
            int idx_min = from_order[k].item<int64_t>();
            int idx_max = to_order[k].item<int64_t>();

            boardCoordinateType fromCoord = index_to_coord(idx_min);
            boardCoordinateType toCoord = index_to_coord(idx_max);

            std::cout << "[ML] Candidate " << (k + 1) << ": "
                      << fromCoord.file << fromCoord.rank
                      << " -> " << toCoord.file << toCoord.rank << '\n';

            boardPositionType startPos = game.query_position(fromCoord);
            boardPositionType endPos = game.query_position(toCoord);

            for (const auto &mv : legalMoves)
            {
                if (mv.start_position.coord.file == startPos.coord.file &&
                    mv.start_position.coord.rank == startPos.coord.rank &&
                    mv.dest_position.coord.file == endPos.coord.file &&
                    mv.dest_position.coord.rank == endPos.coord.rank)
                {
                    outMove = mv; // preserve moveType (captures, promotions, etc.)
                    std::cout << "[ML] Chosen candidate " << (k + 1)
                              << " as legal move: "
                              << fromCoord.file << fromCoord.rank
                              << " -> " << toCoord.file << toCoord.rank << '\n';
                    return true;
                }
            }
        }

        std::cout << "[ML] No legal move found among model candidates." << std::endl;
        return false;
    }

} // namespace

bool chess::mlMove()
{
    if (!load_ml_module_once())
    {
        std::cout << "[ML] Could not load model; aborting ML move." << std::endl;
        return false;
    }

    torch::Tensor position_tensor = board_to_tensor(*this);
    std::vector<torch::jit::IValue> inputs;
    inputs.emplace_back(position_tensor);

    at::Tensor scores = g_ml_module.forward(inputs).toTensor();

    motionType mlMove;
    if (!scores_to_legal_move(*this, scores, mlMove))
    {
        return false;
    }

    mlMove.moved_by_whom = moved_by::ai;
    std::cout << "[ML] Executing move: "
              << mlMove.start_position.coord.file << mlMove.start_position.coord.rank
              << " -> "
              << mlMove.dest_position.coord.file << mlMove.dest_position.coord.rank
              << " (moved_by=" << movedByToString(mlMove.moved_by_whom) << ")" << std::endl;

    executeMove(mlMove);
    swapPlayers();

    return true;
}