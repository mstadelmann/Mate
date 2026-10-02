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
  See `select_network_moves()` in [rl_self_play.py](fdq/rl_self_play.py).
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
| [fdq/chess_rl_p00_random.yaml](fdq/chess_rl_p00_random.yaml) | The FDQ experiment config for this pipeline - same architecture as the supervised `chessCNN` model in [chess_cnn_p00.yaml](fdq/chess_cnn_p00.yaml), defined here under the key `chessRL` instead (so it's clear which pipeline produced a given checkpoint/export), pointed at `train_rl.py`/`rl_evaluator.py` instead of `train.py`/`chess_evaluator.py`, plus the self-play-specific settings (see section 4). |
| [fdq/chess_rl_p01_sunfish.yaml](fdq/chess_rl_p01_sunfish.yaml), [fdq/chess_rl_p02_stockfish.yaml](fdq/chess_rl_p02_stockfish.yaml) | Training against Sunfish / Stockfish Skill Level 5, warm-started from the best supervised CNN (section 7). Trained from scratch, both learned essentially nothing - see section 8. |
| [fdq/chess_rl_p03_warmstart.yaml](fdq/chess_rl_p03_warmstart.yaml) | Like p00 (trains and evaluates against `"random"`), but warm-started from the best supervised CNN - see section 7. |

### Why a fixed external opponent, and which one?

Self-play in the "network plays itself" sense (both sides controlled by
the same weights) is common in RL for games, but it has a bootstrapping
problem: a freshly-initialized, from-scratch network playing against an
exact copy of itself is really just two random policies playing each
other - there's no source of "reasonably sensible" chess anywhere in the
loop for either side to learn from, especially early on. Playing against
a fixed external opponent instead gives the network something real to
push against.

`engine_command` in [chess_rl_p00_random.yaml](fdq/chess_rl_p00_random.yaml) picks the
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
[chess_rl_p00_random.yaml](fdq/chess_rl_p00_random.yaml), and set
`engine_uci_options: {"Skill Level": 0}` to actually use its weak mode.

If you do have a package manager with root:

```bash
# Arch Linux (AUR)
yay -S stockfish

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
	--config-name chess_rl_p00_random \
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
For the same reason, fdq's `valLoss` (which picks the "best" checkpoint
and drives early stopping) is set to `1 - win rate`, not the policy loss:
against the `train.args.val` opponent if that's enabled (`nb_games > 0` -
greedy games vs a separately configured opponent, e.g. an easier
Stockfish Skill Level than the training one, logged as
`<log_name>/val_win_rate`; with a list of val opponents, the one marked
`select_best: true`),
otherwise against the training opponent. `trainLoss` stays the policy
loss, so `best_train` checkpoints are *not* meaningful here - use
`best_val`/`"best"`.
Once that win rate against `"random"` sits comfortably above 50% for a
while, move up to a tougher opponent. In the measured runs (section 8),
Sunfish and Stockfish Skill Level 5 were both far too strong (zero
training wins in 50k games), so p02 now trains against Stockfish
Skill Level 0 with a 10ms movetime, warm-started from the supervised
model (section 7; p01-p03 all are).

Config knobs worth knowing about (all in `train.args` in
[chess_rl_p00_random.yaml](fdq/chess_rl_p00_random.yaml)):

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
  matters here. Ignored for `"random"`. This works for any UCI engine,
  but it is a weak strength knob: Stockfish's `Skill Level` already caps
  its search depth, and Sunfish stays strong even at 1ms.
- `init_weights_path`: a `.fdqm` model whose weights initialize `chessRL`
  before training (warm start), e.g. a supervised `best_val_chessCNN_e*.fdqm`.
  `null` (the default) trains from random weights. See section 7.
- `freeze_batchnorm_stats`: `true` plays and trains with the network in
  `eval()` mode, so BatchNorm's running statistics stay fixed while every
  weight still learns. Required together with `init_weights_path` - see
  section 8, finding 4.

The learning signal (`move_returns()` and the update in
[train_rl.py](fdq/train_rl.py)) has its own knobs. Their defaults in code
give back the plain REINFORCE of section 1. The configs turn them on
(section 8, recommendation 3):

- `material_reward_scale` (0.02): per-move reward per pawn of material
  won, measured from just before a network move to just before its next
  one, so the opponent's reply counts too. Hanging a queen costs 0.18 on
  that move instead of being smeared over the whole game.
- `draw_reward` (-0.2 vs random, 0 vs Stockfish/Sunfish) and
  `stalemate_when_ahead_reward` (-0.5): added to the last move instead of
  the win's +1 / loss's -1. Both push the network to actually mate.
- `gamma` (0.99): discount per network move, so a move is judged mostly
  by what happened soon after it.
- `use_baseline` (true): subtract the update batch's mean return, so a
  move is reinforced only if it did better than usual. With all-lost
  batches this stops REINFORCE from blindly pushing down every move.
- `entropy_coef` (0.01): entropy bonus that keeps the policy exploring.
  Logged as `policy_entropy`, next to `mean_return`.

An opponent engine that returns no move (Sunfish resigns, or plays
`bestmove (none)`) ends that game as a win for the network, with
termination `resignation`. Before, this crashed the run.

### Why validation games were drawn (`val_draw_<reason>_rate`)

For each validation opponent, `play_val_games()` in
[rl_self_play.py](fdq/rl_self_play.py) logs one extra metric per entry of
`DRAW_REASONS`: `<log_name>/val_draw_<reason>_rate`, the fraction of *all*
that opponent's games that were drawn for that reason. The reasons add up to
`val_draw_rate`. The console summary shows the same breakdown as counts
(e.g. `(draws - fivefold_repetition: 6, max_plies: 2)`). Because the list is
fixed, every reason is logged every epoch, even at zero, so the wandb curves
have no gaps. Only validation is broken down like this. The training games
and the test games (`rl_evaluator.py`) still report a single draw count.

The reason is `board.outcome(claim_draw=True).termination` from python-chess,
lower-cased, or `max_plies` if python-chess reports no outcome at all:

| Reason | Meaning | Ends the game by itself? |
| --- | --- | --- |
| `max_plies` | Reached `max_plies_per_game` with no result and no claimable draw | Yes, but it's our own cutoff, not a chess rule |
| `stalemate` | The side to move has no legal move and is not in check | Yes |
| `insufficient_material` | Neither side can possibly checkmate (e.g. K vs K, K+B vs K, K+N vs K) | Yes |
| `threefold_repetition` | The same position has occurred 3 times | No, only claimable |
| `fifty_moves` | 50 moves per side (100 plies) with no capture or pawn move | No, only claimable |
| `fivefold_repetition` | The same position has occurred 5 times | Yes |
| `seventyfive_moves` | 75 moves per side (150 plies) with no capture or pawn move | Yes |

**One subtlety about the claimable draws.** The game loop stops on
`board.is_game_over()`, which does *not* claim draws. So a game keeps going
after a threefold repetition or the 50-move point. It only stops at
checkmate, an automatic draw, or `max_plies_per_game`. Draws are claimed
once, at the end. What that means for the curves:

- `threefold_repetition` and `fifty_moves` only show up in games that ran to
  `max_plies_per_game` and could claim the draw in their **final**
  position. A repetition earlier in the game that no longer applies at the
  end is counted as `max_plies`.
- `fivefold_repetition` is the usual label when the network shuffles pieces
  back and forth instead of making progress. That is a typical failure of a
  weak policy that wins material but can't deliver mate. A high rate against
  `"random"` is the main sign to look for. Section 8, recommendation 3 (a
  small draw penalty) targets exactly this.
- `seventyfive_moves` needs 150 plies without a capture or pawn move, so it
  is rare with the default 200-ply cap.
- `stalemate` against a weak opponent usually means the network had a won
  position and blundered it away at the end.

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
(`play_greedy_games()`, not the exploratory sampling training
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
([torch_model.md](torch_model.md) section 2.5): `mode.dump_model: true` is
already set in `chess_rl_p00_random.yaml`, driven by its `model_dump:`
section (`model_name: chessRL`, `input_source: CHESS` - see that section
for why the RL configs point at their own `CHESS` entry directly instead
of a separate `CHESS_EXPORT` one), so a normal training run already
exports `chessRL_torchscript.onnx` at the end. To export without
(re)training, e.g. from an existing checkpoint:

```bash
fdq \
	--config-path "$(pwd)/torch_model/fdq" \
	--config-name chess_rl_p00_random \
	mode.run_train=false mode.run_test_auto=false
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
- **No value network.** There is only a crude baseline (the batch's mean
  return, `use_baseline`) and material-based per-move rewards (section 4).
  A value network (predicting "how good is this position for me") would
  give a per-position baseline and a much less noisy *advantage* - a
  natural next step if you want to extend this.
- **No replay buffer.** Every batch of games is played fresh and then
  discarded after one gradient step; nothing is reused or prioritized.
- **Promotion is simplified**, same as everywhere else in this project:
  under-promotions collapse to queen promotion (see
  `legal_move_candidates()` in [chess_encoding.py](fdq/chess_encoding.py)).

Any of these would make the model stronger, and each is a well-documented,
standard extension in the RL literature if you want to pursue it - they're
just not needed to demonstrate the core RL idea, which is the goal here.

## 7) Combining supervised and RL (warm start)

Because both pipelines train the exact same `ChessCNN` class, RL can start
from a supervised checkpoint instead of from random weights: it then
fine-tunes a model that already plays sensible chess, instead of having to
learn chess from zero. This mirrors how real-world systems commonly
combine the two (AlphaGo: pretrain by imitation, then refine with RL).
After the from-scratch results in section 8, this is the recommended way
to use this pipeline.

[chess_rl_p01_sunfish.yaml](fdq/chess_rl_p01_sunfish.yaml) and
[chess_rl_p02_stockfish.yaml](fdq/chess_rl_p02_stockfish.yaml) do this,
and so does [chess_rl_p03_warmstart.yaml](fdq/chess_rl_p03_warmstart.yaml),
the warm-started counterpart of p00 (trained and evaluated against
`"random"`). [chess_rl_p00_random.yaml](fdq/chess_rl_p00_random.yaml)
itself still trains from scratch (`init_weights_path: null`):

```yaml
train:
  args:
    init_weights_path: "~/data_ML/results/Chess/chess_cnn_p01/20260923_17_37_49__fervent_keller/best_val_chessCNN_e148.fdqm"
    freeze_batchnorm_stats: true
```

How it works (`load_init_weights()` in [train_rl.py](fdq/train_rl.py)):
fdq instantiates `chessRL` with random weights and builds its optimizer as
usual. Then, before the first game, `train_rl.py` loads the `.fdqm` file
(a whole pickled model) and copies its weights into `chessRL` with
`load_state_dict(strict=True)`. The copy happens in place, so the
optimizer built by fdq stays valid, and a mismatched architecture fails
loudly instead of loading partially. When a run is resumed
(`mode.resume_chpt_path`), the warm start is skipped, because the
checkpoint already holds the RL-trained weights.

Two fdq mechanisms were deliberately **not** used:

- `models.chessRL.trained_model_path`: fdq also uses it to pick the
  model for testing and ONNX export, so it would evaluate and export the
  supervised starting point instead of the RL result.
- `mode.resume_chpt_path`: it resumes a full fdq checkpoint of the *same*
  experiment (epoch counter, optimizer state, ...), not "start a new run
  from these weights".

**Why this checkpoint:** every supervised `best_val` model under
`~/data_ML/results/Chess/chess_cnn_p0*` was played greedily against
random (100 games) and Stockfish Skill Level 0 (50 games, 10ms/move):

| Run | Test move accuracy | vs random W/D/L | vs Stockfish Skill 0 W/D/L |
| --- | --- | --- | --- |
| chess_cnn_p00 (2k games, 5 runs) | 0.16-0.23 | 23-41 / 59-77 / 0 | 0-2 / 0-2 / 47-50 |
| chess_cnn_p01 eloquent_kilby e99 | 0.283 | 65 / 35 / 0 | 0 / 2 / 48 |
| chess_cnn_p01 amazing_einstein e1 | 0.285 | 43 / 57 / 0 | 1 / 2 / 47 |
| **chess_cnn_p01 fervent_keller e148** | **0.287** | **68 / 32 / 0** | 0 / 4 / 46 |
| chess_cnn_p01 gifted_meitner e167 | 0.278 | 64 / 36 / 0 | 1 / 2 / 47 |

`fervent_keller` is best on both the supervised metric and actual play.
Note that even the best supervised model loses almost every game against
Stockfish Skill Level 0. So a warm start alone likely won't give p01
(Sunfish) and p02 (Stockfish Skill 5) enough training wins - see
section 8, recommendation 2.

p01-p03 also lower the learning rate to `1e-4` (p00: `3e-4`), to
fine-tune rather than overwrite the supervised knowledge. p03 also plays
100 test games instead of 50, for a less noisy final score. (Validation
is the same for every RL config: 100 games vs random and 20 each vs
Stockfish Skill Level 0 and Sunfish - the supervised configs play 20 vs
random too - with the random win rate
picking the "best" checkpoint - see `train.args.val` in
chess_rl_p00_random.yaml.) Since p03 starts from a model that
already wins ~68% vs random, it's the cleanest measure of what RL adds on
top of the supervised model.

## 8) Findings from the first training runs (September 2026)

The three from-scratch configs were trained for 500 epochs (100 games
each, i.e. 50k games per run). Results of the last runs
(`~/data_ML/results/Chess/chess_rl_p0*`, logs in
`fdq/logs/train_all_20260924_064256/`), all *tested* against `"random"`
(50 greedy games):

| Config | Training games (epochs 50-450) | Val vs random (20 games) | Test vs random |
| --- | --- | --- | --- |
| p00 random | 53-87% won, 0-3% lost | 50-95% won | **90% won**, 10% drawn |
| p01 sunfish | **0 wins**, 66-99% lost | 5-45% won | 28% won, 70% drawn |
| p02 stockfish (Skill 5) | **0 wins, 100% lost, every epoch** | 0-5% won | **2% won**, 92% drawn |

p00 learned to beat a random mover and nothing more. p01 and p02 learned
essentially nothing: the p02 model can't even checkmate a random mover,
and its "best" checkpoint (by validation) is from epoch 7, i.e. almost
untrained.

### Why

1. **Training opponent too strong → no learning signal.** In p01 and p02
   the network never won a single training game. Every return is -1, so
   REINFORCE only ever pushes down whichever move was sampled. That tells
   the network what to avoid, but never what to prefer - this is the whole
   failure of p01/p02. Sunfish has no weak mode at all; Stockfish Skill 5
   is far too strong for a from-scratch network.
2. **Random initialization.** Learning chess from zero with a single
   end-of-game ±1 per game needs vastly more than 50k games. The
   supervised CNN already wins ~65% vs random before any RL.
3. **Sparse reward.** Every move of a game gets the same ±1, a draw
   (including hitting `max_plies_per_game`) is 0, and there is no baseline
   and no per-move reward.
4. **BatchNorm in `train()` mode during play.** Each batched forward pass
   during self-play updates BatchNorm's running mean/var with statistics
   of whatever boards happen to be waiting - a batch that shrinks down to
   1 as games end. Validation and testing then use those running
   statistics in `eval()` mode, i.e. a slightly different network than the
   one that played. Measured on the warm-start checkpoint: 100 sampled
   games in `train()` mode, **without any optimizer step**, drop it from 68
   to 22 wins out of 100 vs random; in `eval()` mode it stays at 68. Fixed
   by `freeze_batchnorm_stats: true` (on in the warm-started p01-p03,
   still off in the from-scratch p00).
5. **Factorized move scoring.** A move's score is `from_logit + to_logit`
   ([rl_self_play.py](fdq/rl_self_play.py), `select_network_moves()`), so
   the network cannot prefer a specific from/to *pair* - it ranks
   from-squares and to-squares independently. That caps its strength
   regardless of the training method.
6. **Noisy checkpoint selection.** 20 validation games are too few, and
   `test_model: "best"` then picks whichever epoch got lucky.

### What to change, by expected impact

1. **Warm start from the best supervised model.** ✅ Implemented, see
   section 7 (in p01-p03), together with the BatchNorm
   fix (finding 4), without which the warm start is mostly lost.
2. **Beatable opponents, as a curriculum.** random → Stockfish Skill 0
   with a short movetime (`engine_uci_options: {"Skill Level": 0}`,
   `engine_movetime_ms: 10`) → Skill 1, 2, 3, ... Move up only once the
   training win rate is above ~30-50%. Drop Sunfish from the curriculum.
   Self-play against past copies of the network is an alternative that
   keeps the opponent roughly as strong as the network.
3. **A better learning signal.** ✅ Implemented (except the Stockfish
   evaluation reward), see the learning-signal knobs in section 4.
   Subtract a baseline (running mean of
   returns, or a value head) to get an advantage; add per-move rewards
   (material change, or better, the change in Stockfish's evaluation after
   each move - by far the biggest credit-assignment improvement); add a
   small entropy bonus; against random, give draws a small penalty (e.g.
   -0.2) to push the network to actually finish games.
4. **BatchNorm for from-scratch runs as well.** Either use
   `freeze_batchnorm_stats` there too (and recompute log-probs in one
   batched forward pass before the backward pass), or replace BatchNorm
   with GroupNorm/LayerNorm.
5. **A joint move head.** A 64×64 (4096) from-to output, or AlphaZero's
   73×64. This changes the ONNX output contract and
   [src/chess_ML.cpp](../src/chess_ML.cpp).
6. **Proper evaluation.** (Partly done: 100 validation games vs random,
   the opponent that picks the best checkpoint.) 100+ validation games, testing against
   Stockfish Skill 0-3 instead of only random, possibly `test_model: last`.

A pragmatic alternative to most of the above: train the network
*supervised* on Stockfish's best moves (Stockfish labels millions of
positions). That is much cheaper and more reliable than REINFORCE, and is
how most decent small chess networks are made - RL is then a fine-tuning
step on top.

