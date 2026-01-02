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

#define MAIN_MENU_ITEMS(X)                                        \
    X(StartNewGame, "Start new game")                             \
    X(BoardEditor, "Board editor (set up custom board position)") \
    X(LoadFromDatabase, "Load game from database")                \
    X(PLAY, "Play with current board configuration")              \
    X(StartNetworkGame, "Start network game")                     \
    X(Quit, "Quit")

enum class MainMenuChoice
{
#define ENUM_ITEM(name, label) name,
    MAIN_MENU_ITEMS(ENUM_ITEM)
#undef ENUM_ITEM
};

MainMenuChoice MainMenu(bool print_menu = true);

#define GAME_MENU_ITEMS(X)                      \
    X(ManualMove, " m: Enter manual move")      \
    X(SmartMove, " s: Run smart move")          \
    X(RandomMove, " r: Run random move")        \
    X(Undo, " u: Undo last move")               \
    X(ListAllMoves, " a: List all legal moves") \
    X(ShowHistory, " l: Show game history")     \
    X(WriteDB, " w: Write to database")         \
    X(Help, " h: Show help menu")               \
    X(Quit, " q: Quit game and return to main menu")

enum class GameMenuChoice
{
#define ENUM_ITEM(name, label) name,
    GAME_MENU_ITEMS(ENUM_ITEM)
#undef ENUM_ITEM
};

GameMenuChoice GameMenu(bool print_menu = true);
void debugMessage(const std::string &msg);

#endif /* UTILS_H */
