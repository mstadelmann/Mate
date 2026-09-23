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

import random
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
MoveGetter = Callable[[chess.Board], chess.Move]


@dataclass
class GameTrajectory:
    """Everything one played game contributes to the RL update.

    `log_probs` has one entry per move the *network* made (not the
    opponent's moves - we have no gradient to take through Stockfish's
    move choice, so those plies simply aren't part of the trajectory).
    `result` is from the network's own point of view, already accounting
    for which color it played that game.
    """

    log_probs: List[torch.Tensor] = field(default_factory=list)
    result: str = "draw"  # one of "win", "loss", "draw"
    nb_plies: int = 0


def select_network_move(model: torch.nn.Module, board: chess.Board, device: torch.device, greedy: bool = False):
    """Ask the policy network for one move over the board's legal moves,
    and return (chosen_move, log_prob_of_that_move).

    By default (``greedy=False``) the move is *sampled*, not argmax'd, from
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
    candidates = legal_move_candidates(board)

    board_tensor = torch.from_numpy(board_to_array(board)).unsqueeze(0).to(device)
    from_logits, to_logits = model(board_tensor)
    from_logits = from_logits.squeeze(0)
    to_logits = to_logits.squeeze(0)

    # Score each legal move as from_logit + to_logit for its two squares -
    # the same "independent from/to squares" simplification used everywhere
    # else in this project (see src/chess_ML.cpp's scores_to_legal_move()).
    # This is NOT the network's full 64x64 belief, only its belief
    # restricted to moves that are actually legal right now.
    move_scores = torch.stack(
        [from_logits[from_idx] + to_logits[to_idx] for _, from_idx, to_idx in candidates]
    )

    # A Categorical distribution turns those raw scores into proper
    # probabilities (via softmax, handled internally) that sum to 1 across
    # the legal moves, and gives us both a way to sample from them and the
    # log-probability of whatever gets sampled - exactly the two things a
    # policy-gradient method needs.
    distribution = torch.distributions.Categorical(logits=move_scores)
    chosen_idx = torch.argmax(move_scores) if greedy else distribution.sample()
    log_prob = distribution.log_prob(chosen_idx)

    chosen_move, _, _ = candidates[int(chosen_idx.item())]
    return chosen_move, log_prob


def play_one_game(
    model: torch.nn.Module,
    opponent_move_getter: MoveGetter,
    network_plays_white: bool,
    device: torch.device,
    max_plies: int = 200,
    greedy: bool = False,
) -> GameTrajectory:
    """Play one game, the network against `opponent_move_getter`, and
    return the network's trajectory (its move log-probs plus the final
    result from its own perspective).

    Alternating which color the network plays across games (see
    train_rl.py) is what teaches one network to handle both colors, the
    same way canonicalization does for the supervised model - the network
    itself has no notion of "White" or "Black", only "me" and "the
    opponent", via the same board_to_array() encoding used everywhere else.

    ``greedy`` is passed straight through to select_network_move() - leave
    it False during training (exploration matters), set it True for
    evaluation (see rl_evaluator.py).
    """
    board = chess.Board()
    trajectory = GameTrajectory()

    while not board.is_game_over() and trajectory.nb_plies < max_plies:
        network_to_move = (board.turn == chess.WHITE) == network_plays_white

        if network_to_move:
            move, log_prob = select_network_move(model, board, device, greedy=greedy)
            trajectory.log_probs.append(log_prob)
        else:
            move = opponent_move_getter(board)

        board.push(move)
        trajectory.nb_plies += 1

    outcome = board.outcome(claim_draw=True)
    if outcome is None or outcome.winner is None:
        trajectory.result = "draw"
    elif (outcome.winner == chess.WHITE) == network_plays_white:
        trajectory.result = "win"
    else:
        trajectory.result = "loss"

    return trajectory


def play_greedy_games(
    model: torch.nn.Module,
    opponent_move_getter: MoveGetter,
    nb_games: int,
    device: torch.device,
    max_plies: int = 200,
) -> Tuple[int, int, int]:
    """Play `nb_games` greedy games (no exploration, no gradients) against
    `opponent_move_getter`, alternating colors, and return (wins, draws,
    losses). Used for per-epoch validation by both train_rl.py and the
    supervised train.py - works for either model, since both share the
    same board_to_array() encoding and (from_logits, to_logits) output."""
    wins = draws = losses = 0
    with torch.no_grad():
        for game_idx in range(nb_games):
            trajectory = play_one_game(
                model=model,
                opponent_move_getter=opponent_move_getter,
                network_plays_white=game_idx % 2 == 0,
                device=device,
                max_plies=max_plies,
                greedy=True,
            )
            if trajectory.result == "win":
                wins += 1
            elif trajectory.result == "loss":
                losses += 1
            else:
                draws += 1
    return wins, draws, losses


def random_move_getter(board: chess.Board) -> chess.Move:
    """Trivial opponent used only for local testing of the self-play loop
    itself, without needing a real chess engine installed. Never used
    during real RL training - see make_engine_move_getter() for that."""
    return random.choice(list(board.legal_moves))


def make_engine_move_getter(engine: "chess.engine.SimpleEngine", movetime_seconds: float) -> MoveGetter:
    """Wrap a running UCI engine (see train_rl.py for how it's opened and
    configured) as a plain MoveGetter. Works with any UCI-speaking engine -
    Stockfish, Sunfish (pure Python, see torch_model/rl_training.md), or
    anything else - since it only relies on the standard `play` command."""
    limit = chess.engine.Limit(time=movetime_seconds)

    def get_move(board: chess.Board) -> chess.Move:
        return engine.play(board, limit).move

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
