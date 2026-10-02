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

import os

import torch
from fdq.experiment import fdqExperiment
from fdq.ui_functions import iprint, startProgBar

from rl_self_play import (
    GameTrajectory,
    close_engines,
    make_opponents,
    make_val_opponents,
    play_games,
    play_val_games,
)


def load_init_weights(experiment: fdqExperiment, model: torch.nn.Module, init_weights_path: str) -> None:
    """Warm-start: copy the weights of a trained model (e.g. a supervised
    chess_cnn_p01 best_val_chessCNN_e*.fdqm) into the freshly instantiated
    chessRL model, before any RL update.

    Deliberately NOT fdq's `models.chessRL.trained_model_path`: fdq also
    uses that one to pick the model for testing and dumping, which would
    then evaluate/export the supervised starting point instead of the
    RL-trained result. And NOT `mode.resume_chpt_path`, which resumes a
    whole fdq checkpoint (epoch counter, optimizer, ...) of the *same*
    experiment. Only the weights are copied, in place (load_state_dict), so
    the optimizer fdq already built around the model's parameters stays
    valid. Architectures must match exactly (strict=True raises otherwise).

    Skipped when resuming (start_epoch > 0): the resumed checkpoint already
    holds the RL-trained weights, which must not be overwritten.
    """
    if experiment.start_epoch > 0:
        iprint(f"Resuming at epoch {experiment.start_epoch}: ignoring init_weights_path.")
        return

    path = os.path.expanduser(init_weights_path)
    iprint(f"Warm start: loading initial weights from {path}")
    # .fdqm files are whole pickled models (torch.save(model)), not state dicts.
    source = torch.load(path, weights_only=False, map_location=experiment.device)
    state_dict = source.state_dict() if isinstance(source, torch.nn.Module) else source
    model.load_state_dict(state_dict, strict=True)


def move_returns(
    trajectory: GameTrajectory,
    gamma: float,
    material_reward_scale: float,
    draw_reward: float,
    stalemate_when_ahead_reward: float,
) -> torch.Tensor:
    """Discounted return G_t of each network move t of one game.

    Per-move reward r_t: the material balance change from just before
    move t to just before the network's next move (i.e. including the
    opponent's reply), times material_reward_scale - so winning a piece or
    walking into a capture is credited to the move that caused it, not
    smeared over the whole game. The game result is added to the last
    move's reward: +1 win, -1 loss, draw_reward for a draw, or
    stalemate_when_ahead_reward for a stalemate while ahead on material
    (the typical "won the material, couldn't mate" failure).
    G_t = r_t + gamma * G_{t+1}.
    """
    materials = trajectory.materials
    rewards = [material_reward_scale * (materials[t + 1] - materials[t]) for t in range(len(trajectory.log_probs))]

    if trajectory.result == "win":
        final_reward = 1.0
    elif trajectory.result == "loss":
        final_reward = -1.0
    elif trajectory.termination == "stalemate" and materials[-1] > 0:
        final_reward = stalemate_when_ahead_reward
    else:
        final_reward = draw_reward
    rewards[-1] += final_reward

    returns = []
    running = 0.0
    for reward in reversed(rewards):
        running = reward + gamma * running
        returns.append(running)
    return torch.tensor(returns[::-1], dtype=torch.float32)


