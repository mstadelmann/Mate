#ifndef GUI_H
#define GUI_H

#include "chess.h"

#include <memory>
#include <string>
#include <vector>

enum class ChessGuiMode
{
    main_menu,
    local_game,
    network_game,
    board_editor,
    database_browser,
    network_setup,
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
    quit_game,
    editor_board_click,
    editor_clear_board,
    editor_default_board,
    editor_save_board,
    editor_back,
    database_selection_changed,
    database_load_snapshot,
    database_back,
    network_submit,
    network_back
};

struct ChessGuiDatabaseEntry
{
    std::string name;
    int move_count = 0;
};

struct ChessGuiBoardEditorState
{
    pieceType selected_piece{pieceCode::pawn, playerColor::white};
    std::string save_name;
    std::string status_message;
};

struct ChessGuiDatabaseState
{
    std::vector<ChessGuiDatabaseEntry> games;
    int selected_game_index = -1;
    int selected_snapshot_index = 0;
    int snapshot_count = 0;
    std::string current_move_label;
    std::string status_message;
};

enum class ChessGuiNetworkRole
{
    host,
    join
};

struct ChessGuiNetworkState
{
    ChessGuiNetworkRole role = ChessGuiNetworkRole::host;
    bool host_plays_white = true;
    std::string username;
    std::string host = "127.0.0.1";
    std::string password;
    std::string status_message;
    bool waiting_for_peer = false;
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
    virtual void set_board_editor_state(const ChessGuiBoardEditorState &state) = 0;
    virtual ChessGuiBoardEditorState board_editor_state() const = 0;
    virtual void set_database_state(const ChessGuiDatabaseState &state) = 0;
    virtual ChessGuiDatabaseState database_state() const = 0;
    virtual void set_network_state(const ChessGuiNetworkState &state) = 0;
    virtual ChessGuiNetworkState network_state() const = 0;
    // The color the local player actually controls in the active network
    // game (resolved after the host/join handshake); playerColor::none
    // outside of network play.
    virtual void set_local_player_color(playerColor color) = 0;
    virtual playerColor local_player_color() const = 0;
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

inline void set_chess_gui_board_editor_state(ChessGui *gui, const ChessGuiBoardEditorState &state)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->set_board_editor_state(state);
    }
}

inline ChessGuiBoardEditorState get_chess_gui_board_editor_state(ChessGui *gui)
{
    return (gui != nullptr && gui->is_open()) ? gui->board_editor_state() : ChessGuiBoardEditorState{};
}

inline void set_chess_gui_database_state(ChessGui *gui, const ChessGuiDatabaseState &state)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->set_database_state(state);
    }
}

inline ChessGuiDatabaseState get_chess_gui_database_state(ChessGui *gui)
{
    return (gui != nullptr && gui->is_open()) ? gui->database_state() : ChessGuiDatabaseState{};
}

inline void set_chess_gui_network_state(ChessGui *gui, const ChessGuiNetworkState &state)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->set_network_state(state);
    }
}

inline ChessGuiNetworkState get_chess_gui_network_state(ChessGui *gui)
{
    return (gui != nullptr && gui->is_open()) ? gui->network_state() : ChessGuiNetworkState{};
}

inline void set_chess_gui_local_player_color(ChessGui *gui, playerColor color)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->set_local_player_color(color);
    }
}

#endif /* GUI_H */
