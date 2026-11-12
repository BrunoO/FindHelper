# Benchmark baselines

Reference baselines for the search-path regression gates, recorded with
`./scripts/run_search_benchmark.sh --record-baseline`.

## search_path_2026-10-15.json

Guards the two proposed search-path levers (see
[2026-10-15_search_path_regex_findings.md](../../internal-docs/optimization/2026-10-15_search_path_regex_findings.md)
and the [step-0 test scope](../../internal-docs/plans/2026-10-15_search_path_A_B_test_additions_scope.md)).

| Config | Query shape | Guards |
|---|---|---|
| `benchmark_config_regex_alternation_suffix.json` | filename `re:.*\.(cpp\|h)$`, no ext | Lever B (suffix prefilter) |
| `benchmark_config_regexfn_selective_path.json` | path `pp:**/imgui/**` + filename regex | Lever B (suffix prefilter under a path filter) |
| `benchmark_config_substrfn_backtrack_path.json` | filename `main` + path `pp:**/imgui/**` | Lever A reverse regression (reorder must not hurt) |
| `benchmark_config_charclassfn_selective_path.json` | filename `re:\d{4}` + path `/src` | Lever A (path-first ordering) |
| `benchmark_config_regex_path_ext.json` | path `re:.*\.cpp$` | extension-index routing for `re:` |
| `benchmark_config_regex_prefix_alternation.json` | filename `re:^(get\|set)_` | prefix prefilter for start-anchored `re:` |

**Environment:** macOS Apple Silicon (8 cores, no HT), Release, `FAST_LIBS_BOOST=ON`
(boost::regex), index `tests/data/std-linux-filesystem.txt` (706,120 rows),
9 iterations / 2 warmup. Absolute timings are machine- and build-specific; the
gate uses relative deltas (`--compare ... --max-regression-pct`).

Re-gate after a change:

```bash
# Strict gate for the ms-scale regex configs:
./scripts/run_search_benchmark.sh \
  --config tests/benchmark_config_regex_alternation_suffix.json \
  --config tests/benchmark_config_regexfn_selective_path.json \
  --config tests/benchmark_config_charclassfn_selective_path.json \
  --compare tests/baselines/search_path_2026-10-15.json \
  --verify --max-regression-pct 5.0 --fail-on-hit-mismatch

# The sub-millisecond configs (~0.05-1.9 ms) trip a 5% bound on timing noise
# alone; their failure modes are large multiples, so gate them loosely.
./scripts/run_search_benchmark.sh \
  --config tests/benchmark_config_substrfn_backtrack_path.json \
  --config tests/benchmark_config_regex_path_ext.json \
  --config tests/benchmark_config_regex_prefix_alternation.json \
  --compare tests/baselines/search_path_2026-10-15.json \
  --verify --max-regression-pct 50.0 --fail-on-hit-mismatch
```