## 9) Second training batch: analysis and fixes (2026-10-01 21:46 UTC)

Analysis of the `train_all.sh` batch of 2026-09-29 to 2026-10-01
(wandb project `stmd/ChessMate`, runs `0tn7lug2` to `7b0mymjr`; results
under `~/data_ML/results/Chess/`, logs in
`fdq/logs/train_all_20260929_210523/`), and the fixes made as a result.
All rates are greedy validation games, averaged over the last quarter of
each run.

### Results

| Run | vs random W | vs Stockfish Skill 0 W / L | vs Sunfish W / L | Notes |
| --- | --- | --- | --- | --- |
| fc_p00 | 0.22 | 0 / 1.00 | 0 / 0.82 | 40-60% of games vs random hit `max_plies` |
| fc_p01 | 0.31 | 0 / 1.00 | 0 / 0.87 | |
| cnn_p00 | 0.39 | 0 / 1.00 | 0 / 0.81 | up to 75% of games vs random end in stalemate |
| cnn_p01 | 0.65 | 0.005 / 0.97 | 0.007 / 0.79 | test move accuracy 0.27 |
| rl_p00_random | 0.77 | 0 / 1.00 | 0 / 0.90 | the only run with a clear upward trend |
| rl_p01_warmstart | 0.82 | 0 / 1.00 | 0 / 0.92 | |
| rl_p02_stockfish | 0.58 (falling from 0.66) | 0 / 0.96 | 0 / 0.90 | lost **every** training game (Skill 5), all 500 epochs |
| rl_p03_sunfish | 0.66 | 0 / 0.97 | 0 / 0.90 | lost ~100% of training games, crashed at epoch 95 |

