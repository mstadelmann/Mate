"""Self-play game loop for reinforcement learning (RL).

This is the "environment" half of the RL training pipeline: it plays whole
games move by move and hands train_rl.py the pieces it needs to compute a
policy-gradient update. See torch_model/rl_training.md for a from-scratch
explanation of the RL concepts used here (policy, episode, return, why we
sample instead of always taking the best move, etc.) - the comments in this
file assume you've read that first.

Kept free of any fdq import, like chess_encoding.py, so it can be tested and
used standalone.
"""

import os
import random
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Tuple

import chess
import chess.engine
import torch

from chess_encoding import board_to_array, legal_move_candidates

# A move-getter is just "give me a move for this board" - in real training
# this calls out to a UCI engine subprocess (see make_engine_move_getter
# below) or, for the zero-dependency "random" opponent tier, is just
# random_move_getter directly. Keeping it as a plain callable means the
# game loop itself doesn't need to know anything about UCI engines.
MoveGetter = Callable[[chess.Board], Optional[chess.Move]]


@dataclass
class GameTrajectory:
    """Everything one played game contributes to the RL update.

    `log_probs` has one entry per move the *network* made (not the
    opponent's moves - we have no gradient to take through Stockfish's
    move choice, so those plies simply aren't part of the trajectory).
    `entropies` has the policy's entropy over the legal moves at each of
    those moves (for the entropy bonus in train_rl.py), and `materials`
    the material balance (network minus opponent, in pawns) just *before*
    each of them, plus one final entry for the end of the game - so
    materials[t + 1] - materials[t] is what network move t (and the
    opponent's reply) changed. `result` is from the network's own point of
    view, already accounting for which color it played that game.
    `termination` says how the game ended: a lower-cased chess.Termination
    name ("checkmate", "stalemate", "threefold_repetition", ...),
    "resignation" if the opponent engine returned no move (scored as a
    win), or "max_plies" if it was cut off at max_plies with no claimable
    draw (still scored as a draw).
    """

    log_probs: List[torch.Tensor] = field(default_factory=list)
    entropies: List[torch.Tensor] = field(default_factory=list)
    materials: List[int] = field(default_factory=list)
    result: str = "draw"  # one of "win", "loss", "draw"
    termination: str = "max_plies"
    nb_plies: int = 0


# Every way a (standard chess) game can end in a draw - see
# GameTrajectory.termination. Fixed list so each reason gets logged every
# epoch, even at zero, instead of its wandb curve having gaps.
DRAW_REASONS = (
    "max_plies",
    "stalemate",
    "insufficient_material",
    "threefold_repetition",
    "fifty_moves",
    "fivefold_repetition",
    "seventyfive_moves",
)

PIECE_VALUES = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9}


def material_balance(board: chess.Board, color: chess.Color) -> int:
    """Material of `color` minus its opponent's, in pawns (P=1, N=B=3,
    R=5, Q=9, kings not counted)."""
    return sum(
        value * (len(board.pieces(piece, color)) - len(board.pieces(piece, not color)))
        for piece, value in PIECE_VALUES.items()
    )


