# floatinghotel vs px0

Recorded 2026-10-06T14:07:05.261311+00:00. floatinghotel `45b3ecdbb` (dirty) binary `74bf2e253d09`; px0 `5743e84e6` px0 0.1.1 (darwin/arm64) binary `2dc66dd00540`; Google Chrome 154.0.8037.98; macOS 26.6.2 on Mac16,12 (Apple M4). Fixture `ee948260a`.

Times are milliseconds from the input reaching the app to the first frame showing the destination (CPU completion, not presentation). Warm samples are repetitions after the first in a launch; each launch is a fresh process.

## Predeclared journey

quick_open / warm / 100 / wide: floatinghotel p95 42.4 ms vs px0 135.4 ms (n = 40 / 40); ratio 0.31, 95% CI 0.07-1.55. Rule: floatinghotel p95 at least 20% lower with the whole 95% CI of the ratio below 0.80. **No win claimed.**

## Every journey

| app | step | temp | zoom | window | n | failed | p50 | p95 (95% CI) | max |
|---|---|---|---:|---|---:|---:|---:|---:|---:|
| floatinghotel | back | cold | 100 | wide | 10 | 0 | 12.9 | 61.1 (14.1-88.0) | 88.0 |
| floatinghotel | back | warm | 100 | wide | 40 | 0 | 13.6 | 27.6 (25.7-31.3) | 31.3 |
| floatinghotel | decoy | cold | 100 | wide | 10 | 0 | 192.4 | 319.7 (199.6-325.2) | 325.2 |
| floatinghotel | decoy | warm | 100 | wide | 40 | 0 | 179.9 | 249.3 (196.9-527.7) | 527.7 |
| floatinghotel | far_page_text | cold | 100 | wide | 10 | 0 | 71.2 | 94.0 (81.9-97.1) | 97.1 |
| floatinghotel | far_page_text | warm | 100 | wide | 40 | 0 | 71.1 | 116.5 (98.7-136.6) | 136.6 |
| floatinghotel | find_later_page | cold | 100 | wide | 10 | 0 | 412.6 | 533.2 (465.1-538.7) | 538.7 |
| floatinghotel | find_later_page | warm | 100 | wide | 40 | 0 | 381.6 | 591.8 (485.4-652.7) | 652.7 |
| floatinghotel | forward | cold | 100 | wide | 10 | 0 | 13.0 | 28.7 (17.0-33.8) | 33.8 |
| floatinghotel | forward | warm | 100 | wide | 40 | 0 | 15.5 | 28.9 (24.5-92.1) | 92.1 |
| floatinghotel | goto_line_far | cold | 100 | wide | 10 | 0 | 521.6 | 815.4 (554.3-971.0) | 971.0 |
| floatinghotel | goto_line_far | warm | 100 | wide | 40 | 0 | 541.0 | 697.3 (657.6-870.6) | 870.6 |
| floatinghotel | markdown_preview | cold | 100 | wide | 10 | 0 | 13.4 | 24.0 (17.0-25.4) | 25.4 |
| floatinghotel | markdown_preview | warm | 100 | wide | 40 | 0 | 11.6 | 26.5 (17.0-56.0) | 56.0 |
| floatinghotel | open_big | cold | 100 | wide | 10 | 0 | 49.2 | 100.0 (54.4-122.4) | 122.4 |
| floatinghotel | open_big | warm | 100 | wide | 40 | 0 | 44.9 | 59.1 (54.0-86.1) | 86.1 |
| floatinghotel | quick_open | cold | 100 | wide | 10 | 0 | 17.6 | 39.0 (23.0-47.4) | 47.4 |
| floatinghotel | quick_open | warm | 100 | wide | 40 | 0 | 16.9 | 42.4 (21.8-199.8) | 199.8 |
| floatinghotel | search_to_line | cold | 100 | wide | 10 | 0 | 11.8 | 70.5 (14.5-107.6) | 107.6 |
| floatinghotel | search_to_line | warm | 100 | wide | 40 | 0 | 8.5 | 20.6 (14.0-24.4) | 24.4 |
| floatinghotel | tab_a | cold | 100 | wide | 10 | 0 | 24.0 | 28.6 (24.8-30.8) | 30.8 |
| floatinghotel | tab_a | warm | 100 | wide | 40 | 0 | 20.3 | 32.1 (28.3-56.8) | 56.8 |
| floatinghotel | tab_a_again | cold | 100 | wide | 10 | 0 | 19.6 | 37.0 (21.2-41.4) | 41.4 |
| floatinghotel | tab_a_again | warm | 100 | wide | 40 | 0 | 19.6 | 31.3 (25.7-64.2) | 64.2 |
| floatinghotel | tab_b | cold | 100 | wide | 10 | 0 | 21.1 | 29.1 (24.3-29.5) | 29.5 |
| floatinghotel | tab_b | warm | 100 | wide | 40 | 0 | 22.0 | 38.3 (28.3-49.6) | 49.6 |
| floatinghotel | tree_open | cold | 100 | wide | 10 | 0 | 15.9 | 29.4 (20.9-32.0) | 32.0 |
| floatinghotel | tree_open | warm | 100 | wide | 40 | 0 | 9.6 | 21.3 (15.6-797.7) | 797.7 |
| px0 | back | cold | 100 | wide | 10 | 0 | 26.8 | 39.7 (27.2-47.6) | 47.6 |
| px0 | back | warm | 100 | wide | 40 | 0 | 25.3 | 31.5 (27.1-32.5) | 32.5 |
| px0 | far_page_syntax | cold | 100 | wide | 10 | 0 | 42.5 | 145.5 (46.8-223.3) | 223.3 |
| px0 | far_page_syntax | warm | 100 | wide | 40 | 0 | 44.4 | 53.2 (51.2-66.2) | 66.2 |
| px0 | far_page_text | cold | 100 | wide | 10 | 0 | 42.5 | 145.5 (46.8-223.3) | 223.3 |
| px0 | far_page_text | warm | 100 | wide | 40 | 0 | 44.3 | 53.2 (51.1-66.1) | 66.1 |
| px0 | find_later_page | cold | 100 | wide | 10 | 0 | 224.2 | 362.9 (227.9-450.1) | 450.1 |
| px0 | find_later_page | warm | 100 | wide | 40 | 0 | 219.1 | 268.4 (251.3-522.7) | 522.7 |
| px0 | forward | cold | 100 | wide | 10 | 0 | 26.0 | 30.1 (26.9-31.5) | 31.5 |
| px0 | forward | warm | 100 | wide | 40 | 0 | 25.3 | 27.4 (26.9-28.2) | 28.2 |
| px0 | goto_line_far | cold | 100 | wide | 10 | 0 | 51.8 | 72.5 (60.0-78.0) | 78.0 |
| px0 | goto_line_far | warm | 100 | wide | 40 | 0 | 51.2 | 67.5 (61.4-96.2) | 96.2 |
| px0 | markdown_preview | cold | 100 | wide | 10 | 0 | 23.7 | 760.2 (29.0-1354.1) | 1354.1 |
| px0 | markdown_preview | warm | 100 | wide | 40 | 0 | 14.3 | 57.2 (31.4-215.6) | 215.6 |
| px0 | open_big | cold | 100 | wide | 10 | 0 | 106.8 | 168.2 (135.0-180.9) | 180.9 |
| px0 | open_big | warm | 100 | wide | 40 | 0 | 100.1 | 240.2 (125.5-1319.6) | 1319.6 |
| px0 | quick_open | cold | 100 | wide | 10 | 0 | 58.1 | 111.9 (72.2-136.0) | 136.0 |
| px0 | quick_open | warm | 100 | wide | 40 | 0 | 52.7 | 135.4 (91.5-441.6) | 441.6 |
| px0 | search_api | cold | 100 | wide | 10 | 0 | 42.7 | 816.7 (82.7-1382.1) | 1382.1 |
| px0 | search_api | warm | 100 | wide | 40 | 0 | 19.2 | 216.4 (150.5-593.0) | 593.0 |
| px0 | tab_a | cold | 100 | wide | 10 | 0 | 4.9 | 8.4 (6.6-8.5) | 8.5 |
| px0 | tab_a | warm | 100 | wide | 40 | 0 | 5.8 | 8.1 (7.9-8.7) | 8.7 |
| px0 | tab_a_again | cold | 100 | wide | 10 | 0 | 5.4 | 8.5 (6.6-8.7) | 8.7 |
| px0 | tab_a_again | warm | 100 | wide | 40 | 0 | 6.6 | 11.7 (9.2-13.6) | 13.6 |
| px0 | tab_b | cold | 100 | wide | 10 | 0 | 5.2 | 9.8 (6.7-11.7) | 11.7 |
| px0 | tab_b | warm | 100 | wide | 40 | 0 | 5.2 | 9.2 (8.4-9.5) | 9.5 |
| px0 | tree_open | cold | 100 | wide | 10 | 0 | 66.7 | 113.9 (87.1-123.1) | 123.1 |
| px0 | tree_open | warm | 100 | wide | 40 | 0 | 54.3 | 143.6 (72.3-338.8) | 338.8 |

