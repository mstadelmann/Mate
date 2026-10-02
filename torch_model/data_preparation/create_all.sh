#!/usr/bin/env bash
# Regenerates every supervised dataset, one after another - runs
# generate_chess_tensor.py once per dataset config below (2k, 10k, 50k and
# 100k games), then label_with_stockfish.py on its train and test files
# (the .sflabels.npz the chess_cnn_sf_* configs train on), i.e. everything
# torch_model/fdq/train_all.sh needs.
#
# Stockfish labelling takes far longer than generating: at depth 10 about
# 8 ms per position per core - on 4 cores roughly 10 min for 10k games,
# 1.7 h for 50k and 3.4 h for 100k. Environment variables:
#   SKIP_LABEL=1                 generate only, no Stockfish labels
#   SF_DEPTH=8                   search depth (default 10; 8 is ~2x faster)
#   STOCKFISH=/path/to/stockfish engine binary (default /usr/games/stockfish)
# An interrupted labelling run resumes where it stopped when rerun with
# the same depth - but regenerating a dataset invalidates its labels.
#
# Prerequisites:
#   - the venv with this directory's dependencies active:
#     pip install -r torch_model/fdq/requirements.txt
#   - disk space in output_dir (~/data_ML/chess): roughly 0.6 GB per 10k
#     games (train + test), ~10 GB for all four datasets together.
#   - RAM: the 100k dataset peaks at ~15 GB while generating.
#   - Stockfish installed, unless SKIP_LABEL=1 (see torch_model/rl_training.md
#     section 3).
#
# Existing .chessarray files with the same name are overwritten. A failed
# dataset does not stop the others - see the summary printed at the end,
# and the per-dataset logs under logs/create_all_<timestamp>/.
#
# Any arguments given select which datasets to create instead of all of
# them, e.g. just the two large ones:
#   torch_model/data_preparation/create_all.sh 50k 100k
#
# Usage: torch_model/data_preparation/create_all.sh [2k|10k|50k|100k ...]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DATASETS=(
	2k
	10k
	50k
	100k
)
if [ "$#" -gt 0 ]; then
	DATASETS=("$@")
fi

PYTHON="${PYTHON:-python3}"
SKIP_LABEL="${SKIP_LABEL:-0}"
SF_DEPTH="${SF_DEPTH:-10}"
STOCKFISH="${STOCKFISH:-/usr/games/stockfish}"
if [ "$SKIP_LABEL" != "1" ] && [ ! -x "$STOCKFISH" ]; then
	echo "error: Stockfish not found at '$STOCKFISH' - set STOCKFISH=/path/to/stockfish, or SKIP_LABEL=1." >&2
	exit 1
fi
if ! "$PYTHON" -c "import chess, datasets" >/dev/null 2>&1; then
	echo "error: '$PYTHON' can't import chess/datasets - activate the right venv first (pip install -r torch_model/fdq/requirements.txt)." >&2
	exit 1
fi

LOG_DIR="${SCRIPT_DIR}/logs/create_all_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$LOG_DIR"

failed=()
succeeded=()

for ds in "${DATASETS[@]}"; do
	config="${SCRIPT_DIR}/chess_tensor_config_${ds}.yaml"
	log_file="${LOG_DIR}/${ds}.log"
	echo
	echo "=================================================================="
	echo "Creating dataset ${ds}  (config: ${config}, log: ${log_file})"
	echo "=================================================================="

	if [ ! -f "$config" ]; then
		echo "!! ${ds}: no such config ${config}" >&2
		failed+=("$ds")
		continue
	fi

	if ! "$PYTHON" "${SCRIPT_DIR}/generate_chess_tensor.py" --config "$config" 2>&1 | tee "$log_file"; then
		echo "!! ${ds} failed - see ${log_file}" >&2
		failed+=("$ds")
		continue
	fi

	if [ "$SKIP_LABEL" != "1" ]; then
		echo "--- labelling ${ds} with Stockfish (depth ${SF_DEPTH}) ---"
		if ! "$PYTHON" "${SCRIPT_DIR}/label_with_stockfish.py" --config "$config" \
			--depth "$SF_DEPTH" --engine "$STOCKFISH" 2>&1 | tee -a "$log_file"; then
			echo "!! ${ds}: Stockfish labelling failed - see ${log_file}" >&2
			failed+=("$ds")
			continue
		fi
	fi
	succeeded+=("$ds")
done

echo
echo "=================================================================="
echo "Summary"
echo "=================================================================="
echo "Succeeded (${#succeeded[@]}): ${succeeded[*]:-none}"
echo "Failed    (${#failed[@]}): ${failed[*]:-none}"

[ "${#failed[@]}" -eq 0 ]