def select_network_moves(
    model: torch.nn.Module, boards: List[chess.Board], device: torch.device, greedy: bool = False
) -> List[Tuple[chess.Move, torch.Tensor, torch.Tensor]]:
    """Ask the policy network for one move per board, over each board's
    legal moves, and return one (chosen_move, log_prob_of_that_move,
    entropy_over_legal_moves) triple per board.

    All boards go through the network in a single batched forward pass -
    one GPU call for every game currently waiting on the network, instead
    of one call per game (see play_games()).

    By default (``greedy=False``) each move is *sampled*, not argmax'd, from
    the network's current belief. Why sample instead of always playing the
    network's top choice? Because RL learns from trial and error: if the
    network always played its current favorite move, it could never
    discover that some *other* move it currently underrates is actually
    better. Sampling is how the network keeps trying alternatives
    ("exploration") instead of only ever reinforcing whatever it already
    (perhaps wrongly) believes is best. `log_prob` is the log-probability
    the network assigned to the move that was actually sampled - this is
    the quantity the REINFORCE update (see train_rl.py) nudges up or down
    depending on whether the game was won.

    ``greedy=True`` instead always plays the network's single top-scoring
    move - appropriate when *evaluating* a trained model's actual playing
    strength (see rl_evaluator.py), where you want its best guess, not a
    deliberately-exploratory alternative.
    """
    board_tensor = torch.stack([torch.from_numpy(board_to_array(board)) for board in boards]).to(device)
    all_from_logits, all_to_logits = model(board_tensor)[:2]  # ignore a value output, if any

    choices = []
    for board, from_logits, to_logits in zip(boards, all_from_logits, all_to_logits):
        candidates = legal_move_candidates(board)

        # Score each legal move as from_logit + to_logit for its two squares -
        # the same "independent from/to squares" simplification used everywhere
        # else in this project (see src/chess_ML.cpp's scores_to_legal_move()).
        # This is NOT the network's full 64x64 belief, only its belief
        # restricted to moves that are actually legal right now.
        from_idx = torch.tensor([c[1] for c in candidates], device=from_logits.device)
        to_idx = torch.tensor([c[2] for c in candidates], device=to_logits.device)
        move_scores = from_logits[from_idx] + to_logits[to_idx]

        # A Categorical distribution turns those raw scores into proper
        # probabilities (via softmax, handled internally) that sum to 1 across
        # the legal moves, and gives us both a way to sample from them and the
        # log-probability of whatever gets sampled - exactly the two things a
        # policy-gradient method needs.
        distribution = torch.distributions.Categorical(logits=move_scores)
        chosen_idx = torch.argmax(move_scores) if greedy else distribution.sample()
        log_prob = distribution.log_prob(chosen_idx)

        chosen_move, _, _ = candidates[int(chosen_idx.item())]
        choices.append((chosen_move, log_prob, distribution.entropy()))
    return choices


