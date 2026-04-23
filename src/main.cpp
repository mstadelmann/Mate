#include "chess.h"
#include "utils.h"
#include "config.h"
#include "database.h"
#include "gui.h"
#include "network.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <sys/select.h>
#include <unistd.h>

namespace
{
    struct RuntimeOptions
    {
        bool enable_gui = false;
    };

    enum class ParseOptionsResult
    {
        ok,
        exit_success,
        exit_failure
    };

    void print_usage(const char *program_name)
    {
        std::cout << "Usage: " << program_name << " [--gui]" << std::endl;
        std::cout << "  --gui   Show a live SDL2 board window alongside the terminal UI" << std::endl;
    }

    ParseOptionsResult parse_runtime_options(int argc, char *argv[], RuntimeOptions &options)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--gui")
            {
                options.enable_gui = true;
            }
            else if (arg == "--help" || arg == "-h")
            {
                print_usage(argv[0]);
                return ParseOptionsResult::exit_success;
            }
            else
            {
                std::cerr << "Unknown option: " << arg << std::endl;
                print_usage(argv[0]);
                return ParseOptionsResult::exit_failure;
            }
        }

        return ParseOptionsResult::ok;
    }

    struct LoopInput
    {
        bool end_of_input = false;
        bool from_gui = false;
        ChessGuiAction gui_action{};
        std::string cli_command;
    };

    bool stdin_ready(int timeout_ms)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        timeval timeout{};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        const int rv = ::select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &timeout);
        return rv > 0 && FD_ISSET(STDIN_FILENO, &rfds);
    }

    LoopInput wait_for_main_menu_input(ChessGui *gui)
    {
        LoopInput input;
        print_main_menu();
        while (true)
        {
            if (poll_chess_gui_action(gui, input.gui_action))
            {
                input.from_gui = true;
                return input;
            }

            if (!stdin_ready(75))
            {
                continue;
            }

            if (!(std::cin >> input.cli_command))
            {
                input.end_of_input = true;
            }
            return input;
        }
    }

    LoopInput wait_for_game_loop_input(ChessGui *gui, bool print_menu)
    {
        LoopInput input;
        if (print_menu)
        {
            print_game_menu();
        }

        bool prompt_printed = print_menu;
        while (true)
        {
            if (poll_chess_gui_action(gui, input.gui_action))
            {
                input.from_gui = true;
                return input;
            }

            if (!print_menu && !prompt_printed)
            {
                cout << ">MATE " << std::flush;
                prompt_printed = true;
            }

            if (!stdin_ready(75))
            {
                continue;
            }

            if (!(std::cin >> input.cli_command))
            {
                input.end_of_input = true;
            }
            return input;
        }
    }

    bool try_map_gui_main_menu_action(const ChessGuiAction &action, MainMenuChoice &selection)
    {
        switch (action.type)
        {
        case ChessGuiActionType::start_new_game:
            selection = MainMenuChoice::StartNewGame;
            return true;
        case ChessGuiActionType::board_editor:
            selection = MainMenuChoice::BoardEditor;
            return true;
        case ChessGuiActionType::load_from_database:
            selection = MainMenuChoice::LoadFromDatabase;
            return true;
        case ChessGuiActionType::play_current_board:
            selection = MainMenuChoice::PLAY;
            return true;
        case ChessGuiActionType::start_network_game:
            selection = MainMenuChoice::StartNetworkGame;
            return true;
        default:
            return false;
        }
    }

    bool wait_for_gui_action(ChessGui *gui, ChessGuiAction &action, int timeout_ms = 75)
    {
        if (gui == nullptr)
        {
            return false;
        }

        while (gui->is_open())
        {
            if (poll_chess_gui_action(gui, action))
            {
                return true;
            }
            ::usleep(static_cast<useconds_t>(timeout_ms * 1000));
        }

        return false;
    }

    void apply_board_to_game(chess &game, const boardType &board)
    {
        for (char file = 'A'; file <= 'H'; ++file)
        {
            const int file_index = file - 'A';
            for (int rank = 1; rank <= 8; ++rank)
            {
                game.place_piece({file, rank}, board[static_cast<std::size_t>(file_index)][static_cast<std::size_t>(rank - 1)]);
            }
        }
    }

    void refresh_board_preview(chess &game, playerColor current_player)
    {
        game.set_current_player(current_player);
        game.detectCheckmate();
    }

    std::string default_gui_username()
    {
        const char *user_env = std::getenv("USER");
        if (user_env != nullptr && *user_env != '\0')
        {
            return user_env;
        }
        return "player";
    }

    std::string trim_copy(const std::string &text)
    {
        const auto is_ws = [](unsigned char ch)
        {
            return std::isspace(ch) != 0;
        };

        std::size_t begin = 0;
        while (begin < text.size() && is_ws(static_cast<unsigned char>(text[begin])))
        {
            ++begin;
        }

        std::size_t end = text.size();
        while (end > begin && is_ws(static_cast<unsigned char>(text[end - 1])))
        {
            --end;
        }

        return text.substr(begin, end - begin);
    }

    std::string piece_label(pieceType piece)
    {
        if (piece.piece == pieceCode::empty)
        {
            return "eraser";
        }

        return playerColorToString(piece.color) + " " + pieceCodeToString(piece.piece);
    }

    void run_gui_board_editor(chess &game, ChessGui *gui)
    {
        if (gui == nullptr)
        {
            game.board_editor();
            return;
        }

        ChessGuiBoardEditorState editor_state;
        editor_state.selected_piece = {pieceCode::pawn, playerColor::white};
        editor_state.save_name = game.gameName();
        editor_state.status_message = "Click a square to place the selected piece.";
        set_chess_gui_mode(gui, ChessGuiMode::board_editor);
        set_chess_gui_board_editor_state(gui, editor_state);
        refresh_board_preview(game, playerColor::white);
        sync_chess_gui(gui, game);

        ChessGuiAction action;
        while (wait_for_gui_action(gui, action))
        {
            switch (action.type)
            {
            case ChessGuiActionType::editor_board_click:
            {
                const ChessGuiBoardEditorState state = get_chess_gui_board_editor_state(gui);
                game.place_piece(action.dest, state.selected_piece);
                ChessGuiBoardEditorState next_state = state;
                next_state.status_message = "Placed " + piece_label(state.selected_piece) + " on " + std::string(1, action.dest.file) + std::to_string(action.dest.rank) + ".";
                set_chess_gui_board_editor_state(gui, next_state);
                refresh_board_preview(game, playerColor::white);
                sync_chess_gui(gui, game);
                break;
            }
            case ChessGuiActionType::editor_clear_board:
            {
                game.clear_board();
                ChessGuiBoardEditorState state = get_chess_gui_board_editor_state(gui);
                state.status_message = "Board cleared.";
                set_chess_gui_board_editor_state(gui, state);
                refresh_board_preview(game, playerColor::white);
                sync_chess_gui(gui, game);
                break;
            }
            case ChessGuiActionType::editor_default_board:
            {
                game.load_starting_position();
                ChessGuiBoardEditorState state = get_chess_gui_board_editor_state(gui);
                state.status_message = "Restored the default starting position.";
                set_chess_gui_board_editor_state(gui, state);
                refresh_board_preview(game, playerColor::white);
                sync_chess_gui(gui, game);
                break;
            }
            case ChessGuiActionType::editor_save_board:
            {
                ChessGuiBoardEditorState state = get_chess_gui_board_editor_state(gui);
                const std::string save_name = trim_copy(state.save_name);
                if (save_name.empty())
                {
                    state.status_message = "Enter a save name before writing to the database.";
                    set_chess_gui_board_editor_state(gui, state);
                    break;
                }

                game.init_game();
                game.set_game_name(save_name);
                store_to_DB(game);
                state.save_name = save_name;
                state.status_message = "Saved '" + save_name + "' to the database.";
                set_chess_gui_board_editor_state(gui, state);
                refresh_board_preview(game, playerColor::white);
                sync_chess_gui(gui, game);
                break;
            }
            case ChessGuiActionType::editor_back:
                set_chess_gui_mode(gui, ChessGuiMode::main_menu);
                return;
            default:
                break;
            }
        }
    }

    struct DatabaseBrowserSession
    {
        std::vector<DatabaseGameSummary> games;
        int selected_game_index = 0;
        int selected_snapshot_index = 0;
        std::vector<boardType> boards;
        std::vector<std::string> move_info;
        boardType original_board{};
        playerColor original_player = playerColor::white;
    };

    bool refresh_database_browser(chess &game, ChessGui *gui, DatabaseBrowserSession &session, std::string &error_message)
    {
        ChessGuiDatabaseState state;
        for (const auto &entry : session.games)
        {
            state.games.push_back({entry.name, entry.move_count});
        }

        if (session.games.empty())
        {
            state.selected_game_index = -1;
            state.snapshot_count = 0;
            state.status_message = error_message.empty() ? "No saved games found in the database." : error_message;
            set_chess_gui_database_state(gui, state);
            sync_chess_gui(gui, game);
            return false;
        }

        session.selected_game_index = std::clamp(session.selected_game_index, 0, static_cast<int>(session.games.size()) - 1);
        const std::string selected_name = session.games[static_cast<std::size_t>(session.selected_game_index)].name;

        if (!load_database_game_snapshots(selected_name, session.boards, session.move_info, error_message))
        {
            state.selected_game_index = session.selected_game_index;
            state.snapshot_count = 0;
            state.status_message = error_message;
            set_chess_gui_database_state(gui, state);
            sync_chess_gui(gui, game);
            return false;
        }

        session.selected_snapshot_index = std::clamp(session.selected_snapshot_index, 0, static_cast<int>(session.boards.size()) - 1);
        apply_board_to_game(game, session.boards[static_cast<std::size_t>(session.selected_snapshot_index)]);
        refresh_board_preview(game, (session.selected_snapshot_index % 2 == 0) ? playerColor::white : playerColor::black);

        state.selected_game_index = session.selected_game_index;
        state.selected_snapshot_index = session.selected_snapshot_index;
        state.snapshot_count = static_cast<int>(session.boards.size());
        state.status_message = "Previewing '" + selected_name + "'.";
        if (session.selected_snapshot_index < static_cast<int>(session.move_info.size()))
        {
            state.current_move_label = session.move_info[static_cast<std::size_t>(session.selected_snapshot_index)];
        }

        set_chess_gui_database_state(gui, state);
        sync_chess_gui(gui, game);
        return true;
    }

    void run_gui_database_browser(chess &game, ChessGui *gui)
    {
        if (gui == nullptr)
        {
            LoadFromDatabase(game);
            return;
        }

        DatabaseBrowserSession session;
        session.original_board = game.board();
        session.original_player = game.current_player_color();

        std::string error_message;
        list_database_games(session.games, error_message);

        set_chess_gui_mode(gui, ChessGuiMode::database_browser);
        refresh_database_browser(game, gui, session, error_message);

        ChessGuiAction action;
        while (wait_for_gui_action(gui, action))
        {
            if (action.type == ChessGuiActionType::database_selection_changed)
            {
                const ChessGuiDatabaseState state = get_chess_gui_database_state(gui);
                session.selected_game_index = state.selected_game_index;
                session.selected_snapshot_index = state.selected_snapshot_index;
                refresh_database_browser(game, gui, session, error_message);
            }
            else if (action.type == ChessGuiActionType::database_load_snapshot)
            {
                ChessGuiDatabaseState state = get_chess_gui_database_state(gui);
                if (state.snapshot_count > 0)
                {
                    state.status_message = "Loaded the previewed board into the current session.";
                    set_chess_gui_database_state(gui, state);
                }
                set_chess_gui_mode(gui, ChessGuiMode::main_menu);
                return;
            }
            else if (action.type == ChessGuiActionType::database_back)
            {
                apply_board_to_game(game, session.original_board);
                refresh_board_preview(game, session.original_player);
                sync_chess_gui(gui, game);
                set_chess_gui_mode(gui, ChessGuiMode::main_menu);
                return;
            }
        }
    }

    void run_gui_network_setup(chess &game, ChessGui *gui)
    {
        if (gui == nullptr)
        {
            return;
        }

        ChessGuiNetworkState state;
        state.username = default_gui_username();
        state.status_message = "Choose Host or Join, then press Start.";
        set_chess_gui_mode(gui, ChessGuiMode::network_setup);
        set_chess_gui_network_state(gui, state);
        sync_chess_gui(gui, game);

        ChessGuiAction action;
        while (wait_for_gui_action(gui, action))
        {
            if (action.type == ChessGuiActionType::network_back)
            {
                set_chess_gui_mode(gui, ChessGuiMode::main_menu);
                return;
            }

            if (action.type != ChessGuiActionType::network_submit)
            {
                continue;
            }

            state = get_chess_gui_network_state(gui);
            state.username = trim_copy(state.username);
            state.host = trim_copy(state.host);
            if (state.username.empty())
            {
                state.status_message = "Enter a username before starting a network game.";
                set_chess_gui_network_state(gui, state);
                continue;
            }
            if (state.role == ChessGuiNetworkRole::join && state.host.empty())
            {
                state.status_message = "Enter the host IP or hostname before joining.";
                set_chess_gui_network_state(gui, state);
                continue;
            }

            const uint16_t port = static_cast<uint16_t>(network_port);
            NetConnection conn;
            set_chess_gui_mode(gui, ChessGuiMode::busy);

            if (state.role == ChessGuiNetworkRole::host)
            {
                state.waiting_for_peer = true;
                state.status_message = "Waiting for a player on port " + std::to_string(port) + "...";
                set_chess_gui_network_state(gui, state);
                sync_chess_gui(gui, game);

                if (!start_server(port, state.username, state.host_plays_white, state.password, conn))
                {
                    state.waiting_for_peer = false;
                    state.status_message = "Failed to start the server or accept a client.";
                    set_chess_gui_network_state(gui, state);
                    set_chess_gui_mode(gui, ChessGuiMode::network_setup);
                    continue;
                }
            }
            else
            {
                state.status_message = "Joining " + state.host + ":" + std::to_string(port) + "...";
                set_chess_gui_network_state(gui, state);
                sync_chess_gui(gui, game);

                std::string connect_error;
                if (!connect_client(state.host, port, state.username, state.password, conn, connect_error))
                {
                    state.status_message = connect_error.empty() ? "Could not join the network game." : connect_error;
                    set_chess_gui_network_state(gui, state);
                    set_chess_gui_mode(gui, ChessGuiMode::network_setup);
                    continue;
                }
            }

            run_network_game(game, conn, gui);
            close_connection(conn);
            set_chess_gui_mode(gui, ChessGuiMode::main_menu);
            return;
        }
    }

    bool handle_game_menu_choice(chess &game, GameMenuChoice selection, bool &showMenu)
    {
        switch (selection)
        {
        case GameMenuChoice::ManualMove:
            cout << "Entering manual move..." << endl;
            game.manualMove();
            return true;
        case GameMenuChoice::SmartMove:
            cout << "Performing smart move..." << endl;
            game.performSmartMove();
            return true;
        case GameMenuChoice::MLMove:
            cout << "Performing ML move..." << endl;
            game.mlMove();
            return true;
        case GameMenuChoice::RandomMove:
            cout << "Performing random move..." << endl;
            game.randomMove();
            return true;
        case GameMenuChoice::Undo:
            cout << "Undoing last move..." << endl;
            if (game.reverseMove())
            {
                game.swapPlayers();
            }
            return true;
        case GameMenuChoice::ListAllMoves:
            game.listLegalMoves();
            return true;
        case GameMenuChoice::ShowHistory:
            cout << "Listing game history..." << endl;
            game.listMoveHistory();
            return true;
        case GameMenuChoice::WriteDB:
            cout << "Writing to database..." << endl;
            store_to_DB(game);
            return true;
        case GameMenuChoice::Help:
            showMenu = true;
            return true;
        case GameMenuChoice::Quit:
            cout << "Quitting game and returning to main menu..." << endl;
            return false;
        default:
            cout << "Invalid command. Available: m/M, s, r, u, a, l, w, q." << endl;
            return true;
        }
    }

    bool handle_gui_action(chess &game, const ChessGuiAction &action, bool &showMenu)
    {
        switch (action.type)
        {
        case ChessGuiActionType::move_piece:
            if (!game.applyMove(action.start, action.dest, moved_by::human))
            {
                cout << "Illegal move." << endl;
            }
            return true;
        case ChessGuiActionType::smart_move:
            return handle_game_menu_choice(game, GameMenuChoice::SmartMove, showMenu);
        case ChessGuiActionType::ml_move:
            return handle_game_menu_choice(game, GameMenuChoice::MLMove, showMenu);
        case ChessGuiActionType::random_move:
            return handle_game_menu_choice(game, GameMenuChoice::RandomMove, showMenu);
        case ChessGuiActionType::undo:
            return handle_game_menu_choice(game, GameMenuChoice::Undo, showMenu);
        case ChessGuiActionType::list_moves:
            return handle_game_menu_choice(game, GameMenuChoice::ListAllMoves, showMenu);
        case ChessGuiActionType::show_history:
            return handle_game_menu_choice(game, GameMenuChoice::ShowHistory, showMenu);
        case ChessGuiActionType::write_db:
            return handle_game_menu_choice(game, GameMenuChoice::WriteDB, showMenu);
        case ChessGuiActionType::quit_game:
            return handle_game_menu_choice(game, GameMenuChoice::Quit, showMenu);
        case ChessGuiActionType::none:
        default:
            return true;
        }
    }
} // namespace

