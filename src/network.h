#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include <cstdint>

class chess;

struct NetConnection
{
    int sock = -1;
    bool isServer = false;
    bool myPlaysWhite = false;
    std::string myName;
    std::string peerName;
    uint16_t port = 0;
};

// Server lifecycle: start, wait for a client, announce name, receive JOIN, set peerName.
bool start_server(uint16_t port, const std::string &username, bool hostPlaysWhite, NetConnection &outConn);

// Client lifecycle: connect, receive server name, optionally send JOIN with our username.
// Returns false if user declines to join or connection fails.
bool connect_client(const std::string &host, uint16_t port, const std::string &username, NetConnection &outConn);

// Close underlying socket (safe to call on invalid).
void close_connection(NetConnection &conn);

// Run a networked human-vs-human game. Server plays White, client plays Black.
// Returns 0 on normal exit, non-zero on error.
int run_network_game(chess &game, NetConnection &conn);

#endif // NETWORK_H
