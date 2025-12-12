#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <iostream>
#include <tuple>
#include <vector>
#include <array>
#include <string>
#include <cstdlib>
#include <time.h>
#include <limits>

using std::cout;
using std::endl;
using std::string;
using std::tuple;

#define MAIN_MENU_ITEMS(X)                         \
    X(StartNewGame, "Start New Game")              \
    X(StartEmptyGame, "Start Empty Game")          \
    X(LoadFromDatabase, "Load Game from Database") \
    X(Help, "Help")                                \
    X(Quit, "Quit")

enum class MainMenuChoice
{
#define ENUM_ITEM(name, label) name,
    MAIN_MENU_ITEMS(ENUM_ITEM)
#undef ENUM_ITEM
};
void printLogo(void);
MainMenuChoice MainMenu(void);
void printGameMenu(void);
void debugMessage(string message, int msg_level);

#endif /* UTILS_H */
