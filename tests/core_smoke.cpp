#include "chess.h"
#include "config.h"

#include <cassert>
#include <iostream>

namespace
{
    void clear_board(chess &game)
    {
        for (char file = 'A'; file <= 'H'; ++file)
        {
            for (int rank = 1; rank <= 8; ++rank)
            {
                game.place_piece({file, rank}, {pieceCode::empty, playerColor::none});
            }
        }
    }

    void assert_new_game_resets_state()
    {
        chess game;
        game.load_starting_position();
        game.init_game();

        assert(game.current_player_string() == "white");
        assert(game.getHistory().size() == 1);
        assert(game.getPositionHistory().size() == 1);

        assert(game.randomMove());
        assert(game.current_player_string() == "black");
        assert(game.getHistory().size() == 2);
        assert(game.getPositionHistory().size() == 2);

        game.load_starting_position();
        game.init_game();

        assert(game.current_player_string() == "white");
        assert(game.getHistory().size() == 1);
        assert(game.getPositionHistory().size() == 1);
    }

    void assert_undo_stops_at_initial_state()
    {
        chess game;
        game.load_starting_position();
        game.init_game();

        assert(!game.reverseMove());
        assert(game.getHistory().size() == 1);
        assert(game.current_player_string() == "white");

        assert(game.randomMove());
        assert(game.reverseMove());
        game.swapPlayers();

        assert(game.getHistory().size() == 1);
        assert(game.getPositionHistory().size() == 1);
        assert(game.current_player_string() == "white");
        assert(!game.reverseMove());
    }

    void assert_smart_move_handles_stalemate()
    {
        chess game;
        clear_board(game);
        game.place_piece({'A', 8}, {pieceCode::king, playerColor::black});
        game.place_piece({'C', 6}, {pieceCode::king, playerColor::white});
        game.place_piece({'C', 7}, {pieceCode::queen, playerColor::white});
        game.init_game();
        game.swapPlayers();

        assert(game.current_player_string() == "black");
        assert(game.findAllLegalMoves().empty());

        const auto history_size = game.getHistory().size();
        const auto board_history_size = game.getPositionHistory().size();
        game.performSmartMove();

        assert(game.getHistory().size() == history_size);
        assert(game.getPositionHistory().size() == board_history_size);
        assert(game.current_player_string() == "black");
    }

    void assert_stalemate_is_detected_and_not_confused_with_checkmate()
    {
        chess game;
        clear_board(game);
        game.place_piece({'A', 8}, {pieceCode::king, playerColor::black});
        game.place_piece({'C', 6}, {pieceCode::king, playerColor::white});
        game.place_piece({'C', 7}, {pieceCode::queen, playerColor::white});
        game.init_game();
        game.swapPlayers();

        game.detectCheckmate();
        assert(!game.is_checkmate(playerColor::black));
        assert(game.is_stalemate(playerColor::black));
        assert(!game.is_stalemate(playerColor::white));

        chess mated;
        mated.load_starting_position();
        mated.init_game();
        assert(mated.applyMove(mated.chessCoordinatesFromString("F2"), mated.chessCoordinatesFromString("F3")));
        assert(mated.applyMove(mated.chessCoordinatesFromString("E7"), mated.chessCoordinatesFromString("E5")));
        assert(mated.applyMove(mated.chessCoordinatesFromString("G2"), mated.chessCoordinatesFromString("G4")));
        assert(mated.applyMove(mated.chessCoordinatesFromString("D8"), mated.chessCoordinatesFromString("H4")));
        mated.detectCheckmate();
        assert(mated.is_checkmate(playerColor::white));
        assert(!mated.is_stalemate(playerColor::white));
    }

    void assert_threefold_repetition_is_detected()
    {
        chess game;
        game.load_starting_position();
        game.init_game();
        assert(!game.is_threefold_repetition());

        auto mv = [&](const char *a, const char *b)
        {
            assert(game.applyMove(game.chessCoordinatesFromString(a), game.chessCoordinatesFromString(b)));
        };
        mv("G1", "F3");
        mv("G8", "F6");
        mv("F3", "G1");
        mv("F6", "G8"); // starting position occurs a 2nd time
        mv("G1", "F3");
        mv("G8", "F6");
        mv("F3", "G1");
        mv("F6", "G8"); // starting position occurs a 3rd time

        assert(game.is_threefold_repetition());
    }

