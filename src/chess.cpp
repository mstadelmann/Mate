#include "chess.h"
#include <iostream>

chess::chess()
{
    std::cout << "Initializing chess game..." << std::endl;

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

void chess::load_starting_position()
{
    // Set up pawns
    for (int col = 0; col < 8; ++col)
    {
        chessboard[1][col].piece = pieceCode::pawn;
        chessboard[1][col].color = playerColor::black;
        chessboard[6][col].piece = pieceCode::pawn;
        chessboard[6][col].color = playerColor::white;
    }

    // Set up rooks
    chessboard[0][0].piece = pieceCode::rook;
    chessboard[0][0].color = playerColor::black;
    chessboard[0][7].piece = pieceCode::rook;
    chessboard[0][7].color = playerColor::black;
    chessboard[7][0].piece = pieceCode::rook;
    chessboard[7][0].color = playerColor::white;
    chessboard[7][7].piece = pieceCode::rook;
    chessboard[7][7].color = playerColor::white;

    // Set up knights
    chessboard[0][1].piece = pieceCode::knight;
    chessboard[0][1].color = playerColor::black;
    chessboard[0][6].piece = pieceCode::knight;
    chessboard[0][6].color = playerColor::black;
    chessboard[7][1].piece = pieceCode::knight;
    chessboard[7][1].color = playerColor::white;
    chessboard[7][6].piece = pieceCode::knight;
    chessboard[7][6].color = playerColor::white;

    // Set up bishops
    chessboard[0][2].piece = pieceCode::bishop;
    chessboard[0][2].color = playerColor::black;
    chessboard[0][5].piece = pieceCode::bishop;
    chessboard[0][5].color = playerColor::black;
    chessboard[7][2].piece = pieceCode::bishop;
    chessboard[7][2].color = playerColor::white;
    chessboard[7][5].piece = pieceCode::bishop;
    chessboard[7][5].color = playerColor::white;

    // Set up queens
    chessboard[0][3].piece = pieceCode::queen;
}