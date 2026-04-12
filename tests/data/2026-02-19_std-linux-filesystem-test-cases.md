# std-linux-filesystem regression test cases

**Date:** 2026-02-19  
**Dataset:** `std-linux-filesystem.txt` (one path per line; loaded via `--index-from-file`).  
**Golden file:** `std-linux-filesystem-expected.json`  
**Spec:** `specs/2026-02-19_IMGUI_REGRESSION_TESTS_STD_LINUX_FILESYSTEM_SPEC.md`

## Test cases (search methods covered)

| Id | Search method | Config (JSON search_config) | Expected count |
|----|----------------|-----------------------------|----------------|
| show_all | No filters | `{}` | 789531 |
| filename_tty | Filename substring | `{"filename":"tty"}` | 743 |
| path_dev | Path substring | `{"path":"/dev"}` | 23496 |
| ext_conf | Extension filter | `{"extensions":["conf"]}` | 683 |
| path_etc_ext_conf | Path + extension | `{"path":"/etc","extensions":["conf"]}` | 229 |
| folders_only | Directories only | `{"folders_only":true}` | 170335 |

## Regenerating expected results

From project root, after building with `BUILD_TESTS=ON`:

```bash
INDEX="tests/data/std-linux-filesystem.txt"
BIN="./build_tests/search_benchmark"

$BIN --index "$INDEX" --config '{"version":"1.0","search_config":{}}' --iterations 1 --warmup 0
$BIN --index "$INDEX" --config '{"version":"1.0","search_config":{"filename":"tty"}}' --iterations 1 --warmup 0
$BIN --index "$INDEX" --config '{"version":"1.0","search_config":{"path":"/dev"}}' --iterations 1 --warmup 0
$BIN --index "$INDEX" --config '{"version":"1.0","search_config":{"extensions":["conf"]}}' --iterations 1 --warmup 0
$BIN --index "$INDEX" --config '{"version":"1.0","search_config":{"path":"/etc","extensions":["conf"]}}' --iterations 1 --warmup 0
$BIN --index "$INDEX" --config '{"version":"1.0","search_config":{"folders_only":true}}' --iterations 1 --warmup 0
```

Record the "Results: N matches" line for each and update `std-linux-filesystem-expected.json`.
