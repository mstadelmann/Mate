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
#include <vector>

namespace
{
    // ONNX Runtime environment and sessions, loaded on first use - one
    // independent session per model slot, so model A and model B (config.h's
    // model_a_path / model_b_path) can be loaded and run side by side, e.g.
    // to have two models play each other or split by game phase.
    Ort::Env g_ort_env{ORT_LOGGING_LEVEL_WARNING, "mate-ml"};
    std::array<std::unique_ptr<Ort::Session>, 2> g_ort_sessions;

    char slot_label(MLModelSlot slot)
    {
        return slot == MLModelSlot::A ? 'A' : 'B';
    }

    const std::string &configured_model_path(MLModelSlot slot)
    {
        return slot == MLModelSlot::A ? model_a_path : model_b_path;
    }

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

    bool load_onnx_session_once(MLModelSlot slot)
    {
        std::unique_ptr<Ort::Session> &session = g_ort_sessions[static_cast<std::size_t>(slot)];
        if (session)
        {
            return true;
        }

        const std::string model_path = expand_tilde(configured_model_path(slot));
        if (model_path.empty())
        {
            std::cerr << "[ML] No model path configured for model " << slot_label(slot) << "." << std::endl;
            return false;
        }

        try
        {
            Ort::SessionOptions session_options;
            session_options.SetIntraOpNumThreads(1);
            // Use maximum graph optimization level provided by ONNX Runtime.
            session_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

            session = std::make_unique<Ort::Session>(g_ort_env, model_path.c_str(), session_options);
            std::cout << "[ML] Loaded ONNX model " << slot_label(slot) << " from: " << model_path << '\n';
        }
        catch (const Ort::Exception &e)
        {
            std::cerr << "[ML] Error loading ONNX model " << slot_label(slot) << " from " << model_path << "\n";
            std::cerr << e.what() << '\n';
            session.reset();
            return false;
        }

        return true;
    }

    constexpr int kNbInputChannels = 16;

