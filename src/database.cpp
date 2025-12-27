#include "database.h"

static int callback(void *NotUsed, int argc, char **argv, char **azColName)
{
    int i;
    for (i = 0; i < argc; i++)
    {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

void openDatabase(chess currentGame)
{

    time_t now = time(0);
    tm *ltm = localtime(&now);

    string curTime = std::to_string(1900 + ltm->tm_year) + std::to_string(1 + ltm->tm_mon) + std::to_string(ltm->tm_mday) + "_" + std::to_string(ltm->tm_hour) + "_" + std::to_string(ltm->tm_min) + "_" + std::to_string(ltm->tm_sec);

    std::cout << curTime << std::endl;

    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;

    /* Open database */
    rc = sqlite3_open("chessyDB.db", &db);

    if (rc)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        //	      return(0);
    }
    else
    {
        fprintf(stdout, "Opened database successfully\n");
    }

    // string sq_string = "CREATE TABLE G" + curTime + "("
    //                                                 "ID INT PRIMARY KEY     NOT NULL,"
    //                                                 "MOVEDBY CHAR(1), MOTION CHAR(10),"
    //                                                 "A1 CHAR(2), A2 CHAR(2), A3 CHAR(2), A4 CHAR(2),"
    //                                                 "A5 CHAR(2), A6 CHAR(2), A7 CHAR(2), A8 CHAR(2),"
    //                                                 "B1 CHAR(2), B2 CHAR(2), B3 CHAR(2), B4 CHAR(2),"
    //                                                 "B5 CHAR(2), B6 CHAR(2), B7 CHAR(2), B8 CHAR(2),"
    //                                                 "C1 CHAR(2), C2 CHAR(2), C3 CHAR(2), C4 CHAR(2),"
    //                                                 "C5 CHAR(2), C6 CHAR(2), C7 CHAR(2), C8 CHAR(2),"
    //                                                 "D1 CHAR(2), D2 CHAR(2), D3 CHAR(2), D4 CHAR(2),"
    //                                                 "D5 CHAR(2), D6 CHAR(2), D7 CHAR(2), D8 CHAR(2),"
    //                                                 "E1 CHAR(2), E2 CHAR(2), E3 CHAR(2), E4 CHAR(2),"
    //                                                 "E5 CHAR(2), E6 CHAR(2), E7 CHAR(2), E8 CHAR(2),"
    //                                                 "F1 CHAR(2), F2 CHAR(2), F3 CHAR(2), F4 CHAR(2),"
    //                                                 "F5 CHAR(2), F6 CHAR(2), F7 CHAR(2), F8 CHAR(2),"
    //                                                 "G1 CHAR(2), G2 CHAR(2), G3 CHAR(2), G4 CHAR(2),"
    //                                                 "G5 CHAR(2), G6 CHAR(2), G7 CHAR(2), G8 CHAR(2),"
    //                                                 "H1 CHAR(2), H2 CHAR(2), H3 CHAR(2), H4 CHAR(2),"
    //                                                 "H5 CHAR(2), H6 CHAR(2), H7 CHAR(2), H8 CHAR(2), FIELDCOUNT INT);";

    string sq_string = "CREATE TABLE G" + curTime + "("
                                                    "ID INT PRIMARY KEY     NOT NULL,"
                                                    "MOVE_TYPE TEXT, MOVED_BY TEXT,"
                                                    "START_POS CHAR(2), DEST_POS CHAR(2),"
                                                    "START_PIECE TEXT, DEST_PIECE TEXT,"
                                                    "BOARD_COUNT INT);";

    /* Execute SQL statement */
    rc = sqlite3_exec(db, sq_string.c_str(), callback, 0, &zErrMsg);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        fprintf(stdout, "Table created successfully\n");
    }

    vector<chessMotionType> history = currentGame.getHistory();

    for (uint i = 0; i < history.size(); i++)
    {
        string move_type = moveTypeToString(history[i].type_of_move);
        string moved_by = movedByToString(history[i].moved_by_whom);
        string start_pos = std::string(1, history[i].start_position.coord.file) +
                           std::to_string(static_cast<int>(history[i].start_position.coord.rank));
        string dest_pos = std::string(1, history[i].dest_position.coord.file) +
                          std::to_string(static_cast<int>(history[i].dest_position.coord.rank));
        string start_piece = pieceCodeToString(history[i].start_position.piece.piece) +
                             "_" + playerColorToString(history[i].start_position.piece.color);
        string dest_piece = pieceCodeToString(history[i].dest_position.piece.piece) +
                            "_" + playerColorToString(history[i].dest_position.piece.color);

        int board_count = history[i].board_evaluation;

        sq_string = "INSERT INTO G" + curTime +
                    " (ID,MOVE_TYPE,MOVED_BY,START_POS,DEST_POS,START_PIECE,DEST_PIECE,BOARD_COUNT) VALUES (" +
                    std::to_string(i) + ", '" + move_type + "', '" + moved_by + "', '" + start_pos + "', '" + dest_pos + "', '" + start_piece + "', '" + dest_piece + "', " +
                    std::to_string(board_count) + ");";

        std::cout << sq_string << std::endl;

        /* Execute SQL statement */
        rc = sqlite3_exec(db, sq_string.c_str(), callback, 0, &zErrMsg);

        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
        }
        else
        {
            fprintf(stdout, "Record added successfully\n");
        }
    }
    sqlite3_close(db);
    //	   return 0;
}