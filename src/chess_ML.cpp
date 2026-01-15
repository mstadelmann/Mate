// ML-based move integration for Mate chess engine
// Uses an ONNX model to suggest a move based on the
// current board position.

#include "chess.h"
#include "config.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numeric>

namespace
{
    // ONNX Runtime environment and session, loaded on first use.
    Ort::Env g_ort_env{ORT_LOGGING_LEVEL_WARNING, "mate-ml"};
    std::unique_ptr<Ort::Session> g_ort_session;

    // Expand a path that may start with '~' to an absolute path using $HOME.
    std::string expand_tilde(const std::string &path)
    {
        if (!path.empty() && path[0] == '~')
        {
            const char *home = std::getenv("HOME");
            if (home && (path.size() == 1 || path[1] == '/'))
            {
                return std::string(home) + path.substr(1);
            }
        }
        return path;
    }

    bool load_onnx_session_once()
    {
        if (g_ort_session)
        {
            return true;
        }

        try
        {
            const std::string model_path = expand_tilde(ml_model_path);
            Ort::SessionOptions session_options;
            session_options.SetIntraOpNumThreads(1);
            // Use maximum graph optimization level provided by ONNX Runtime.
            session_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

            g_ort_session = std::make_unique<Ort::Session>(g_ort_env, model_path.c_str(), session_options);
            std::cout << "[ML] Loaded ONNX model from: " << model_path << '\n';
        }
        catch (const Ort::Exception &e)
        {
            const std::string model_path = expand_tilde(ml_model_path);
            std::cerr << "[ML] Error loading ONNX model from " << model_path << "\n";
            std::cerr << e.what() << '\n';
            g_ort_session.reset();
            return false;
        }

        return true;
    }

    // Convert the current board to a flat input buffer of shape (1, 6, 8, 8).
    // Channels: 0=P, 1=R, 2=N, 3=B, 4=Q, 5=K.
    // Values: +1 for white pieces, -1 for black pieces.
    // Layout matches the training pipeline: input[0, c, row, col]
    // where row=0 is rank 8 and row=7 is rank 1, col=0 is file A.
    std::array<float, 6 * 8 * 8> board_to_input(chess &game)
    {
        std::array<float, 6 * 8 * 8> input{};
        input.fill(0.0f);

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
                    const int idx = channel * 64 + row * 8 + col;
                    input[static_cast<size_t>(idx)] = sign;
                }
            }
        }

        return input;
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

    // Run the ONNX model and fill scores[64] with output logits.
    // NOTE: The exported model was trained with a fixed batch size of 256
    // and expects input of shape (256, 6, 8, 8). We replicate the current
    // board 256 times and only use the first prediction.
    bool run_onnx(const std::array<float, 6 * 8 * 8> &input, std::array<float, 64> &scores)
    {
        if (!g_ort_session)
        {
            return false;
        }

        Ort::AllocatorWithDefaultOptions allocator;

        // Assume single input and single output.
        auto input_name = g_ort_session->GetInputNameAllocated(0, allocator);
        auto output_name = g_ort_session->GetOutputNameAllocated(0, allocator);

        constexpr int64_t kBatchSize = 256;
        std::array<int64_t, 4> input_shape{kBatchSize, 6, 8, 8};
        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);

        // Create a batched input by repeating the single-board input
        // kBatchSize times to match the model's fixed batch dimension.
        std::vector<float> input_batched(static_cast<size_t>(kBatchSize) * input.size());
        for (int64_t b = 0; b < kBatchSize; ++b)
        {
            std::copy(input.begin(), input.end(),
                      input_batched.begin() + static_cast<size_t>(b) * input.size());
        }

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            mem_info,
            input_batched.data(),
            static_cast<size_t>(input_batched.size()),
            input_shape.data(),
            input_shape.size());

        const char *input_names[] = {input_name.get()};
        const char *output_names[] = {output_name.get()};

        auto output_tensors = g_ort_session->Run(
            Ort::RunOptions{nullptr},
            input_names,
            &input_tensor,
            1,
            output_names,
            1);

        if (output_tensors.empty() || !output_tensors[0].IsTensor())
        {
            std::cerr << "[ML] ONNX model did not return a tensor output." << std::endl;
            return false;
        }

        float *out_data = output_tensors[0].GetTensorMutableData<float>();
        // Assume shape (1, 64) or (64,).
        for (size_t i = 0; i < scores.size(); ++i)
        {
            scores[i] = out_data[i];
        }

        return true;
    }

    // Given the model's score vector, try the best-scoring suggestions in
    // descending order until a legal move is found.
    bool scores_to_legal_move(chess &game, const std::array<float, 64> &scores, motionType &outMove)
    {
        // Build index arrays 0..63 and sort them by score.
        std::array<int, 64> from_order{};
        std::array<int, 64> to_order{};
        std::iota(from_order.begin(), from_order.end(), 0);
        std::iota(to_order.begin(), to_order.end(), 0);

        std::sort(from_order.begin(), from_order.end(), [&](int a, int b)
                  { return scores[static_cast<size_t>(a)] < scores[static_cast<size_t>(b)]; });
        std::sort(to_order.begin(), to_order.end(), [&](int a, int b)
                  { return scores[static_cast<size_t>(a)] > scores[static_cast<size_t>(b)]; });

        motionVector legalMoves = game.findAllLegalMoves();

        for (std::size_t k = 0; k < from_order.size(); ++k)
        {
            int idx_min = from_order[k];
            int idx_max = to_order[k];

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
    // ML-based move generation is currently only supported for black.
    if (current_player != playerColor::black)
    {
        std::cout << "[ML] ML moves are only supported for the black player (current: "
                  << current_player_string() << ")." << std::endl;
        return false;
    }

    if (!load_onnx_session_once())
    {
        std::cout << "[ML] Could not load ONNX model; aborting ML move." << std::endl;
        return false;
    }

    auto input = board_to_input(*this);
    std::array<float, 64> scores{};
    if (!run_onnx(input, scores))
    {
        std::cout << "[ML] ONNX inference failed; aborting ML move." << std::endl;
        return false;
    }

    motionType mlMove;
    if (!scores_to_legal_move(*this, scores, mlMove))
    {
        return false;
    }

    mlMove.moved_by_whom = moved_by::ai;
    std::cout << "[ML] Executing move: "
              << mlMove.start_position.coord.file << mlMove.start_position.coord.rank
              << " -> "
              << mlMove.dest_position.coord.file << mlMove.dest_position.coord.rank << std::endl;

    executeMove(mlMove);
    swapPlayers();

    return true;
}