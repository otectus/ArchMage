# Fuzzing

libFuzzer harnesses live in `fuzz/` and build only under the `fuzz` preset (Clang, libFuzzer with
AddressSanitizer and UndefinedBehaviorSanitizer; D-03). Every run is recorded here with its command and
result (plan S1-5.8).

## Harnesses

| Target | Input | Calls | Since |
| --- | --- | --- | --- |
| `native_reader_fuzz` (`fuzz/native_reader_fuzz.cpp`) | Bytes written to a scratch `.archmage` file | `readDocument`, `validateFile(Deep)`, `inspectFile`, under a 256 MiB RAM budget | S1-5 |
| `png_fuzz` | | | S1-8, with the PNG codec |

Seeds: `fuzz/seed_native_reader.sh <dir>` writes the malformed native corpus
(`tests/resilience/corpus/make_native_corpus.py`) and copies the native fixtures and sample documents.

## Runs

### 2026-10-13: native_reader_fuzz, 10 minutes

Built from the tree committed as `da49be0` plus `fuzz/native_reader_fuzz.cpp` (committed with this record);
Clang 23.1.1, SQLite 3.53.4, zstd 1.5.7.

```
cmake --preset fuzz && cmake --build --preset fuzz --target native_reader_fuzz
mkdir -p build/fuzz/fuzz-run/{seeds,corpus,artifacts}
# seeds: the ten malformed files, tests/io/native/fixtures/v0.1.archmage and
# docs/samples/s1-4-layered.archmage (what fuzz/seed_native_reader.sh now produces)
cd build/fuzz/fuzz-run
../fuzz/native_reader_fuzz -max_total_time=600 -timeout=20 -rss_limit_mb=2048 -max_len=262144 \
    -artifact_prefix=artifacts/ -print_final_stats=1 corpus seeds
```

| Measure | Result |
| --- | --- |
| Duration | 601 s (21:02:13 to 21:12:14 UTC) |
| Executions | 78,079 (129 per second on average) |
| Coverage | 10,377 edges after the seeds, 11,553 at the end; features 20,213 to 42,573 |
| Corpus | 11 seed units grew to 729 units (62 MB) |
| Crashes, leaks, sanitizer reports, timeouts | none; no artifacts written |
| Peak RSS | 546 MB |

The run log and the grown corpus stay in `build/fuzz/fuzz-run/` (not committed).

## Limits

- Each input is a whole SQLite database of up to 256 KiB, read through the filesystem, so executions are
  slow; mutations mostly perturb page contents rather than SQL-level structure.
- SQLite itself is not fuzzed here; the harness exercises ArchMage's use of it and its own parsing.
