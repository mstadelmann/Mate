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
#include "chess.h"

using std::cout;
using std::endl;
using std::string;
using std::tuple;

void printLogo(void);

#define MAIN_MENU_ITEMS(X)                           \
    X(StartNewGame, "Start new game")                \
    X(PLAY, "Play with current board configuration") \
    X(BoardEditor, "Create custom board position")   \
    X(LoadFromDatabase, "Load game from database")   \
    X(StartNetworkGame, "Start network game")        \
    X(Help, "Help")                                  \
    X(Quit, "Quit")

enum class MainMenuChoice
{
#define ENUM_ITEM(name, label) name,
    MAIN_MENU_ITEMS(ENUM_ITEM)
#undef ENUM_ITEM
};

MainMenuChoice MainMenu(bool print_menu = true);

#define GAME_MENU_ITEMS(X)                  \
    X(ManualMove, "Enter Manual Move")      \
    X(SmartMove, "Run Smart Move")          \
    X(RandomMove, "Run Random Move")        \
    X(Undo, "Undo Last Move")               \
    X(ListAllMoves, "List all legal moves") \
    X(ShowHistory, "List game history")     \
    X(WriteDB, "Write DB")                  \
    X(Help, "Show this help menu")          \
    X(Quit, "Quit Game and return to Main Menu")

enum class GameMenuChoice
{
#define ENUM_ITEM(name, label) name,
    GAME_MENU_ITEMS(ENUM_ITEM)
#undef ENUM_ITEM
};

GameMenuChoice GameMenu(bool print_menu = true);
void debugMessage(const std::string &msg);

#endif /* UTILS_H */
