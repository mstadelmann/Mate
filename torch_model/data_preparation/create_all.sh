#!/usr/bin/env bash
# Regenerates every supervised dataset, one after another - runs
# generate_chess_tensor.py once per dataset config below (2k, 10k, 50k and
# 100k games), i.e. everything torch_model/fdq/train_all.sh needs.
#
# Prerequisites:
#   - the venv with this directory's dependencies active:
#     pip install -r torch_model/fdq/requirements.txt
#   - disk space in output_dir (~/data_ML/chess): roughly 0.6 GB per 10k
#     games (train + test), ~10 GB for all four datasets together.
#   - RAM: the 100k dataset peaks at ~15 GB while generating.
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

	if "$PYTHON" "${SCRIPT_DIR}/generate_chess_tensor.py" --config "$config" 2>&1 | tee "$log_file"; then
		succeeded+=("$ds")
	else
		echo "!! ${ds} failed - see ${log_file}" >&2
		failed+=("$ds")
	fi
done

echo
echo "=================================================================="
echo "Summary"
echo "=================================================================="
echo "Succeeded (${#succeeded[@]}): ${succeeded[*]:-none}"
echo "Failed    (${#failed[@]}): ${failed[*]:-none}"

[ "${#failed[@]}" -eq 0 ]
