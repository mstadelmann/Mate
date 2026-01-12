# AI Model for Mate

## 1) Data Preparation

**Chess Tensor Generation**
- **Script:** Generates training tensors from PGN games in [torch_model/data_preparation/generate_chess_tensor.py](torch_model/data_preparation/generate_chess_tensor.py).
- **Pipeline:** Extract header + sample, clean to CSV, filter, build tensors, dedupe positions, save pickle.
- **Packages:** chess, pandas, numpy (see [torch_model/data_preparation/requirements.txt](torch_model/data_preparation/requirements.txt)).

**Configuration**
- **Config file:** JSON at [torch_model/data_preparation/chess_tensor_config.json](torch_model/data_preparation/chess_tensor_config.json).
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
- **Export:** `save_tensor()` writes `{in_array, out_array}` to a `.chesstensor` pickle named like:
	- `/path/to/chess_db_nbGames{N}_stopAfter{X}_nbMoves{M}_minElo{E}.chesstensor`.

**Run**
- **Install deps:**
```bash
pip install -r torch_model/data_preparation/requirements.txt
```

- **Generate data:**
```bash
cd torch_model/data_preparation
python3 generate_chess_tensor.py --config chess_tensor_config.json
```

**Validation**
- The script validates the JSON config before running:
	- Checks required keys and types (e.g., `number_of_games:int`, `debug:bool`).
	- Ensures numeric constraints (e.g., `number_of_games > 0`).
	- Verifies the DB file path exists after expanding `~`.
- On invalid config, it raises a clear error message and exits.

**Notes**
- Temp files created during processing are cleaned up at the end.
- Large `number_of_games` values increase memory/time; start small to validate.
- Set `stopAfterXMoves` to `null` in the JSON to process full games.
