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

    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;

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

    string sq_string = "CREATE TABLE G" + currentGame.gameName() + "("
                                                                   "ID INT PRIMARY KEY     NOT NULL,"
                                                                   "MOVE_TYPE TEXT, MOVED_BY TEXT,"
                                                                   "START_POS CHAR(2), DEST_POS CHAR(2),"
                                                                   "START_PIECE TEXT, DEST_PIECE TEXT,"
                                                                   "BOARD_COUNT INT);";

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

    auto pos_str = [](const boardPositionType &p)
    {
        return std::string(1, p.coord.file) + std::to_string(p.coord.rank);
    };
    auto piece_str = [](const pieceType &pc)
    {
        return pieceCodeToString(pc.piece) + "_" + playerColorToString(pc.color);
    };

    for (uint i = 0; i < history.size(); i++)
    {
        const auto &m = history[i];
        string move_type = moveTypeToString(m.type_of_move);
        string moved_by = movedByToString(m.moved_by_whom);
        string start_pos = pos_str(m.start_position);
        string dest_pos = pos_str(m.dest_position);
        string start_piece = piece_str(m.start_position.piece);
        string dest_piece = piece_str(m.dest_position.piece);

        int board_count = history[i].board_evaluation;

        sq_string = "INSERT INTO G" + currentGame.gameName() +
                    " (ID,MOVE_TYPE,MOVED_BY,START_POS,DEST_POS,START_PIECE,DEST_PIECE,BOARD_COUNT) VALUES (" +
                    std::to_string(i) + ", '" + move_type + "', '" + moved_by + "', '" + start_pos + "', '" + dest_pos + "', '" + start_piece + "', '" + dest_piece + "', " +
                    std::to_string(board_count) + ");";

        std::cout << sq_string << std::endl;

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
}