# AI Model for Mate

## Overview

The model plays **both colors**. Positions are always encoded from the
perspective of the side to move (canonicalized), so a single network is
trained on every ply of every game instead of only ever seeing one color.
The model predicts a move as two independent square classifications - a
"from-square" and a "to-square" - rather than regressing a single lossy
delta mask, which lets a principled ranked list of move candidates be
built at inference time instead of an arbitrary heuristic pairing.

**Two independent ways to produce a model:** the ONNX contract consumed by
[src/chess_ML.cpp](../src/chess_ML.cpp) - a 16-channel canonicalized board
in, two 64-way policy heads (`from_logits`, `to_logits`) out - doesn't care
how the model was produced. This document covers **supervised learning**
on real human games (below). [rl_training.md](rl_training.md) covers the
second option, **reinforcement learning** via self-play against Stockfish
- a from-scratch-friendly, no-dataset-needed alternative that trains the
exact same [chess_cnn.py](fdq/chess_cnn.py) network class through a
different training loop. Point `model_a_path` and/or `model_b_path` at
whichever export(s) you want Mate to use - it supports two independent
models at once (e.g. this pipeline's export as one, rl_training.md's as
the other), useful for having them play each other, or for specializing
one per game phase; the C++ side can't tell how either was produced. A
value head could be added later as a third ONNX output without breaking
this contract, since the C++ side only reads the first two named outputs.

## 1) Data Preparation

*(This section covers the supervised pipeline only - see
[rl_training.md](rl_training.md) if you want to train via self-play
instead, which needs no dataset at all.)*

