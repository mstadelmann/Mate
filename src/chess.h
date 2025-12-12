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

typedef struct pType
{
    pieceCode piece;
    playerColor color;
} piece;

typedef array<array<piece, 8>, 8> chessboardType; // chessboardType[file][rank]
typedef vector<chessboardType> chessboard_historyType;

struct chessPosition
{
    char file; // Vertical column → A-H
    int rank;  // Horizontal row → 1–8)
};

class chess
{
private:
    chessboardType chessboard;
    chessboard_historyType chessboard_history;
    playerColor current_player;
    playerColor other_player;
    bool replace_black_pawn;

public:
    chess();
    ~chess();
    void load_starting_position();
    void printCurrentGame();
    string current_player_string() const;
    void set_piece(chessPosition, piece);
};

#endif /* CHESS_H */
