#include "database.h"

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

    const std::string table_name = "Moves";
    string sq_string = "CREATE TABLE IF NOT EXISTS " + table_name + "("
                                                                    "GAME_NAME TEXT,"
                                                                    "ID INT,"
                                                                    "MOVE_TYPE TEXT, MOVED_BY TEXT,"
                                                                    "START_POS CHAR(2), DEST_POS CHAR(2),"
                                                                    "START_PIECE TEXT, DEST_PIECE TEXT,"
                                                                    "BOARD_COUNT INT);";

    rc = sqlite3_exec(db, sq_string.c_str(), nullptr, nullptr, &zErrMsg);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        fprintf(stdout, "Table created successfully\n");
    }

    // If game already exists, warn and clear old entries
    const std::string game_name = currentGame.gameName();
    int existing_count = 0;
    {
        sqlite3_stmt *count_stmt = nullptr;
        std::string count_sql = "SELECT COUNT(*) FROM " + table_name + " WHERE GAME_NAME=?;";
        rc = sqlite3_prepare_v2(db, count_sql.c_str(), -1, &count_stmt, nullptr);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(count_stmt, 1, game_name.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(count_stmt) == SQLITE_ROW)
            {
                existing_count = sqlite3_column_int(count_stmt, 0);
            }
        }
        sqlite3_finalize(count_stmt);
    }

    if (existing_count > 0)
    {
        fprintf(stdout, "Warning: GAME_NAME '%s' already exists (%d rows). Overwriting...\n", game_name.c_str(), existing_count);
        char *delete_sql = sqlite3_mprintf("DELETE FROM %s WHERE GAME_NAME='%q';", table_name.c_str(), game_name.c_str());
        rc = sqlite3_exec(db, delete_sql, nullptr, nullptr, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "SQL error (delete old): %s\n", zErrMsg);
            fprintf(stderr, "Failed SQL: %s\n", delete_sql);
            sqlite3_free(zErrMsg);
            zErrMsg = nullptr;
        }
        else
        {
            fprintf(stdout, "Old entries removed for GAME_NAME '%s'\n", game_name.c_str());
        }
        sqlite3_free(delete_sql);
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

    for (size_t i = 0; i < history.size(); ++i)
    {
        const auto &m = history[i];
        string move_type = moveTypeToString(m.type_of_move);
        string moved_by = movedByToString(m.moved_by_whom);
        string start_pos = pos_str(m.start_position);
        string dest_pos = pos_str(m.dest_position);
        string start_piece = piece_str(m.start_position.piece);
        string dest_piece = piece_str(m.dest_position.piece);

        int board_count = history[i].board_evaluation;

        // Use sqlite3_mprintf to escape text values (%q)
        char *insert_sql = sqlite3_mprintf(
            "INSERT INTO %s (GAME_NAME,ID,MOVE_TYPE,MOVED_BY,START_POS,DEST_POS,START_PIECE,DEST_PIECE,BOARD_COUNT) VALUES ('%q', %d, '%q', '%q', '%q', '%q', '%q', '%q', %d);",
            table_name.c_str(),
            currentGame.gameName().c_str(),
            static_cast<int>(i),
            move_type.c_str(),
            moved_by.c_str(),
            start_pos.c_str(),
            dest_pos.c_str(),
            start_piece.c_str(),
            dest_piece.c_str(),
            board_count);

        std::cout << insert_sql << std::endl;

        rc = sqlite3_exec(db, insert_sql, nullptr, nullptr, &zErrMsg);

        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "SQL error (insert): %s\n", zErrMsg);
            fprintf(stderr, "Failed SQL: %s\n", insert_sql);
            sqlite3_free(zErrMsg);
            zErrMsg = nullptr;
        }
        else
        {
            fprintf(stdout, "Record added successfully\n");
        }
        sqlite3_free(insert_sql);
    }
    sqlite3_close(db);
}