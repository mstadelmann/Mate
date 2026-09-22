"""Evaluation for the RL pipeline: play a batch of games against a fixed
opponent (no learning, no exploration - see play_one_game(..., greedy=True))
and report the win rate.

This deliberately does NOT reuse chess_evaluator.py's "does the model's move
match what a human played in this dataset position" accuracy - that's the
right check for the supervised pipeline (it was trained to imitate those
exact moves), but meaningless here: the RL model was never trained to
imitate anyone, so there's no reason its moves should match a database of
human games, even if it plays well. See torch_model/rl_training.md section
on evaluation for the full explanation. The only question that actually
means something for an RL-trained policy is: does it win games?
"""

import chess.engine
import torch

from rl_self_play import make_engine_move_getter, play_one_game, random_move_getter


def fdq_test(experiment) -> float:
    """Play `nb_eval_games` evaluation games against a fixed opponent and
    return the win rate (0.0-1.0), also printed as win/draw/loss counts.
    """
    model_name = next(iter(experiment.models))
    model = experiment.models[model_name]
    model.eval()

    args = experiment.cfg.test.args
    nb_eval_games: int = args.get("nb_eval_games", 50)
    max_plies_per_game: int = args.get("max_plies_per_game", 200)
    engine_command: str = args.get("engine_command", "random")
    engine_uci_options: dict = dict(args.get("engine_uci_options", {}) or {})
    engine_movetime_ms: int = args.get("engine_movetime_ms", 50)

    # Deliberately a *separate* set of engine_* settings from train.args,
    # not shared with it: evaluating against the exact same opponent the
    # model trained against only tells you "did it learn to beat this one
    # opponent's specific patterns," which can look like progress even when
    # it's really just overfitting to one opponent's quirks. Pointing this
    # at a tougher or just different opponent (e.g. "sunfish-uci" if you
    # trained against "random") is a more honest check - see
    # torch_model/rl_training.md.
    engine = None
    if engine_command == "random":
        opponent_move_getter = random_move_getter
        print("Evaluation opponent: uniformly random legal moves.")
    else:
        print(f"Evaluation opponent: '{engine_command}' (uci options: {engine_uci_options or 'none'})")
        engine = chess.engine.SimpleEngine.popen_uci(engine_command)
        if engine_uci_options:
            engine.configure(engine_uci_options)
        opponent_move_getter = make_engine_move_getter(engine, engine_movetime_ms / 1000.0)

    try:
        wins = losses = draws = 0
        with torch.no_grad():
            for i in range(nb_eval_games):
                # Alternate colors, same reasoning as training: the model
                # should be evaluated on both, not just whichever it
                # happens to play better.
                network_plays_white = i % 2 == 0
                trajectory = play_one_game(
                    model=model,
                    opponent_move_getter=opponent_move_getter,
                    network_plays_white=network_plays_white,
                    device=experiment.device,
                    max_plies=max_plies_per_game,
                    greedy=True,
                )

                if trajectory.result == "win":
                    wins += 1
                elif trajectory.result == "loss":
                    losses += 1
                else:
                    draws += 1

                print(f"game {i + 1}/{nb_eval_games}: {wins} wins, {draws} draws, {losses} losses so far")
    finally:
        if engine is not None:
            engine.quit()

    win_rate = wins / nb_eval_games
    draw_rate = draws / nb_eval_games
    loss_rate = losses / nb_eval_games
    print(
        f"\nFinal result vs {engine_command}: {wins} wins, {draws} draws, {losses} losses "
        f"({win_rate:.1%} / {draw_rate:.1%} / {loss_rate:.1%})"
    )

    return { "win_rate": win_rate, "draw_rate": draw_rate, "loss_rate": loss_rate }