void game_loop(chess &, ChessGui *gui = nullptr);

int main(int argc, char *argv[])
{
    RuntimeOptions runtime_options;
    switch (parse_runtime_options(argc, argv, runtime_options))
    {
    case ParseOptionsResult::exit_success:
        return 0;
    case ParseOptionsResult::exit_failure:
        return 1;
    case ParseOptionsResult::ok:
    default:
        break;
    }

    init_config_defaults();
    if (!load_config_from_json())
    {
        save_config_to_json();
    }

    printLogo();
    chess game;
    std::unique_ptr<ChessGui> gui;
    if (runtime_options.enable_gui)
    {
        std::string gui_error;
        gui = create_chess_gui(gui_error);
        if (gui == nullptr)
        {
            std::cerr << "GUI disabled: " << gui_error << std::endl;
        }
        else
        {
            sync_chess_gui(gui.get(), game);
        }
    }

    while (true)
    {
        set_chess_gui_mode(gui.get(), ChessGuiMode::main_menu);
        sync_chess_gui(gui.get(), game);
        MainMenuChoice selection = MainMenuChoice::Quit;
        if (gui == nullptr)
        {
            selection = MainMenu();
        }
        else
        {
            const LoopInput input = wait_for_main_menu_input(gui.get());
            if (input.end_of_input)
            {
                cout << "\nInput closed. Quitting." << endl;
                return 0;
            }

            if (input.from_gui)
            {
                if (!try_map_gui_main_menu_action(input.gui_action, selection))
                {
                    continue;
                }
            }
            else if (!try_parse_main_menu_command(input.cli_command, selection))
            {
                cout << "Error: please enter a number between 1 and 6." << endl;
                continue;
            }
        }

        switch (selection)
        {
        case MainMenuChoice::StartNewGame:
            cout << "\nStarting a new game..." << endl;
            game.load_starting_position();
            game_loop(game, gui.get());
            break;
        case MainMenuChoice::StartNetworkGame:
        {
            if (gui != nullptr)
            {
                run_gui_network_setup(game, gui.get());
                break;
            }

            set_chess_gui_mode(gui.get(), ChessGuiMode::busy);
            sync_chess_gui(gui.get(), game);
            cout << "\nNetwork mode selected." << endl;
            cout << "Start a server or join a game? (s/j): " << std::flush;
            std::string mode;
            std::cin >> mode;
            if (mode.empty())
                break;

            const uint16_t port = static_cast<uint16_t>(network_port);
            NetConnection conn;

            if (mode[0] == 's' || mode[0] == 'S')
            {
                cout << "Enter your username: " << std::flush;
                std::string uname;
                std::cin >> uname;
                cout << "Play as white or black? (w/b): " << std::flush;
                std::string col;
                std::cin >> col;
                bool hostWhite = (!col.empty() && (col[0] == 'w' || col[0] == 'W'));
                cout << "Set a server password (leave empty for none): " << std::flush;
                std::string srvPass;
                // Consume leftover newline from previous formatted input, then allow empty line
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::getline(std::cin, srvPass);
                if (!start_server(port, uname, hostWhite, srvPass, conn))
                {
                    cout << "Failed to start server or accept connection." << endl;
                    break;
                }
                run_network_game(game, conn, gui.get());
                close_connection(conn);
            }
            else if (mode[0] == 'j' || mode[0] == 'J')
            {
                cout << "Enter server IP or hostname: " << std::flush;
                std::string host;
                std::cin >> host;
                cout << "Enter your username: " << std::flush;
                std::string uname;
                std::cin >> uname;
                if (!connect_client(host, port, uname, conn))
                {
                    cout << "Could not join a game." << endl;
                    break;
                }
                run_network_game(game, conn, gui.get());
                close_connection(conn);
            }
            else
            {
                cout << "Unknown choice." << endl;
            }
            break;
        }
        case MainMenuChoice::PLAY:
            cout << "\nPlaying with current board configuration..." << endl;
            game_loop(game, gui.get());
            break;
        case MainMenuChoice::BoardEditor:
            if (gui != nullptr)
            {
                run_gui_board_editor(game, gui.get());
            }
            else
            {
                game.board_editor();
            }
            break;
        case MainMenuChoice::LoadFromDatabase:
            if (gui != nullptr)
            {
                run_gui_database_browser(game, gui.get());
            }
            else
            {
                cout << "\nLoading game from database..." << endl;
                LoadFromDatabase(game);
            }
            break;
        case MainMenuChoice::Quit:
            cout << "\nQuitting." << endl;
            return 0;
        default:
            cout << "\nInvalid selection; feature not implemented." << endl;
            break;
        }
    }
    return 0;
}

