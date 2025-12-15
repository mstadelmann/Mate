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

    int showMenuAndGetIndex(const char *title, size_t count, const char *const *labels, bool print_menu = true)
    {
        int choice = -1;
        while (true)
        {
            if (print_menu)
            {
                cout << title << endl;
                for (size_t i = 0; i < count; ++i)
                {
                    cout << (i + 1) << ". " << labels[i] << endl;
                }
                cout << "Enter choice (1-" << count << "): " << endl;
            }
            else
            {
                cout << ">MATE ";
            }

            if (!(std::cin >> choice) || choice < 1 || choice > static_cast<int>(count))
            {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                cout << "Error: please enter a number between 1 and " << count << "." << endl;
                continue;
            }
            break;
        }
        return choice - 1; // zero-based index
    }
} // namespace

MainMenuChoice MainMenu(bool print_menu)
{
#define MAKE_ITEM(name, label) MenuItemMain{MainMenuChoice::name, label},
    static const std::vector<MenuItemMain> kMainMenuItems = {MAIN_MENU_ITEMS(MAKE_ITEM)};
#undef MAKE_ITEM

    std::vector<const char *> labels;
    labels.reserve(kMainMenuItems.size());
    for (const auto &it : kMainMenuItems)
        labels.push_back(it.label);

    int idx = showMenuAndGetIndex("Main Menu:", kMainMenuItems.size(), labels.data(), print_menu);
    return kMainMenuItems[static_cast<size_t>(idx)].choice;
}

GameMenuChoice GameMenu(bool print_menu)
{
#define MAKE_ITEM(name, label) MenuItemGame{GameMenuChoice::name, label},
    static const std::vector<MenuItemGame> kGameMenuItems = {GAME_MENU_ITEMS(MAKE_ITEM)};
#undef MAKE_ITEM

    std::vector<const char *> labels;
    labels.reserve(kGameMenuItems.size());
    for (const auto &it : kGameMenuItems)
        labels.push_back(it.label);

    int idx = showMenuAndGetIndex("Game Menu:", kGameMenuItems.size(), labels.data(), print_menu);
    return kGameMenuItems[static_cast<size_t>(idx)].choice;
}

void debugMessage(const std::string &msg)
{
    if (enable_debug_messages)
    {
        cout << msg << endl;
    }
}