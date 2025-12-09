#include "utils.h"

void printLogo(void)
{
    cout << "  __  __       _       " << endl;
    cout << " |  \\/  | __ _| |_ ___ " << endl;
    cout << " | |\\/| |/ _` | __/ _ \\" << endl;
    cout << " | |  | | (_| | ||  __/" << endl;
    cout << " |_|  |_|\\__,_|\\__\\___|" << endl;
    cout << "----------------------------------------" << endl;
    cout << "          MATE - Chess Engine           " << endl;
    cout << "       marc.stadelman@gmail.com         " << endl;
    cout << "----------------------------------------" << endl
         << endl;
}

void MainMenu(void)
{
    cout << "Main Menu:" << endl;
    cout << "1. Start New Game" << endl;
    cout << "2. Load Game from Database" << endl;
    cout << "3. Help" << endl;
    cout << "4. Quit" << endl;
}

void printGameMenu(void)
{
    cout << "C = Current count, D = Write to DB, N = New game, M = Manual move, B = Back," << endl
         << "R = Random move, T = Smart move, E = Empty field, S = Show field, P = Place piece," << endl
         << "H = Help, ? = Show current count and player, F = Find all legal moves, O = Show history" << endl
         << "Q = Quit" << endl;
}