#!/usr/bin/env bash
# Prepares the seed corpus of native_reader_fuzz (plan S1-5.8): the malformed native corpus, every native
# fixture and the sample documents, all generated or copied from files in the repository.
# Usage: fuzz/seed_native_reader.sh <seed-directory>
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <seed-directory>" >&2
    exit 2
fi
out="$1"
mkdir -p "$out"
python3 -I tests/resilience/corpus/make_native_corpus.py tests/io/native/fixtures/v0.1.archmage "$out"
rm -f "$out/corpus.json"
for file in tests/io/native/fixtures/*.archmage docs/samples/*.archmage; do
    cp "$file" "$out/seed-$(basename "$file")"
done
echo "seed_native_reader.sh: $(find "$out" -maxdepth 1 -type f | wc -l) seeds in $out"
