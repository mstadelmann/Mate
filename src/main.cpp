#include "chess.h"
#include "utils.h"

void game_loop(chess &);

int main(int argc, char **argv)
{

    printLogo();
    chess game;
    auto selection = MainMenu();

    switch (selection)
    {
    case MainMenuChoice::StartNewGame:
        cout << "Starting a new game..." << endl;
        game.load_starting_position();
        game_loop(game);
        break;
    case MainMenuChoice::BoardEditor:
        cout << "Starting board editor..." << endl;
        break;
    case MainMenuChoice::LoadFromDatabase:
        cout << "Loading game from database..." << endl;
        break;
    case MainMenuChoice::Help:
        cout << "Help: Use the menu to select actions." << endl;
        break;
    case MainMenuChoice::Quit:
        cout << "Quitting." << endl;
        return 0;
    default:
        cout << "Invalid selection; feature not implemented." << endl;
        break;
    }

    return 0;
}

void game_loop(chess &game)
{

    while (true)
    {
        game.printCurrentGame();

        auto selection = GameMenu();
        switch (selection)
        {
        case GameMenuChoice::ShowCount:
            cout << "Showing count..." << endl;
            break;
        case GameMenuChoice::WriteDB:
            cout << "Writing to database..." << endl;
            break;
        case GameMenuChoice::ManualMove:
            cout << "Entering manual move..." << endl;
            game.manualMove();
            break;
        case GameMenuChoice::SmartMove:
            cout << "Running smart move..." << endl;
            break;
        case GameMenuChoice::RandomMove:
            cout << "Running random move..." << endl;
            break;
        case GameMenuChoice::Undo:
            cout << "Undoing last move..." << endl;
            break;
        case GameMenuChoice::Redo:
            cout << "Redoing last move..." << endl;
            break;
        case GameMenuChoice::Help:
            cout << "Showing help menu..." << endl;
            break;
        case GameMenuChoice::ListAllMoves:
            cout << "Listing all legal moves..." << endl;
            break;
        case GameMenuChoice::ShowHistory:
            cout << "Listing game history..." << endl;
            break;
        case GameMenuChoice::Quit:
            cout << "Quitting game and returning to main menu..." << endl;
            break;
        default:
            cout << "Invalid selection; feature not implemented." << endl;
            break;
        }
    }
}