# Training a Chess Model with Reinforcement Learning

This is a second, independent way to produce a model for Mate's ML move
integration - an alternative to the supervised pipeline in
[torch_model.md](torch_model.md), not a replacement for it. Both produce
the exact same kind of file (an ONNX model with a 16-channel board input
and two 64-way `from`/`to` square outputs), so [src/chess_ML.cpp](../src/chess_ML.cpp)
doesn't need to know or care which one produced the model you point it at.

**This document assumes no prior reinforcement learning (RL) knowledge.**
If you already know what a policy, an episode, and a policy gradient are,
skim section 1 and jump to section 2.

**Expectation-setting, up front:** this pipeline is deliberately simple and
is not tuned for strength. The point is to give you a clear, working,
readable example of RL applied to chess - not to produce a strong engine.
See section 6 for what "simple" leaves out and why.

## 1) Reinforcement learning, from scratch

Supervised learning (the other pipeline) works by imitation: show the
network millions of (position, human move) pairs, and train it to predict
the human's move. It never plays a game itself during training.

Reinforcement learning works differently: the network actually plays
games, and learns from whether it **won or lost** - nobody tells it which
individual moves were good or bad, only how the whole game turned out.
That's both RL's strength (it needs no labeled dataset - it generates its
own experience by playing) and its central difficulty (a game's outcome is
a very indirect, noisy signal about which of the dozens of moves in it
actually mattered).

A few terms, since they're used throughout the code:

- **Policy**: the thing being trained - a function from "board position" to
  "a probability for each possible move." Here, that's `ChessCNN` itself
  (the exact same network class the supervised pipeline uses - see
  [chess_cnn.py](fdq/chess_cnn.py)). "Training the policy" means adjusting
  the network's weights so it assigns higher probability to moves that
  tend to lead to wins.
- **Episode** (a.k.a. trajectory): one full game, start to finish, plus
  everything about it we recorded along the way (here: the network's own
  moves and how confident it was in each one).
- **Return**: how the episode turned out, as a number. This project uses
  about the simplest possible scheme: **+1** if the network's side won,
  **-1** if it lost, **0** for a draw - see `play_one_game()` in
  [rl_self_play.py](fdq/rl_self_play.py).
- **Exploration vs. exploitation**: if the network always played its
  current favorite move, it could never discover that some other move -
  one it currently underrates - is actually better. So during training we
  **sample** a move from the policy's probability distribution instead of
  always taking the top one; occasionally trying an "unexpected" move is
  what lets the network discover it's actually good (or confirm it's bad).
  See `select_network_move()` in [rl_self_play.py](fdq/rl_self_play.py).
