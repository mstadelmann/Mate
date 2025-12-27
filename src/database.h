#ifndef DATABASE_H_
#define DATABASE_H_

#include <stdio.h>
#include <iostream>
#include <tuple>
#include <vector>
#include <array>
#include <string>
#include <cstdlib>
#include <sqlite3.h>
#include <ctime>
#include <string>

#include "chess.h"

static int callback(void *NotUsed, int argc, char **argv, char **azColName);
void openDatabase(chess);
void writeDatabase(void);

#endif /* DATABASE_H_ */