def fdq_train(experiment: fdqExperiment) -> None:
    """Train chessRL via self-play against a fixed opponent, using
    REINFORCE (Monte Carlo policy gradient) - the simplest policy-gradient
    RL algorithm there is: play a game, then nudge every move the network
    made up (if it won) or down (if it lost). Optionally (see the
    gamma/material_reward_scale/draw_reward/use_baseline/entropy_coef
    knobs in rl_training.md section 4) with per-move material rewards, a
    batch-mean baseline and an entropy bonus.

    No value network, no MCTS, no replay buffer - see rl_training.md for
    why those are the "usual" extra pieces (they mainly help you learn
    faster/stronger, not learn *correctly* - REINFORCE alone is already a
    valid, converging RL algorithm) and why they're skipped here.
    """
    iprint("RL (self-play vs a fixed opponent) training")

    model = experiment.models["chessRL"]
    optimizer = experiment.optimizers["chessRL"]
    args = experiment.cfg.train.args

    # Optional warm start from a (supervised) model - null trains from
    # random weights, see load_init_weights() and rl_training.md section 8.
    init_weights_path = args.get("init_weights_path", None)
    if init_weights_path:
        load_init_weights(experiment, experiment.models_no_ddp["chessRL"], init_weights_path)
    freeze_batchnorm_stats: bool = args.get("freeze_batchnorm_stats", False)

    games_per_epoch: int = args.get("games_per_epoch", 100)
    games_per_update: int = args.get("games_per_update", 8)
    max_plies_per_game: int = args.get("max_plies_per_game", 200)
    # Reward shaping and variance reduction - see move_returns() and the
    # update below. All default to the plain REINFORCE of rl_training.md
    # section 1 (game result only, no baseline, no entropy bonus).
    gamma: float = args.get("gamma", 1.0)
    material_reward_scale: float = args.get("material_reward_scale", 0.0)
    draw_reward: float = args.get("draw_reward", 0.0)
    stalemate_when_ahead_reward: float = args.get("stalemate_when_ahead_reward", draw_reward)
    use_baseline: bool = args.get("use_baseline", False)
    entropy_coef: float = args.get("entropy_coef", 0.0)
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
    #
    # One engine process per CPU core by default (train.args.nb_engines
    # overrides), so the games of each update batch are answered in
    # parallel - see play_games() in rl_self_play.py.
    nb_engines = args.get("nb_engines", None)
    opponent_move_getters, engines = make_opponents(
        engine_command, engine_uci_options, engine_movetime_ms, nb_engines
    )

    # Optional per-epoch validation against its *own* opponent(s)
    # (train.args.val), e.g. train vs Stockfish Skill Level 5 but validate
    # vs Skill Level 0, so progress stays visible even while the training
    # opponent still wins nearly every game. Played greedily (the network's
    # best move, no exploration) and without gradients - same as
    # rl_evaluator.py does. train.args.val is either a single opponent
    # config or a list of them, each logged as <log_name>/val_win_rate etc.
    # (see make_val_opponents() in rl_self_play.py); with several, exactly
    # one sets select_best: true to pick the win rate that drives valLoss
    # below. Omit train.args.val (or set nb_games: 0) to skip validation.
    try:
        val_opponents, val_engines = make_val_opponents(
            args.get("val", None),
            nb_engines=nb_engines,
            default_max_plies=max_plies_per_game,
            default_movetime_ms=engine_movetime_ms,
            require_select_best=True,
        )
    except BaseException:
        close_engines(engines)
        raise
    best_val_opponent = next((opp for opp in val_opponents if opp.select_best), None)

    try:
        for epoch in range(experiment.start_epoch, experiment.nb_epochs):
            experiment.on_epoch_start(epoch=epoch)

            # In train() mode every forward pass during play overwrites
            # BatchNorm's running mean/var with statistics of whatever odd
            # batch of boards happens to be waiting (shrinking to 1 as games
            # end). That alone - without any optimizer step - drops the
            # warm-started supervised model from 68 to 22 wins in 100 games
            # vs random (see rl_training.md section 8). eval() keeps the
            # running statistics frozen; gradients still flow and every
            # weight, including BatchNorm's affine scale/shift, still learns.
            if freeze_batchnorm_stats:
                model.eval()
            else:
                model.train()
            epoch_loss_sum = 0.0
            epoch_entropy_sum = 0.0
            epoch_return_sum = 0.0
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
                #
                # All games of the batch are played at once (batched
                # network forward passes, engines thinking in parallel) -
                # they all use the same weights, as REINFORCE requires,
                # since the update only happens after the whole batch.
                trajectories = play_games(
                    model=model,
                    opponent_move_getters=opponent_move_getters,
                    # Alternate colors so the one network learns to play
                    # both sides, mirroring canonicalization on the
                    # supervised side (see torch_model/torch_model.md).
                    network_plays_white=[(games_played + game_idx) % 2 == 0 for game_idx in range(batch_size)],
                    device=experiment.device,
                    max_plies=max_plies_per_game,
                )
                wins += sum(t.result == "win" for t in trajectories)
                losses += sum(t.result == "loss" for t in trajectories)
                draws += sum(t.result == "draw" for t in trajectories)

                games_played += batch_size
                pbar.update(games_played)

                played = [t for t in trajectories if t.log_probs]
                if not played:
                    continue  # network made no move in any game (can't happen from the initial position)

                # One return per network move (see move_returns()), so a
                # move is judged by what happened *after* it, not by the
                # whole game's result.
                returns = [
                    move_returns(t, gamma, material_reward_scale, draw_reward, stalemate_when_ahead_reward).to(
                        experiment.device
                    )
                    for t in played
                ]
                # Baseline: subtract the batch's mean return, so a move is
                # pushed up only if it did *better than usual*, down only
                # if worse. Without it, a batch of all-lost games (every
                # return -1) pushes down every sampled move - the reason the
                # Stockfish/Sunfish runs only got worse (rl_training.md
                # section 8). It doesn't change the expected gradient, only
                # its variance.
                baseline = torch.cat(returns).mean() if use_baseline else 0.0

                # The core of REINFORCE: the log-probability of each move
                # the network *actually played*, scaled by its advantage
                # (return - baseline), averaged over the game's moves.
                # Negated because torch optimizers *minimize* a loss, and
                # we want to *maximize* advantage-weighted log-probability.
                # The entropy bonus rewards keeping several moves likely,
                # so the policy keeps exploring instead of collapsing onto
                # one move per position.
                game_losses = []
                entropies = []
                for t, game_returns in zip(played, returns):
                    log_probs = torch.stack(t.log_probs)
                    game_losses.append(-((game_returns - baseline) * log_probs).mean())
                    entropies.append(torch.stack(t.entropies).mean())
                mean_entropy = torch.stack(entropies).mean()
                batch_loss = torch.stack(game_losses).mean() - entropy_coef * mean_entropy

                optimizer.zero_grad()
                batch_loss.backward()
                optimizer.step()

                epoch_loss_sum += batch_loss.detach().item()
                epoch_entropy_sum += mean_entropy.detach().item()
                epoch_return_sum += sum(r[0].item() for r in returns) / len(returns)
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

            # Raw counts disabled: games_per_epoch is fixed, so they're the
            # same curves as the *_rate ones, just scaled.
            log_scalars = {
                # "wins": wins,
                # "draws": draws,
                # "losses": losses,
                "win_rate": win_rate,
                "draw_rate": draw_rate,
                "loss_rate": loss_rate,
                # Mean entropy of the policy over the legal moves (nats) -
                # near 0 means it has collapsed onto one move per position.
                "policy_entropy": epoch_entropy_sum / max(nb_updates, 1),
                # Mean return of a game's first move, i.e. the whole
                # (discounted, shaped) game - rises as play improves.
                "mean_return": epoch_return_sum / max(nb_updates, 1),
            }

            if val_opponents:
                model.eval()
                for opp in val_opponents:
                    opp_scalars, summary = play_val_games(model, opp, experiment.device)
                    log_scalars.update(opp_scalars)
                    iprint(f"Epoch {epoch} val {summary}")

            # fdq expects both a train and a val loss to track "best"
            # checkpoints and drive early stopping. The REINFORCE policy
            # loss is NOT usable for that: winning games push it up, losing
            # games push it down, and its magnitude mostly tracks how
            # confident the policy is (see rl_training.md) - so "lowest
            # loss" would tend to pick one of the *worst* checkpoints.
            # valLoss is therefore 1 - win rate: against the select_best
            # val opponent if train.args.val is enabled (greedy, cleanest
            # signal), otherwise against the training opponent. Draws count
            # as "not won", same as losses.
            experiment.trainLoss = avg_loss
            if best_val_opponent is not None:
                experiment.valLoss = 1.0 - log_scalars[f"{best_val_opponent.log_name}/val_win_rate"]
            else:
                experiment.valLoss = 1.0 - win_rate

            experiment.on_epoch_end(log_scalars=log_scalars)

            if experiment.check_early_stop():
                break
    finally:
        close_engines(engines)
        close_engines(val_engines)
