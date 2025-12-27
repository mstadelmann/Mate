#include "chess.h"
#include "utils.h"
#include <iostream>
#include <random>

chess::chess()
{
    // default values
    replace_black_pawn = true;
    RecFuncCounter = 0;
    white_checked = false;
    black_checked = false;
    white_checkmate = false;
    black_checkmate = false;

    // Initialize players
    current_player = playerColor::white;
    other_player = playerColor::black;

    // Initialize an empty board
    for (int row = 0; row < 8; ++row)
    {
        for (int col = 0; col < 8; ++col)
        {
            chessboard[row][col].piece = pieceCode::empty;
            chessboard[row][col].color = playerColor::none;
        }
    }
}

chess::~chess()
{
    std::cout << "Destroying chess game..." << std::endl;
}

bool chess::check_board_valid()
{
    int white_king_count = 0;
    int black_king_count = 0;

    for (int file = 0; file < 8; ++file)
    {
        for (int rank = 0; rank < 8; ++rank)
        {
            const pieceType &pc = chessboard[file][rank];
            if (pc.piece == pieceCode::king)
            {
                if (pc.color == playerColor::white)
                    white_king_count++;
                else if (pc.color == playerColor::black)
                    black_king_count++;
            }
        }
    }

    return (white_king_count == 1) && (black_king_count == 1);
}

void chess::place_piece(boardPositionType position)
{
    if (validatePosition(position.coord))
    {
        at(position.coord) = position.piece;
    }
    else
    {
        std::cerr << "Error: Invalid chess position." << std::endl;
    }
}

void chess::place_piece(boardCoordinateType coordinates, pieceType piece)
{
    if (validatePosition(coordinates))
    {
        at(coordinates) = piece;
    }
    else
    {
        std::cerr << "Error: Invalid chess position." << std::endl;
    }
}

boardPositionType chess::query_position(boardCoordinateType coordinates)
{
    boardPositionType result;
    result.coord.file = coordinates.file;
    result.coord.rank = coordinates.rank;
    if (validatePosition(coordinates))
    {
        result.piece = at(coordinates);
    }
    else
    {
        std::cerr << "Error: Invalid chess position." << std::endl;
        result.piece.piece = pieceCode::empty;
        result.piece.color = playerColor::none;
    }
    return result;
}

void chess::load_starting_position()
{
    for (int file = 0; file < 8; ++file)
    {
        char current_file = static_cast<char>('A' + file);
        place_piece({current_file, 2, {pieceCode::pawn, playerColor::white}});
        place_piece({current_file, 7, {pieceCode::pawn, playerColor::black}});
    }

    place_piece({'A', 1, {pieceCode::rook, playerColor::white}});
    place_piece({'H', 1, {pieceCode::rook, playerColor::white}});
    place_piece({'A', 8, {pieceCode::rook, playerColor::black}});
    place_piece({'H', 8, {pieceCode::rook, playerColor::black}});

    place_piece({'B', 1, {pieceCode::knight, playerColor::white}});
    place_piece({'G', 1, {pieceCode::knight, playerColor::white}});
    place_piece({'B', 8, {pieceCode::knight, playerColor::black}});
    place_piece({'G', 8, {pieceCode::knight, playerColor::black}});

    place_piece({'C', 1, {pieceCode::bishop, playerColor::white}});
    place_piece({'F', 1, {pieceCode::bishop, playerColor::white}});
    place_piece({'C', 8, {pieceCode::bishop, playerColor::black}});
    place_piece({'F', 8, {pieceCode::bishop, playerColor::black}});

    place_piece({'D', 1, {pieceCode::queen, playerColor::white}});
    place_piece({'D', 8, {pieceCode::queen, playerColor::black}});

    place_piece({'E', 1, {pieceCode::king, playerColor::white}});
    place_piece({'E', 8, {pieceCode::king, playerColor::black}});
}

std::string chess::current_player_string() const
{
    switch (current_player)
    {
    case playerColor::white:
        return "white";
    case playerColor::black:
        return "black";
    case playerColor::none:
    default:
        return "none";
    }
}

