#include "network.h"
#include "chess.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

using std::cout;
using std::endl;
using std::string;

namespace
{
    bool send_line(int sock, const string &line)
    {
        string data = line;
        if (data.empty() || data.back() != '\n')
            data.push_back('\n');
        const char *buf = data.c_str();
        size_t total = 0;
        size_t len = data.size();
        while (total < len)
        {
            ssize_t n = ::send(sock, buf + total, len - total, 0);
            if (n <= 0)
                return false;
            total += static_cast<size_t>(n);
        }
        return true;
    }

    bool recv_line(int sock, string &out)
    {
        out.clear();
        char ch;
        while (true)
        {
            ssize_t n = ::recv(sock, &ch, 1, 0);
            if (n <= 0)
                return false;
            if (ch == '\n')
                break;
            out.push_back(ch);
            if (out.size() > 1024)
                return false; // sanity limit
        }
        return true;
    }

    bool apply_move_string(chess &game, const string &startField, const string &endField)
    {
        try
        {
            auto startCoord = game.chessCoordinatesFromString(startField);
            auto endCoord = game.chessCoordinatesFromString(endField);

            auto startPos = game.query_position(startCoord);
            auto endPos = game.query_position(endCoord);

            motionType moveToMake{startPos, endPos, moveType::undefined, moved_by::network, 0};
            motionVector legalMoves = game.findAllLegalMoves();
            for (const auto &legalMove : legalMoves)
            {
                if (legalMove.start_position.coord.file == moveToMake.start_position.coord.file &&
                    legalMove.start_position.coord.rank == moveToMake.start_position.coord.rank &&
                    legalMove.dest_position.coord.file == moveToMake.dest_position.coord.file &&
                    legalMove.dest_position.coord.rank == moveToMake.dest_position.coord.rank)
                {
                    moveToMake.type_of_move = legalMove.type_of_move;
                    game.executeMove(moveToMake);
                    game.swapPlayers();
                    return true;
                }
            }
        }
        catch (...)
        {
            return false;
        }
        return false;
    }
}

bool start_server(uint16_t port, const std::string &username, bool hostPlaysWhite, NetConnection &outConn)
{
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0)
        return false;

    int opt = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(srv, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        ::close(srv);
        return false;
    }
    if (::listen(srv, 1) < 0)
    {
        ::close(srv);
        return false;
    }

    cout << "Waiting for a player on port " << port << "..." << endl;
    sockaddr_in cli{};
    socklen_t clilen = sizeof(cli);
    int cliSock = ::accept(srv, (sockaddr *)&cli, &clilen);
    ::close(srv);
    if (cliSock < 0)
        return false;

    // Announce our name and color, then expect JOIN <name> or QUIT
    if (!send_line(cliSock, string("NAME ") + username + " " + (hostPlaysWhite ? "WHITE" : "BLACK")))
    {
        ::close(cliSock);
        return false;
    }

    string line;
    if (!recv_line(cliSock, line))
    {
        ::close(cliSock);
        return false;
    }

    if (line.rfind("JOIN ", 0) == 0)
    {
        outConn.sock = cliSock;
        outConn.isServer = true;
        outConn.myName = username;
        outConn.peerName = line.substr(5);
        outConn.myPlaysWhite = hostPlaysWhite;
        outConn.port = port;
        cout << "Player '" << outConn.peerName << "' joined. Starting game." << endl;
        return true;
    }

    ::close(cliSock);
    return false;
}

bool connect_client(const std::string &host, uint16_t port, const std::string &username, NetConnection &outConn)
{
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
    {
        // Try DNS resolution
        hostent *he = ::gethostbyname(host.c_str());
        if (!he)
        {
            ::close(sock);
            return false;
        }
        std::memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (::connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        ::close(sock);
        return false;
    }

    string line;
    if (!recv_line(sock, line))
    {
        ::close(sock);
        return false;
    }
    if (line.rfind("NAME ", 0) != 0)
    {
        ::close(sock);
        return false;
    }
    // Expect: NAME <serverName> <color>
    std::istringstream iss(line.substr(5));
    string serverName, colorToken;
    iss >> serverName >> colorToken;
    bool hostIsWhite = (colorToken == "WHITE");
    cout << "Found server hosted by '" << serverName << "' (" << (hostIsWhite ? "White" : "Black") << "). Join? (y/n): " << std::flush;
    string ans;
    std::cin >> ans;
    if (ans.empty() || (ans[0] != 'y' && ans[0] != 'Y'))
    {
        (void)send_line(sock, "QUIT");
        ::close(sock);
        return false;
    }

    if (!send_line(sock, string("JOIN ") + username))
    {
        ::close(sock);
        return false;
    }

    outConn.sock = sock;
    outConn.isServer = false;
    outConn.myName = username;
    outConn.peerName = serverName;
    outConn.myPlaysWhite = !hostIsWhite;
    outConn.port = port;
    cout << "Joined game with '" << outConn.peerName << "'." << endl;
    return true;
}

void close_connection(NetConnection &conn)
{
    if (conn.sock >= 0)
    {
        ::close(conn.sock);
        conn.sock = -1;
    }
}

int run_network_game(chess &game, NetConnection &conn)
{
    cout << (conn.myPlaysWhite ? "You are White." : "You are Black.") << endl;
    cout << "You: " << conn.myName << ", Opponent: " << conn.peerName << endl;

    game.load_starting_position();
    game.init_game();
    // Assign player names so the board shows current player with name
    if (conn.myPlaysWhite)
        game.set_player_names(conn.myName, conn.peerName);
    else
        game.set_player_names(conn.peerName, conn.myName);

    // White moves first: server plays White
    while (true)
    {
        game.detectCheckmate();
        game.printCurrentGame();

        bool myTurnIsWhite = conn.myPlaysWhite;
        // Determine whose turn based on game state
        bool myTurn = myTurnIsWhite ? (game.current_player_string() == "white") : (game.current_player_string() == "black");

        if (myTurn)
        {
            cout << "Enter move (e.g. E2 E4) or 'q' to quit: " << std::flush;
            string s, e;
            std::cin >> s;
            if (s == "q" || s == "Q")
            {
                (void)send_line(conn.sock, "QUIT");
                cout << "You quit the game." << endl;
                break;
            }
            std::cin >> e;

            if (!apply_move_string(game, s, e))
            {
                cout << "Illegal move. Try again." << endl;
                continue;
            }
            if (!send_line(conn.sock, string("MOVE ") + s + " " + e))
            {
                cout << "Network error sending move." << endl;
                break;
            }
        }
        else
        {
            cout << "Waiting for opponent move..." << endl;
            string line;
            if (!recv_line(conn.sock, line))
            {
                cout << "Connection closed." << endl;
                break;
            }
            if (line == "QUIT")
            {
                cout << "Opponent quit the game." << endl;
                break;
            }
            if (line.rfind("MOVE ", 0) == 0)
            {
                std::istringstream iss(line.substr(5));
                string s, e;
                iss >> s >> e;
                if (!apply_move_string(game, s, e))
                {
                    cout << "Received illegal move. Terminating." << endl;
                    break;
                }
            }
            else
            {
                cout << "Protocol error. Terminating." << endl;
                break;
            }
        }
    }

    return 0;
}