def play_games(
    model: torch.nn.Module,
    opponent_move_getters: List[MoveGetter],
    network_plays_white: List[bool],
    device: torch.device,
    max_plies: int = 200,
    greedy: bool = False,
) -> List[GameTrajectory]:
    """Play one game per entry of `network_plays_white`, all at the same
    time, the network against the opponent(s), and return each game's
    trajectory (its move log-probs plus the final result from the
    network's own point of view).

    The games advance in lockstep, which is what makes this fast:
      - every game waiting on the network is answered by one batched
        forward pass (select_network_moves()), keeping the GPU busy instead
        of feeding it one board at a time;
      - every game waiting on the opponent is answered in parallel, one
        thread per entry of `opponent_move_getters` (each typically wrapping
        its own engine process - see make_opponents()), so N engines think
        on N CPU cores at once instead of one after another. Game i always
        uses getter i % len(opponent_move_getters), and each getter is only
        ever called from one thread at a time.

    Alternating which color the network plays across games (see
    train_rl.py) is what teaches one network to handle both colors, the
    same way canonicalization does for the supervised model - the network
    itself has no notion of "White" or "Black", only "me" and "the
    opponent", via the same board_to_array() encoding used everywhere else.

    ``greedy`` is passed straight through to select_network_moves() - leave
    it False during training (exploration matters), set it True for
    evaluation (see rl_evaluator.py).
    """
    nb_games = len(network_plays_white)
    boards = [chess.Board() for _ in range(nb_games)]
    trajectories = [GameTrajectory() for _ in range(nb_games)]
    nb_getters = len(opponent_move_getters)
    # Games where the opponent engine returned no move (see
    # make_engine_move_getter()) - ended as a resignation, won by the network.
    resigned = [False] * nb_games

    def is_active(i: int) -> bool:
        return not resigned[i] and not boards[i].is_game_over() and trajectories[i].nb_plies < max_plies

    def network_to_move(i: int) -> bool:
        return (boards[i].turn == chess.WHITE) == network_plays_white[i]

    def play_opponent_moves(getter_idx: int, game_indices: List[int]) -> None:
        for i in game_indices:
            move = opponent_move_getters[getter_idx](boards[i])
            if move is None:
                resigned[i] = True
                continue
            boards[i].push(move)
            trajectories[i].nb_plies += 1

    with ThreadPoolExecutor(max_workers=nb_getters) as pool:
        while True:
            active = [i for i in range(nb_games) if is_active(i)]
            if not active:
                break

            net_games = [i for i in active if network_to_move(i)]
            if net_games:
                for i, (move, log_prob, entropy) in zip(
                    net_games, select_network_moves(model, [boards[i] for i in net_games], device, greedy=greedy)
                ):
                    trajectories[i].materials.append(material_balance(boards[i], boards[i].turn))
                    boards[i].push(move)
                    trajectories[i].log_probs.append(log_prob)
                    trajectories[i].entropies.append(entropy)
                    trajectories[i].nb_plies += 1

            opp_games = [i for i in range(nb_games) if is_active(i) and not network_to_move(i)]
            by_getter = [[i for i in opp_games if i % nb_getters == g] for g in range(nb_getters)]
            if nb_getters == 1:
                play_opponent_moves(0, by_getter[0])
            else:
                # list() re-raises any exception from a worker thread here.
                list(pool.map(play_opponent_moves, range(nb_getters), by_getter))

    for board, trajectory, plays_white, has_resigned in zip(boards, trajectories, network_plays_white, resigned):
        trajectory.materials.append(material_balance(board, chess.WHITE if plays_white else chess.BLACK))
        if has_resigned:
            trajectory.termination = "resignation"
            trajectory.result = "win"
            continue
        outcome = board.outcome(claim_draw=True)
        trajectory.termination = "max_plies" if outcome is None else outcome.termination.name.lower()
        if outcome is None or outcome.winner is None:
            trajectory.result = "draw"
        elif (outcome.winner == chess.WHITE) == plays_white:
            trajectory.result = "win"
        else:
            trajectory.result = "loss"

    return trajectories


def play_one_game(
    model: torch.nn.Module,
    opponent_move_getter: MoveGetter,
    network_plays_white: bool,
    device: torch.device,
    max_plies: int = 200,
    greedy: bool = False,
) -> GameTrajectory:
    """Play a single game - play_games() with one game, for standalone use."""
    return play_games(model, [opponent_move_getter], [network_plays_white], device, max_plies, greedy)[0]


def play_greedy_games(
    model: torch.nn.Module,
    opponent_move_getters: List[MoveGetter],
    nb_games: int,
    device: torch.device,
    max_plies: int = 200,
) -> Tuple[int, int, int]:
    """Play `nb_games` greedy games (no exploration, no gradients) against
    the opponent(s), alternating colors, all at once (see play_games()),
    and return (wins, draws, losses). Used for per-epoch validation by both
    train_rl.py and the supervised train.py, and for testing by
    rl_evaluator.py - works for either model, since both share the same
    board_to_array() encoding and (from_logits, to_logits) output."""
    results = [t.result for t in play_greedy_trajectories(model, opponent_move_getters, nb_games, device, max_plies)]
    return results.count("win"), results.count("draw"), results.count("loss")


def play_greedy_trajectories(
    model: torch.nn.Module,
    opponent_move_getters: List[MoveGetter],
    nb_games: int,
    device: torch.device,
    max_plies: int = 200,
) -> List[GameTrajectory]:
    """play_greedy_games(), but returning the full trajectories, for callers
    that also want e.g. each game's `termination`."""
    with torch.no_grad():
        return play_games(
            model=model,
            opponent_move_getters=opponent_move_getters,
            network_plays_white=[game_idx % 2 == 0 for game_idx in range(nb_games)],
            device=device,
            max_plies=max_plies,
            greedy=True,
        )


