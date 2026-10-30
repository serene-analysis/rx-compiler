#!/usr/bin/env bash
# Usage:
#   ./run_ir.bash              # compile input.local.rx
#   ./run_ir.bash foo.rx       # compile another source
#   NO_BUILD=1 ./run_ir.bash   # skip cmake build
#
# Optional stdin for get_i32: create input.local.txt next to this script.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

SRC="${1:-input.local.rx}"
OUTDIR="$ROOT/target/ir-smoke"
LL="$OUTDIR/program.ll"
ASM="$OUTDIR/program.s"
RUNTIME="$OUTDIR/runtime.s"
STDOUT="$OUTDIR/program.out"

if [[ ! -f "$SRC" ]]; then
    echo "source not found: $SRC" >&2
    echo "create it or pass a .rx path" >&2
    exit 1
fi

if [[ "${NO_BUILD:-0}" != "1" ]]; then
    cmake --build target/build -j1
fi

CLANG="${CLANG:-clang-22}"
if ! command -v "$CLANG" >/dev/null 2>&1; then
    if [[ -x "$HOME/.local/bin/clang-22" ]]; then
        CLANG="$HOME/.local/bin/clang-22"
    else
        echo "clang-22 not found" >&2
        exit 1
    fi
fi

XMAKE="${XMAKE:-xmake}"
if ! command -v "$XMAKE" >/dev/null 2>&1; then
    if [[ -x "$HOME/.local/bin/xmake" ]]; then
        XMAKE="$HOME/.local/bin/xmake"
    else
        echo "xmake not found" >&2
        exit 1
    fi
fi

mkdir -p "$OUTDIR"
: > "$RUNTIME"

echo "[1/3] rxcc: $SRC -> $LL"
./target/build/rxcc --stage ir "$SRC" -o "$LL"

echo "[2/3] clang: $LL -> $ASM"
"$CLANG" -S -x ir "$LL" \
    --target=riscv32-unknown-none-elf \
    -march=rv32im -mabi=ilp32 -O0 \
    -o "$ASM"

echo "[3/3] reimu"
if [[ -f "$ROOT/input.local.txt" ]]; then
    "$XMAKE" run -P vendor/REIMU reimu --memory=256M --stack=1M \
        -f "$ASM,$RUNTIME" -o "$STDOUT" < "$ROOT/input.local.txt"
else
    "$XMAKE" run -P vendor/REIMU reimu --memory=256M --stack=1M \
        -f "$ASM,$RUNTIME" -o "$STDOUT" < /dev/null
fi

echo "--- output ---"
cat "$STDOUT"