void chess::printCurrentGame()
{
    std::cout << "\n     A   B   C   D   E   F   G   H  \n";

    for (int rank = 7; rank >= 0; rank--)
    {
        std::cout << "   --------------------------------- \n";
        std::cout << " " << rank + 1 << " ";
        for (int file = 0; file < 8; file++)
        {
            std::cout << "| ";
            const pieceType pc = chessboard.at(file).at(rank);
            const std::string ch = pieceTypeToChar(pc);
            if (!ch.empty())
                std::cout << ch << " ";
            else
                std::cout << "  ";
        }

        std::cout << "| " << rank + 1;
        if (rank == 7)
        {
            std::cout << "     Current Player: " << current_player_string() << "\n";
        }
        else if (rank == 6)
        {
            std::cout << "     Moves played: " << gameHistory.size() << "\n";
        }
        else if (rank == 5)
        {
            string last_move_str = "None";
            if (!gameHistory.empty())
            {
                chessMotionType last_move = gameHistory.back();
                last_move_str = pieceTypeToChar(last_move.start_position.piece) + " " +
                                last_move.start_position.coord.file + std::to_string(last_move.start_position.coord.rank) + " - " +
                                last_move.dest_position.coord.file + std::to_string(last_move.dest_position.coord.rank);
            }

            std::cout << "     Last move: " << last_move_str << "\n";
        }
        else if (rank == 4)
        {
            std::cout << "     Board evaluation: " << evaluateBoard() << "\n";
        }
        else if (rank == 3)
        {
            if (white_checkmate)
                std::cout << "     Checkmate! Black wins.\n";
            else if (black_checkmate)
                std::cout << "     Checkmate! White wins.\n";
            else if (white_checked)
                std::cout << "     White is in check!\n";
            else if (black_checked)
                std::cout << "     Black is in check!\n";
            else
                std::cout << "\n";
        }
        else
        {
            std::cout << "\n";
        }
    }

    std::cout << "   ---------------------------------";
    std::cout << "\n     A   B   C   D   E   F   G   H  \n\n\n";
}

boardCoordinateType chess::chessCoordinatesFromString(const std::string &coordStr)
{
    if (coordStr.length() != 2)
    {
        throw std::invalid_argument("Invalid coordinate string length");
    }

    char file = toupper(coordStr[0]);
    int rank = coordStr[1] - '0';

    if (file < 'A' || file > 'H' || rank < 1 || rank > 8)
    {
        throw std::invalid_argument("Invalid chess coordinates");
    }

    return {file, rank};
}

