#ifndef CHESS_H
#define CHESS_H

#include <vector>
#include <string>
#include <array>
#include "config.h"

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
    init,
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
    string game_name;
    chessboardType chessboard;
    chessboard_historyType chessboard_history;
    playerColor current_player;
    playerColor other_player;
    bool replace_black_pawn;
    vector<chessMotionType> gameHistory;
    vector<chessboardType> gamePositionHistory;
    int RecFuncCounter;
    int skippedBranches;
    // Castling rights and En Passant
    bool wCanCastleKs;
    bool wCanCastleQs;
    bool bCanCastleKs;
    bool bCanCastleQs;
    boardCoordinateType en_passant_target; // invalid when rank==0 or file<'A'/'Z'
    struct GameStateSnapshot
    {
        bool wck;
        bool wcq;
        bool bck;
        bool bcq;
        boardCoordinateType ep;
    };
    vector<GameStateSnapshot> gameStateHistory;
    // Helpers for board access
    inline static int fileIndex(char f) { return f - 'A'; }
    inline static int rankIndex(int r) { return r - 1; }
    inline pieceType &at(char file, int rank) { return chessboard[fileIndex(file)][rankIndex(rank)]; }
    inline const pieceType &at(char file, int rank) const { return chessboard[fileIndex(file)][rankIndex(rank)]; }
    inline pieceType &at(boardCoordinateType c) { return chessboard[fileIndex(c.file)][rankIndex(c.rank)]; }
    inline const pieceType &at(boardCoordinateType c) const { return chessboard[fileIndex(c.file)][rankIndex(c.rank)]; }
    bool white_checked;
    bool black_checked;
    bool white_checkmate;
    bool black_checkmate;

    bool is_square_attacked(boardCoordinateType sq, playerColor byColor) const;
    void update_castling_rights_on_move(const boardPositionType &startPos, const boardPositionType &destPos);
    void update_castling_rights_on_capture(const pieceType &captured, const boardCoordinateType &capturedCoord);
    void push_state_snapshot();
    void pop_state_snapshot();

public:
    chess();
    ~chess();
    void init_game();
    void set_game_name(const std::string &name);
    void load_starting_position();
    void printCurrentGame();
    string current_player_string() const;
    string gameName() const { return game_name; }
    void place_piece(boardPositionType);
    void place_piece(boardCoordinateType, pieceType);
    boardPositionType query_position(boardCoordinateType);
    vector<boardPositionType> get_all_pieces_of_color(playerColor);
    vector<chessMotionType> findAllLegalMoves();

    vector<chessMotionType> findLegalPawnMoves(boardCoordinateType);
    vector<chessMotionType> findLegalRookMoves(boardCoordinateType);
    vector<chessMotionType> findLegalKnightMoves(boardCoordinateType);
    vector<chessMotionType> findLegalBishopMoves(boardCoordinateType);
    vector<chessMotionType> findLegalQueenMoves(boardCoordinateType);
    vector<chessMotionType> findLegalKingMoves(boardCoordinateType);
    bool validatePosition(boardCoordinateType) const;
    bool check_move_legal(chessMotionType);
    void executeMove(chessMotionType);
    bool manualMove();
    bool randomMove();
    boardCoordinateType chessCoordinatesFromString(const string &);
    bool currentlyChecked();
    boardCoordinateType findKing();
    void reverseMove();
    void listLegalMoves();
    string pieceTypeToChar(pieceType);
    int getPieceValue(pieceCode);
    double getPositionEvalFactor(boardPositionType);
    double evaluateBoard(void);
    chessMotionType smartMoveR(int depth, int alpha, int beta);
    vector<chessMotionType> getHistory() const;
    chessMotionType getHistoryLast() const;
    chessboard_historyType getPositionHistory() const;

    void performSmartMove();
    void board_editor();
    void listMoveHistory();
    bool check_board_valid();
    void detectCheckmate();
    void swapPlayers() { std::swap(current_player, other_player); }
};

std::string moveTypeToString(moveType);
std::string movedByToString(moved_by);
std::string pieceCodeToString(pieceCode);
std::string playerColorToString(playerColor);

#endif /* CHESS_H */