No model beats a real engine. RL learns to beat a random mover and
nothing more, and RL against a real engine makes the model *worse*.

### Findings

1. **No learning signal against Stockfish/Sunfish.** `rl_p02` had
   `win_rate = draw_rate = 0` in every epoch. With rewards of only
   +1/0/-1 and no baseline, every update pushes down whichever moves were
   sampled, so the win rate against random drifts down. Its training
   opponent was Skill Level 5, although the model already loses 96% vs
   Skill 0. The policy was also nearly deterministic: the mean
   log-probability of the played moves was about -1.0, against about -3.4
   for uniform play over the legal moves.
2. **Validation loss was training loss.** `chess_preparator.py` trained on
   the full training file and drew the validation positions from that
   same file. That's why `val_loss < train_loss` in every supervised run,
   and why `best_val` picked the most memorized checkpoint - the one the
   RL warm start then used. cnn_p00 had the *lowest* val loss (0.0015 vs
   0.009 for cnn_p01) but played worse (0.39 vs 0.65 vs random). A
   position-level split would leak too, since consecutive positions of
   one game are nearly identical.
3. **The supervised models never learned to mate.** Players above 1800 Elo
   resign long before checkmate, so the human games contain almost no
   mating sequences. The models win material and then stalemate or
   repeat moves - stalemate and fivefold repetition are most of the draws
   vs random.
