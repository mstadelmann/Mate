#include "utils.h"
#include <cctype>

#ifndef MATE_VERSION
#define MATE_VERSION "dev"
#endif

void printLogo(void)
{
    cout << "  __  __       _       " << endl;
    cout << " |  \\/  | __ _| |_ ___ " << endl;
    cout << " | |\\/| |/ _` | __/ _ \\" << endl;
    cout << " | |  | | (_| | ||  __/" << endl;
    cout << " |_|  |_|\\__,_|\\__\\___|" << endl;
    cout << "----------------------------------------" << endl;
    cout << "         MATE - Chess Engine v." << MATE_VERSION << "       " << endl;
    cout << "   https://github.com/mstadelmann/Mate  " << endl;
    cout << "----------------------------------------" << endl
         << endl;
}

namespace
{
    struct MenuItemMain
    {
        MainMenuChoice choice;
        const char *label;
    };
    struct MenuItemGame
    {
        GameMenuChoice choice;
        const char *label;
    };
} // namespace

MainMenuChoice MainMenu(bool print_menu)
{
    if (print_menu)
    {
        print_main_menu();
    }

    while (true)
    {
        std::string cmd;
        if (!(std::cin >> cmd) || cmd.empty())
        {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            cout << "Error: please enter a number between 1 and 6." << endl;
            continue;
        }

        MainMenuChoice choice = MainMenuChoice::Quit;
        if (try_parse_main_menu_command(cmd, choice))
        {
            return choice;
        }

        cout << "Error: please enter a number between 1 and 6." << endl;
    }
}

void print_main_menu()
{
#define MAKE_ITEM(name, label) MenuItemMain{MainMenuChoice::name, label},
    static const std::vector<MenuItemMain> kMainMenuItems = {MAIN_MENU_ITEMS(MAKE_ITEM)};
#undef MAKE_ITEM

    cout << "\nMain Menu:" << endl;
    for (size_t i = 0; i < kMainMenuItems.size(); ++i)
    {
        cout << (i + 1) << ". " << kMainMenuItems[i].label << endl;
    }
    cout << "Enter choice (1-" << kMainMenuItems.size() << "): " << endl;
}

bool try_parse_main_menu_command(const std::string &cmd, MainMenuChoice &choice)
{
    if (cmd.size() != 1 || !std::isdigit(static_cast<unsigned char>(cmd[0])))
    {
        return false;
    }

    switch (cmd[0])
    {
    case '1':
        choice = MainMenuChoice::StartNewGame;
        return true;
    case '2':
        choice = MainMenuChoice::BoardEditor;
        return true;
    case '3':
        choice = MainMenuChoice::LoadFromDatabase;
        return true;
    case '4':
        choice = MainMenuChoice::PLAY;
        return true;
    case '5':
        choice = MainMenuChoice::StartNetworkGame;
        return true;
    case '6':
        choice = MainMenuChoice::Quit;
        return true;
    default:
        return false;
    }
}

GameMenuChoice GameMenu(bool print_menu)
{
    // Build items from single source in utils.h to avoid duplication
#define MAKE_ITEM(name, label) MenuItemGame{GameMenuChoice::name, label},
    static const std::vector<MenuItemGame> kGameMenuItems = {GAME_MENU_ITEMS(MAKE_ITEM)};
#undef MAKE_ITEM

    auto printOptions = [&]()
    {
        print_game_menu();
    };

    if (print_menu)
        printOptions();

    while (true)
    {
        std::string cmd;
        if (!print_menu)
            cout << ">MATE ";

        if (!(std::cin >> cmd) || cmd.empty())
        {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            cout << "Unknown command." << endl;
            printOptions();
            print_menu = true;
            continue;
        }

        GameMenuChoice choice = GameMenuChoice::Help;
        if (try_parse_game_menu_command(cmd, choice))
        {
            if (choice == GameMenuChoice::Help)
            {
                printOptions();
                return GameMenuChoice::Help;
            }
            return choice;
        }

        cout << "Unknown command." << endl;
        printOptions();
        print_menu = true;
        continue;
    }
}

void print_game_menu()
{
    // Build items from single source in utils.h to avoid duplication
#define MAKE_ITEM(name, label) MenuItemGame{GameMenuChoice::name, label},
    static const std::vector<MenuItemGame> kGameMenuItems = {GAME_MENU_ITEMS(MAKE_ITEM)};
#undef MAKE_ITEM

    cout << "Game Menu:" << endl;
    for (const auto &it : kGameMenuItems)
    {
        cout << it.label << endl;
    }
    cout << "Enter command: " << endl;
}

bool try_parse_game_menu_command(const std::string &cmd, GameMenuChoice &choice)
{
    if (cmd.empty())
    {
        return false;
    }

    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(cmd[0])));
    switch (c)
    {
    case 'm':
        choice = GameMenuChoice::ManualMove;
        return true;
    case 's':
        choice = GameMenuChoice::SmartMove;
        return true;
    case 'p':
        choice = GameMenuChoice::MLMove;
        return true;
    case 'r':
        choice = GameMenuChoice::RandomMove;
        return true;
    case 'u':
        choice = GameMenuChoice::Undo;
        return true;
    case 'a':
        choice = GameMenuChoice::ListAllMoves;
        return true;
    case 'l':
        choice = GameMenuChoice::ShowHistory;
        return true;
    case 'w':
        choice = GameMenuChoice::WriteDB;
        return true;
    case 'h':
        choice = GameMenuChoice::Help;
        return true;
    case 'q':
        choice = GameMenuChoice::Quit;
        return true;
    default:
        return false;
    }
}

void debugMessage(const std::string &msg)
{
    if (enable_debug_messages)
    {
        cout << msg << endl;
    }
}
