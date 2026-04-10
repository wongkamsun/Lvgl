#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

mkdir -p tools
mkdir -p ui
mkdir -p resources/images

echo "[1/3] Build generators (cmake build)"
cmake --build build >/dev/null

echo "[2/3] Convert widget*.csv -> ui/ui_*.bin (and tr_*.bin)"
shopt -s nullglob
csvs=(ui/widget*.csv)
if (( ${#csvs[@]} == 0 )); then
  echo "skip: no ui/widget*.csv found"
  exit 0
fi

idx=1
for f in "${csvs[@]}"; do
  out="ui/ui_${idx}.bin"
  echo "  ${f} -> ${out}"
  ./bin/csv_to_widgets_bin "$f" "$out"

  tr_csv="ui/translation_${idx}.csv"
  tr_out="ui/tr_${idx}.bin"
  if [[ -f "${tr_csv}" ]]; then
    echo "  ${tr_csv} -> ${tr_out}"
    ./bin/csv_to_translation_bin "${tr_csv}" "${tr_out}"
  fi

  idx=$((idx+1))
done

echo "[3/3] (optional) Convert ui/translation.csv -> ui/tr.bin (fallback)"
if [[ -f "ui/translation.csv" ]]; then
  ./bin/csv_to_translation_bin "ui/translation.csv" "ui/tr.bin"
fi

echo "done."

