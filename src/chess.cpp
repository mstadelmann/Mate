#include "chess.h"
#include <iostream>
#include <random>

chess::chess()
{
    // default values
    replace_black_pawn = true;

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

    // // Store initial board state in history
    // chessboard_history.push_back(chessboard);
}

chess::~chess()
{
    std::cout << "Destroying chess game..." << std::endl;
}

void chess::place_piece(boardPositionType position)
{
    int rank = position.coord.rank - 1;   // Convert rank to 0-based index
    int file = position.coord.file - 'A'; // Convert file (A-H) to 0-based index

    if (rank >= 0 && rank < 8 && file >= 0 && file < 8)
    {
        chessboard[file][rank] = position.piece;
    }
    else
    {
        std::cerr << "Error: Invalid chess position." << std::endl;
    }
}

void chess::place_piece(boardCoordinateType coordinates, pieceType piece)
{
    int rank = coordinates.rank - 1;   // Convert rank to 0-based index
    int file = coordinates.file - 'A'; // Convert file (A-H) to 0-based index

    if (rank >= 0 && rank < 8 && file >= 0 && file < 8)
    {
        chessboard[file][rank] = piece;
    }
    else
    {
        std::cerr << "Error: Invalid chess position." << std::endl;
    }
}

boardPositionType chess::query_position(boardCoordinateType coordinates)
{
    int rank = coordinates.rank - 1;   // Convert rank to 0-based index
    int file = coordinates.file - 'A'; // Convert file (A-H) to 0-based index

    boardPositionType result;
    result.coord.file = coordinates.file;
    result.coord.rank = coordinates.rank;
    if (rank >= 0 && rank < 8 && file >= 0 && file < 8)
    {
        result.piece = chessboard[file][rank];
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
    // Set up pawns
    for (int file = 0; file < 8; ++file)
    {
        char current_file = static_cast<char>('A' + file);
        place_piece({current_file, 2, {pieceCode::pawn, playerColor::white}});
        place_piece({current_file, 7, {pieceCode::pawn, playerColor::black}});
    }

    // Set up rooks
    place_piece({'A', 1, {pieceCode::rook, playerColor::white}});
    place_piece({'H', 1, {pieceCode::rook, playerColor::white}});
    place_piece({'A', 8, {pieceCode::rook, playerColor::black}});
    place_piece({'H', 8, {pieceCode::rook, playerColor::black}});

    // Set up knights
    place_piece({'B', 1, {pieceCode::knight, playerColor::white}});
    place_piece({'G', 1, {pieceCode::knight, playerColor::white}});
    place_piece({'B', 8, {pieceCode::knight, playerColor::black}});
    place_piece({'G', 8, {pieceCode::knight, playerColor::black}});

    // Set up bishops
    place_piece({'C', 1, {pieceCode::bishop, playerColor::white}});
    place_piece({'F', 1, {pieceCode::bishop, playerColor::white}});
    place_piece({'C', 8, {pieceCode::bishop, playerColor::black}});
    place_piece({'F', 8, {pieceCode::bishop, playerColor::black}});

    // Set up queens
    place_piece({'D', 1, {pieceCode::queen, playerColor::white}});
    place_piece({'D', 8, {pieceCode::queen, playerColor::black}});

    // Set up kings
    place_piece({'E', 1, {pieceCode::king, playerColor::white}});
    place_piece({'E', 8, {pieceCode::king, playerColor::black}});

    gamePositionHistory.push_back(chessboard);
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
    std::swap(current_player, other_player);
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
                std::swap(current_player, other_player);
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

    // save history
    gameHistory.push_back(moveToExecute);
    gamePositionHistory.push_back(chessboard);

    // Switch current player
    // std::swap(current_player, other_player);
}

void chess::reverseMove()
{
    if (gameHistory.empty() || gamePositionHistory.size() < 2)
    {
        std::cout << "No moves to undo." << std::endl;
        return;
    }

    // Remove the last move from history
    gameHistory.pop_back();
    // Restore the previous board position
    chessboard = gamePositionHistory[gamePositionHistory.size() - 2];
    gamePositionHistory.pop_back();

    // Switch current player
    // std::swap(current_player, other_player);
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
        tempVal = (pos.piece.color == playerColor::white ? pawnEvalWhite[rank_idx][file_idx] : pawnEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::rook:
        tempVal = (pos.piece.color == playerColor::white ? rookEvalWhite[rank_idx][file_idx] : rookEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::knight:
        tempVal = (pos.piece.color == playerColor::white ? knightEval[rank_idx][file_idx] : knightEval[rank_idx][file_idx]);
        break;
    case pieceCode::bishop:
        tempVal = (pos.piece.color == playerColor::white ? bishopEvalWhite[rank_idx][file_idx] : bishopEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::king:
        tempVal = (pos.piece.color == playerColor::white ? kingEvalWhite[rank_idx][file_idx] : kingEvalBlack[rank_idx][file_idx]);
        break;
    case pieceCode::queen:
        tempVal = (pos.piece.color == playerColor::white ? evalQueen[rank_idx][file_idx] : evalQueen[rank_idx][file_idx]);
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
            double posFac = getPositionEvalFactor(currentPosition);
            gameCount = gameCount + position_gamma * posFac * pieceVal + (1 - position_gamma) * pieceVal;
        }
    }
    return gameCount;
}