def random_move_getter(board: chess.Board) -> chess.Move:
    """Trivial opponent used only for local testing of the self-play loop
    itself, without needing a real chess engine installed. Never used
    during real RL training - see make_engine_move_getter() for that."""
    return random.choice(list(board.legal_moves))


def make_engine_move_getter(engine: "chess.engine.SimpleEngine", movetime_seconds: float) -> MoveGetter:
    """Wrap a running UCI engine (see train_rl.py for how it's opened and
    configured) as a plain MoveGetter. Works with any UCI-speaking engine -
    Stockfish, Sunfish (pure Python, see torch_model/rl_training.md), or
    anything else - since it only relies on the standard `play` command.

    Returns None when the engine plays no move (`bestmove (none)`, or a
    resignation - Sunfish does this when it sees itself lost); play_games()
    then ends that game as a win for the network."""
    limit = chess.engine.Limit(time=movetime_seconds)

    def get_move(board: chess.Board) -> Optional[chess.Move]:
        result = engine.play(board, limit)
        return None if result.resigned else result.move

    return get_move


def make_opponent(
    engine_command: str, engine_uci_options: dict, engine_movetime_ms: int
) -> Tuple[MoveGetter, Optional["chess.engine.SimpleEngine"]]:
    """Build the opponent move-getter for `engine_command`, plus the
    underlying chess.engine.SimpleEngine to `quit()` afterwards (None for
    "random", which needs no engine process). Shared by training,
    per-epoch validation (both train_rl.py) and testing (rl_evaluator.py).
    """
    if engine_command == "random":
        print("Opponent: uniformly random legal moves (no engine process).")
        return random_move_getter, None

    print(f"Opening chess engine '{engine_command}' (uci options: {engine_uci_options or 'none'})...")
    engine = chess.engine.SimpleEngine.popen_uci(engine_command)
    if engine_uci_options:
        engine.configure(engine_uci_options)
    return make_engine_move_getter(engine, engine_movetime_ms / 1000.0), engine


def make_opponents(
    engine_command: str, engine_uci_options: dict, engine_movetime_ms: int, nb_engines: Optional[int] = None
) -> Tuple[List[MoveGetter], List["chess.engine.SimpleEngine"]]:
    """Like make_opponent(), but opens `nb_engines` independent engine
    processes (default: one per CPU core), so play_games() can have them
    all think in parallel. Returns (move_getters, engines), engines being
    the list to close_engines() afterwards. "random" needs no engine
    process and is pure Python (so gains nothing from threads) - it always
    gets a single getter and no engines.

    Each engine keeps whatever "Threads" setting engine_uci_options gives it
    (Stockfish defaults to 1) - with one engine per core, leave it at 1.
    """
    if engine_command == "random":
        getter, _ = make_opponent(engine_command, engine_uci_options, engine_movetime_ms)
        return [getter], []

    nb_engines = max(1, nb_engines or os.cpu_count() or 1)
    getters, engines = [], []
    try:
        for _ in range(nb_engines):
            getter, engine = make_opponent(engine_command, engine_uci_options, engine_movetime_ms)
            getters.append(getter)
            engines.append(engine)
    except BaseException:
        close_engines(engines)
        raise
    return getters, engines


@dataclass
class ValOpponent:
    """One entry of train.args.val, with its engine(s) opened - see
    make_val_opponents()."""

    log_name: str
    move_getters: List[MoveGetter]
    nb_games: int
    max_plies: int
    select_best: bool


