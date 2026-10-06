#!/usr/bin/env bash
# Trains every experiment config in this directory, one after another, using
# each config's own mode: defaults (run_train + run_test_auto, no interactive
# prompts) - so this is equivalent to running the `fdq` command from
# torch_model.md/rl_training.md once per experiment below. Meant to be
# started once and left alone for a day or two:
#   nohup torch_model/fdq/train_all.sh > train_all.out 2>&1 &   (or in tmux)
#
# Two phases:
#   1. SUPERVISED: fc_p00-p03, cnn_p00-p03 (human moves), cnn_sf_p00-p03
#      (Stockfish labels, see torch_model/rl_training.md section 10).
#   2. RL: chess_rl_p00_random trains from scratch; p01-p03 warm-start from
#      the init_weights_path set in chess_rl_p01_warmstart.yaml (p02/p03
#      inherit it) - update it there by hand after retraining.
#
# Before training anything, every dataset file (and Stockfish labels file)
# the configs need is checked - missing ones abort immediately
# instead of failing after a day of training.
#
# Prerequisites:
#   - `fdq` on PATH: pip install -r torch_model/fdq/requirements.txt
#   - the datasets, generated and Stockfish-labelled:
#     torch_model/data_preparation/create_all.sh (see its header for timings).
#   - chess_rl_p03_sunfish needs Sunfish installed (`pip install sunfish`);
#     chess_rl_p02_stockfish needs a real Stockfish binary - see
#     torch_model/rl_training.md section 3 for both.
#   - wandb_keys.yaml (gitignored, next to this script) with
#     store.wandb_key: every experiment inherits it and logs train/val loss
#     and wins/draws/losses per epoch to wandb (project "ChessMate", entity "stmd").
#
# A failed experiment does not stop the others - see the summary printed at
# the end, and the per-experiment logs under logs/train_all_<timestamp>/.
#
# Any arguments given are passed through as Hydra overrides to every `fdq`
# call, overriding each config's own settings - e.g. to smoke-test the whole
# batch with just 2 epochs each:
#   torch_model/fdq/train_all.sh train.args.epochs=2
# or to test-only every experiment instead of training it (no new
# checkpoints):
#   torch_model/fdq/train_all.sh mode.run_train=false mode.run_test_auto=true
#
# Usage: torch_model/fdq/train_all.sh [hydra overrides...]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Run in this order: supervised first, then RL.
EXPERIMENTS=(
	chess_fc_p00
	chess_fc_p01
	chess_fc_p02
	chess_fc_p03
	chess_cnn_p00
	chess_cnn_p01
	chess_cnn_p02
	chess_cnn_p03
	chess_cnn_sf_p00
	chess_cnn_sf_p01
	chess_cnn_sf_p02
	chess_cnn_sf_p03
	chess_rl_p00_random
	chess_rl_p01_warmstart
	chess_rl_p02_stockfish
	chess_rl_p03_sunfish
)

if ! command -v fdq >/dev/null 2>&1; then
	echo "error: 'fdq' not found on PATH - pip install -r ${SCRIPT_DIR}/requirements.txt (in the right venv) first." >&2
	exit 1
fi

# --- Check all datasets before training anything -------------------------
echo "Checking datasets of all experiments..."
missing=$(python3 - "$SCRIPT_DIR" "$@" "--" "${EXPERIMENTS[@]}" 2>/dev/null <<'EOF'
import os, sys
from hydra import compose, initialize_config_dir
args = sys.argv[1:]
config_dir, rest = args[0], args[1:]
sep = rest.index("--")
overrides, experiments = rest[:sep], rest[sep + 1:]
missing = set()
with initialize_config_dir(config_dir=config_dir, version_base=None):
    for exp in experiments:
        a = compose(config_name=exp, overrides=overrides).data.CHESS.args
        for name in (a.train_set, a.test_set):
            path = os.path.join(os.path.expanduser(a.base_path), name)
            needed = [path]
            if a.get("label_source", "human") == "stockfish":
                needed.append(os.path.splitext(path)[0] + ".sflabels.npz")
            missing.update(p for p in needed if not os.path.exists(p))
print("\n".join(sorted(missing)))
EOF
)
if [ $? -ne 0 ]; then
	echo "error: could not compose the experiment configs to check their datasets." >&2
	exit 1
fi
if [ -n "$missing" ]; then
	echo "error: missing dataset files - run torch_model/data_preparation/create_all.sh first:" >&2
	echo "$missing" | sed 's/^/  /' >&2
	exit 1
fi

# Forces a non-interactive matplotlib backend - works around the
# fork/Tcl SIGILL crash documented in torch_model.md section 2.6.
export MPLBACKEND="${MPLBACKEND:-Agg}"

LOG_DIR="${SCRIPT_DIR}/logs/train_all_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$LOG_DIR"
failed=()
succeeded=()

run_experiment() {
	local exp="$1"
	shift
	local log_file="${LOG_DIR}/${exp}.log"
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
}

for exp in "${EXPERIMENTS[@]}"; do
	run_experiment "$exp" "$@"
done

echo
echo "=================================================================="
echo "Summary"
echo "=================================================================="
echo "Succeeded (${#succeeded[@]}): ${succeeded[*]:-none}"
echo "Failed    (${#failed[@]}): ${failed[*]:-none}"

[ "${#failed[@]}" -eq 0 ]
