#include "chess.h"
#include "utils.h"
#include "config.h"
#include "database.h"
#include "network.h"

void game_loop(chess &);

int main(int argc, char **argv)
{
    init_config_defaults();
    if (!load_config_from_json())
    {
        save_config_to_json();
    }

    printLogo();
    chess game;
    while (true)
    {
        auto selection = MainMenu();

        switch (selection)
        {
        case MainMenuChoice::StartNewGame:
            cout << "\nStarting a new game..." << endl;
            game.load_starting_position();
            game_loop(game);
            break;
        case MainMenuChoice::StartNetworkGame:
        {
            cout << "\nNetwork mode selected." << endl;
            cout << "Start a server or join a game? (s/j): " << std::flush;
            std::string mode;
            std::cin >> mode;
            if (mode.empty())
                break;

            const uint16_t port = 5555;
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
                if (!start_server(port, uname, hostWhite, conn))
                {
                    cout << "Failed to start server or accept connection." << endl;
                    break;
                }
                run_network_game(game, conn);
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
                run_network_game(game, conn);
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
            game_loop(game);
            break;
        case MainMenuChoice::BoardEditor:
            game.board_editor();
            break;
        case MainMenuChoice::LoadFromDatabase:
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

void game_loop(chess &game)
{
    if (!game.check_board_valid())
    {
        game.printCurrentGame();
        std::cout << "ERROR: The current board position is invalid. Returning to Main Menu" << std::endl;
        return;
    }
    game.init_game();
    bool showMenu = true;

    while (true)
    {
        game.detectCheckmate();
        game.printCurrentGame();
        auto selection = GameMenu(showMenu);
        showMenu = false;

        switch (selection)
        {
        case GameMenuChoice::ManualMove:
            cout << "Entering manual move..." << endl;
            game.manualMove();
            break;
        case GameMenuChoice::SmartMove:
            cout << "Performing smart move..." << endl;
            game.performSmartMove();
            break;
        case GameMenuChoice::RandomMove:
            cout << "Performing random move..." << endl;
            game.randomMove();
            break;
        case GameMenuChoice::Undo:
            cout << "Undoing last move..." << endl;
            game.reverseMove();
            game.swapPlayers();
            break;
        case GameMenuChoice::ListAllMoves:
            game.listLegalMoves();
            break;
        case GameMenuChoice::ShowHistory:
            cout << "Listing game history..." << endl;
            game.listMoveHistory();
            break;
        case GameMenuChoice::WriteDB:
            cout << "Writing to database..." << endl;
            store_to_DB(game);
            break;
        case GameMenuChoice::Help:
            showMenu = true;
            break;
        case GameMenuChoice::Quit:
            cout << "Quitting game and returning to main menu..." << endl;
            return;
        default:
            cout << "Invalid command. Available: m/M, s, r, u, a, l, w, q." << endl;
            break;
        }
    }
}