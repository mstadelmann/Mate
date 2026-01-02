#include "chess.h"
#include "utils.h"
#include "config.h"
#include "database.h"

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
            game.manualMove();
            break;
        case GameMenuChoice::SmartMove:
            game.performSmartMove();
            break;
        case GameMenuChoice::RandomMove:
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
            cout << "Invalid selection. Type 10 to list possible options." << endl;
            break;
        }
    }
}