4. **Model and input are weak.** 0.27M parameters, 4 conv layers, the
   factorized `from_logit + to_logit` move score (section 8, finding 5),
   and no move history (repetitions are invisible), en-passant square or
   50-move counter in the input.
5. **Validation too noisy for checkpoint selection.** 20 games give a
   standard error of about ±0.11; the val curves swing between 0.1 and
   0.65 epoch to epoch, so `select_best` mostly picks a lucky epoch.
6. **Crash on a resigning engine.** Sunfish's `engine.play(...).move` can be
   `None` (resignation, or `bestmove (none)`); `board.push(None)` raised
   `AttributeError` and ended `rl_p03_sunfish` after 4 hours.

### Fixes made

| Problem | Fix | Where |
| --- | --- | --- |
| Validation leaked into training (finding 2) | Hold out `val_ratio` of whole *games*, excluded from training, fixed seed (`val_split_seed`, default 0). New datasets store a per-position `game_array`; older `.chessarray` files get game starts detected by matching the initial position. 2k dataset: 1710 / 190 games, 106529 / 12050 positions | [chess_preparator.py](fdq/chess_preparator.py), [generate_chess_tensor.py](data_preparation/generate_chess_tensor.py) |
| Engine returning no move (finding 6) | A `None` move or resignation ends the game as a win for the network, termination `resignation` | [rl_self_play.py](fdq/rl_self_play.py) |
| Noisy checkpoint selection (finding 5) | 100 validation games vs random (the `select_best` opponent) instead of 20 | [chess_rl_p00_random.yaml](fdq/chess_rl_p00_random.yaml) |
| No learning signal (finding 1) | Per-move material reward, discounted returns, batch-mean baseline, entropy bonus, draw and stalemate-when-ahead penalties - see the learning-signal knobs in section 4 | [train_rl.py](fdq/train_rl.py) (`move_returns()`), [chess_rl_p00_random.yaml](fdq/chess_rl_p00_random.yaml) |
| Training opponent too strong (finding 1) | p02 trains vs Stockfish Skill 0, 10ms per move (was Skill 5); p02/p03 use `draw_reward: 0` | [chess_rl_p02_stockfish.yaml](fdq/chess_rl_p02_stockfish.yaml), [chess_rl_p03_sunfish.yaml](fdq/chess_rl_p03_sunfish.yaml) |

