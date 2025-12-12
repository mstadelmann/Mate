#include "chess.h"
#include <iostream>

// chessboard::chessboard()
// {
//     // Initialize an empty chessboard
//     for (int row = 0; row < 8; ++row)
//     {
//         for (int col = 0; col < 8; ++col)
//         {
//             board[row][col].piece = pieceCode::empty;
//             board[row][col].color = playerColor::none;
//         }
//     }
// }

// chessboard::~chessboard()
// {
//     // Destructor logic if needed
// }

// void chessboard::set_piece(chessPosition pos, piece p)
// {
//     int row = pos.rank - 1;   // Convert rank to 0-based index
//     int col = pos.file - 'A'; // Convert file (A-H) to 0-based index

//     if (row >= 0 && row < 8 && col >= 0 && col < 8)
//     {
//         board[row][col] = p;
//     }
//     else
//     {
//         std::cerr << "Error: Invalid chess position." << std::endl;
//     }
// }

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

void chess::set_piece(chessPosition pos, piece p)
{
    int row = pos.rank - 1;   // Convert rank to 0-based index
    int col = pos.file - 'A'; // Convert file (A-H) to 0-based index

    if (row >= 0 && row < 8 && col >= 0 && col < 8)
    {
        chessboard[row][col] = p;
    }
    else
    {
        std::cerr << "Error: Invalid chess position." << std::endl;
    }
}

void chess::load_starting_position()
{
    // Set up pawns
    for (int rank = 0; rank < 8; ++rank)
    {
        char crank = static_cast<char>('A' + rank);
        set_piece({crank, 2}, {pieceCode::pawn, playerColor::white});
        set_piece({crank, 7}, {pieceCode::pawn, playerColor::black});
    }

    // Set up rooks
    set_piece({'A', 1}, {pieceCode::rook, playerColor::white});
    set_piece({'H', 1}, {pieceCode::rook, playerColor::white});
    set_piece({'A', 8}, {pieceCode::rook, playerColor::black});
    set_piece({'H', 8}, {pieceCode::rook, playerColor::black});

    // Set up knights
    set_piece({'B', 1}, {pieceCode::knight, playerColor::white});
    set_piece({'G', 1}, {pieceCode::knight, playerColor::white});
    set_piece({'B', 8}, {pieceCode::knight, playerColor::black});
    set_piece({'G', 8}, {pieceCode::knight, playerColor::black});

    // Set up bishops
    set_piece({'C', 1}, {pieceCode::bishop, playerColor::white});
    set_piece({'F', 1}, {pieceCode::bishop, playerColor::white});
    set_piece({'C', 8}, {pieceCode::bishop, playerColor::black});
    set_piece({'F', 8}, {pieceCode::bishop, playerColor::black});

    // Set up queens
    set_piece({'D', 1}, {pieceCode::queen, playerColor::white});
    set_piece({'D', 8}, {pieceCode::queen, playerColor::black});

    // Set up kings
    set_piece({'E', 1}, {pieceCode::king, playerColor::white});
    set_piece({'E', 8}, {pieceCode::king, playerColor::black});
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

    std::cout << "Print current game:\n";
    std::cout << "Current player = " << current_player_string() << std::endl;
    // if (gameHistory.size() > 0)
    // {
    //     cout << "Last moved piece = " << gameHistory.back().startPiece
    //          << gameHistory.back().startPiceCol << ", "
    //          << indicesToChessCoordinates(gameHistory.back().startpos)
    //          << " to "
    //          << indicesToChessCoordinates(gameHistory.back().endpos)
    //          << endl;
    // }

    std::cout << "\n     A   B   C   D   E   F   G   H  \n";

    for (int rank = 7; rank >= 0; rank--)
    {
        std::cout << "   --------------------------------- \n";
        std::cout << " " << rank + 1 << " ";
        for (int file = 0; file < 8; file++)
        {
            std::cout << "| ";
            if (chessboard.at(file).at(rank).piece != pieceCode::empty)
            {

                if (chessboard.at(file).at(rank).color == playerColor::black)
                {
                    switch (chessboard.at(file).at(rank).piece)
                    {
                    case pieceCode::rook:
                        std::cout << "\u265C "; // ♜ -> black rook
                        break;
                    case pieceCode::bishop:
                        std::cout << "\u265D "; // ♝ -> black bishop
                        break;
                    case pieceCode::knight:
                        std::cout << "\u265E "; // ♞ -> black knight
                        break;
                    case pieceCode::king:
                        std::cout << "\u265A "; // ♚ -> black king
                        break;
                    case pieceCode::queen:
                        std::cout << "\u265B "; // ♛ -> black queen
                        break;
                    case pieceCode::pawn:
                        std::cout << "\u265F "; // ♟ -> black pawn
                        // std::cout << "\u265F\uFE0E "; // ♟️ -> black pawn (with text variation selector to avoid emoji style)
                        // std::cout << "\u25CF "; // black circle -> plan B ...
                        break;
                    default:
                        std::cout << "\u2610 "; // ☐
                        break;
                    };
                }
                else
                {
                    switch (chessboard.at(file).at(rank).piece)
                    {
                    case pieceCode::rook:
                        std::cout << "\u2656 "; // ♖ -> white rook
                        break;
                    case pieceCode::bishop:
                        std::cout << "\u2657 "; // ♗ -> white bishop
                        break;
                    case pieceCode::knight:
                        std::cout << "\u2658 "; // ♘ -> white knight
                        break;
                    case pieceCode::king:
                        std::cout << "\u2654 "; // ♔ -> white king
                        break;
                    case pieceCode::queen:
                        std::cout << "\u2655 "; // ♕ -> white queen
                        break;
                    case pieceCode::pawn:
                        std::cout << "\u2659 "; // ♙ -> white pawn
                        break;
                    default:
                        std::cout << "\u2610 "; // ☐
                        break;
                    };
                }
            }
            else
            {
                std::cout << "  ";
            }
        }

        std::cout << "| " << rank + 1 << "\n";
    }
    std::cout << "   ---------------------------------";
    std::cout << "\n     A   B   C   D   E   F   G   H  \n\n\n\n\n\n";
}