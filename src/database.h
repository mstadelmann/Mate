#ifndef DATABASE_H_
#define DATABASE_H_

#include <sqlite3.h>
#include <string>
#include <vector>

#include "chess.h"

struct DatabaseGameSummary
{
    std::string name;
    int move_count = 0;
};

void store_to_DB(const chess &);
void write_moves(void);
void LoadFromDatabase(chess &);
bool list_database_games(std::vector<DatabaseGameSummary> &games, std::string &error_message);
bool load_database_game_snapshots(const std::string &game_name,
                                  std::vector<boardType> &boards,
                                  std::vector<std::string> &move_info,
                                  std::string &error_message);

#endif /* DATABASE_H_ */
