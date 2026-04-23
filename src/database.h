#ifndef DATABASE_H_
#define DATABASE_H_

#include <sqlite3.h>

#include "chess.h"

void store_to_DB(const chess &);
void write_moves(void);
void LoadFromDatabase(chess &);

#endif /* DATABASE_H_ */
