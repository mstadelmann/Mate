#include "network.h"
#include "chess.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include "database.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/select.h>

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
                return false;
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

    void print_network_help()
    {
        cout << "\nNetwork Game Commands:" << endl;
        cout << " - Enter a move like: E2 E4" << endl;
        cout << " - a: List all legal moves" << endl;
        cout << " - l: Show game history" << endl;
        cout << " - w: Write to database" << endl;
        cout << " - c: Send a chat message" << endl;
        cout << " - q: Quit game" << endl;
        cout << endl;
    }
}

bool start_server(uint16_t port, const std::string &username, bool hostPlaysWhite, const std::string &password, NetConnection &outConn)
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
    if (::listen(srv, 4) < 0)
    {
        ::close(srv);
        return false;
    }

    cout << "Waiting for a player on port " << port << "..." << endl;

    // Keep accepting connections until a client joins successfully
    while (true)
    {
        sockaddr_in cli{};
        socklen_t clilen = sizeof(cli);
        int cliSock = ::accept(srv, (sockaddr *)&cli, &clilen);
        if (cliSock < 0)
        {
            // Accept failed; keep server alive to try again
            continue;
        }

        // Announce name, color and whether password is required,
        // then expect JOIN <name> [password] or QUIT
        if (!send_line(cliSock, string("NAME ") + username + " " + (hostPlaysWhite ? "WHITE" : "BLACK") + " " + (password.empty() ? "OPEN" : "LOCKED")))
        {
            ::close(cliSock);
            continue;
        }

        string line;
        if (!recv_line(cliSock, line))
        {
            ::close(cliSock);
            continue;
        }

        if (line.rfind("JOIN ", 0) == 0)
        {
            // Parse JOIN line: JOIN <name> [password]
            std::istringstream iss(line.substr(5));
            string clientName, clientPass;
            iss >> clientName >> clientPass;
            if (!password.empty() && clientPass != password)
            {
                (void)send_line(cliSock, "DENY");
                ::close(cliSock);
                // Do not kill the server; keep listening for others
                continue;
            }

            (void)send_line(cliSock, "OK");
            outConn.sock = cliSock;
            outConn.isServer = true;
            outConn.myName = username;
            outConn.peerName = clientName;
            outConn.myPlaysWhite = hostPlaysWhite;
            outConn.port = port;
            cout << "Player '" << outConn.peerName << "' joined. Starting game." << endl;
            // Successful join: close listening socket and start game
            ::close(srv);
            return true;
        }

        // Any other message: close and keep listening
        ::close(cliSock);
    }
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
    // Expect: NAME <serverName> <color> [OPEN|LOCKED]
    std::istringstream iss(line.substr(5));
    string serverName, colorToken, lockToken;
    iss >> serverName >> colorToken >> lockToken;
    bool hostIsWhite = (colorToken == "WHITE");
    bool requiresPass = (lockToken == "LOCKED");
    cout << "Found server hosted by '" << serverName << "' (" << (hostIsWhite ? "White" : "Black") << (requiresPass ? ", password protected" : "") << "). Join? (y/n): " << std::flush;
    string ans;
    std::cin >> ans;
    if (ans.empty() || (ans[0] != 'y' && ans[0] != 'Y'))
    {
        (void)send_line(sock, "QUIT");
        ::close(sock);
        return false;
    }

    string pwd;
    if (requiresPass)
    {
        cout << "Enter server password: " << std::flush;
        std::getline(std::cin >> std::ws, pwd);
    }
    if (!send_line(sock, string("JOIN ") + username + (pwd.empty() ? string("") : string(" ") + pwd)))
    {
        ::close(sock);
        return false;
    }

    // Expect server response OK or DENY
    string resp;
    if (!recv_line(sock, resp))
    {
        ::close(sock);
        return false;
    }
    if (resp == "DENY")
    {
        cout << "Wrong password. Access denied." << endl;
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
    if (conn.myPlaysWhite)
        game.set_player_names(conn.myName, conn.peerName);
    else
        game.set_player_names(conn.peerName, conn.myName);

    // Show available commands at the start of a network game
    print_network_help();
    while (true)
    {
        game.detectCheckmate();
        game.printCurrentGame();

        bool myTurnIsWhite = conn.myPlaysWhite;
        bool myTurn = myTurnIsWhite ? (game.current_player_string() == "white") : (game.current_player_string() == "black");

        if (myTurn)
        {
            cout << "Enter move (e.g. E2 E4) or 'q' (help: 'h', chat: 'c'): " << std::flush;
            bool moveSent = false;
            bool terminate = false;
            while (!moveSent && !terminate)
            {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(conn.sock, &rfds);
                FD_SET(STDIN_FILENO, &rfds);
                int nfds = std::max(conn.sock, STDIN_FILENO) + 1;
                int rv = ::select(nfds, &rfds, nullptr, nullptr, nullptr);
                if (rv < 0)
                {
                    cout << "Select error. Terminating." << endl;
                    terminate = true;
                    break;
                }

                // Handle incoming network messages immediately (e.g., chat)
                if (FD_ISSET(conn.sock, &rfds))
                {
                    string line;
                    if (!recv_line(conn.sock, line))
                    {
                        cout << "Connection closed." << endl;
                        terminate = true;
                        break;
                    }
                    if (line == "QUIT")
                    {
                        cout << "Opponent quit the game." << endl;
                        terminate = true;
                        break;
                    }
                    if (line.rfind("CHAT ", 0) == 0)
                    {
                        cout << "\nMessage from " << conn.peerName << ": " << line.substr(5) << endl;
                        continue;
                    }
                    if (line.rfind("MOVE ", 0) == 0)
                    {
                        // Opponent moved during our turn -> protocol error
                        cout << "Protocol error: move from opponent during your turn. Terminating." << endl;
                        terminate = true;
                        break;
                    }
                    else
                    {
                        cout << "Protocol error. Terminating." << endl;
                        terminate = true;
                        break;
                    }
                }

                // Handle user input for commands/moves
                if (FD_ISSET(STDIN_FILENO, &rfds))
                {
                    std::string input;
                    if (!std::getline(std::cin, input))
                    {
                        continue;
                    }
                    std::istringstream iss(input);
                    std::string tok1;
                    iss >> tok1;
                    if (tok1.empty())
                    {
                        continue;
                    }
                    if (tok1.size() == 1)
                    {
                        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(tok1[0])));
                        if (c == 'q')
                        {
                            (void)send_line(conn.sock, "QUIT");
                            cout << "You quit the game." << endl;
                            terminate = true;
                            break;
                        }
                        else if (c == 'a')
                        {
                            game.listLegalMoves();
                            cout << "Enter move or 'q' (help: 'h', chat: 'c'): " << std::flush;
                            continue;
                        }
                        else if (c == 'l')
                        {
                            cout << "Listing game history..." << endl;
                            game.listMoveHistory();
                            cout << "Enter move or 'q' (help: 'h', chat: 'c'): " << std::flush;
                            continue;
                        }
                        else if (c == 'w')
                        {
                            cout << "Writing to database..." << endl;
                            store_to_DB(game);
                            cout << "Enter move or 'q' (help: 'h', chat: 'c'): " << std::flush;
                            continue;
                        }
                        else if (c == 'h')
                        {
                            print_network_help();
                            cout << "Enter move or 'q' (help: 'h', chat: 'c'): " << std::flush;
                            continue;
                        }
                        else if (c == 'c')
                        {
                            cout << "Enter message: " << std::flush;
                            std::string msg;
                            std::getline(std::cin, msg);
                            if (!msg.empty())
                            {
                                if (!send_line(conn.sock, std::string("CHAT ") + msg))
                                {
                                    cout << "Network error sending chat." << endl;
                                    terminate = true;
                                    break;
                                }
                                cout << "Sent." << endl;
                            }
                            cout << "Enter move or 'q' (help: 'h', chat: 'c'): " << std::flush;
                            continue;
                        }
                        // Fall through to treat as start field if not a known single-letter command
                    }

                    std::string tok2;
                    iss >> tok2;
                    if (tok2.empty())
                    {
                        cout << "Please enter a move like: E2 E4" << endl;
                        cout << "Enter move or 'q' (help: 'h', chat: 'c'): " << std::flush;
                        continue;
                    }

                    if (!apply_move_string(game, tok1, tok2))
                    {
                        cout << "Illegal move. Try again." << endl;
                        cout << "Enter move or 'q' (help: 'h', chat: 'c'): " << std::flush;
                        continue;
                    }
                    if (!send_line(conn.sock, string("MOVE ") + tok1 + " " + tok2))
                    {
                        cout << "Network error sending move." << endl;
                        terminate = true;
                        break;
                    }
                    moveSent = true;
                }
            }
            if (terminate)
                break;
        }
        else
        {
            cout << "Waiting for opponent move..." << endl;
            // Allow sending chat while waiting by monitoring both socket and stdin
            bool terminated = false;
            while (!terminated)
            {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(conn.sock, &rfds);
                FD_SET(STDIN_FILENO, &rfds);
                int nfds = std::max(conn.sock, STDIN_FILENO) + 1;
                int rv = ::select(nfds, &rfds, nullptr, nullptr, nullptr);
                if (rv < 0)
                {
                    cout << "Select error. Terminating." << endl;
                    terminated = true;
                    break;
                }

                // Incoming network message
                if (FD_ISSET(conn.sock, &rfds))
                {
                    string line;
                    if (!recv_line(conn.sock, line))
                    {
                        cout << "Connection closed." << endl;
                        terminated = true;
                        break;
                    }
                    if (line == "QUIT")
                    {
                        cout << "Opponent quit the game." << endl;
                        terminated = true;
                        break;
                    }
                    if (line.rfind("CHAT ", 0) == 0)
                    {
                        cout << "\nMessage from " << conn.peerName << ": " << line.substr(5) << endl;
                        // Keep waiting for opponent's move
                        continue;
                    }
                    if (line.rfind("MOVE ", 0) == 0)
                    {
                        std::istringstream iss(line.substr(5));
                        string s, e;
                        iss >> s >> e;
                        if (!apply_move_string(game, s, e))
                        {
                            cout << "Received illegal move. Terminating." << endl;
                            terminated = true;
                            break;
                        }
                        // Opponent moved; exit waiting loop to refresh board
                        break;
                    }
                    else
                    {
                        cout << "Protocol error. Terminating." << endl;
                        terminated = true;
                        break;
                    }
                }

                // User input for chat while waiting
                if (FD_ISSET(STDIN_FILENO, &rfds))
                {
                    std::string cmd;
                    if (!std::getline(std::cin, cmd))
                    {
                        // Input stream closed; keep waiting on network
                        continue;
                    }
                    // Trim leading spaces to check command
                    size_t i = 0;
                    while (i < cmd.size() && std::isspace(static_cast<unsigned char>(cmd[i])))
                        ++i;
                    if (i < cmd.size())
                    {
                        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(cmd[i])));
                        if (c == 'c')
                        {
                            cout << "Enter message: " << std::flush;
                            std::string msg;
                            std::getline(std::cin, msg);
                            if (!msg.empty())
                            {
                                if (!send_line(conn.sock, std::string("CHAT ") + msg))
                                {
                                    cout << "Network error sending chat." << endl;
                                    terminated = true;
                                    break;
                                }
                                cout << "Sent." << endl;
                            }
                        }
                        else
                        {
                            cout << "It's not your turn. Type 'c' to chat." << endl;
                        }
                    }
                }
            }
            if (terminated)
                break;
        }
    }

    return 0;
}