- **Policy gradient / REINFORCE**: the actual learning rule. After a game,
  for every move the network made, nudge the network's weights so that
  move becomes *more* likely if the game was won, or *less* likely if it
  was lost. Concretely, for one move with log-probability `log_prob` in a
  game with return `R`, the loss contributed is:

  ```
  loss = -R * log_prob
  ```

  Two things worth pausing on:
  - The **sign**: PyTorch optimizers *minimize* a loss by walking downhill.
    We want to *maximize* `R * log_prob` (make winning moves more likely).
    Minimizing its negative is the standard trick to turn a "maximize this"
    goal into the "minimize this" shape every optimizer expects.
  - **Every move in the game gets the same `R`.** This is the simplest
    possible version of the idea (formally: "REINFORCE with a Monte Carlo
    return, no baseline, no discounting"). A move made on turn 3 of a
    50-move game that was eventually won gets exactly as much credit as
    the move that actually delivered checkmate. Real implementations often
    fix this with a "baseline" (subtract some expected/average return so
    only *better-than-expected* outcomes get reinforced) or per-move
    "advantage" estimates from a value network - deliberately skipped here
    to keep the algorithm easy to hold in your head. See section 6.

This whole recipe - self-play, sample a move, remember its log-probability,
apply the same return to every move in the game at the end - is literally
all there is to it. No search tree, no opponent model, no replay buffer.

## 2) The pipeline, file by file

| File | Role |
| --- | --- |
| [fdq/chess_encoding.py](fdq/chess_encoding.py) | Board -> tensor encoding, shared with the supervised pipeline's evaluator (see [torch_model.md](torch_model.md)'s board encoding section - identical 16-channel, canonicalized representation). Also has `legal_move_candidates()`, used only by RL. |
| [fdq/rl_self_play.py](fdq/rl_self_play.py) | The "environment" half: plays one game at a time, move by move, and returns what's needed for the update (see `GameTrajectory`). Has no fdq dependency - testable on its own. |
| [fdq/train_rl.py](fdq/train_rl.py) | The "trainer" half: opens the opponent (an engine, or the built-in random mover), runs the epoch loop, plays batches of self-play games, computes the REINFORCE loss, and steps the optimizer. This is fdq's `train.path` entry point, playing the same role `train.py` does for the supervised pipeline. |
| [fdq/rl_evaluator.py](fdq/rl_evaluator.py) | This pipeline's own `test.processor`: plays evaluation games against a fixed opponent (no learning, no exploration) and reports the win rate. Deliberately **not** `chess_evaluator.py` - see section 4.5. |
| [fdq/chess_rl_p00.yaml](fdq/chess_rl_p00.yaml) | The FDQ experiment config for this pipeline - same architecture as the supervised `chessCNN` model in [chess_cnn_p00.yaml](fdq/chess_cnn_p00.yaml), defined here under the key `chessRL` instead (so it's clear which pipeline produced a given checkpoint/export), pointed at `train_rl.py`/`rl_evaluator.py` instead of `train.py`/`chess_evaluator.py`, plus the self-play-specific settings (see section 4). |

### Why a fixed external opponent, and which one?

Self-play in the "network plays itself" sense (both sides controlled by
the same weights) is common in RL for games, but it has a bootstrapping
problem: a freshly-initialized, from-scratch network playing against an
exact copy of itself is really just two random policies playing each
other - there's no source of "reasonably sensible" chess anywhere in the
loop for either side to learn from, especially early on. Playing against
a fixed external opponent instead gives the network something real to
push against.

`engine_command` in [chess_rl_p00.yaml](fdq/chess_rl_p00.yaml) picks the
opponent, and this project supports three tiers, meant to be used as a
curriculum (start easy, move up once the network is actually beating the
current one):

1. **`"random"` (the default)** - a uniformly random legal move, generated
   directly in Python (`random_move_getter()` in
   [rl_self_play.py](fdq/rl_self_play.py)). No engine process, no install
   of any kind, on any machine. Genuinely beatable from move one, which is
   exactly what a from-scratch network needs early on: REINFORCE can only
   learn from *some* wins, and a random opponent is the surest way to get
   some.
2. **`"sunfish-uci"`** - [Sunfish](https://github.com/thomasahle/sunfish),
   a real chess engine written entirely in Python
   (`pip install sunfish`, MIT-licensed). No compiler, no system package
   manager, no root - it's a pure-Python pip package like any other,
   which is exactly what makes it usable on a training cluster where you
   can't install arbitrary system binaries. **Important measured caveat:**
   unlike Stockfish, Sunfish has no official "play deliberately weaker"
   mode. Testing it directly against a from-scratch network at various
   `engine_movetime_ms` values (1ms, 5ms, 10ms, 50ms) found it still won
   almost every game at all of them - a from-scratch network drew
   occasionally but never won in that test. It's still a reasonable
   *second* curriculum stage (once the network reliably beats "random"),
   just don't expect it to feel "easy" the way Stockfish's Skill Level 0
   does - see section 4 for what to realistically expect from training
   against it.
3. **A real Stockfish binary path** (e.g. `/usr/bin/stockfish` or wherever
   you extracted it - see section 3) - the strongest option, and the only
   one of the three with an official, human-plausible "play weaker" mode
   (`engine_uci_options: {"Skill Level": 0}`). Needs either root (a
   package manager) or at least the ability to download and execute an
   arbitrary binary, which some clusters also disallow - hence tiers 1
   and 2 above existing at all.

Whichever tier you use, it's a **training-time sparring partner only** -
never linked into Mate itself, and has nothing to do with how Mate's own
`smart move` engine (`src/chess.cpp`'s minimax) works.

## 3) Installing an opponent (tiers 2 and 3)

Only needed on whatever machine actually runs `train_rl.py`; Mate itself
never depends on either of these. Skip this section entirely if you're
using the default `"random"` opponent.

**Sunfish (tier 2, recommended if you can't install system binaries -
e.g. most shared training clusters):**

```bash
pip install sunfish
```

That's the whole install. `sunfish-uci` (the value already set for
`engine_command` when you switch to this tier) is a console script pip
just put on your `PATH`; no further setup needed.

**Stockfish (tier 3, if you have root or can execute downloaded
binaries):**

No root/sudo strictly needed either, if your environment at least allows
running an arbitrary downloaded executable - grab a ready-to-run static
binary from the
[releases page](https://github.com/official-stockfish/Stockfish/releases/latest)
(`stockfish-linux-x86-64-universal.tar.gz` for a typical x86-64 Linux
machine):

```bash
mkdir -p ~/stockfish
curl -L -o ~/stockfish/stockfish.tar.gz \
  https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-linux-x86-64-universal.tar.gz
tar xf ~/stockfish/stockfish.tar.gz -C ~/stockfish --strip-components=1
chmod +x ~/stockfish/stockfish-linux-x86-64-universal
```

Then use that path directly as `engine_command` in
[chess_rl_p00.yaml](fdq/chess_rl_p00.yaml), and set
`engine_uci_options: {"Skill Level": 0}` to actually use its weak mode.

If you do have a package manager with root:

```bash
# Arch Linux
sudo pacman -S --needed stockfish

# Ubuntu/Debian
sudo apt update && sudo apt install -y stockfish
```

Either way, find the binary's path with `which stockfish`.

## 4) Running RL training

Install the same fdq environment as the supervised pipeline
([torch_model.md](torch_model.md) section 2), plus this pipeline's own
[requirements.txt](fdq/requirements.txt) (adds `chess`, needed both for
board handling and for `chess.engine`'s UCI support once you move past
the "random" opponent).

The default config trains against `"random"` out of the box - no setup
needed. From the Mate project root (see [torch_model.md](torch_model.md)
section 2.3 for why `--config-path` must be absolute):

```bash
cd /home/marc/dev/Mate

fdq \
	--config-path "$(pwd)/torch_model/fdq" \
	--config-name chess_rl_p00 \
	mode.run_train=true mode.run_test_auto=false mode.dump_model=false
```

Each epoch prints something like:

```
Epoch 3: policy loss 0.6123 | vs random: 61 wins, 12 draws, 27 losses (61.0% / 12.0% / 27.0%)
```

**Read this as a trend across many epochs, not a single number to
optimize.** The policy loss here isn't comparable to the supervised
pipeline's cross-entropy loss (it can legitimately go up sometimes even
while the network is improving, since it depends on which games happened
to be won/lost, not just "how confident/correct was each prediction") -
the win/draw/loss rate is the more meaningful thing to watch over time.
Once that win rate against `"random"` sits comfortably above 50% for a
while, switch `engine_command` to `"sunfish-uci"` (section 3) for a
tougher second stage - expect the win rate to drop back down sharply when
you do, per section 2's measured caveat about Sunfish.

Config knobs worth knowing about (all in `train.args` in
[chess_rl_p00.yaml](fdq/chess_rl_p00.yaml)):

- `games_per_epoch` / `games_per_update`: how many self-play games make up
  one "epoch," and how many are averaged together before each gradient
  step (averaging several games' updates together reduces noise, since any
  single game's outcome is a high-variance signal about which moves in it
  were actually good).
- `max_plies_per_game`: a safety cap. A game that hits this cap without a
  natural conclusion is treated as a draw (return 0) - i.e. it contributes
  no learning signal, same as a real draw does under this simple reward
  scheme.
- `engine_command`: `"random"`, `"sunfish-uci"`, or a Stockfish binary
  path - see section 2 for the three-tier curriculum.
- `engine_uci_options`: engine-specific UCI options, e.g.
  `{"Skill Level": 0}` for Stockfish. Leave empty (`{}`) for `"random"`
  and for Sunfish (it has none).
- `engine_movetime_ms`: how long the engine is allowed to think per move.
  Keep it short - see section 2's explanation of why a beatable opponent
  matters here. Ignored for `"random"`.

## 4.5) Evaluating a trained model (`mode.run_test_auto` / `run_test_interactive`)

Running fdq's test mode against this config uses
[rl_evaluator.py](fdq/rl_evaluator.py), **not**
[chess_evaluator.py](fdq/chess_evaluator.py) (the supervised pipeline's
evaluator) - this matters, and picking the wrong one gives a real but
misleading number.

`chess_evaluator.py` measures "does the model's predicted move exactly
match what a strong human played" in a held-out position from the
supervised dataset. That's the right check for the *supervised* model - it
was trained to imitate those exact moves. It is close to meaningless for
this RL model: nothing here ever trained it to imitate humans, only to win
games against its training opponent. A move it picks can be perfectly
reasonable, even winning, without matching what some Lichess player did in
a similar-looking position - so don't be alarmed by a near-zero score
there; it isn't measuring what you think it's measuring. (If you try it
anyway: point `test.processor` back at `chess_evaluator.py`, but expect a
number close to 0, not a sign that training failed.)

`rl_evaluator.py` instead plays `nb_eval_games` full games against a fixed
opponent - using the model's single best move each time
(`play_one_game(..., greedy=True)`, not the exploratory sampling training
uses) - and reports the win/draw/loss rate. That's the question that
actually matters: does it win? Its own `engine_command`/`engine_uci_options`/
`engine_movetime_ms` under `test.args` are intentionally **separate** from
`train.args`' - evaluating against the exact opponent it trained against
mostly tells you whether it overfit to that opponent's specific patterns.
A more honest check is to move up a tier for evaluation (e.g. train
against `"random"`, evaluate against `"sunfish-uci"`) - if the win rate
against the tougher opponent is also climbing over successive checkpoints,
that's real evidence of learning, not just memorizing one opponent's
blind spots.

## 5) Exporting to ONNX and using it in Mate

Identical to the supervised pipeline's export flow
([torch_model.md](torch_model.md) section 2.5) - just point `--config-name`
at `chess_rl_p00` instead:

```bash
fdq \
	--config-path "$(pwd)/torch_model/fdq" \
	--config-name chess_rl_p00 \
	mode.run_train=false mode.run_test_auto=false mode.dump_model=true \
	data.CHESS.args.train_batch_size=1
```

Then set `model_a_path` or `model_b_path` in `~/.mate/config.json` to the
resulting file, exactly as you would for a supervised model -
`src/chess_ML.cpp` cannot tell the difference, and doesn't need to. Mate
supports two independent model slots at once (see the README's Optional ML
Support section) - useful for having this RL model and the supervised one
loaded simultaneously, e.g. to play them against each other.

## 6) What's deliberately left out (and why)

This is a "see the whole mechanism clearly" implementation, not a strong
one. Specifically missing, compared to a serious chess RL system (e.g.
AlphaZero-style approaches):

- **No Monte Carlo Tree Search (MCTS).** Real strong self-play chess
  engines search several moves ahead during both training and play, using
  the network to guide that search. Here the network's raw move
  probabilities are used directly - much simpler, much weaker.
- **No value network / no baseline.** As noted in section 1, every move in
  a game gets the same credit/blame. A value network (predicting "how
  good is this position for me") lets you compute a per-move *advantage*
  instead, which is a much less noisy learning signal - a natural next
  step if you want to extend this.
- **No replay buffer.** Every batch of games is played fresh and then
  discarded after one gradient step; nothing is reused or prioritized.
- **Promotion is simplified**, same as everywhere else in this project:
  under-promotions collapse to queen promotion (see
  `legal_move_candidates()` in [chess_encoding.py](fdq/chess_encoding.py)).

Any of these would make the model stronger, and each is a well-documented,
standard extension in the RL literature if you want to pursue it - they're
just not needed to demonstrate the core RL idea, which is the goal here.

## 7) Combining supervised and RL (a possible extension, not implemented here)

Because both pipelines train the exact same `ChessCNN` class, an obvious
next experiment - explicitly **not** built here since the from-scratch
option was chosen for this version - would be to warm-start RL from a
supervised checkpoint (`models.chessRL.trained_model_path` in
[chess_rl_p00.yaml](fdq/chess_rl_p00.yaml)) rather than training from
random weights: fine-tune an already-competent model with self-play
instead of teaching the RL loop chess from zero. This mirrors how
real-world systems commonly combine the two (pretrain by imitation, then
refine with RL) - a natural follow-up once you're comfortable with the
mechanics here.
