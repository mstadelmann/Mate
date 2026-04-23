#ifndef GUI_H
#define GUI_H

#include "chess.h"

#include <memory>
#include <string>

enum class ChessGuiMode
{
    main_menu,
    local_game,
    busy
};

enum class ChessGuiActionType
{
    none,
    start_new_game,
    board_editor,
    load_from_database,
    play_current_board,
    start_network_game,
    move_piece,
    smart_move,
    ml_move,
    random_move,
    undo,
    list_moves,
    show_history,
    write_db,
    quit_game
};

struct ChessGuiAction
{
    ChessGuiActionType type = ChessGuiActionType::none;
    boardCoordinateType start{'A', 1};
    boardCoordinateType dest{'A', 1};
};

class ChessGui
{
public:
    virtual ~ChessGui() = default;
    virtual void sync(const chess &game) = 0;
    virtual bool is_open() const = 0;
    virtual void set_mode(ChessGuiMode mode) = 0;
    virtual bool poll_action(ChessGuiAction &action) = 0;
};

std::unique_ptr<ChessGui> create_chess_gui(std::string &error_message);

inline void sync_chess_gui(ChessGui *gui, const chess &game)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->sync(game);
    }
}

inline void set_chess_gui_mode(ChessGui *gui, ChessGuiMode mode)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->set_mode(mode);
    }
}

inline bool poll_chess_gui_action(ChessGui *gui, ChessGuiAction &action)
{
    return gui != nullptr && gui->is_open() && gui->poll_action(action);
}

#endif /* GUI_H */
