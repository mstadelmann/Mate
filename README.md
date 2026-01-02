# Mate

Mate is a terminal chess engine and board editor with an interactive menu-driven UI, a simple evaluation-based engine (minimax with optional alpha–beta pruning), SQLite persistence, and network play with chat.

It is designed to be fast to try, easy to play, and hackable for experimentation.

## Features

- New game and free play on any position
- Board editor: place/remove pieces, reset, save position
- Engine moves: random and smart (minimax with alpha–beta)
- Legal move listing and move history display
- Undo with full board/state snapshots (castling, en passant)
- Save games to a SQLite DB, including full board snapshots per move
- Browse and load games from the database interactively
- Configurable evaluation and engine depth via `config.json`
- Unicode board rendering in the terminal
- Network play: host/join over TCP, player names on board, real-time chat

## Build & Run

Prerequisites (Linux):

1. Install a C++17 compiler, CMake, and SQLite3 dev libs:

```bash
sudo apt update
sudo apt install -y build-essential cmake libsqlite3-dev
```

2. Configure and build:

```bash
cmake -S . -B buildCLI
cmake --build buildCLI --config Debug -j6
```

3. Run:

```bash
./bin/Mate
```

On first run, `config.json` is created next to the binary in `bin/` if missing.

## Main Menu

- Start new game: loads the standard chess starting position
- Play with current board: starts a game from whatever position is currently loaded
- Create custom board position: opens the Board Editor (see below)
- Load game from database: lists saved games and lets you browse positions
- Start network game: host a server or join one by IP/hostname
- Quit: exits the program

## Game Menu (during a game)

- Enter Manual Move: type moves like `E2 E4`
- Run Smart Move: engine computes a move via minimax (`minMaxDepth`, alpha–beta optional)
- Run Random Move: picks a random legal move
- Undo Last Move: restores the previous board and game state
- List all legal moves: shows legal moves for the current side
- List game history: shows moves played so far
- Write DB: saves the full game to DB
- Help: shows menu options
- Quit Game: return to Main Menu

## Board Editor

Interactive commands while editing:

- `PLACE <piece> <color> <coord>`: place a piece (e.g., `PLACE K W E1`)
	- Pieces: `K` (king), `Q` (queen), `R` (rook), `B` (bishop), `N` (knight), `P` (pawn)
	- Colors: `W` (white), `B` (black)
- `REMOVE <coord>`: remove any piece at the coordinate
- `EMPTY`: clear the board
- `DEFAULT`: reset to the standard starting position
- `SAVE`: prompt for a name, snapshot the position as a new game, and store it in DB
- `BACK`/`QUIT`: leave the editor

Saving from the editor initializes minimal history and writes both the Moves and BOARD snapshots to the database.

## Database Persistence

- File: a SQLite file in the current working directory
- Tables:
	- `Moves(GAME_NAME, ID, MOVE_TYPE, MOVED_BY, START_POS, DEST_POS, START_PIECE, DEST_PIECE, BOARD_COUNT)`
	- `BOARD(GAME_NAME, ID, A1..H8)` — each square stored as a two-character code, e.g. `PW` (white pawn), `KB` (black king), `EN` (empty/none)
- Writing: use Game Menu → Write DB or Board Editor → SAVE
- Loading: Main Menu → Load game from database
	- Choose by number or name, then browse positions with left/right (`a`/`d` or arrow keys)
	- Press Enter to load the currently shown snapshot

## Engine & Evaluation

- Move generation for all pieces, including castling, en passant, and promotion (to queen)
- Board evaluation combines material values and piece-square tables
- Minimax search depth configurable (`minMaxDepth`), with optional alpha–beta pruning (`use_AB_pruning`)
- Runtime stats after smart move: analyzed nodes and pruned branches

## Configuration (`bin/config.json`)

Automatically written and loaded at startup. Key options:

- `pawnValue`, `rookValue`, `knightValue`, `bishopValue`, `queenValue`, `kingValue`: material values
- `position_gamma`: weight for positional evaluation
- `minMaxDepth`: search depth for smart moves
- `use_AB_pruning`: enable/disable alpha–beta pruning
- `enable_debug_messages`: print extra debug output
- `network_port`: TCP port for hosting/joining network games (default: 5555)
- Piece-square tables: `pawnEvalWhite/Black`, `knightEvalWhite/Black`, `bishopEvalWhite/Black`, `rookEvalWhite/Black`, `evalQueenWhite/Black`, `kingEvalWhite/Black`

Edit `bin/config.json`, then rerun Mate to apply changes.

## Tips

- Manual moves are validated against generated legal moves
- Unicode rendering may vary by terminal; black pawns may be drawn as a solid circle
- If the DB already contains a game with the same name, saving will overwrite entries for that game


## Network Play

Host a game (server):

1. In Main Menu, choose Start network game → Host
2. Enter your display name and choose side (White/Black)
3. Optionally set a server password (press Enter for none)
4. The server announces `OPEN` (no password) or `LOCKED` (password required)
5. Share your host IP/hostname and the configured port with your opponent

Join a game (client):

1. In Main Menu, choose Start network game → Join
2. Enter the server host (IP/hostname) and your display name
3. If the server is `LOCKED`, you’ll be prompted for the password
4. On success, the game starts with player names shown on the board

In-game commands (network):

- Moves: enter start and end squares like `E2 E4`
- `a`: list all legal moves
- `l`: show game history
- `w`: write the current game to the database
- `h`: show network help
- `c`: send a chat message (works both while playing and while waiting)
- `q`: quit the game


## Troubleshooting

- Ensure `libsqlite3-dev` (headers and library) is installed
- If `config.json` is missing or malformed, Mate regenerates defaults
- Run with a modern terminal that supports UTF-8 for best board rendering
