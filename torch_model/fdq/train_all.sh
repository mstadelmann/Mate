#!/usr/bin/env bash
# Sequentially trains every experiment config in this directory, one after
# another, using each config's own mode: defaults (run_train + run_test_auto,
# no interactive prompts) - so this is equivalent to running the `fdq`
# command from torch_model.md/rl_training.md once per experiment below.
#
# Prerequisites:
#   - `fdq` on PATH: pip install -r torch_model/fdq/requirements.txt
#   - the supervised configs' .chessarray datasets already generated (see
#     torch_model/torch_model.md section 1); chess_cnn_p01/chess_fc_p01 need
#     the larger nbGames10000 dataset specifically.
#   - chess_rl_p01_sunfish needs Sunfish installed (`pip install sunfish`);
#     chess_rl_p02_stockfish needs a real Stockfish binary - see
#     torch_model/rl_training.md section 3 for both.
#
# A failed experiment does not stop the others - see the summary printed at
# the end, and the per-experiment logs under logs/train_all_<timestamp>/.
#
# Any arguments given are passed through as Hydra overrides to every `fdq`
# call, overriding each config's own settings - e.g. to test-only every
# experiment instead of training it:
#   torch_model/fdq/train_all.sh mode.run_train=false mode.run_test_auto=true
# or to smoke-test the whole batch with just 2 epochs each:
#   torch_model/fdq/train_all.sh train.args.epochs=2
#
# Usage: torch_model/fdq/train_all.sh [hydra overrides...]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

EXPERIMENTS=(
	chess_cnn_p00
	chess_cnn_p01
	chess_fc_p00
	chess_fc_p01
	chess_rl_p00_random
	chess_rl_p01_sunfish
	chess_rl_p02_stockfish
)

if ! command -v fdq >/dev/null 2>&1; then
	echo "error: 'fdq' not found on PATH - pip install -r ${SCRIPT_DIR}/requirements.txt (in the right venv) first." >&2
	exit 1
fi

# Forces a non-interactive matplotlib backend - works around the
# fork/Tcl SIGILL crash documented in torch_model.md section 2.6.
export MPLBACKEND="${MPLBACKEND:-Agg}"

LOG_DIR="${SCRIPT_DIR}/logs/train_all_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$LOG_DIR"

failed=()
succeeded=()

for exp in "${EXPERIMENTS[@]}"; do
	log_file="${LOG_DIR}/${exp}.log"
	echo
	echo "=================================================================="
	echo "Training ${exp}  (log: ${log_file})"
	echo "=================================================================="

	if fdq --config-path "$SCRIPT_DIR" --config-name "$exp" "$@" 2>&1 | tee "$log_file"; then
		succeeded+=("$exp")
	else
		echo "!! ${exp} failed - see ${log_file}" >&2
		failed+=("$exp")
	fi
done

echo
echo "=================================================================="
echo "Summary"
echo "=================================================================="
echo "Succeeded (${#succeeded[@]}): ${succeeded[*]:-none}"
echo "Failed    (${#failed[@]}): ${failed[*]:-none}"

[ "${#failed[@]}" -eq 0 ]
