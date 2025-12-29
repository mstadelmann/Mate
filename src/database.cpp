#include "database.h"
#include <sstream>
#include <algorithm>
#include <cctype>

void store_to_DB(const chess &currentGame)
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

static pieceType decode_square_code(const std::string &code)
{
    pieceType p{};
    p.piece = pieceCode::empty;
    p.color = playerColor::none;
    if (code.size() >= 2)
    {
        char pc = code[0];
        char cc = code[1];
        switch (pc)
        {
        case 'P':
            p.piece = pieceCode::pawn;
            break;
        case 'R':
            p.piece = pieceCode::rook;
            break;
        case 'N':
            p.piece = pieceCode::knight;
            break;
        case 'B':
            p.piece = pieceCode::bishop;
            break;
        case 'Q':
            p.piece = pieceCode::queen;
            break;
        case 'K':
            p.piece = pieceCode::king;
            break;
        case 'E':
        default:
            p.piece = pieceCode::empty;
            break;
        }
        switch (cc)
        {
        case 'W':
            p.color = playerColor::white;
            break;
        case 'B':
            p.color = playerColor::black;
            break;
        case 'N':
        default:
            p.color = playerColor::none;
            break;
        }
    }
    return p;
}

void LoadFromDatabase(chess &game)
{
    sqlite3 *db = nullptr;
    char *zErrMsg = nullptr;
    int rc = sqlite3_open("chessyDB.db", &db);
    if (rc)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        if (db)
            sqlite3_close(db);
        return;
    }

    // List games with move counts
    std::vector<std::pair<std::string, int>> games;
    {
        const char *sql = "SELECT GAME_NAME, COUNT(*) FROM Moves GROUP BY GAME_NAME ORDER BY GAME_NAME;";
        sqlite3_stmt *stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const unsigned char *gn = sqlite3_column_text(stmt, 0);
                int cnt = sqlite3_column_int(stmt, 1);
                games.emplace_back(std::string(reinterpret_cast<const char *>(gn ? gn : reinterpret_cast<const unsigned char *>(""))), cnt);
            }
        }
        sqlite3_finalize(stmt);
    }

    if (games.empty())
    {
        std::cout << "No games found in database." << std::endl;
        sqlite3_close(db);
        return;
    }

    std::cout << "Available games:" << std::endl;
    for (size_t i = 0; i < games.size(); ++i)
    {
        std::cout << "  [" << (i + 1) << "] " << games[i].first << " (" << games[i].second << " moves)" << std::endl;
    }

    std::string selectedName;
    while (selectedName.empty())
    {
        std::cout << "Pick a game by number or name (or 'q' to cancel): ";
        std::string choice;
        if (!std::getline(std::cin, choice))
        {
            std::cout << "Input error." << std::endl;
            sqlite3_close(db);
            return;
        }
        if (choice.empty())
        {
            // Newline leftover from prior input; re-prompt
            continue;
        }
        if (choice == "q" || choice == "Q")
        {
            sqlite3_close(db);
            std::cout << "Canceled load." << std::endl;
            return;
        }
        bool is_digits = std::all_of(choice.begin(), choice.end(), [](unsigned char ch)
                                     { return std::isdigit(ch); });
        if (is_digits)
        {
            int idx = std::stoi(choice);
            if (idx >= 1 && idx <= static_cast<int>(games.size()))
            {
                selectedName = games[idx - 1].first;
                break;
            }
        }
        // Try exact name match
        for (const auto &g : games)
        {
            if (g.first == choice)
            {
                selectedName = g.first;
                break;
            }
        }
        if (selectedName.empty())
        {
            std::cout << "Invalid selection. Try again." << std::endl;
        }
    }

    // Load BOARD snapshots for selected game
    std::vector<chessboardType> boards;
    {
        std::ostringstream oss;
        oss << "SELECT ID, ";
        for (char file = 'A'; file <= 'H'; ++file)
        {
            for (int rank = 1; rank <= 8; ++rank)
            {
                oss << file << rank;
                if (!(file == 'H' && rank == 8))
                    oss << ", ";
            }
        }
        oss << " FROM BOARD WHERE GAME_NAME=? ORDER BY ID ASC;";

        sqlite3_stmt *stmt = nullptr;
        rc = sqlite3_prepare_v2(db, oss.str().c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, selectedName.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                chessboardType board{};
                // Columns: 0 -> ID, 1..64 -> squares
                for (char file = 'A'; file <= 'H'; ++file)
                {
                    int fi = file - 'A';
                    for (int rank = 1; rank <= 8; ++rank)
                    {
                        int ri = rank - 1;
                        int col = 1 + fi * 8 + (rank - 1);
                        const unsigned char *txt = sqlite3_column_text(stmt, col);
                        std::string code = txt ? std::string(reinterpret_cast<const char *>(txt)) : std::string("EN");
                        board[fi][ri] = decode_square_code(code);
                    }
                }
                boards.push_back(board);
            }
        }
        sqlite3_finalize(stmt);
    }

    // Optional: Load simple move descriptors for UI
    std::vector<std::string> moveInfo;
    {
        const char *sql = "SELECT ID, MOVE_TYPE, MOVED_BY, START_POS, DEST_POS FROM Moves WHERE GAME_NAME=? ORDER BY ID ASC;";
        sqlite3_stmt *stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, selectedName.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                int id = sqlite3_column_int(stmt, 0);
                const char *mt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                const char *mb = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                const char *sp = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                const char *dp = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                std::ostringstream oss;
                oss << "#" << id << " "
                    << (mb ? mb : "") << " "
                    << (mt ? mt : "") << ": "
                    << (sp ? sp : "") << " -> "
                    << (dp ? dp : "");
                moveInfo.push_back(oss.str());
            }
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);

    if (boards.empty())
    {
        std::cout << "No board snapshots found for '" << selectedName << "'." << std::endl;
        return;
    }

    auto apply_board = [&](const chessboardType &board)
    {
        for (char file = 'A'; file <= 'H'; ++file)
        {
            int fi = file - 'A';
            for (int rank = 1; rank <= 8; ++rank)
            {
                int ri = rank - 1;
                boardCoordinateType c{file, rank};
                game.place_piece(c, board[fi][ri]);
            }
        }
    };

    int idx = 0;
    while (true)
    {
        apply_board(boards[idx]);
        std::cout << "\nBrowsing '" << selectedName << "' (" << (idx + 1) << "/" << boards.size() << ")" << std::endl;
        if (idx < static_cast<int>(moveInfo.size()))
        {
            std::cout << moveInfo[idx] << std::endl;
        }
        game.printCurrentGame();
        std::cout << "Use Left/Right arrows or 'a'/'d'. Enter=select, 'q'=cancel" << std::endl;

        std::string in;
        std::getline(std::cin, in);
        if (in.empty())
        {
            // Enter selects current position
            std::cout << "Loaded position #" << (idx + 1) << " for '" << selectedName << "'." << std::endl;
            return;
        }
        if (in == "q" || in == "Q")
        {
            std::cout << "Canceled load." << std::endl;
            return;
        }
        // Arrow escape sequences or fallback keys
        if (in == "\x1b[C" || in == "d" || in == "D" || in == "right")
        {
            if (idx + 1 < static_cast<int>(boards.size()))
                idx++;
        }
        else if (in == "\x1b[D" || in == "a" || in == "A" || in == "left")
        {
            if (idx > 0)
                idx--;
        }
    }
}