New wandb curves for the RL runs: `policy_entropy` (nats; near 0 means
the policy collapsed onto one move per position) and `mean_return` (mean
shaped return of a whole game).

Verified with a 2-epoch `chess_rl_p02_stockfish` run (16 games per epoch):
trains without errors, but the network still lost all 16 training games
vs Skill 0 - with the material reward and baseline those games now give
a learning signal, whether that is enough shows only in a full run.

### Before the next `train_all.sh` run

- **Retrain the supervised models first.** Every existing `best_val`
  checkpoint was selected on the leaked validation set, including
  `chess_cnn_p01/20260930_03_20_44__hopeful_babbage/best_val_chessCNN_e791`,
  which p01-p03 warm-start from. After retraining `chess_cnn_p01`, update
  `init_weights_path` in
  [chess_rl_p01_warmstart.yaml](fdq/chess_rl_p01_warmstart.yaml) by hand -
  `train_all.sh` runs everything in one go, so the RL runs would otherwise
  still start from the old checkpoint.
- **Supervised validation loss will look worse.** It is now an honest
  held-out number, not a regression; training also uses 10% fewer
  positions.
- **RL `train_loss` and `mean_return` are not comparable to earlier runs** -
  the reward changed.

### Still open, by expected impact

1. **Supervised training on Stockfish labels** (best move and evaluation)
   instead of human moves - teaches tactics and mating, which the human
   games don't contain. Plus a value head, and a 1-2 ply search at
   inference.
2. **Stockfish evaluation change as the per-move reward** instead of
   material.
3. **Architecture:** 6-10 residual blocks of 64-128 channels, a joint move
   head (section 8, recommendation 5), and the last few positions in the
   input.
4. **Mixed opponents** (random plus Stockfish Skill 0) until the training
   loss rate against Stockfish drops below ~90%.
