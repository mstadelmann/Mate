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
    X(StartNetworkGame, "Network game")                           \
    X(Settings, "Settings (view/edit config.json)")               \
    X(Quit, "Quit")

enum class MainMenuChoice
{
#define ENUM_ITEM(name, label) name,
    MAIN_MENU_ITEMS(ENUM_ITEM)
#undef ENUM_ITEM
};

MainMenuChoice MainMenu(bool print_menu = true);
void print_main_menu();
bool try_parse_main_menu_command(const std::string &cmd, MainMenuChoice &choice);

// ML move items are only ever shown/accepted when the corresponding model
// slot (config.h's model_a_path / model_b_path) is actually configured -
// see the ml_a_available/ml_b_available parameters on GameMenu(),
// print_game_menu() and try_parse_game_menu_command() below. They still
// need entries in this always-present list so GameMenuChoice has a value
// for them; only their visibility/acceptance is conditional.
#define GAME_MENU_ITEMS(X)                      \
    X(ManualMove, " m: Enter manual move")      \
    X(SmartMove, " s: Run smart move")          \
    X(MLMoveA, " p: Run ML move (model A)")     \
    X(MLMoveB, " o: Run ML move (model B)")     \
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

GameMenuChoice GameMenu(bool print_menu, bool ml_a_available, bool ml_b_available);
void print_game_menu(bool ml_a_available, bool ml_b_available);
bool try_parse_game_menu_command(const std::string &cmd, GameMenuChoice &choice, bool ml_a_available, bool ml_b_available);
void debugMessage(const std::string &msg);

#endif /* UTILS_H */
