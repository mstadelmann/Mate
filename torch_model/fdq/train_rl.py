"""Reinforcement learning (RL) training loop for the CHESS experiment,
using the fdq framework.

Start here if you don't know RL yet: torch_model/rl_training.md walks
through, in plain language, what a "policy" is, what "self-play" means,
what a "return" is, and why the update below (`REINFORCE`) is written the
way it is. This file is the "trainer" half; rl_self_play.py is the
"environment" half that actually plays games.

This trains the exact same ChessCNN architecture as the supervised
pipeline (chess_cnn_p00.yaml / train.py), so the same ONNX export flow,
and the same C++ src/chess_ML.cpp consumer, work for either model - only
how the weights are produced differs.
"""

import chess
import chess.engine
import torch
from fdq.experiment import fdqExperiment
from fdq.ui_functions import iprint, startProgBar

from rl_self_play import make_engine_move_getter, play_one_game, random_move_getter


def fdq_train(experiment: fdqExperiment) -> None:
    """Train chessRL via self-play against a fixed opponent, using
    REINFORCE (Monte Carlo policy gradient) - the simplest policy-gradient
    RL algorithm there is: play a game, then nudge every move the network
    made up (if it won) or down (if it lost), all by the same amount.

    No value network, no MCTS, no replay buffer - see rl_training.md for
    why those are the "usual" extra pieces (they mainly help you learn
    faster/stronger, not learn *correctly* - REINFORCE alone is already a
    valid, converging RL algorithm) and why they're skipped here.
    """
    iprint("RL (self-play vs a fixed opponent) training")

    model = experiment.models["chessRL"]
    optimizer = experiment.optimizers["chessRL"]
    args = experiment.cfg.train.args

    games_per_epoch: int = args.get("games_per_epoch", 100)
    games_per_update: int = args.get("games_per_update", 8)
    max_plies_per_game: int = args.get("max_plies_per_game", 200)
    engine_command: str = args.engine_command
    # Passed straight through to the engine's UCI `setoption` command, e.g.
    # {"Skill Level": 0} for Stockfish. Not every engine has a strength
    # knob like this - Sunfish (see rl_training.md), for instance, doesn't
    # - so this is empty by default, and only touched if you actually set
    # something here, rather than assuming any particular option exists.
    engine_uci_options: dict = dict(args.get("engine_uci_options", {}) or {})
    engine_movetime_ms: int = args.get("engine_movetime_ms", 50)

    # Whatever engine this points at, keeping it weak/fast matters: a
    # from-scratch network has no chance at all against a strong opponent,
    # and if it never wins or draws, REINFORCE's "make winning moves more
    # likely" half of the signal never fires - only the "make losing moves
    # less likely" half does, which is a much weaker teacher. A short
    # per-move time budget (engine_movetime_ms) works as a universal
    # strength cap for any UCI engine; engine_uci_options can add an
    # engine-specific one (like Stockfish's Skill Level) on top. Even so,
    # some engines (Sunfish included - see rl_training.md, which measures
    # this directly) have no genuine "easy mode" and may still beat a
    # from-scratch network almost every game regardless of movetime. The
    # special value "random" sidesteps that entirely: no engine process is
    # started at all, and the opponent just plays a uniformly random legal
    # move - a genuinely beatable (if unambitious) bootstrap opponent that
    # needs no install of any kind, useful as an easier first curriculum
    # stage before switching to a real engine.
    engine = None
    if engine_command == "random":
        iprint("Opponent: uniformly random legal moves (no engine process).")
        opponent_move_getter = random_move_getter
    else:
        iprint(f"Opening chess engine '{engine_command}' (uci options: {engine_uci_options or 'none'})...")
        engine = chess.engine.SimpleEngine.popen_uci(engine_command)
        if engine_uci_options:
            engine.configure(engine_uci_options)
        opponent_move_getter = make_engine_move_getter(engine, engine_movetime_ms / 1000.0)

    try:
        for epoch in range(experiment.start_epoch, experiment.nb_epochs):
            experiment.on_epoch_start(epoch=epoch)

            model.train()
            epoch_loss_sum = 0.0
            nb_updates = 0
            wins = losses = draws = 0

            games_played = 0
            pbar = startProgBar(games_per_epoch, "self-play...")
            while games_played < games_per_epoch:
                batch_size = min(games_per_update, games_per_epoch - games_played)

                # --- Play one small batch of self-play games -----------
                # We play several games before every gradient update (rather
                # than updating after each single game) purely to average
                # out some of the noise: any one game's outcome is a very
                # high-variance estimate of "were the moves in it good,"
                # since a single lucky or unlucky moment can flip a whole
                # game's result. Averaging several games' updates together
                # gives a steadier, less noisy nudge to the network.
                game_losses = []
                for game_idx in range(batch_size):
                    # Alternate colors so the one network learns to play
                    # both sides, mirroring canonicalization on the
                    # supervised side (see torch_model/torch_model.md).
                    network_plays_white = (games_played + game_idx) % 2 == 0

                    trajectory = play_one_game(
                        model=model,
                        opponent_move_getter=opponent_move_getter,
                        network_plays_white=network_plays_white,
                        device=experiment.device,
                        max_plies=max_plies_per_game,
                    )

                    if trajectory.result == "win":
                        wins += 1
                        game_return = 1.0
                    elif trajectory.result == "loss":
                        losses += 1
                        game_return = -1.0
                    else:
                        draws += 1
                        game_return = 0.0

                    if trajectory.log_probs:
                        # The core of REINFORCE, in one line: take the
                        # average log-probability the network assigned to
                        # the moves *it actually played* this game, and
                        # scale it by the game's outcome (+1 / -1 / 0).
                        # Negated because torch optimizers *minimize* a
                        # loss, and we want to *maximize* return-weighted
                        # log-probability - minimizing its negative is the
                        # same thing. A draw (game_return=0) contributes
                        # nothing to learn from under this simplest
                        # possible reward scheme - see rl_training.md for
                        # how you could change that.
                        mean_log_prob = torch.stack(trajectory.log_probs).mean()
                        game_losses.append(-game_return * mean_log_prob)

                games_played += batch_size
                pbar.update(games_played)

                if not game_losses:
                    continue  # every game in this batch was a draw; nothing to update on

                batch_loss = torch.stack(game_losses).mean()

                optimizer.zero_grad()
                batch_loss.backward()
                optimizer.step()

                epoch_loss_sum += batch_loss.detach().item()
                nb_updates += 1

            pbar.finish()

            avg_loss = epoch_loss_sum / max(nb_updates, 1)
            win_rate = wins / games_per_epoch
            draw_rate = draws / games_per_epoch
            loss_rate = losses / games_per_epoch

            iprint(
                f"Epoch {epoch}: policy loss {avg_loss:.4f} | "
                f"vs {engine_command}: "
                f"{wins} wins, {draws} draws, {losses} losses "
                f"({win_rate:.1%} / {draw_rate:.1%} / {loss_rate:.1%})"
            )

            # fdq expects both a train and a val loss to track "best"
            # checkpoints and drive early stopping. Self-play has no held
            # out validation set (there's no fixed dataset to split), so we
            # reuse the same policy loss for both - a documented stand-in,
            # not a real train/val split.
            experiment.trainLoss = avg_loss
            experiment.valLoss = avg_loss

            experiment.on_epoch_end(
                log_scalars={
                    "win_rate": win_rate,
                    "draw_rate": draw_rate,
                    "loss_rate": loss_rate,
                }
            )

            if experiment.check_early_stop():
                break
    finally:
        if engine is not None:
            engine.quit()
