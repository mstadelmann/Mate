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
} // namespace

int main()
{
    init_config_defaults();
    assert_new_game_resets_state();
    assert_undo_stops_at_initial_state();
    assert_smart_move_handles_stalemate();
    assert_stalemate_is_detected_and_not_confused_with_checkmate();
    assert_threefold_repetition_is_detected();
    std::cout << "mate_core_tests: all checks passed" << std::endl;
    return 0;
}
