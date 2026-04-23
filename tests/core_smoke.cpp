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
} // namespace

int main()
{
    init_config_defaults();
    assert_new_game_resets_state();
    assert_undo_stops_at_initial_state();
    assert_smart_move_handles_stalemate();
    std::cout << "mate_core_tests: all checks passed" << std::endl;
    return 0;
}