**Source:** [Lichess/standard-chess-games](https://huggingface.co/datasets/Lichess/standard-chess-games)
on Hugging Face (CC0 license) - the public Lichess game database, streamed
directly (never downloaded in full; the dataset has billions of rows).

**Script:** [torch_model/data_preparation/generate_chess_tensor.py](data_preparation/generate_chess_tensor.py)

**Pipeline:** stream games matching an Elo/termination filter -> split by
game into train/test -> parse each game's movetext with `python-chess` ->
for every ply (both colors), encode the canonicalized board plus the
played move's (from, to) squares -> save two pickles (train/test).

**Packages:** `chess`, `datasets`, `numpy`, `PyYAML` (see
[torch_model/data_preparation/requirements.txt](data_preparation/requirements.txt)).

### Board encoding

Each position is a `(16, 8, 8)` tensor, oriented as if the side to move
were White (a 180-degree rotation of both rank and file is applied when
Black is to move):

- Channels 0-5: the mover's own P, R, N, B, Q, K (binary presence).
- Channels 6-11: the opponent's P, R, N, B, Q, K (binary presence).
- Channels 12-13: the mover's own kingside / queenside castling rights
  (constant-value planes).
- Channels 14-15: the opponent's kingside / queenside castling rights.

Move labels are the played move's `from_square`/`to_square`, converted to
a flat index in `[0, 63]` in the same canonicalized (row, col) frame as
the board tensor - see `square_to_canonical_index()` in the script.
Promotion is not modeled separately; the engine always defaults to queen
promotion for ML moves, matching `chess::applyMove`'s own default.

### Configuration

- **Config file:** YAML at [torch_model/data_preparation/chess_tensor_config.yaml](data_preparation/chess_tensor_config.yaml) (JSON also supported).
- **Fields:**
	- **`hf_dataset_name`:** Hugging Face dataset id to stream from.
	- **`number_of_games`:** Number of games to sample after filtering.
	- **`min_elo`:** Minimum Elo required for both players.
	- **`allowed_terminations`:** Game-ending reasons to keep (e.g. `["Normal"]`).
	- **`max_plies_per_game`:** Cap plies kept per game; `null` to keep full games.
	- **`test_ratio`:** Fraction of sampled games (not positions) held out for testing.
	- **`output_dir`:** Where the generated `.chessarray` pickle files are written.
	- **`EXPORT_PICKLE`:** If true, writes the pickle files.
	- **`debug`:** Verbose logging and sample board prints.

**Run**

```bash
pip install -r torch_model/data_preparation/requirements.txt
cd torch_model/data_preparation
python3 generate_chess_tensor.py --config chess_tensor_config.yaml
```

This writes `<dataset>_nbGames{N}_minElo{E}_train.chessarray` and
`..._test.chessarray` under `output_dir`.

**Hugging Face authentication (optional)**

Without a token, `huggingface_hub` prints:

```
Warning: You are sending unauthenticated requests to the HF Hub. Please set a HF_TOKEN to enable higher rate limits and faster downloads.
```

Streaming still works unauthenticated - this only raises the rate limit /
download speed. To silence it, put a Hugging Face access token
(https://huggingface.co/settings/tokens) in the `HF_TOKEN` environment
variable, either for the one run:

```bash
export HF_TOKEN=hf_xxxxxxxxxxxxxxxxxxxx
```

or persisted across shells via `~/.bashrc` / `~/.zshrc`, or once via
`huggingface-cli login` (stores it under `~/.cache/huggingface/token`, no
env var needed afterward). Don't put the token in
`chess_tensor_config.yaml` or commit it anywhere.

**Notes**

- `number_of_games` in the shipped config is intentionally small (a few
  thousand) so the pipeline is fast to validate end-to-end without a GPU;
  raise it substantially for an actual training run on a GPU machine.
- Splitting is done by game, not by position, so no single game's
  positions leak between the train and test sets.

## 2) Training with FDQ

This project uses [FDQ (Fonduecaquelon)](https://github.com/mstadelmann/fonduecaquelon)
v0.1.23 to manage the training loop, data loading and model checkpoints.

```bash
pip install -r torch_model/fdq/requirements.txt
```

### 2.1 Configuration

- **FDQ experiment config:** [torch_model/fdq/chess_cnn_p00.yaml](fdq/chess_cnn_p00.yaml)
	- **`globals.project`**: logical name of the experiment ("Chess").
	- **`mode`**: what to run (train / test / dump). For training set, e.g.:

		```yaml
		mode:
			run_train: true
			run_test_auto: false
			run_test_interactive: false
			dump_model: false
			run_inference: false
		```

	- **`models.chessCNN`**: the network in [torch_model/fdq/chess_cnn.py](fdq/chess_cnn.py):
		- `nb_in_channels`: 16 (see the board encoding above).
		- `conv_channels`: widths of the 3x3 conv blocks (e.g. `[64, 64, 128, 128]`).
		- The backbone feeds two 1x1-conv heads producing `from_logits`/`to_logits`, each `(N, 64)`.

	- **`data.CHESS`**:
		- `processor`: [torch_model/fdq/chess_preparator.py](fdq/chess_preparator.py) (builds the data loaders from `.chessarray` files).
		- `args.base_path`: root directory for the generated `.chessarray` files.
		- `train_set` / `test_set`: filenames of the training and test `.chessarray` tensors.

	- **`losses`**: `ce_from` and `ce_to`, both `torch.nn.CrossEntropyLoss`, summed during training.
	- **`train.path`**: training script used by FDQ: [torch_model/fdq/train.py](fdq/train.py).
	- **`store.results_path`**: base folder where FDQ stores results, logs and checkpoints.

Make sure all paths in [torch_model/fdq/chess_cnn_p00.yaml](fdq/chess_cnn_p00.yaml) match your local folders (especially `base_path` and the `.chessarray` filenames produced by step 1).

Sibling configs, same training pipeline: [chess_cnn_p01.yaml](fdq/chess_cnn_p01.yaml)
trains the same CNN on the larger `nbGames10000` dataset;
[chess_fc_p00.yaml](fdq/chess_fc_p00.yaml) swaps the architecture for a
plain fully-connected network - see section 2.7.

### 2.2 Training loop (fdq_train)

The training procedure is implemented in [torch_model/fdq/train.py](fdq/train.py) as required by FDQ:

```python
def fdq_train(experiment: fdqExperiment) -> None:
		...
```

Inside `fdq_train`:

- `experiment.data["CHESS"]` provides `train_data_loader` and `val_data_loader` built by `chess_preparator.py`.
- `experiment.models["chessCNN"]` is the instantiated PyTorch model, returning `(from_logits, to_logits)`.
- Per batch: run the model, compute `ce_from(from_logits, from_label) + ce_to(to_logits, to_label)`, backward, `experiment.update_gradients(...)`.
- After each epoch, compute average train/val loss and call `experiment.on_epoch_end()` for logging, checkpointing and early stopping.

### 2.3 Running a local training

From the Mate project root:

```bash
cd /home/marc/dev/Mate

fdq \
	--config-path "$(pwd)/torch_model/fdq" \
	--config-name chess_cnn_p00 \
	mode.run_train=true mode.run_test_auto=false mode.dump_model=false
```

`--config-path` must be an **absolute** path here - `fdq`'s Hydra entry
point declares no `config_path` of its own, so a relative one resolves
against the installed `fdq` package's directory (e.g. site-packages),
not your shell's working directory, and fails with an error like
`Primary config module 'fdq.torch_model.fdq' not found`.

FDQ will load the YAML config, expand `~` in paths, instantiate an
`fdqExperiment` with the chess data loaders and `chessCNN`, run
`fdq_train()` for the configured number of epochs, and store results
(history, checkpoints, best model) under `store.results_path`.

### 2.4 Testing

[torch_model/fdq/chess_evaluator.py](fdq/chess_evaluator.py) implements
`fdq_test(experiment)`. Its accuracy metric compares
`argmax(from_logits)`/`argmax(to_logits)` against the played move's
actual squares - `correct_move` requires both to match, `correct_position`
only one.

### 2.5 Exporting to ONNX

Use FDQ 0.1.23's interactive `dump_model` export flow (`mode.dump_model:
true`). FDQ's exporter (`fdq/dump.py`) does **not** support a dynamic
batch axis - it always bakes in whatever batch size its example input
tensor has, which by default is `train_batch_size` (256 in
`chess_cnn_p00.yaml`). Since [src/chess_ML.cpp](../src/chess_ML.cpp)
always calls the model with a single board (batch=1), override the batch
size for the export run so the two match:

```bash
fdq \
	--config-path "$(pwd)/torch_model/fdq" \
	--config-name chess_cnn_p00 \
	mode.run_train=false mode.run_test_auto=false mode.dump_model=true \
	data.CHESS.args.train_batch_size=1
```

Then pick `2) ONNX export` and `n` (non-dynamo) at the prompts - the
non-dynamo path is what's been validated against the C++ side. The
"Shape of sample tensor" prompt should read `torch.Size([1, 16, 8, 8])`
before you confirm the export.

FDQ's export call only declares one output name (`"output"`), so the two
heads will show up in the file as e.g. `output` and `view_1` rather than
`from_logits`/`to_logits` - harmless, since
[src/chess_ML.cpp](../src/chess_ML.cpp) reads outputs by **index** (0 =
from, 1 = to), never by name, matching `ChessCNN.forward()`'s fixed
`(from_logits, to_logits)` return order.

The exported model is then consumed by the C++ engine via ONNX Runtime;
see [README.md](../README.md) for how to build Mate with ONNX support.

- if `torch_model/trained_models/chessCNN_torchscript.onnx` exists, Mate auto-detects it into `model_a_path`
- otherwise set `model_a_path` (or `model_b_path`) in `~/.mate/config.json` to your exported model - see the README's Optional ML Support section for the two-model-slot setup

### 2.6 Troubleshooting: DataLoader worker crash (SIGILL in libtcl)

**Symptom:** a CPU-only training run (no accelerator, so `num_workers > 0`
DataLoader workers are separate forked processes) dies mid-epoch - often
tens of minutes / dozens of epochs in, not on the first batch - with:

```
Tcl_AsyncDelete: async handler deleted by the wrong thread
...
RuntimeError: DataLoader worker (pid ...) is killed by signal: Illegal instruction.
RuntimeError: DataLoader worker (pid(s) ...) exited unexpectedly
```

and the kernel log (`dmesg` / `journalctl -k`) shows, at the same
timestamp:

```
kernel: traps: pt_data_worker[...] trap invalid opcode ip:... in libtcl9.0.so
```

**Root cause:** FDQ's own `fdq/misc.py` does `import matplotlib.pyplot as
plt` at module import time (used for the ASCII train/val loss plots
printed each epoch). This happens as a side effect of importing
`fdq.experiment`, i.e. before FDQ ever loads this project's
[train.py](fdq/train.py) - so it runs unconditionally, whether or not this
project's code touches matplotlib itself. If matplotlib resolves to the
**TkAgg** backend on the machine (the default when Tk is available and no
backend is forced), importing `pyplot` initializes Tcl/Tk, which starts
its own internal notifier thread in the main process.

PyTorch's `DataLoader` workers use the default **fork** start method on
Linux. Forking duplicates the process's memory but not its other threads,
so each worker inherits a partially-initialized, broken copy of Tcl's
threading state. Tcl is not fork-safe; a forked worker eventually touches
that broken state (typically during worker teardown/respawn between
epochs), prints the `Tcl_AsyncDelete` warning, then executes an invalid
opcode - crashing the worker with `SIGILL`. This is a fork/Tcl
interaction, not a hardware fault, a corrupt dataset, or a real
`torch`/CPU-instruction incompatibility - confirm by checking `dmesg -T`
for `trap invalid opcode ... libtcl` at the crash timestamp.

**Fix 1 - force a non-GUI matplotlib backend via environment variable (no
code changes):**

```bash
MPLBACKEND=Agg fdq \
	--config-path "$(pwd)/torch_model/fdq" \
	--config-name chess_cnn_p00 \
	mode.run_train=true mode.run_test_auto=true mode.dump_model=false
```

This is read by matplotlib before any backend resolution happens, so it
doesn't matter that `fdq.misc` imports `pyplot` before this project's own
code ever runs.

**Fix 2 - a thin wrapper launcher** that sets the backend before
importing `fdq` at all, for anyone who wants this baked into how training
is invoked rather than relying on remembering the env var:

```python
# torch_model/fdq/run.py
import matplotlib
matplotlib.use("Agg")

from fdq.run_experiment import main

if __name__ == "__main__":
	main()
```

run as `python torch_model/fdq/run.py ...` (same Hydra overrides as
`fdq` above) instead of the installed `fdq` console script. Setting the
backend **inside [train.py](fdq/train.py)** does *not* work - by the time
FDQ loads this project's `train.py`, `fdq.misc` has already imported
`pyplot` with the default backend, since `fdq` is an installed
third-party package (`site-packages`), not part of this repo.

### 2.7 Architecture comparison: fully-connected variant (chess_fc_p00.yaml)

[torch_model/fdq/chess_fc_p00.yaml](fdq/chess_fc_p00.yaml) runs the exact
same data, losses, and training loop as `chess_cnn_p00.yaml`, but swaps
the model for [torch_model/fdq/chess_fc.py](fdq/chess_fc.py)'s `ChessFC` -
a plain fully-connected (dense) network, no convolutions at all. It's here
purely to contrast against `ChessCNN` (this is an educational project),
not to produce a strong engine:

- The `(16, 8, 8)` board tensor is flattened to a 1024-value vector before
  the first layer. Unlike the CNN's 3x3 kernels, which see a local
  neighbourhood and reuse the same small set of weights at every board
  position, every hidden unit here has its own independent weight for each
  of the 1024 inputs - far more parameters, no translation invariance, and
  no built-in notion of which squares are adjacent.
- **`models.chessCNN.args`** (`chess_fc_p00.yaml`): `nb_in_channels: 16`,
  `board_size: 8`, `hidden_dims` (widths of the dense hidden layers, e.g.
  `[1024, 512]`) in place of `conv_channels`/`kernel_size`. The backbone
  feeds two linear heads producing `from_logits`/`to_logits`, each
  `(N, 64)` - the same shape `ChessCNN` produces, so it's a drop-in
  replacement for training, testing and ONNX export.
- The model dict key stays `chessCNN` even though it's the dense network -
  [train.py](fdq/train.py) hardcodes `experiment.models["chessCNN"]`, so
  only `path`/`class_name`/`args` actually point at `ChessFC`.
- Unlike `chess_cnn_p01.yaml`, this config is **not** written as
  `defaults: [chess_cnn_p00]`: FDQ instantiates models as
  `cls(**model_def.args)` (`experiment.py`) with no unused-kwarg
  filtering, so deep-merging onto `chess_cnn_p00`'s model args would leak
  its CNN-only `conv_channels`/`kernel_size` keys into `ChessFC`'s
  constructor and crash. `chess_fc_p00.yaml` is a full standalone file
  instead.

Run it the same way as section 2.3/2.4, just with the other config name:

```bash
fdq \
	--config-path "$(pwd)/torch_model/fdq" \
	--config-name chess_fc_p00 \
	mode.run_train=true mode.run_test_auto=true mode.dump_model=false
```

Expect noticeably more parameters and likely worse/slower-converging
accuracy than `chess_cnn_p00.yaml` at the same dataset size - that gap is
the point of the comparison, not a bug.
