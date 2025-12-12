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
MainMenuChoice MainMenu(void)
{
    struct MenuItem
    {
        MainMenuChoice choice;
        const char *label;
    };

#define MAKE_ITEM(name, label) MenuItem{MainMenuChoice::name, label},
    static const std::vector<MenuItem> kMainMenuItems = {
        MAIN_MENU_ITEMS(MAKE_ITEM)};
#undef MAKE_ITEM

    int choice = -1;
    while (true)
    {
        cout << "Main Menu:" << endl;
        for (size_t i = 0; i < kMainMenuItems.size(); ++i)
        {
            cout << (i + 1) << ". " << kMainMenuItems[i].label << endl;
        }
        cout << "Enter choice (1-" << kMainMenuItems.size() << "): " << endl;

        if (!(std::cin >> choice) ||
            choice < 1 ||
            choice > static_cast<int>(kMainMenuItems.size()))
        {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            cout << "Error: please enter a number between 1 and " << kMainMenuItems.size() << "." << endl;
            continue;
        }

        break;
    }

    return kMainMenuItems[static_cast<size_t>(choice - 1)].choice;
}

void printGameMenu(void)
{
    cout << "C = Current count, D = Write to DB, N = New game, M = Manual move, B = Back," << endl
         << "R = Random move, T = Smart move, E = Empty field, S = Show field, P = Place piece," << endl
         << "H = Help, ? = Show current count and player, F = Find all legal moves, O = Show history" << endl
         << "Q = Quit" << endl;
}