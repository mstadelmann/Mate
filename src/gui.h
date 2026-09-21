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
    settings,
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
    network_back,
    open_settings,
    settings_save,
    settings_back,
    send_chat
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

// One editable config.json entry: `value` is always the raw text the user
// sees/types, even for numeric fields, since the GUI edits it as a plain
// text field; `is_bool` fields are toggled ("yes"/"no") by a click instead
// of opening for typing. `is_path` fields additionally get a "Browse"
// button that opens an in-GUI file picker instead of requiring the path to
// be typed by hand. The field list order is meaningful - main.cpp builds
// and parses it positionally, mirroring the CLI settings menu.
struct ChessGuiSettingsField
{
    std::string label;
    std::string value;
    bool is_bool = false;
    bool is_path = false;
};

struct ChessGuiSettingsState
{
    std::vector<ChessGuiSettingsField> fields;
    int selected_field_index = -1;
    std::string status_message;
};

// Feedback for the in-game quick actions (Legal Moves, ML Move, Save) that
// used to only print to the console the GUI window has no view of.
struct ChessGuiGameActionState
{
    std::string message;
};

// Network-game chat: `messages` holds fully-formatted lines ("You: hi" /
// "Marc: hi") in the order they arrived, appended by whichever side
// (CLI or GUI) sent or received them; `pending_input` is the text currently
// being typed into the GUI's chat box.
struct ChessGuiChatState
{
    std::vector<std::string> messages;
    std::string pending_input;
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
    virtual void set_settings_state(const ChessGuiSettingsState &state) = 0;
    virtual ChessGuiSettingsState settings_state() const = 0;
    virtual void set_game_action_state(const ChessGuiGameActionState &state) = 0;
    virtual ChessGuiGameActionState game_action_state() const = 0;
    virtual void set_chat_state(const ChessGuiChatState &state) = 0;
    virtual ChessGuiChatState chat_state() const = 0;
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

inline void set_chess_gui_settings_state(ChessGui *gui, const ChessGuiSettingsState &state)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->set_settings_state(state);
    }
}

inline ChessGuiSettingsState get_chess_gui_settings_state(ChessGui *gui)
{
    return (gui != nullptr && gui->is_open()) ? gui->settings_state() : ChessGuiSettingsState{};
}

inline void set_chess_gui_game_action_state(ChessGui *gui, const ChessGuiGameActionState &state)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->set_game_action_state(state);
    }
}

inline ChessGuiGameActionState get_chess_gui_game_action_state(ChessGui *gui)
{
    return (gui != nullptr && gui->is_open()) ? gui->game_action_state() : ChessGuiGameActionState{};
}

inline void set_chess_gui_chat_state(ChessGui *gui, const ChessGuiChatState &state)
{
    if (gui != nullptr && gui->is_open())
    {
        gui->set_chat_state(state);
    }
}

inline ChessGuiChatState get_chess_gui_chat_state(ChessGui *gui)
{
    return (gui != nullptr && gui->is_open()) ? gui->chat_state() : ChessGuiChatState{};
}

#endif /* GUI_H */