void game_loop(chess &game, ChessGui *gui)
{
    set_chess_gui_mode(gui, ChessGuiMode::local_game);
    if (!game.check_board_valid())
    {
        game.printCurrentGame();
        std::cout << "ERROR: The current board position is invalid. Returning to Main Menu" << std::endl;
        set_chess_gui_mode(gui, ChessGuiMode::main_menu);
        return;
    }
    game.init_game();
    bool showMenu = true;

    while (true)
    {
        game.detectCheckmate();
        sync_chess_gui(gui, game);
        game.printCurrentGame();
        const LoopInput input = wait_for_game_loop_input(gui, showMenu);
        showMenu = false;

        if (input.end_of_input)
        {
            cout << "\nInput closed. Returning to main menu..." << endl;
            set_chess_gui_mode(gui, ChessGuiMode::main_menu);
            return;
        }

        if (input.from_gui)
        {
            if (!handle_gui_action(game, input.gui_action, showMenu))
            {
                set_chess_gui_mode(gui, ChessGuiMode::main_menu);
                return;
            }
            continue;
        }

        GameMenuChoice selection = GameMenuChoice::Help;
        if (!try_parse_game_menu_command(input.cli_command, selection))
        {
            cout << "Unknown command." << endl;
            showMenu = true;
            continue;
        }

        if (!handle_game_menu_choice(game, selection, showMenu))
        {
            set_chess_gui_mode(gui, ChessGuiMode::main_menu);
            return;
        }
    }
}
