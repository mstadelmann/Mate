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
#   2. RL: chess_rl_p00_random trains from scratch. With AUTO_WARMSTART=1
#      (default, see below) the warm-started RL experiments start from the
#      best_val checkpoint of the last chess_cnn_sf_* experiment that
#      succeeded in phase 1 (sf_p03, else sf_p02, ...) - passed as a Hydra
#      override, the config files stay untouched (fdq stores the composed
#      config, including the path used, in each run's results folder). If no
#      sf experiment succeeded, those RL experiments are skipped (reported as
#      failed) rather than silently starting from another checkpoint.
#      With AUTO_WARMSTART=0 they use their configs' own init_weights_path.
#
# Before training anything, every dataset file (and Stockfish labels file)
# the supervised configs need is checked - missing ones abort immediately
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
# checkpoints, so disable the warm-start update):
#   AUTO_WARMSTART=0 torch_model/fdq/train_all.sh mode.run_train=false mode.run_test_auto=true
#
# Usage: [AUTO_WARMSTART=0] torch_model/fdq/train_all.sh [hydra overrides...]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 1 = warm-start the RL experiments in WARMSTART_RL from this batch's newest
# chess_cnn_sf_* checkpoint; 0 = keep their configs' own init_weights_path.
AUTO_WARMSTART="${AUTO_WARMSTART:-1}"

SUPERVISED=(
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
)

RL=(
	chess_rl_p00_random
	chess_rl_p01_warmstart
	chess_rl_p02_stockfish
	chess_rl_p03_sunfish
)

# RL experiments that get the warm-start checkpoint (p00 trains from scratch).
WARMSTART_RL=(
	chess_rl_p01_warmstart
	chess_rl_p02_stockfish
	chess_rl_p03_sunfish
)

# Where the warm-start checkpoint comes from: the first of these that
# succeeded in this batch.
WARMSTART_SOURCES=(
	chess_cnn_sf_p03
	chess_cnn_sf_p02
	chess_cnn_sf_p01
	chess_cnn_sf_p00
)

if ! command -v fdq >/dev/null 2>&1; then
	echo "error: 'fdq' not found on PATH - pip install -r ${SCRIPT_DIR}/requirements.txt (in the right venv) first." >&2
	exit 1
fi

# Prints one value of an experiment's composed config (with this script's
# Hydra overrides applied), e.g. `cfg_value chess_cnn_p00 store.results_path`.
cfg_value() {
	local exp="$1" key="$2"
	shift 2
	python3 - "$SCRIPT_DIR" "$exp" "$key" "$@" <<'EOF'
import sys
from hydra import compose, initialize_config_dir
from omegaconf import OmegaConf
config_dir, exp, key, *overrides = sys.argv[1:]
with initialize_config_dir(config_dir=config_dir, version_base=None):
    print(OmegaConf.select(compose(config_name=exp, overrides=overrides), key))
EOF
}

# --- Check all supervised datasets before training anything --------------
echo "Checking datasets of the supervised experiments..."
missing=$(python3 - "$SCRIPT_DIR" "$@" "--" "${SUPERVISED[@]}" 2>/dev/null <<'EOF'
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
# Run folders newer than this marker were created by this batch.
touch "${LOG_DIR}/.batch_start"

failed=()
succeeded=()

# run_experiment <experiment> [extra hydra overrides...] (after "$@")
run_experiment() {
	local exp="$1"
	shift
	local log_file="${LOG_DIR}/${exp}.log"
	echo
	echo "=================================================================="
	echo "Training ${exp}  (log: ${log_file})"
	[ "$#" -gt 0 ] && echo "  overrides: $*"
	echo "=================================================================="

	if fdq --config-path "$SCRIPT_DIR" --config-name "$exp" "$@" 2>&1 | tee "$log_file"; then
		succeeded+=("$exp")
	else
		echo "!! ${exp} failed - see ${log_file}" >&2
		failed+=("$exp")
	fi
}

contains() {
	local item="$1"
	shift
	local x
	for x in "$@"; do [ "$x" = "$item" ] && return 0; done
	return 1
}

# --- Phase 1: supervised --------------------------------------------------
for exp in "${SUPERVISED[@]}"; do
	run_experiment "$exp" "$@"
done

# --- Pick the RL warm-start checkpoint ------------------------------------
warmstart_path=""
warmstart_note="config's own init_weights_path (AUTO_WARMSTART=0)"
if [ "$AUTO_WARMSTART" = "1" ]; then
	warmstart_note="none - no chess_cnn_sf_* experiment succeeded, warm-started RL skipped"
	for src in "${WARMSTART_SOURCES[@]}"; do
		contains "$src" "${succeeded[@]}" || continue
		results_dir="$(cfg_value "$src" store.results_path "$@" 2>/dev/null)/$(cfg_value "$src" globals.project "$@" 2>/dev/null)/${src}"
		results_dir="${results_dir/#\~/$HOME}"
		# This batch's run folder of $src, then its best_val model.
		run_dir=$(find "$results_dir" -mindepth 1 -maxdepth 1 -type d -newer "${LOG_DIR}/.batch_start" -printf '%T@ %p\n' 2>/dev/null | sort -n | tail -1 | cut -d' ' -f2-)
		[ -n "$run_dir" ] || continue
		candidate=$(find "$run_dir" -maxdepth 1 -name 'best_val_*.fdqm' ! -name '*temp_copy*' -printf '%T@ %p\n' | sort -n | tail -1 | cut -d' ' -f2-)
		if [ -n "$candidate" ]; then
			warmstart_path="$candidate"
			warmstart_note="$warmstart_path"
			break
		fi
	done
fi
echo
echo "=================================================================="
echo "RL warm start: ${warmstart_note}"
echo "=================================================================="

# --- Phase 2: RL ----------------------------------------------------------
for exp in "${RL[@]}"; do
	if [ "$AUTO_WARMSTART" = "1" ] && contains "$exp" "${WARMSTART_RL[@]}"; then
		if [ -z "$warmstart_path" ]; then
			echo "!! ${exp} skipped - no warm-start checkpoint (see above)" >&2
			failed+=("$exp")
			continue
		fi
		run_experiment "$exp" "$@" "train.args.init_weights_path=${warmstart_path}"
	else
		run_experiment "$exp" "$@"
	fi
done

echo
echo "=================================================================="
echo "Summary"
echo "=================================================================="
echo "RL warm start: ${warmstart_note}"
echo "Succeeded (${#succeeded[@]}): ${succeeded[*]:-none}"
echo "Failed    (${#failed[@]}): ${failed[*]:-none}"

[ "${#failed[@]}" -eq 0 ]