## Failures and timeouts

None.

## Frames

floatinghotel: CPU frame time, 10 blocks of 100 frames each after a long scroll. px0: animation-frame interval while scrolling (the page cannot see CPU time per frame); not comparable.

- px0: per-launch p99 18.0-18.5 ms over 10 launches
- floatinghotel: per-launch p99 4.9-128.7 ms over 10 launches

## Stress correctness

| case | floatinghotel | px0 |
|---|---|---|
| changed_between_pages | pass | FAIL: far rows rewritten: true; top rows original: true |
| close_during_load | pass | n/a: not applicable: no tab exists while loading; the close hit the previous tab, then big.txt opened (tabs: alpha.cpp, beta.cpp, big.txt, gamma.cpp) |
| copied_bytes | pass | - |
| evicted_reopen | pass | pass |
| rapid_aba | pass | pass |
| rapid_aba_no_stale | pass | - |
| same_size_same_mtime | pass | pass |
| superseded_query | - | pass |
| superseded_search | pass | - |
| syntax_far_page | pass | - |

## Memory (RSS, 20 tabs, scroll, close, 30 s idle)

Summed resident set of every process the app uses; shared pages are counted once per process, which overstates multi-process totals.

| app | phase | total MB | processes | largest components |
|---|---|---:|---:|---|
