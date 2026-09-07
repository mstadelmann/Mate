#include "network.h"
#include "chess.h"
#include "gui.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
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
    // Bounds how long a single recv() can block, so a peer that opens a
    // connection (or sends a partial line) and then stalls cannot freeze the
    // whole program indefinitely - recv_line() reads one byte at a time and is
    // otherwise only protected by a prior select() check on the first byte.
    void set_recv_timeout(int sock, int seconds)
    {
        timeval tv{seconds, 0};
        ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    // Compares in time proportional to the expected password's length only,
    // regardless of where (or whether) the strings first differ.
    bool constant_time_equal(const string &expected, const string &actual)
    {
        unsigned char diff = static_cast<unsigned char>(expected.size() != actual.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            unsigned char a = static_cast<unsigned char>(expected[i]);
            unsigned char b = i < actual.size() ? static_cast<unsigned char>(actual[i]) : 0;
            diff |= static_cast<unsigned char>(a ^ b);
        }
        return diff == 0;
    }

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
            return game.applyMove(startCoord, endCoord, moved_by::network);
        }
        catch (...)
        {
            return false;
        }
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

    bool connect_client_impl(const std::string &host,
                             uint16_t port,
                             const std::string &username,
                             const std::string &password,
                             bool prompt_before_join,
                             NetConnection &outConn,
                             std::string &error_message)
    {
        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            error_message = "Could not create client socket.";
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
        {
            hostent *he = ::gethostbyname(host.c_str());
            if (!he)
            {
                ::close(sock);
                error_message = "Could not resolve host '" + host + "'.";
                return false;
            }
            std::memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        }

        if (::connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
        {
            ::close(sock);
            error_message = "Could not connect to " + host + ":" + std::to_string(port) + ".";
            return false;
        }
        set_recv_timeout(sock, 10);

        string line;
        if (!recv_line(sock, line))
        {
            ::close(sock);
            error_message = "Server closed the connection during handshake.";
            return false;
        }
        if (line.rfind("NAME ", 0) != 0)
        {
            ::close(sock);
            error_message = "Server sent an unexpected handshake response.";
            return false;
        }

        std::istringstream iss(line.substr(5));
        string serverName, colorToken, lockToken;
        iss >> serverName >> colorToken >> lockToken;
        const bool hostIsWhite = (colorToken == "WHITE");
        const bool requiresPass = (lockToken == "LOCKED");

        if (prompt_before_join)
        {
            cout << "Found server hosted by '" << serverName << "' (" << (hostIsWhite ? "White" : "Black") << (requiresPass ? ", password protected" : "") << "). Join? (y/n): " << std::flush;
            string ans;
            std::cin >> ans;
            if (ans.empty() || (ans[0] != 'y' && ans[0] != 'Y'))
            {
                (void)send_line(sock, "QUIT");
                ::close(sock);
                error_message = "Join canceled.";
                return false;
            }
        }

        string join_password = password;
        if (requiresPass && prompt_before_join && join_password.empty())
        {
            cout << "Enter server password: " << std::flush;
            std::getline(std::cin >> std::ws, join_password);
        }

        if (!send_line(sock, string("JOIN ") + username + (join_password.empty() ? string("") : string(" ") + join_password)))
        {
            ::close(sock);
            error_message = "Could not send JOIN request to the server.";
            return false;
        }

        string resp;
        if (!recv_line(sock, resp))
        {
            ::close(sock);
            error_message = "Server closed the connection before completing JOIN.";
            return false;
        }
        if (resp == "DENY")
        {
            cout << "Wrong password. Access denied." << endl;
            ::close(sock);
            error_message = "Access denied by the server.";
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

    // Backs off further after each wrong-password attempt across this
    // session, so a client cannot brute-force the password at line rate.
    int failed_join_attempts = 0;

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
        set_recv_timeout(cliSock, 10);

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
            if (!password.empty() && !constant_time_equal(password, clientPass))
            {
                failed_join_attempts++;
                const int delay_ms = std::min(failed_join_attempts * 500, 5000);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
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
    std::string error_message;
    return connect_client_impl(host, port, username, "", true, outConn, error_message);
}

bool connect_client(const std::string &host,
                    uint16_t port,
                    const std::string &username,
                    const std::string &password,
                    NetConnection &outConn,
                    std::string &error_message)
{
    return connect_client_impl(host, port, username, password, false, outConn, error_message);
}

void close_connection(NetConnection &conn)
{
    if (conn.sock >= 0)
    {
        ::close(conn.sock);
        conn.sock = -1;
    }
}

int run_network_game(chess &game, NetConnection &conn, ChessGui *gui)
{
    set_chess_gui_mode(gui, ChessGuiMode::network_game);
    set_chess_gui_local_player_color(gui, conn.myPlaysWhite ? playerColor::white : playerColor::black);
    cout << (conn.myPlaysWhite ? "You are White." : "You are Black.") << endl;
    cout << "You: " << conn.myName << ", Opponent: " << conn.peerName << endl;

    game.load_starting_position();
    game.init_game();
    if (conn.myPlaysWhite)
        game.set_player_names(conn.myName, conn.peerName);
    else
        game.set_player_names(conn.peerName, conn.myName);

    print_network_help();

    auto handle_gui_action = [&](const ChessGuiAction &action, bool my_turn, bool &move_sent, bool &terminate)
    {
        switch (action.type)
        {
        case ChessGuiActionType::move_piece:
            if (!my_turn)
            {
                cout << "It's not your turn." << endl;
                return;
            }
            if (!game.applyMove(action.start, action.dest, moved_by::network))
            {
                cout << "Illegal move." << endl;
                return;
            }
            if (!send_line(conn.sock, string("MOVE ") + string(1, action.start.file) + std::to_string(action.start.rank) + " " +
                                         string(1, action.dest.file) + std::to_string(action.dest.rank)))
            {
                cout << "Network error sending move." << endl;
                terminate = true;
                return;
            }
            move_sent = true;
            return;
        case ChessGuiActionType::list_moves:
            game.listLegalMoves();
            return;
        case ChessGuiActionType::write_db:
            cout << "Writing to database..." << endl;
            store_to_DB(game);
            return;
        case ChessGuiActionType::quit_game:
            (void)send_line(conn.sock, "QUIT");
            cout << "You quit the game." << endl;
            terminate = true;
            return;
        default:
            return;
        }
    };

    while (true)
    {
        game.detectCheckmate();
        sync_chess_gui(gui, game);
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
                ChessGuiAction gui_action;
                while (poll_chess_gui_action(gui, gui_action))
                {
                    handle_gui_action(gui_action, true, moveSent, terminate);
                    if (moveSent || terminate)
                    {
                        break;
                    }
                }
                if (moveSent || terminate)
                {
                    break;
                }

                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(conn.sock, &rfds);
                FD_SET(STDIN_FILENO, &rfds);
                int nfds = std::max(conn.sock, STDIN_FILENO) + 1;
                timeval timeout{0, 75 * 1000};
                int rv = ::select(nfds, &rfds, nullptr, nullptr, &timeout);
                if (rv < 0)
                {
                    cout << "Select error. Terminating." << endl;
                    terminate = true;
                    break;
                }
                if (rv == 0)
                {
                    continue;
                }

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
            bool terminated = false;
            while (!terminated)
            {
                ChessGuiAction gui_action;
                while (poll_chess_gui_action(gui, gui_action))
                {
                    bool move_sent = false;
                    handle_gui_action(gui_action, false, move_sent, terminated);
                    if (terminated)
                    {
                        break;
                    }
                }
                if (terminated)
                {
                    break;
                }

                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(conn.sock, &rfds);
                FD_SET(STDIN_FILENO, &rfds);
                int nfds = std::max(conn.sock, STDIN_FILENO) + 1;
                timeval timeout{0, 75 * 1000};
                int rv = ::select(nfds, &rfds, nullptr, nullptr, &timeout);
                if (rv < 0)
                {
                    cout << "Select error. Terminating." << endl;
                    terminated = true;
                    break;
                }
                if (rv == 0)
                {
                    continue;
                }

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

    set_chess_gui_mode(gui, ChessGuiMode::main_menu);
    return 0;
}
