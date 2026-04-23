#include "chess.h"
#include "utils.h"
#include "config.h"
#include "database.h"
#include "gui.h"
#include "network.h"
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
            set_chess_gui_mode(gui.get(), ChessGuiMode::busy);
            sync_chess_gui(gui.get(), game);
            game.board_editor();
            break;
        case MainMenuChoice::LoadFromDatabase:
            set_chess_gui_mode(gui.get(), ChessGuiMode::busy);
            sync_chess_gui(gui.get(), game);
            cout << "\nLoading game from database..." << endl;
            LoadFromDatabase(game);
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
