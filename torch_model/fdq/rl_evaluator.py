"""Evaluation for the RL pipeline: play a batch of games against a fixed
opponent (no learning, no exploration - see play_greedy_games())
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

from typing import Tuple

from fdq.ui_functions import getIntInput

from rl_self_play import close_engines, make_opponents, play_greedy_games


def _play_eval_games(
    experiment,
    model,
    opponent_move_getters,
    nb_eval_games: int,
    max_plies_per_game: int,
    opponent_label: str,
) -> dict:
    """Play `nb_eval_games` against the opponent(s), all at once (see
    play_games() in rl_self_play.py), and return/print the win/draw/loss
    rates. Colors alternate, same reasoning as training: the model should
    be evaluated on both, not just whichever it happens to play better."""
    print(f"Playing {nb_eval_games} games vs {opponent_label}...")
    wins, draws, losses = play_greedy_games(
        model=model,
        opponent_move_getters=opponent_move_getters,
        nb_games=nb_eval_games,
        device=experiment.device,
        max_plies=max_plies_per_game,
    )

    win_rate = wins / nb_eval_games
    draw_rate = draws / nb_eval_games
    loss_rate = losses / nb_eval_games
    print(
        f"\nFinal result vs {opponent_label}: {wins} wins, {draws} draws, {losses} losses "
        f"({win_rate:.1%} / {draw_rate:.1%} / {loss_rate:.1%})"
    )

    return {"win_rate": win_rate, "draw_rate": draw_rate, "loss_rate": loss_rate}


def _select_opponent_interactively() -> Tuple[str, dict, int, int]:
    """Prompt for an opponent (random / sunfish-uci / a Stockfish binary)
    and a number of games to play. Returns
    (engine_command, engine_uci_options, engine_movetime_ms, nb_eval_games).
    """
    choice = getIntInput(
        "\nSelect opponent:\n"
        "1: random (uniformly random legal moves)\n"
        "2: sunfish-uci (requires `pip install sunfish`)\n"
        "3: a Stockfish binary (requires a local install - see"
        " torch_model/rl_training.md section 3)\n",
        [1, 3],
    )

    engine_uci_options: dict = {}
    engine_movetime_ms = 50

    if choice == 1:
        engine_command = "random"
    elif choice == 2:
        engine_command = "sunfish-uci"
    else:
        default_path = "/usr/bin/stockfish"
        typed_path = input(f"Stockfish binary path [{default_path}]: ").strip()
        engine_command = typed_path or default_path
        skill_level = getIntInput(
            "Skill Level (0 = weakest, 20 = full strength)\n", [0, 20]
        )
        engine_uci_options = {"Skill Level": skill_level}

    nb_eval_games = getIntInput("How many games do you want to play?\n", [1, 10000])

    return engine_command, engine_uci_options, engine_movetime_ms, nb_eval_games


def fdq_test(experiment) -> dict:
    """Play evaluation games against a fixed opponent and return the
    win/draw/loss rates (also printed as counts along the way).

    In automatic mode (`mode.run_test_auto` or unittest), the opponent and
    game count come from `test.args` in the experiment config. Otherwise,
    prompts interactively for both - see `_select_opponent_interactively`.
    """
    model_name = next(iter(experiment.models))
    model = experiment.models[model_name]
    model.eval()

    args = experiment.cfg.test.args
    max_plies_per_game: int = args.get("max_plies_per_game", 200)

    if experiment.mode.op_mode.unittest or experiment.cfg.mode.run_test_auto:
        # Deliberately a *separate* set of engine_* settings from
        # train.args, not shared with it: evaluating against the exact
        # same opponent the model trained against only tells you "did it
        # learn to beat this one opponent's specific patterns," which can
        # look like progress even when it's really just overfitting to one
        # opponent's quirks. Pointing this at a tougher or just different
        # opponent (e.g. "sunfish-uci" if you trained against "random") is
        # a more honest check - see torch_model/rl_training.md.
        nb_eval_games: int = args.get("nb_eval_games", 50)
        engine_command: str = args.get("engine_command", "random")
        engine_uci_options: dict = dict(args.get("engine_uci_options", {}) or {})
        engine_movetime_ms: int = args.get("engine_movetime_ms", 50)
    else:
        engine_command, engine_uci_options, engine_movetime_ms, nb_eval_games = (
            _select_opponent_interactively()
        )

    opponent_move_getters, engines = make_opponents(
        engine_command, engine_uci_options, engine_movetime_ms, args.get("nb_engines", None)
    )

    try:
        return _play_eval_games(
            experiment, model, opponent_move_getters, nb_eval_games, max_plies_per_game, engine_command
        )
    finally:
        close_engines(engines)
