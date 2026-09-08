#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <input-video> [output-dir]"
  exit 1
fi

INPUT="$1"
OUTDIR="${2:-reports/benchmarks/$(date +%Y%m%d-%H%M%S)}"
THREADS_LIST="${THREADS_LIST:-1 2 4 0}"
QUEUE="${QUEUE:-8}"
START_SEC="${START_SEC:-0}"
END_SEC="${END_SEC:-5}"
SEED="${SEED:-0.5}"

mkdir -p "$OUTDIR"
cmake -S . -B build-wsl -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build-wsl -j"$(nproc)"

{
  echo "# VideoChaosCipher Benchmark"
  echo
  echo "- Input: $INPUT"
  echo "- Queue capacity: $QUEUE"
  echo "- Range: ${START_SEC}s ~ ${END_SEC}s"
  echo "- Seed: $SEED"
  echo "- Date: $(date -Is)"
  echo
  echo '```text'
  ./scripts/inspect_codecs.sh || true
  echo '```'
  echo
} > "$OUTDIR/report.md"

for t in $THREADS_LIST; do
  label="threads-$t"
  output="$OUTDIR/${label}.mp4"
  log="$OUTDIR/${label}.log"
  echo "== Running $label =="
  ./build-wsl/app "$INPUT" "$output" "$START_SEC" "$END_SEC" "$SEED" -t "$t" -q "$QUEUE" | tee "$log"
  {
    echo
    echo "## $label"
    echo
    echo '```text'
    grep -E '\[VPUDecoder\]|\[VPUEncoder\]|\[Runtime\]|Performance Report|Frames decoded|Frames encrypted|Frames written|Throughput|Total wall time|readQueue|writeQueue|Time span' "$log" || true
    echo '```'
  } >> "$OUTDIR/report.md"
done

echo "Benchmark report: $OUTDIR/report.md"
