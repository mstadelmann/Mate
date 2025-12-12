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

void printLogo(void);

#define MAIN_MENU_ITEMS(X)                         \
    X(StartNewGame, "Start new game")              \
    X(BoardEditor, "Create custom board position") \
    X(LoadFromDatabase, "Load game from database") \
    X(StartNetworkGame, "Start network game")      \
    X(Help, "Help")                                \
    X(Quit, "Quit")

enum class MainMenuChoice
{
#define ENUM_ITEM(name, label) name,
    MAIN_MENU_ITEMS(ENUM_ITEM)
#undef ENUM_ITEM
};

MainMenuChoice MainMenu(void);

#define GAME_MENU_ITEMS(X)                  \
    X(ShowCount, "Show Count")              \
    X(WriteDB, "Write DB")                  \
    X(ManualMove, "Enter Manual Move")      \
    X(SmartMove, "Run Smart Move")          \
    X(RandomMove, "Run Random Move")        \
    X(Undo, "Undo Last Move")               \
    X(Redo, "Redo Last Move")               \
    X(Help, "Show this help menu")          \
    X(ListAllMoves, "List all legal moves") \
    X(ShowHistory, "List game history")     \
    X(Quit, "Quit Game and return to Main Menu")

enum class GameMenuChoice
{
#define ENUM_ITEM(name, label) name,
    GAME_MENU_ITEMS(ENUM_ITEM)
#undef ENUM_ITEM
};

GameMenuChoice GameMenu(void);

#endif /* UTILS_H */
