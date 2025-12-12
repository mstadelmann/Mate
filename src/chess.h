#ifndef CHESS_H
#define CHESS_H

#include <vector>
#include <array>

using std::array;
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

typedef struct pType
{
    pieceCode piece;
    playerColor color;
} piece;

typedef array<array<piece, 8>, 8> chessboardType;
typedef vector<chessboardType> chessboard_historyType;

struct chessPosition
{
    int letter = 0;
    int number = 0;
};

class chess
{
private:
    chessboardType chessboard;
    chessboard_historyType chessboard_history;
    playerColor current_player;
    playerColor other_player;

public:
    chess();
    ~chess();
    void load_starting_position();
};

#endif /* CHESS_H */