    void assert_underpromotion_is_available_and_selectable()
    {
        chess game;
        clear_board(game);
        game.place_piece({'A', 1}, {pieceCode::king, playerColor::white});
        game.place_piece({'H', 8}, {pieceCode::king, playerColor::black});
        game.place_piece({'B', 7}, {pieceCode::pawn, playerColor::white});
        game.init_game();

        int promotion_variants = 0;
        for (const auto &move : game.findAllLegalMoves())
        {
            if (move.start_position.coord.file == 'B' && move.start_position.coord.rank == 7 &&
                move.dest_position.coord.file == 'B' && move.dest_position.coord.rank == 8)
            {
                promotion_variants++;
            }
        }
        assert(promotion_variants == 4); // queen, rook, bishop, knight

        assert(game.applyMove(game.chessCoordinatesFromString("B7"), game.chessCoordinatesFromString("B8"),
                               moved_by::human, pieceCode::knight));
        assert(game.query_position({'B', 8}).piece.piece == pieceCode::knight);
        assert(game.query_position({'B', 8}).piece.color == playerColor::white);
    }

    void assert_insufficient_material_is_detected()
    {
        chess king_vs_king;
        clear_board(king_vs_king);
        king_vs_king.place_piece({'A', 1}, {pieceCode::king, playerColor::white});
        king_vs_king.place_piece({'H', 8}, {pieceCode::king, playerColor::black});
        king_vs_king.init_game();
        assert(king_vs_king.is_insufficient_material());

        chess king_vs_king_and_knight;
        clear_board(king_vs_king_and_knight);
        king_vs_king_and_knight.place_piece({'A', 1}, {pieceCode::king, playerColor::white});
        king_vs_king_and_knight.place_piece({'H', 8}, {pieceCode::king, playerColor::black});
        king_vs_king_and_knight.place_piece({'B', 1}, {pieceCode::knight, playerColor::white});
        king_vs_king_and_knight.init_game();
        assert(king_vs_king_and_knight.is_insufficient_material());

        chess bishop_vs_bishop;
        clear_board(bishop_vs_bishop);
        bishop_vs_bishop.place_piece({'A', 1}, {pieceCode::king, playerColor::white});
        bishop_vs_bishop.place_piece({'H', 8}, {pieceCode::king, playerColor::black});
        bishop_vs_bishop.place_piece({'B', 1}, {pieceCode::bishop, playerColor::white});
        bishop_vs_bishop.place_piece({'G', 8}, {pieceCode::bishop, playerColor::black});
        bishop_vs_bishop.init_game();
        assert(bishop_vs_bishop.is_insufficient_material());

        // A lone rook is enough mating material - must not be flagged as a draw.
        chess king_vs_king_and_rook;
        clear_board(king_vs_king_and_rook);
        king_vs_king_and_rook.place_piece({'A', 1}, {pieceCode::king, playerColor::white});
        king_vs_king_and_rook.place_piece({'H', 8}, {pieceCode::king, playerColor::black});
        king_vs_king_and_rook.place_piece({'B', 1}, {pieceCode::rook, playerColor::white});
        king_vs_king_and_rook.init_game();
        assert(!king_vs_king_and_rook.is_insufficient_material());

        // Two minors on one side (e.g. bishop pair) can force mate - not covered here.
        chess two_minors_one_side;
        clear_board(two_minors_one_side);
        two_minors_one_side.place_piece({'A', 1}, {pieceCode::king, playerColor::white});
        two_minors_one_side.place_piece({'H', 8}, {pieceCode::king, playerColor::black});
        two_minors_one_side.place_piece({'B', 1}, {pieceCode::knight, playerColor::white});
        two_minors_one_side.place_piece({'C', 1}, {pieceCode::bishop, playerColor::white});
        two_minors_one_side.init_game();
        assert(!two_minors_one_side.is_insufficient_material());
    }
} // namespace

int main()
{
    init_config_defaults();
    assert_new_game_resets_state();
    assert_undo_stops_at_initial_state();
    assert_smart_move_handles_stalemate();
    assert_stalemate_is_detected_and_not_confused_with_checkmate();
    assert_threefold_repetition_is_detected();
    assert_underpromotion_is_available_and_selectable();
    assert_insufficient_material_is_detected();
    std::cout << "mate_core_tests: all checks passed" << std::endl;
    return 0;
}
