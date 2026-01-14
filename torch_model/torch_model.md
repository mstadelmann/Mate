# AI Model for Mate

## 1) Data Preparation

**Chess Tensor Generation**
- **Script:** Generates training tensors from PGN games in [torch_model/data_preparation/generate_chess_tensor.py](torch_model/data_preparation/generate_chess_tensor.py).
- **Pipeline:** Extract header + sample, clean to CSV, filter, build tensors, dedupe positions, save pickle.
- **Packages:** chess, pandas, numpy, PyYAML (see [torch_model/data_preparation/requirements.txt](torch_model/data_preparation/requirements.txt)).

**Configuration**
- **Config file:** YAML at [torch_model/data_preparation/chess_tensor_config.yaml](torch_model/data_preparation/chess_tensor_config.yaml) (JSON also supported).
- **Fields:**
	- **`chess_db_base_path`:** Path to large PGN text DB.
	- **`number_of_games`:** Number of games to sample (keeps first N after headers).
	- **`stopAfterXMoves`:** Max moves per game; set to `null` to disable.
	- **`min_nb_moves`:** Minimum moves per game to keep.
	- **`minElo`:** Minimum ELO for both players.
	- **`remove_duplicates`:** If true, dedupe identical input positions by best black response (rating-weighted).
	- **`EXPORT_PICKLE`:** If true, writes pickle with tensors.
	- **`debug`:** Verbose logging and sample board prints.

**How It Works**
- **Extract:** `extract_head_lines()` copies the first `number_of_games + 5` lines to a temp text file.
- **Clean:** `cleanChessDB()` converts the raw text to CSV with headers and fields.
- **Filter:** `load_and_filter_data()` applies validity checks (ratings/results), `min_nb_moves`, and `minElo`.
- **Tensors:** `generate_tensors()` builds:
	- **`in_array`:** (N, 6, 8, 8) board before black move.
	- **`out_array`:** (N, 1, 8, 8) move mask (source −1, destination +1; clipped to [-1, 1]).
	- Keeps only black moves (positions where it becomes White's turn after push).
- **Deduping:** `dedupe_by_best_response()` removes duplicate `in_array` positions, keeping the black reply with highest cumulative rating.
- **Export:** `save_tensor()` writes `{in_array, out_array}` to a `.chessarray` pickle named like:
	- `/path/to/chess_db_nbGames{N}_stopAfter{X}_nbMoves{M}_minElo{E}.chessarray`.

**Run**
- **Install deps:**
```bash
pip install -r torch_model/data_preparation/requirements.txt
```

- **Generate data (YAML):**
```bash
cd torch_model/data_preparation
python3 generate_chess_tensor.py --config chess_tensor_config.yaml
```

**Validation**
- The script validates the YAML config before running:
	- Checks required keys and types (e.g., `number_of_games:int`, `debug:bool`).
	- Ensures numeric constraints (e.g., `number_of_games > 0`).
	- Verifies the DB file path exists after expanding `~`.
- On invalid config, it raises a clear error message and exits.

**Notes**
- Temp files created during processing are cleaned up at the end.
- Large `number_of_games` values increase memory/time; start small to validate.
- Set `stopAfterXMoves` to `null` in the config to process full games.


## 2) Training with FDQ

This project uses [FDQ (Fonduecaquelon)](https://github.com/mstadelmann/fonduecaquelon) to manage the training loop, data loading and model checkpoints.

### 2.1 Configuration

- **FDQ experiment config:** [torch_model/fdq/chess_dense_p00.yaml](torch_model/fdq/chess_dense_p00.yaml)
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

	- **`models.simpleNet`**: points to the network definition in the FDQ repo:
		- `path`: path to `simpleNet.py` (e.g. `~/dev/fonduecaquelon/src/networks/simpleNet.py`).
		- `class_name`: `"simpleNet"`.
		- `args`: channels and layer sizes used for chess (see YAML).

	- **`data.CHESS`**:
		- `processor`: [torch_model/fdq/chess_preparator.py](torch_model/fdq/chess_preparator.py) (builds the data loaders from `.chessarray` files).
		- `args.base_path`: root directory for the generated `.chessarray` files.
		- `train_set` / `test_set`: filenames of the training and test `.chessarray` tensors.

	- **`train.path`**: training script used by FDQ: [torch_model/fdq/train.py](torch_model/fdq/train.py).
	- **`store.results_path`**: base folder where FDQ stores results, logs and checkpoints.

Make sure all paths in [torch_model/fdq/chess_dense_p00.yaml](torch_model/fdq/chess_dense_p00.yaml) match your local folders (especially `simpleNet.py`, `base_path`, and the `.chessarray` filenames).

### 2.2 Training loop (fdq_train)

The training procedure is implemented in [torch_model/fdq/train.py](torch_model/fdq/train.py) as required by FDQ:

```python
def fdq_train(experiment: fdqExperiment) -> None:
		...
```

Inside `fdq_train`:

- `experiment.data["CHESS"]` provides `train_data_loader` and `val_data_loader` built by `chess_preparator.py`.
- `experiment.models["simpleNet"]` is the instantiated PyTorch model.
- The loop follows FDQ’s pattern:
	- Call `experiment.on_epoch_start(epoch=...)` at the beginning of each epoch.
	- For each batch, move inputs/targets to `experiment.device`, run the model, compute loss, call `backward()` and `experiment.update_gradients(...)`.
	- After each epoch, compute average train/val loss and call `experiment.on_epoch_end()` for logging, checkpointing and early stopping.

### 2.3 Running a local training

From the Mate project root:

```bash
cd /home/marc/dev/Mate

# Example: run only training as defined in chess_dense_p00.yaml
fdq \
	--config-path torch_model/fdq \
	--config-name chess_dense_p00 \
	mode.run_train=true mode.run_test_auto=false mode.dump_model=false
```

FDQ will:

- Load the YAML config and expand `~` in paths.
- Instantiate an `fdqExperiment` with the chess data loader and `simpleNet`.
- Run `fdq_train()` for the configured number of epochs.
- Store results (history, checkpoints, best model) under the folder derived from `store.results_path`.

### 2.4 After training

- The best model weights are stored in the results directory chosen via `store.results_path`.
- A separate FDQ "dump" or export step can convert the trained `simpleNet` to ONNX; the exported model is then consumed by the C++ engine via ONNX Runtime (see [src/chess_ML.cpp](src/chess_ML.cpp)).