def make_val_opponents(
    val_cfg,
    nb_engines: Optional[int] = None,
    default_max_plies: int = 200,
    default_movetime_ms: int = 50,
    require_select_best: bool = False,
) -> Tuple[List[ValOpponent], List["chess.engine.SimpleEngine"]]:
    """Open the per-epoch validation opponents of `train.args.val`, used by
    both train.py and train_rl.py. `val_cfg` is either a single opponent
    config or a list of them (None = no validation); entries with
    nb_games <= 0 are skipped. Each entry's `log_name` (default:
    engine_command's basename) must be unique - it prefixes that
    opponent's logged metrics (see play_val_games()).

    `select_best: true` marks the entry whose win rate should drive
    checkpoint selection (train_rl.py's valLoss). A lone entry is selected
    implicitly; more than one flagged entry is an error, and so is having
    several entries with none flagged when `require_select_best` is set.
    Returns (opponents, engines), engines being the list to close_engines()
    afterwards.
    """
    if val_cfg is None:
        val_cfg = []
    elif hasattr(val_cfg, "keys"):
        val_cfg = [val_cfg]

    # Check the whole config before opening any engine process.
    entries = []
    for cfg in val_cfg:
        if cfg.get("nb_games", 0) <= 0:
            continue
        engine_command = cfg.get("engine_command", "random")
        log_name = cfg.get("log_name", None) or os.path.basename(engine_command)
        if any(e["log_name"] == log_name for e in entries):
            raise ValueError(f"Duplicate train.args.val log_name '{log_name}'")
        entries.append({"log_name": log_name, "engine_command": engine_command, "cfg": cfg})

    nb_selected = sum(bool(e["cfg"].get("select_best", False)) for e in entries)
    if nb_selected > 1:
        raise ValueError("train.args.val: at most one entry may set select_best: true")
    if nb_selected == 0 and len(entries) > 1 and require_select_best:
        raise ValueError(
            "train.args.val has several opponents: set select_best: true on the one"
            " whose win rate should pick the best checkpoint"
        )

    opponents, engines = [], []
    try:
        for e in entries:
            cfg = e["cfg"]
            move_getters, new_engines = make_opponents(
                e["engine_command"],
                dict(cfg.get("engine_uci_options", {}) or {}),
                cfg.get("engine_movetime_ms", default_movetime_ms),
                nb_engines,
            )
            engines.extend(new_engines)
            opponents.append(
                ValOpponent(
                    log_name=e["log_name"],
                    move_getters=move_getters,
                    nb_games=cfg.get("nb_games"),
                    max_plies=cfg.get("max_plies_per_game", default_max_plies),
                    select_best=bool(cfg.get("select_best", False)) or len(entries) == 1,
                )
            )
    except BaseException:
        close_engines(engines)
        raise
    return opponents, engines


def play_val_games(model: torch.nn.Module, opponent: ValOpponent, device: torch.device) -> Tuple[dict, str]:
    """Play one validation opponent's greedy games. Returns (log_scalars,
    summary): the metrics `<log_name>/val_win_rate`, `val_draw_rate`,
    `val_loss_rate` and one `val_draw_<reason>_rate` per DRAW_REASONS
    entry (fractions of all games, so the reasons sum to val_draw_rate),
    plus a one-line human-readable summary for the console."""
    trajectories = play_greedy_trajectories(
        model=model,
        opponent_move_getters=opponent.move_getters,
        nb_games=opponent.nb_games,
        device=device,
        max_plies=opponent.max_plies,
    )
    nb_games = opponent.nb_games
    results = [t.result for t in trajectories]
    wins, draws, losses = results.count("win"), results.count("draw"), results.count("loss")
    draw_reasons = Counter(t.termination for t in trajectories if t.result == "draw")

    # Raw counts not logged: nb_games is fixed, so they're the same curves
    # as the *_rate ones, just scaled.
    prefix = opponent.log_name
    log_scalars = {
        f"{prefix}/val_win_rate": wins / nb_games,
        f"{prefix}/val_draw_rate": draws / nb_games,
        f"{prefix}/val_loss_rate": losses / nb_games,
    }
    for reason in DRAW_REASONS:
        log_scalars[f"{prefix}/val_draw_{reason}_rate"] = draw_reasons[reason] / nb_games

    summary = f"vs {prefix}: {wins} wins, {draws} draws, {losses} losses"
    if draws:
        summary += " (draws - " + ", ".join(f"{r}: {n}" for r, n in draw_reasons.most_common()) + ")"
    return log_scalars, summary


def close_engines(engines: List["chess.engine.SimpleEngine"]) -> None:
    for engine in engines:
        engine.quit()