bool chess::randomMove()
{
    vector<chessMotionType> legalMoves = findAllLegalMoves();
    if (legalMoves.empty())
    {
        std::cout << "No legal moves available." << std::endl;
        return false;
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<std::size_t> dist(0, legalMoves.size() - 1);

    chessMotionType moveToMake = legalMoves[dist(gen)];
    executeMove(moveToMake);
    swapPlayers();
    return true;
}

bool chess::manualMove()
{
    string startField, endField;
    std::cout << "Enter move (e.g. E2 E4): " << std::flush;
    std::cin >> startField >> endField;

    try
    {
        boardCoordinateType startCoord = chessCoordinatesFromString(startField);
        boardCoordinateType endCoord = chessCoordinatesFromString(endField);

        boardPositionType startPos = query_position(startCoord);
        boardPositionType endPos = query_position(endCoord);

        chessMotionType moveToMake = {startPos, endPos, moveType::undefined, moved_by::human, 0};

        vector<chessMotionType> legalMoves = findAllLegalMoves();

        for (const auto &legalMove : legalMoves)
        {
            if (legalMove.start_position.coord.file == moveToMake.start_position.coord.file &&
                legalMove.start_position.coord.rank == moveToMake.start_position.coord.rank &&
                legalMove.dest_position.coord.file == moveToMake.dest_position.coord.file &&
                legalMove.dest_position.coord.rank == moveToMake.dest_position.coord.rank)
            {
                moveToMake.type_of_move = legalMove.type_of_move;
                executeMove(moveToMake);
                swapPlayers();
                return true;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "Error: " << e.what() << std::endl;
        return false;
    }

    std::cout << "Illegal move." << std::endl;
    return false;
}

vector<chessMotionType> chess::findAllLegalMoves()
{
    vector<chessMotionType> allLegalMoves;

    // For each piece of the current player, find its legal moves
    vector<boardPositionType> playerPieces = get_all_pieces_of_color(current_player);
    for (const auto &piecePos : playerPieces)
    {
        vector<chessMotionType> currentLegalMoves;
        // Depending on the piece type, find its legal moves
        switch (piecePos.piece.piece)
        {
        case pieceCode::pawn:
        {
            currentLegalMoves = findLegalPawnMoves(piecePos.coord);
            allLegalMoves.insert(allLegalMoves.end(), currentLegalMoves.begin(), currentLegalMoves.end());
            break;
        }
        case pieceCode::rook:
            currentLegalMoves = findLegalRookMoves(piecePos.coord);
            allLegalMoves.insert(allLegalMoves.end(), currentLegalMoves.begin(), currentLegalMoves.end());
            break;
        case pieceCode::knight:
            currentLegalMoves = findLegalKnightMoves(piecePos.coord);
            allLegalMoves.insert(allLegalMoves.end(), currentLegalMoves.begin(), currentLegalMoves.end());
            break;
        case pieceCode::bishop:
            currentLegalMoves = findLegalBishopMoves(piecePos.coord);
            allLegalMoves.insert(allLegalMoves.end(), currentLegalMoves.begin(), currentLegalMoves.end());
            break;
        case pieceCode::queen:
            currentLegalMoves = findLegalQueenMoves(piecePos.coord);
            allLegalMoves.insert(allLegalMoves.end(), currentLegalMoves.begin(), currentLegalMoves.end());
            break;
        case pieceCode::king:
            currentLegalMoves = findLegalKingMoves(piecePos.coord);
            allLegalMoves.insert(allLegalMoves.end(), currentLegalMoves.begin(), currentLegalMoves.end());
            break;
        default:
            break;
        }
    }

    return allLegalMoves;
}

vector<boardPositionType> chess::get_all_pieces_of_color(playerColor color)
{
    vector<boardPositionType> piecesList;

    for (int file = 0; file < 8; file++)
    {
        for (int rank = 0; rank < 8; rank++)
        {
            pieceType piece = chessboard.at(file).at(rank);
            if (piece.color == color)
            {
                boardPositionType pos = {{static_cast<char>('A' + file), rank + 1}, piece};
                piecesList.push_back(pos);
            }
        }
    }
    return piecesList;
}

void chess::executeMove(chessMotionType moveToExecute)
{
    // Save current board position to history before making the move
    gamePositionHistory.push_back(chessboard);

    boardPositionType startPos = moveToExecute.start_position;
    boardPositionType destPos = moveToExecute.dest_position;

    if (moveToExecute.type_of_move == moveType::castling_kingside)
    {
        place_piece(startPos.coord, {pieceCode::empty, playerColor::none});
        place_piece(destPos.coord, {pieceCode::empty, playerColor::none});

        if (current_player == playerColor::white)
        {
            place_piece({'G', 1}, {pieceCode::king, playerColor::white});
            place_piece({'F', 1}, {pieceCode::rook, playerColor::white});
        }
        else
        {
            place_piece({'G', 8}, {pieceCode::king, playerColor::black});
            place_piece({'F', 8}, {pieceCode::rook, playerColor::black});
        }
    }

    else if (moveToExecute.type_of_move == moveType::castling_queenside)
    {
        place_piece(startPos.coord, {pieceCode::empty, playerColor::none});
        place_piece(destPos.coord, {pieceCode::empty, playerColor::none});
        if (current_player == playerColor::white)
        {
            place_piece({'C', 1}, {pieceCode::king, playerColor::white});
            place_piece({'D', 1}, {pieceCode::rook, playerColor::white});
        }
        else
        {
            place_piece({'C', 8}, {pieceCode::king, playerColor::black});
            place_piece({'D', 8}, {pieceCode::rook, playerColor::black});
        }
    }
    else if (moveToExecute.type_of_move == moveType::en_passant)
    {
        place_piece(destPos.coord, startPos.piece);
        place_piece(startPos.coord, {pieceCode::empty, playerColor::none});
        // remove the captured pawn
        if (current_player == playerColor::white)
        {
            boardCoordinateType capturedPawnCoord = {destPos.coord.file, destPos.coord.rank - 1};
            place_piece(capturedPawnCoord, {pieceCode::empty, playerColor::none});
        }
        else
        {
            boardCoordinateType capturedPawnCoord = {destPos.coord.file, destPos.coord.rank + 1};
            place_piece(capturedPawnCoord, {pieceCode::empty, playerColor::none});
        }
    }
    else if (query_position(moveToExecute.dest_position.coord).piece.color == other_player)
    { // capture move
        place_piece(destPos.coord, startPos.piece);
        place_piece(startPos.coord, {pieceCode::empty, playerColor::none});
    }
    else
    { // normal move
        place_piece(destPos.coord, startPos.piece);
        place_piece(startPos.coord, {pieceCode::empty, playerColor::none});
    }

    // check if pawn reached last row -> new queen
    if (query_position(destPos.coord).piece.piece == pieceCode::pawn)
    {
        if (((current_player == playerColor::white) && (destPos.coord.rank == 8)) || ((current_player == playerColor::black) && (destPos.coord.rank == 1)))
        {
            moveToExecute.type_of_move = moveType::promotion_queen;
            place_piece(destPos.coord, {pieceCode::queen, current_player});
        }
    }

    // save move history
    gameHistory.push_back(moveToExecute);

    // Switch current player
    // std::swap(current_player, other_player);
}

void chess::reverseMove()
{
    if (gameHistory.empty() || gamePositionHistory.empty())
    {
        std::cout << "No moves to undo." << std::endl;
        return;
    }

    // Remove the last move from history
    gameHistory.pop_back();
    // Restore the previous board position
    chessboard = gamePositionHistory[gamePositionHistory.size() - 1];
    gamePositionHistory.pop_back();

    // Switch current player
    // game.swapPlayers();
}

boardCoordinateType chess::findKing()
{
    for (int file = 0; file < 8; file++)
    {
        for (int rank = 0; rank < 8; rank++)
        {
            pieceType piece = chessboard.at(file).at(rank);
            if (piece.piece == pieceCode::king && piece.color == current_player)
            {
                return {static_cast<char>('A' + file), rank + 1};
            }
        }
    }
    // Return an invalid position if the king is not found
    return {'Z', -1};
}

void chess::detectCheckmate()
{

    for (int i = 0; i < 2; i++)
    {
        if (current_player == playerColor::white)
        {
            white_checked = currentlyChecked();
            if (white_checked)
            {
                vector<chessMotionType> legalMoves = findAllLegalMoves();
                white_checkmate = legalMoves.empty();
            }
            else
            {
                white_checkmate = false;
            }
        }
        else
        {
            black_checked = currentlyChecked();
            if (black_checked)
            {
                vector<chessMotionType> legalMoves = findAllLegalMoves();
                black_checkmate = legalMoves.empty();
            }
            else
            {
                black_checkmate = false;
            }
        }
        swapPlayers();
    }
}

bool chess::currentlyChecked()
{
    boardCoordinateType kingPos = findKing();
    if (!validatePosition(kingPos))
    {
        return false; // no king found
    }

    // Check if the current player's king is attacked by an opposing pawn
    int dir = (current_player == playerColor::white) ? 1 : -1; // squares from which opposing pawns would attack
    for (int df : {-1, 1})
    {
        boardCoordinateType attacker = {static_cast<char>(kingPos.file + df), kingPos.rank + dir};
        if (validatePosition(attacker))
        {
            pieceType p = query_position(attacker).piece;
            if (p.piece == pieceCode::pawn && p.color == other_player)
            {
                return true;
            }
        }
    }

    // Check if the current player's king is attacked by an opposing rook or queen (horizontal and vertical)
    const std::array<std::pair<int, int>, 4> directions = {{
        {1, 0},  // right
        {-1, 0}, // left
        {0, 1},  // up
        {0, -1}  // down
    }};

    for (const auto &d : directions)
    {
        int df = d.first;
        int dr = d.second;
        boardCoordinateType cur = {kingPos.file, kingPos.rank};

        while (true)
        {
            cur.file = static_cast<char>(cur.file + df);
            cur.rank = cur.rank + dr;

            if (!validatePosition(cur))
                break;

            pieceType p = query_position(cur).piece;
            if (p.piece == pieceCode::empty)
                continue;

            if (p.color == other_player && (p.piece == pieceCode::rook || p.piece == pieceCode::queen))
                return true;

            break; // blocked by any piece
        }
    }

    // Check if the current player's king is attacked by an opposing knight
    const std::array<std::pair<int, int>, 8> knightOffsets = {{{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}}};

    for (const auto &o : knightOffsets)
    {
        boardCoordinateType attacker = {
            static_cast<char>(kingPos.file + o.first),
            kingPos.rank + o.second};

        if (validatePosition(attacker))
        {
            pieceType p = query_position(attacker).piece;
            if (p.piece == pieceCode::knight && p.color == other_player)
            {
                return true;
            }
        }
    }

    // Check if the current player's king is attacked by an opposing bishop or queen (diagonals)
    const std::array<std::pair<int, int>, 4> diagDirs = {{
        {1, 1},  // up-right
        {-1, 1}, // up-left
        {1, -1}, // down-right
        {-1, -1} // down-left
    }};

    for (const auto &d : diagDirs)
    {
        int df = d.first;
        int dr = d.second;
        boardCoordinateType cur = {kingPos.file, kingPos.rank};

        while (true)
        {
            cur.file = static_cast<char>(cur.file + df);
            cur.rank = cur.rank + dr;

            if (!validatePosition(cur))
                break;

            pieceType p = query_position(cur).piece;
            if (p.piece == pieceCode::empty)
                continue;

            if (p.color == other_player && (p.piece == pieceCode::bishop || p.piece == pieceCode::queen))
                return true;

            break; // blocked by any piece
        }
    }

    return false;
}

bool chess::check_move_legal(chessMotionType moveToTest)
{

    executeMove(moveToTest);
    bool cur_checked = currentlyChecked();
    reverseMove();
    return !cur_checked;
}

vector<chessMotionType> chess::findLegalPawnMoves(boardCoordinateType from)
{
    vector<chessMotionType> legalMoves;

    const int direction = (current_player == playerColor::white) ? 1 : -1; // white moves up, black down
    const int startRank = (current_player == playerColor::white) ? 2 : 7;  // zero indexed!

    // Single square forward
    boardCoordinateType ahead = {from.file, from.rank + direction};
    if (validatePosition(ahead) && query_position(ahead).piece.piece == pieceCode::empty)
    {
        boardPositionType start = query_position(from);
        boardPositionType dest = query_position(ahead);

        chessMotionType move = {start, dest, moveType::undefined, moved_by::none, 0};

        if (check_move_legal(move))
            legalMoves.push_back(move);
    }

    // Two squares forward (only from starting rank, and only if path is clear)
    if (from.rank == startRank)
    {
        boardCoordinateType twoAhead = {from.file, from.rank + 2 * direction};
        if (validatePosition(twoAhead) && query_position(twoAhead).piece.piece == pieceCode::empty && query_position(ahead).piece.piece == pieceCode::empty)
        {
            chessMotionType move = {query_position(from), query_position(twoAhead), moveType::undefined, moved_by::none, 0};
            if (check_move_legal(move))
                legalMoves.push_back(move);
        }
    }

    // Captures (diagonals)
    for (int df : {-1, 1})
    {
        boardCoordinateType diag = {static_cast<char>(from.file + df), from.rank + direction};
        if (validatePosition(diag))
        {
            pieceType targetPiece = query_position(diag).piece;
            if (targetPiece.piece != pieceCode::empty && targetPiece.color == other_player)
            {
                chessMotionType move = {query_position(from), query_position(diag), moveType::undefined, moved_by::none, 0};
                if (check_move_legal(move))
                    legalMoves.push_back(move);
            }
        }
    }

    return legalMoves;
}

vector<chessMotionType> chess::findLegalRookMoves(boardCoordinateType from)
{
    vector<chessMotionType> legalMoves;

    const std::array<std::pair<int, int>, 4> directions = {{
        {1, 0},  // right
        {-1, 0}, // left
        {0, 1},  // up
        {0, -1}  // down
    }};

    for (const auto &d : directions)
    {
        int df = d.first;
        int dr = d.second;
        boardCoordinateType cur = from;

        while (true)
        {
            cur.file = static_cast<char>(cur.file + df);
            cur.rank = cur.rank + dr;

            if (!validatePosition(cur))
                break;

            pieceType p = query_position(cur).piece;
            if (p.piece == pieceCode::empty)
            {
                chessMotionType move = {query_position(from), query_position(cur), moveType::undefined, moved_by::none, 0};
                if (check_move_legal(move))
                    legalMoves.push_back(move);
            }
            else
            {
                if (p.color == other_player)
                {
                    chessMotionType move = {query_position(from), query_position(cur), moveType::undefined, moved_by::none, 0};
                    if (check_move_legal(move))
                        legalMoves.push_back(move);
                }
                break; // blocked by any piece
            }
        }
    }

    return legalMoves;
}

vector<chessMotionType> chess::findLegalKnightMoves(boardCoordinateType from)
{
    vector<chessMotionType> legalMoves;

    const std::array<std::pair<int, int>, 8> knightOffsets = {{{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}}};

    for (const auto &o : knightOffsets)
    {
        boardCoordinateType to = {
            static_cast<char>(from.file + o.first),
            from.rank + o.second};

        if (validatePosition(to))
        {
            pieceType targetPiece = query_position(to).piece;
            if (targetPiece.piece == pieceCode::empty || targetPiece.color == other_player)
            {
                chessMotionType move = {query_position(from), query_position(to), moveType::undefined, moved_by::none, 0};
                if (check_move_legal(move))
                    legalMoves.push_back(move);
            }
        }
    }

    return legalMoves;
}

vector<chessMotionType> chess::findLegalBishopMoves(boardCoordinateType from)
{
    vector<chessMotionType> legalMoves;

    const std::array<std::pair<int, int>, 4> directions = {{
        {1, 1},  // up-right
        {-1, 1}, // up-left
        {1, -1}, // down-right
        {-1, -1} // down-left
    }};

    for (const auto &d : directions)
    {
        int df = d.first;
        int dr = d.second;
        boardCoordinateType cur = from;

        while (true)
        {
            cur.file = static_cast<char>(cur.file + df);
            cur.rank = cur.rank + dr;

            if (!validatePosition(cur))
                break;

            pieceType p = query_position(cur).piece;
            if (p.piece == pieceCode::empty)
            {
                chessMotionType move = {query_position(from), query_position(cur), moveType::undefined, moved_by::none, 0};
                if (check_move_legal(move))
                    legalMoves.push_back(move);
            }
            else
            {
                if (p.color == other_player)
                {
                    chessMotionType move = {query_position(from), query_position(cur), moveType::undefined, moved_by::none, 0};
                    if (check_move_legal(move))
                        legalMoves.push_back(move);
                }
                break; // blocked by any piece
            }
        }
    }

    return legalMoves;
}

vector<chessMotionType> chess::findLegalQueenMoves(boardCoordinateType from)
{
    vector<chessMotionType> legalMoves;

    // Combine rook and bishop moves
    vector<chessMotionType> rookMoves = findLegalRookMoves(from);
    vector<chessMotionType> bishopMoves = findLegalBishopMoves(from);

    legalMoves.insert(legalMoves.end(), rookMoves.begin(), rookMoves.end());
    legalMoves.insert(legalMoves.end(), bishopMoves.begin(), bishopMoves.end());

    return legalMoves;
}

vector<chessMotionType> chess::findLegalKingMoves(boardCoordinateType from)
{
    vector<chessMotionType> legalMoves;

    const std::array<std::pair<int, int>, 8> kingOffsets = {{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}}};

    for (const auto &o : kingOffsets)
    {
        boardCoordinateType to = {
            static_cast<char>(from.file + o.first),
            from.rank + o.second};

        if (validatePosition(to))
        {
            pieceType targetPiece = query_position(to).piece;
            if (targetPiece.piece == pieceCode::empty || targetPiece.color == other_player)
            {
                chessMotionType move = {query_position(from), query_position(to), moveType::undefined, moved_by::none, 0};
                if (check_move_legal(move))
                    legalMoves.push_back(move);
            }
        }
    }

    return legalMoves;
}

bool chess::validatePosition(boardCoordinateType coord)
{
    if ((coord.rank >= 1) && (coord.rank <= 8) && (coord.file >= 'A') && (coord.file <= 'H'))
    {
        return true;
    }
    else
    {
        return false;
    }
}

void chess::listLegalMoves()
{
    vector<chessMotionType> legalMoves = findAllLegalMoves();
    std::cout << "Legal moves for " << current_player_string() << ":\n";
    for (const auto &move : legalMoves)
    {
        std::cout << pieceTypeToChar(move.start_position.piece) << " at "
                  << move.start_position.coord.file << move.start_position.coord.rank
                  << " to "
                  << move.dest_position.coord.file << move.dest_position.coord.rank
                  << "\n";
    }
    std::cout << "Total legal moves: " << legalMoves.size() << "\n";
}

void chess::listMoveHistory()
{
    std::cout << "Move history:\n";
    for (size_t i = 0; i < gameHistory.size(); ++i)
    {
        const auto &move = gameHistory[i];
        std::cout << i + 1 << ". "
                  << pieceTypeToChar(move.start_position.piece) << " from "
                  << move.start_position.coord.file << move.start_position.coord.rank
                  << " to "
                  << move.dest_position.coord.file << move.dest_position.coord.rank
                  << "\n";
    }
}

string chess::pieceTypeToChar(pieceType pc)
{
    if (pc.color == playerColor::white)
    {
        switch (pc.piece)
        {
        case pieceCode::king:
            return "\u2654"; // ♔ -> white king
        case pieceCode::queen:
            return "\u2655"; // ♕ -> white queen
        case pieceCode::rook:
            return "\u2656"; // ♖ -> white rook
        case pieceCode::bishop:
            return "\u2657"; // ♗ -> white bishop
        case pieceCode::knight:
            return "\u2658"; // ♘ -> white knight
        case pieceCode::pawn:
            return "\u2659"; // ♙ -> white pawn
        default:
            return "";
        }
    }
    else if (pc.color == playerColor::black)
    {
        switch (pc.piece)
        {
        case pieceCode::king:
            return "\u265A"; // ♚ -> black king
        case pieceCode::queen:
            return "\u265B"; // ♛ -> black queen
        case pieceCode::rook:
            return "\u265C"; // ♜ -> black rook
        case pieceCode::bishop:
            return "\u265D"; // ♝ -> black bishop
        case pieceCode::knight:
            return "\u265E"; // ♞ -> black knight
        case pieceCode::pawn:
            if (replace_black_pawn)
            {
                return "\u25CF"; // black circle to avoid emoji style pawn
            }
            else
            {
                return "\u265F"; // ♟ -> black pawn
            }
        default:
            return "";
        }
    }
    else
    {
        return "";
    }
}

int chess::getPieceValue(pieceCode piece)
{

    switch (piece)
    {
    case pieceCode::pawn:
        return pawnValue;
    case pieceCode::rook:
        return rookValue;
    case pieceCode::knight:
        return knightValue;
    case pieceCode::bishop:
        return bishopValue;
    case pieceCode::king:
        return kingValue;
    case pieceCode::queen:
        return queenValue;
    default:
        return 0;
    }
}

double chess::getPositionEvalFactor(boardPositionType pos)
{
    double tempVal = 0;
    int file_idx = pos.coord.file - 'A';
    int rank_idx = pos.coord.rank - 1;

    switch (pos.piece.piece)
    {
    case pieceCode::pawn:
        tempVal = (pos.piece.color == playerColor::white ? pawnEvalWhite[rank_idx][file_idx] : -pawnEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::rook:
        tempVal = (pos.piece.color == playerColor::white ? rookEvalWhite[rank_idx][file_idx] : -rookEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::knight:
        tempVal = (pos.piece.color == playerColor::white ? knightEvalWhite[rank_idx][file_idx] : -knightEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::bishop:
        tempVal = (pos.piece.color == playerColor::white ? bishopEvalWhite[rank_idx][file_idx] : -bishopEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::king:
        tempVal = (pos.piece.color == playerColor::white ? kingEvalWhite[rank_idx][file_idx] : -kingEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::queen:
        tempVal = (pos.piece.color == playerColor::white ? evalQueenWhite[rank_idx][file_idx] : -evalQueenBlack[rank_idx][file_idx]);
        break;
    default:
        tempVal = 0;
    }
    return tempVal;
}

double chess::evaluateBoard(void)
{
    double gameCount = 0.0;

    for (int file = 0; file < 8; file++)
    {
        for (int rank = 0; rank < 8; rank++)
        {
            boardPositionType currentPosition = query_position({static_cast<char>('A' + file), rank + 1});
            int pol = currentPosition.piece.color == playerColor::white ? 1 : -1;
            double pieceVal = pol * (double)getPieceValue(currentPosition.piece.piece);
            double posVal = getPositionEvalFactor(currentPosition);
            gameCount = gameCount + position_gamma * posVal + pieceVal;
        }
    }
    return gameCount;
}

chessMotionType chess::smartMoveR(int depth, int alpha, int beta)
{

    chessMotionType bestMove;
    vector<chessMotionType> bestMoveList;
    chessMotionType currentMove;
    vector<chessMotionType> legalMovesList = findAllLegalMoves();

    bool is_white = (current_player == playerColor::white);
    bool is_black = (current_player == playerColor::black);

    int playerFactor = (current_player == playerColor::white) ? -1 : 1;
    int minMaxVal = (current_player == playerColor::white) ? maxValStart : minValStart;

    if (depth == 0)
    {
        RecFuncCounter++;

        if (legalMovesList.size() < 1)
        {
            if (currentlyChecked())
            {
                bestMove.board_evaluation = playerFactor * finalMattVal;
            }
            else
            {
                bestMove.board_evaluation = playerFactor * finalPattVal;
            }
        }
        else
        {
            bestMove.board_evaluation = evaluateBoard();
        }
        return bestMove;
    }

    if ((legalMovesList.size() < 1) && (currentlyChecked()))
    {
        // checkmate before depth 0 reached
        bestMove = getHistoryLast();
        bestMove.board_evaluation = -earlyMattVal;
    }
    else
    {
        for (uint i = 0; i < legalMovesList.size(); i++)
        {
            executeMove(legalMovesList[i]);
            swapPlayers();
            currentMove = smartMoveR(depth - 1, alpha, beta);
            reverseMove();
            swapPlayers();

            if (currentMove.board_evaluation == minMaxVal)
            { // found equally good move, add to list for random pick
                bestMove.start_position = legalMovesList[i].start_position;
                bestMove.dest_position = legalMovesList[i].dest_position;
                bestMove.board_evaluation = minMaxVal;
                bestMoveList.push_back(bestMove);
            }
            else if ((is_white && currentMove.board_evaluation > minMaxVal) || (is_black && currentMove.board_evaluation < minMaxVal))
            { // found better move (new Max), reset list and add new move
                minMaxVal = currentMove.board_evaluation;

                bestMove.start_position = legalMovesList[i].start_position;
                bestMove.dest_position = legalMovesList[i].dest_position;
                bestMove.board_evaluation = minMaxVal;

                bestMoveList.clear();
                bestMoveList.push_back(bestMove);

                if ((bestMove.board_evaluation == playerFactor * earlyMattVal) || (bestMove.board_evaluation == playerFactor * finalMattVal))
                {
                    break;
                }
            }
            else
            {
                // debugMessage("no new MAX, current value = " + to_string(maxVal) + " current Depth: " + to_string(depth), 1);
            }
            if (use_AB_pruning)
            {
                alpha = std::max(alpha, minMaxVal);
                if (beta <= alpha)
                {
                    break;
                }
            }
        }

        if (bestMoveList.size() > 1)
        {
            srand(time(0)); // define random piece of list
            int rmdIdx = (rand() % bestMoveList.size());
            bestMove = bestMoveList[rmdIdx];
        }
        else
        {
            bestMove = bestMoveList.back();
        }
    }
    return bestMove;
}

chessMotionType chess::getHistoryLast()
{
    return gameHistory.back();
}

void chess::performSmartMove()
{
    time_t start;
    time_t end;
    time(&start);

    RecFuncCounter = 0;

    bool whiteToMove = current_player == playerColor::white;

    chessMotionType currentSmartMove = smartMoveR(minMaxDepth, maxValStart, minValStart);

    executeMove(currentSmartMove);
    swapPlayers();

    time(&end);
    std::cout << "#Analyzed moves: " << RecFuncCounter << std::endl;
    std::cout << "Required time: " << difftime(end, start) << " seconds." << std::endl;
}

void chess::board_editor()
{
    std::cout << "\nBoard Editor: place or remove pieces." << std::endl;
    std::cout << "Commands: PLACE <piece> <color> <coord>, REMOVE <coord>, EMPTY, DEFAULT, BACK" << std::endl;
    std::cout << "Pieces: K,Q,R,B,N,P  Colors: W,B  Coord: like E2" << std::endl;

    bool done = false;
    while (!done)
    {
        printCurrentGame();
        std::cout << "> " << std::flush;
        std::string cmd;
        if (!(std::cin >> cmd))
            break;
        for (auto &c : cmd)
            c = std::toupper(c);

        if (cmd == "BACK" || cmd == "QUIT")
        {
            done = true;
        }
        else if (cmd == "DEFAULT")
        {
            load_starting_position();
        }
        else if (cmd == "EMPTY")
        {
            for (int rank = 1; rank <= 8; ++rank)
            {
                for (char file = 'A'; file <= 'H'; ++file)
                {
                    place_piece({file, rank}, {pieceCode::empty, playerColor::none});
                }
            }
        }
        else if (cmd == "PLACE")
        {
            std::string pieceStr, colorStr, coordStr;
            if (!(std::cin >> pieceStr >> colorStr >> coordStr))
            {
                std::cout << "Invalid input. Usage: PLACE <piece> <color> <coord>" << std::endl;
                std::cin.clear();
                continue;
            }
            for (auto &c : pieceStr)
                c = std::toupper(c);
            for (auto &c : colorStr)
                c = std::toupper(c);
            try
            {
                boardCoordinateType coord = chessCoordinatesFromString(coordStr);

                pieceCode pc = pieceCode::empty;
                if (pieceStr == "K")
                    pc = pieceCode::king;
                else if (pieceStr == "Q")
                    pc = pieceCode::queen;
                else if (pieceStr == "R")
                    pc = pieceCode::rook;
                else if (pieceStr == "B")
                    pc = pieceCode::bishop;
                else if (pieceStr == "N")
                    pc = pieceCode::knight;
                else if (pieceStr == "P")
                    pc = pieceCode::pawn;
                else
                {
                    std::cout << "Unknown piece. Use K,Q,R,B,N,P." << std::endl;
                    continue;
                }

                playerColor col = playerColor::none;
                if (colorStr == "W")
                    col = playerColor::white;
                else if (colorStr == "B")
                    col = playerColor::black;
                else
                {
                    std::cout << "Unknown color. Use W or B." << std::endl;
                    continue;
                }

                place_piece(coord, {pc, col});
                std::cout << "Placed " << pieceStr << " " << colorStr << " at " << coordStr << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cout << "Error: " << e.what() << std::endl;
            }
        }
        else if (cmd == "REMOVE")
        {
            std::string coordStr;
            if (!(std::cin >> coordStr))
            {
                std::cout << "Invalid input. Usage: REMOVE <coord>" << std::endl;
                std::cin.clear();
                continue;
            }
            try
            {
                boardCoordinateType coord = chessCoordinatesFromString(coordStr);
                place_piece(coord, {pieceCode::empty, playerColor::none});
                std::cout << "Removed piece at " << coordStr << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cout << "Error: " << e.what() << std::endl;
            }
        }
        else
        {
            std::cout << "Unknown command. Use PLACE/REMOVE/EMPTY/DEFAULT/BACK." << std::endl;
        }
    }
}