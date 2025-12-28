#include "database.h"
#include <sstream>

void store_to_DB(chess currentGame)
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

    // Create BOARD table for full board snapshots
    const std::string board_table = "BOARD";
    {
        std::string board_sql = "CREATE TABLE IF NOT EXISTS " + board_table + "("
                                                                              "GAME_NAME TEXT,"
                                                                              "ID INT,"
                                                                              "A1 CHAR(2), A2 CHAR(2), A3 CHAR(2), A4 CHAR(2),"
                                                                              "A5 CHAR(2), A6 CHAR(2), A7 CHAR(2), A8 CHAR(2),"
                                                                              "B1 CHAR(2), B2 CHAR(2), B3 CHAR(2), B4 CHAR(2),"
                                                                              "B5 CHAR(2), B6 CHAR(2), B7 CHAR(2), B8 CHAR(2),"
                                                                              "C1 CHAR(2), C2 CHAR(2), C3 CHAR(2), C4 CHAR(2),"
                                                                              "C5 CHAR(2), C6 CHAR(2), C7 CHAR(2), C8 CHAR(2),"
                                                                              "D1 CHAR(2), D2 CHAR(2), D3 CHAR(2), D4 CHAR(2),"
                                                                              "D5 CHAR(2), D6 CHAR(2), D7 CHAR(2), D8 CHAR(2),"
                                                                              "E1 CHAR(2), E2 CHAR(2), E3 CHAR(2), E4 CHAR(2),"
                                                                              "E5 CHAR(2), E6 CHAR(2), E7 CHAR(2), E8 CHAR(2),"
                                                                              "F1 CHAR(2), F2 CHAR(2), F3 CHAR(2), F4 CHAR(2),"
                                                                              "F5 CHAR(2), F6 CHAR(2), F7 CHAR(2), F8 CHAR(2),"
                                                                              "G1 CHAR(2), G2 CHAR(2), G3 CHAR(2), G4 CHAR(2),"
                                                                              "G5 CHAR(2), G6 CHAR(2), G7 CHAR(2), G8 CHAR(2),"
                                                                              "H1 CHAR(2), H2 CHAR(2), H3 CHAR(2), H4 CHAR(2),"
                                                                              "H5 CHAR(2), H6 CHAR(2), H7 CHAR(2), H8 CHAR(2));";
        rc = sqlite3_exec(db, board_sql.c_str(), nullptr, nullptr, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "SQL error (create BOARD): %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
            sqlite3_close(db);
            return;
        }
        else
        {
            fprintf(stdout, "BOARD table ready\n");
        }
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

        // Also clear BOARD snapshots for this game
        char *delete_board_sql = sqlite3_mprintf("DELETE FROM %s WHERE GAME_NAME='%q';", board_table.c_str(), game_name.c_str());
        rc = sqlite3_exec(db, delete_board_sql, nullptr, nullptr, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "SQL error (delete BOARD): %s\n", zErrMsg);
            fprintf(stderr, "Failed SQL: %s\n", delete_board_sql);
            sqlite3_free(zErrMsg);
            zErrMsg = nullptr;
        }
        else
        {
            fprintf(stdout, "Old BOARD entries removed for GAME_NAME '%s'\n", game_name.c_str());
        }
        sqlite3_free(delete_board_sql);
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

    // Insert BOARD rows (full board per position in history)
    {
        auto square_code = [](const pieceType &pc)
        {
            char p = 'E';
            switch (pc.piece)
            {
            case pieceCode::pawn:
                p = 'P';
                break;
            case pieceCode::rook:
                p = 'R';
                break;
            case pieceCode::knight:
                p = 'N';
                break;
            case pieceCode::bishop:
                p = 'B';
                break;
            case pieceCode::queen:
                p = 'Q';
                break;
            case pieceCode::king:
                p = 'K';
                break;
            case pieceCode::empty:
            default:
                p = 'E';
                break;
            }
            char c = 'N';
            switch (pc.color)
            {
            case playerColor::white:
                c = 'W';
                break;
            case playerColor::black:
                c = 'B';
                break;
            case playerColor::none:
            default:
                c = 'N';
                break;
            }
            return std::string() + p + c; // two-character code
        };

        chessboard_historyType positions = currentGame.getPositionHistory();
        for (size_t idx = 0; idx < positions.size(); ++idx)
        {
            const chessboardType &board = positions[idx];
            std::ostringstream oss;
            oss << "INSERT INTO " << board_table << " (GAME_NAME,ID,";
            // Column list A1..H8
            for (char file = 'A'; file <= 'H'; ++file)
            {
                for (int rank = 1; rank <= 8; ++rank)
                {
                    oss << file << rank;
                    if (!(file == 'H' && rank == 8))
                        oss << ", ";
                }
            }
            oss << ") VALUES ('";
            char *gn = sqlite3_mprintf("%q", game_name.c_str());
            oss << gn << "', " << static_cast<int>(idx) << ", ";
            sqlite3_free(gn);

            for (char file = 'A'; file <= 'H'; ++file)
            {
                int fi = file - 'A';
                for (int rank = 1; rank <= 8; ++rank)
                {
                    int ri = rank - 1;
                    std::string code = square_code(board[fi][ri]);
                    oss << "'" << code << "'";
                    if (!(file == 'H' && rank == 8))
                        oss << ", ";
                }
            }
            oss << ");";

            std::string insert_board = oss.str();
            rc = sqlite3_exec(db, insert_board.c_str(), nullptr, nullptr, &zErrMsg);
            if (rc != SQLITE_OK)
            {
                fprintf(stderr, "SQL error (insert BOARD): %s\n", zErrMsg);
                fprintf(stderr, "Failed SQL: %s\n", insert_board.c_str());
                sqlite3_free(zErrMsg);
                zErrMsg = nullptr;
            }
            else
            {
                fprintf(stdout, "BOARD snapshot added (ID=%d)\n", static_cast<int>(idx));
            }
        }
    }
    sqlite3_close(db);
}