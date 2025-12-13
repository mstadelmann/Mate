#ifndef CHESS_H
#define CHESS_H

#include <vector>
#include <string>
#include <array>

using std::array;
using std::string;
using std::vector;

enum class playerColor
{
    white,
    black,
    none
};

enum class pieceCode
{
    pawn,
    rook,
    knight,
    bishop,
    queen,
    king,
    empty
};

enum class gameState
{
    ongoing,
    check,
    checkmate,
    stalemate,
    draw
};

enum class moveType
{
    undefined,
    normal,
    capture,
    castling_kingside,
    castling_queenside,
    en_passant,
    promotion_queen,
    promotion_rook,
    promotion_bishop,
    promotion_knight
};

enum class moved_by
{
    human,
    engine,
    network,
    none
};

typedef struct pieceStruct
{
    pieceCode piece;
    playerColor color;
} pieceType;

typedef array<array<pieceType, 8>, 8> chessboardType; // chessboardType[file][rank]
typedef vector<chessboardType> chessboard_historyType;

typedef struct boardCoordinateStruct
{
    char file; // Vertical column → A-H
    int rank;  // Horizontal row → 1–8)
} boardCoordinateType;

typedef struct boardPositionStruct
{
    boardCoordinateType coord;
    pieceType piece;
} boardPositionType;

typedef struct chessMotionStruct
{
    boardPositionType start_position;
    boardPositionType dest_position;
    moveType type_of_move;
    moved_by moved_by_whom;
    int board_evaluation;
} chessMotionType;

class chess
{
private:
    chessboardType chessboard;
    chessboard_historyType chessboard_history;
    playerColor current_player;
    playerColor other_player;
    bool replace_black_pawn;
    vector<chessMotionType> gameHistory;
    vector<chessboardType> gamePositionHistory;

public:
    chess();
    ~chess();
    void load_starting_position();
    void printCurrentGame();
    string current_player_string() const;
    void place_piece(boardPositionType);
    void place_piece(boardCoordinateType, pieceType);
    boardPositionType query_position(boardCoordinateType);
    vector<boardPositionType> get_all_pieces_of_color(playerColor);
    vector<chessMotionType> findAllLegalMoves();

    vector<chessMotionType> findLegalPawnMoves(boardCoordinateType);
    bool validatePosition(boardCoordinateType);
    bool check_move_legal(chessMotionType);
    void executeMove(chessMotionType);
    bool manualMove();
    boardCoordinateType chessCoordinatesFromString(const string &);
    bool currentlyChecked();
    boardCoordinateType findKing();
    void reverseMove();
};

#endif /* CHESS_H */