    // Convert the current board to a flat input buffer of shape
    // (1, 16, 8, 8), canonicalized so the side to move is always encoded as
    // though it were White: when it is Black's turn, the board is rotated
    // 180 degrees (both rank and file mirrored) and "mine"/"theirs" replace
    // "white"/"black". This lets a single model play both colors instead of
    // only the one color it happened to be trained on.
    //
    // Channels 0-5:   the mover's own P, R, N, B, Q, K (binary presence).
    // Channels 6-11:  the opponent's P, R, N, B, Q, K (binary presence).
    // Channels 12-13: the mover's own kingside / queenside castling rights
    //                 (constant-value planes: 1.0 if available, else 0.0).
    // Channels 14-15: the opponent's kingside / queenside castling rights.
    //
    // Layout matches the training pipeline (see
    // torch_model/data_preparation/generate_chess_tensor.py):
    // input[0, c, row, col] where, before any canonicalization rotation,
    // row=0 is rank 8 and row=7 is rank 1, col=0 is file A and col=7 is
    // file H.
    std::array<float, kNbInputChannels * 8 * 8> board_to_input(chess &game)
    {
        std::array<float, kNbInputChannels * 8 * 8> input{};
        input.fill(0.0f);

        const playerColor mover = game.current_player_color();
        const playerColor opponent = (mover == playerColor::white) ? playerColor::black : playerColor::white;
        const bool mover_is_black = mover == playerColor::black;

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

                int row = 8 - rank; // rank 8 -> row 0, rank 1 -> row 7
                int col = fileIdx;  // file A -> 0

                if (mover_is_black)
                {
                    row = 7 - row;
                    col = 7 - col;
                }

                int pieceChannel = -1;
                switch (pc.piece)
                {
                case pieceCode::pawn:
                    pieceChannel = 0;
                    break;
                case pieceCode::rook:
                    pieceChannel = 1;
                    break;
                case pieceCode::knight:
                    pieceChannel = 2;
                    break;
                case pieceCode::bishop:
                    pieceChannel = 3;
                    break;
                case pieceCode::queen:
                    pieceChannel = 4;
                    break;
                case pieceCode::king:
                    pieceChannel = 5;
                    break;
                case pieceCode::empty:
                default:
                    pieceChannel = -1;
                    break;
                }

                if (pieceChannel >= 0)
                {
                    const int channelOffset = (pc.color == mover) ? 0 : 6;
                    const int idx = (channelOffset + pieceChannel) * 64 + row * 8 + col;
                    input[static_cast<size_t>(idx)] = 1.0f;
                }
            }
        }

        const float moverKs = game.can_castle_kingside(mover) ? 1.0f : 0.0f;
        const float moverQs = game.can_castle_queenside(mover) ? 1.0f : 0.0f;
        const float oppKs = game.can_castle_kingside(opponent) ? 1.0f : 0.0f;
        const float oppQs = game.can_castle_queenside(opponent) ? 1.0f : 0.0f;
        for (int sq = 0; sq < 64; ++sq)
        {
            input[static_cast<size_t>(12 * 64 + sq)] = moverKs;
            input[static_cast<size_t>(13 * 64 + sq)] = moverQs;
            input[static_cast<size_t>(14 * 64 + sq)] = oppKs;
            input[static_cast<size_t>(15 * 64 + sq)] = oppQs;
        }

        return input;
    }

    // Map a flat index in [0, 63] back to real board coordinates, undoing
    // the 180-degree canonicalization rotation applied in board_to_input()
    // when `mover` is black. Index is row-major over (row, col) in the
    // canonical (post-rotation) frame - see board_to_input() above.
    boardCoordinateType canonical_index_to_coord(int idx, playerColor mover)
    {
        int row = idx / 8; // 0..7
        int col = idx % 8; // 0..7

        if (mover == playerColor::black)
        {
            row = 7 - row;
            col = 7 - col;
        }

        char file = static_cast<char>('A' + col);
        int rank = 8 - row; // row=0 -> rank 8
        return {file, rank};
    }

    // Run the ONNX model and fill from_scores[64]/to_scores[64] with the two
    // policy heads' logits. The model takes a single (1, 16, 8, 8) input -
    // no fixed-batch replication needed - and returns two outputs in order:
    // from_logits, then to_logits (see chess_cnn.py's ChessCNN.forward()).
    // This ONNX input/output contract is deliberately independent of how the
    // model was trained (supervised or otherwise), so any model exposing it
    // works here.
    bool run_onnx(const std::array<float, kNbInputChannels * 8 * 8> &input,
                  MLModelSlot slot,
                  std::array<float, 64> &from_scores,
                  std::array<float, 64> &to_scores)
    {
        std::unique_ptr<Ort::Session> &session = g_ort_sessions[static_cast<std::size_t>(slot)];
        if (!session)
        {
            return false;
        }

        try
        {
            Ort::AllocatorWithDefaultOptions allocator;

            if (session->GetOutputCount() < 2)
            {
                std::cerr << "[ML] ONNX model exposes " << session->GetOutputCount()
                          << " output(s); expected 2 (from_logits, to_logits)." << std::endl;
                return false;
            }

            auto input_name = session->GetInputNameAllocated(0, allocator);
            auto from_output_name = session->GetOutputNameAllocated(0, allocator);
            auto to_output_name = session->GetOutputNameAllocated(1, allocator);

            std::array<int64_t, 4> input_shape{1, kNbInputChannels, 8, 8};
            Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);

            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                mem_info,
                const_cast<float *>(input.data()),
                input.size(),
                input_shape.data(),
                input_shape.size());

            const char *input_names[] = {input_name.get()};
            const char *output_names[] = {from_output_name.get(), to_output_name.get()};

            auto output_tensors = session->Run(
                Ort::RunOptions{nullptr},
                input_names,
                &input_tensor,
                1,
                output_names,
                2);

            if (output_tensors.size() < 2 || !output_tensors[0].IsTensor() || !output_tensors[1].IsTensor())
            {
                std::cerr << "[ML] ONNX model did not return two tensor outputs." << std::endl;
                return false;
            }

            auto read_scores = [](Ort::Value &tensor, std::array<float, 64> &out) -> bool
            {
                // Verify the element count before reading, since a
                // mismatched/corrupt model (the model paths are
                // config-controlled) would otherwise cause an
                // out-of-bounds read here.
                const size_t element_count = tensor.GetTensorTypeAndShapeInfo().GetElementCount();
                if (element_count < out.size())
                {
                    std::cerr << "[ML] ONNX output has " << element_count
                              << " elements, expected at least " << out.size() << "." << std::endl;
                    return false;
                }
                const float *data = tensor.GetTensorMutableData<float>();
                std::copy(data, data + out.size(), out.begin());
                return true;
            };

            return read_scores(output_tensors[0], from_scores) && read_scores(output_tensors[1], to_scores);
        }
        catch (const Ort::Exception &e)
        {
            // A model that doesn't match the expected input/output shapes
            // (e.g. an old model exported before this contract changed)
            // throws here rather than crashing the whole app.
            std::cerr << "[ML] ONNX inference failed: " << e.what() << std::endl;
            return false;
        }
    }

    // Given the model's two score vectors, rank (from, to) candidates by
    // joint score and try them in descending order until a legal move is
    // found. Only the top-K scoring squares on each side are combined
    // (K*K candidates) rather than all 64*64, since the legal one is
    // overwhelmingly likely to involve one of the model's top picks.
    bool scores_to_legal_move(chess &game, const std::array<float, 64> &from_scores,
                               const std::array<float, 64> &to_scores, motionType &outMove)
    {
        constexpr int kTopK = 8;

        std::array<int, 64> from_order{};
        std::array<int, 64> to_order{};
        std::iota(from_order.begin(), from_order.end(), 0);
        std::iota(to_order.begin(), to_order.end(), 0);

        std::sort(from_order.begin(), from_order.end(), [&](int a, int b)
                  { return from_scores[static_cast<size_t>(a)] > from_scores[static_cast<size_t>(b)]; });
        std::sort(to_order.begin(), to_order.end(), [&](int a, int b)
                  { return to_scores[static_cast<size_t>(a)] > to_scores[static_cast<size_t>(b)]; });

        const playerColor mover = game.current_player_color();

        struct Candidate
        {
            int from_idx;
            int to_idx;
            float joint_score;
        };

        std::vector<Candidate> candidates;
        candidates.reserve(static_cast<size_t>(kTopK) * static_cast<size_t>(kTopK));
        for (int i = 0; i < kTopK; ++i)
        {
            for (int j = 0; j < kTopK; ++j)
            {
                int from_idx = from_order[static_cast<size_t>(i)];
                int to_idx = to_order[static_cast<size_t>(j)];
                if (from_idx == to_idx)
                {
                    continue; // a move can't stay on the same square
                }
                candidates.push_back({from_idx, to_idx,
                                       from_scores[static_cast<size_t>(from_idx)] + to_scores[static_cast<size_t>(to_idx)]});
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b)
                  { return a.joint_score > b.joint_score; });

        motionVector legalMoves = game.findAllLegalMoves();

        for (std::size_t k = 0; k < candidates.size(); ++k)
        {
            boardCoordinateType fromCoord = canonical_index_to_coord(candidates[k].from_idx, mover);
            boardCoordinateType toCoord = canonical_index_to_coord(candidates[k].to_idx, mover);

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

bool chess::mlMove(MLModelSlot slot)
{
    if (!load_onnx_session_once(slot))
    {
        std::cout << "[ML] Could not load ONNX model " << slot_label(slot) << "; aborting ML move." << std::endl;
        return false;
    }

    auto input = board_to_input(*this);
    std::array<float, 64> from_scores{};
    std::array<float, 64> to_scores{};
    if (!run_onnx(input, slot, from_scores, to_scores))
    {
        std::cout << "[ML] ONNX inference failed (model " << slot_label(slot) << "); aborting ML move." << std::endl;
        return false;
    }

    motionType mlMove;
    if (!scores_to_legal_move(*this, from_scores, to_scores, mlMove))
    {
        return false;
    }

    mlMove.moved_by_whom = moved_by::ai;
    std::cout << "[ML] Executing move (model " << slot_label(slot) << "): "
              << mlMove.start_position.coord.file << mlMove.start_position.coord.rank
              << " -> "
              << mlMove.dest_position.coord.file << mlMove.dest_position.coord.rank << std::endl;

    executeMove(mlMove);
    swapPlayers();

    return true;
}