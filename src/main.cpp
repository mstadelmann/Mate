#include "chess.h"
#include "utils.h"

int main(int argc, char **argv)
{

    chess game;
    auto selection = MainMenu();

    switch (selection)
    {
    case MainMenuChoice::StartNewGame:
        cout << "Starting a new game..." << endl;
        game.printCurrentGame();
        game.load_starting_position();
        game.printCurrentGame();
        break;
    case MainMenuChoice::StartEmptyGame:
        cout << "Starting an empty game..." << endl;
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
    }

    return 0;
}