# floatinghotel vs px0

Recorded 2026-10-06T13:01:43.355618+00:00. floatinghotel `45b3ecdbb` (dirty) binary `74bf2e253d09`; px0 `5743e84e6` px0 0.1.1 (darwin/arm64) binary `2dc66dd00540`; Google Chrome 154.0.8037.98; macOS 26.6.2 on Mac16,12 (Apple M4). Fixture `5ef2cf46e`.

Times are milliseconds from the input reaching the app to the first frame showing the destination (CPU completion, not presentation). Warm samples are repetitions after the first in a launch; each launch is a fresh process.

## Predeclared journey

quick_open / warm / 100 / wide: floatinghotel p95 20.8 ms vs px0 101.2 ms (n = 120 / 120); ratio 0.21, 95% CI 0.16-0.30. Rule: floatinghotel p95 at least 20% lower with the whole 95% CI of the ratio below 0.80. **Win.**

## Every journey

| app | step | temp | zoom | window | n | failed | p50 | p95 (95% CI) | max |
|---|---|---|---:|---|---:|---:|---:|---:|---:|
| floatinghotel | back | cold | 100 | flask | 5 | 0 | 13.0 | 14.5 (12.9-14.6) | 14.6 |
| floatinghotel | back | cold | 100 | narrow | 3 | 0 | 7.4 | 7.4 (---) | 7.4 |
| floatinghotel | back | cold | 100 | react | 5 | 0 | 5.0 | 9.6 (5.0-10.6) | 10.6 |
| floatinghotel | back | cold | 100 | redis | 5 | 0 | 34.2 | 49.4 (34.2-50.8) | 50.8 |
| floatinghotel | back | cold | 100 | typescript | 5 | 0 | 39.6 | 54.4 (39.6-55.0) | 55.0 |
| floatinghotel | back | cold | 100 | wide | 30 | 0 | 6.9 | 17.8 (12.3-25.3) | 25.3 |
| floatinghotel | back | cold | 140 | wide | 3 | 0 | 4.9 | 6.5 (---) | 6.7 |
| floatinghotel | back | cold | 200 | wide | 3 | 0 | 4.1 | 7.9 (---) | 8.3 |
| floatinghotel | back | warm | 100 | flask | 20 | 0 | 14.4 | 23.9 (19.6-28.9) | 28.9 |
| floatinghotel | back | warm | 100 | narrow | 12 | 0 | 6.8 | 11.1 (7.4-12.1) | 12.1 |
| floatinghotel | back | warm | 100 | react | 20 | 0 | 5.4 | 10.9 (9.1-13.5) | 13.5 |
| floatinghotel | back | warm | 100 | redis | 20 | 0 | 32.8 | 48.8 (42.5-49.4) | 49.4 |
| floatinghotel | back | warm | 100 | typescript | 20 | 0 | 42.0 | 53.5 (46.6-62.3) | 62.3 |
| floatinghotel | back | warm | 100 | wide | 120 | 0 | 8.5 | 18.3 (16.8-23.1) | 36.0 |
| floatinghotel | back | warm | 140 | wide | 12 | 0 | 5.2 | 8.4 (5.3-11.2) | 11.2 |
| floatinghotel | back | warm | 200 | wide | 12 | 0 | 4.3 | 8.1 (5.1-11.2) | 11.2 |
| floatinghotel | decoy | cold | 100 | narrow | 3 | 0 | 185.1 | 312.9 (---) | 327.1 |
| floatinghotel | decoy | cold | 100 | wide | 30 | 0 | 186.4 | 243.8 (209.9-323.2) | 323.2 |
| floatinghotel | decoy | cold | 140 | wide | 3 | 0 | 160.9 | 204.8 (---) | 209.7 |
| floatinghotel | decoy | cold | 200 | wide | 3 | 0 | 195.2 | 206.6 (---) | 207.9 |
| floatinghotel | decoy | warm | 100 | narrow | 12 | 0 | 145.0 | 183.3 (165.9-194.5) | 194.5 |
| floatinghotel | decoy | warm | 100 | wide | 120 | 0 | 171.7 | 205.8 (191.4-226.8) | 249.9 |
| floatinghotel | decoy | warm | 140 | wide | 12 | 0 | 184.2 | 211.7 (187.1-231.1) | 231.1 |
| floatinghotel | decoy | warm | 200 | wide | 12 | 0 | 188.1 | 211.8 (198.2-216.1) | 216.1 |
| floatinghotel | find_later_page | cold | 100 | narrow | 3 | 0 | 315.7 | 549.4 (---) | 575.3 |
| floatinghotel | find_later_page | cold | 100 | wide | 30 | 0 | 353.0 | 431.1 (386.9-528.8) | 528.8 |
| floatinghotel | find_later_page | cold | 140 | wide | 3 | 0 | 252.0 | 392.7 (---) | 408.3 |
| floatinghotel | find_later_page | cold | 200 | wide | 3 | 0 | 360.0 | 381.9 (---) | 384.4 |
| floatinghotel | find_later_page | warm | 100 | narrow | 12 | 0 | 301.6 | 355.8 (329.4-359.6) | 359.6 |
| floatinghotel | find_later_page | warm | 100 | wide | 120 | 0 | 349.7 | 437.7 (398.9-477.4) | 697.8 |
| floatinghotel | find_later_page | warm | 140 | wide | 12 | 0 | 302.1 | 352.6 (326.6-354.4) | 354.4 |
| floatinghotel | find_later_page | warm | 200 | wide | 12 | 0 | 339.7 | 454.8 (351.0-574.2) | 574.2 |
| floatinghotel | markdown_preview | cold | 100 | flask | 5 | 0 | 5.4 | 8.3 (5.4-9.0) | 9.0 |
| floatinghotel | markdown_preview | cold | 100 | narrow | 3 | 0 | 9.0 | 12.5 (---) | 12.9 |
| floatinghotel | markdown_preview | cold | 100 | react | 5 | 0 | 10.7 | 18.5 (10.2-19.9) | 19.9 |
| floatinghotel | markdown_preview | cold | 100 | redis | 5 | 0 | 25.1 | 32.0 (24.9-32.1) | 32.1 |
| floatinghotel | markdown_preview | cold | 100 | typescript | 5 | 0 | 5.4 | 6.7 (5.2-7.0) | 7.0 |
| floatinghotel | markdown_preview | cold | 100 | wide | 30 | 0 | 13.8 | 19.3 (16.9-23.2) | 23.2 |
| floatinghotel | markdown_preview | cold | 140 | wide | 3 | 0 | 7.5 | 13.0 (---) | 13.6 |
| floatinghotel | markdown_preview | cold | 200 | wide | 3 | 0 | 7.5 | 9.2 (---) | 9.4 |
| floatinghotel | markdown_preview | warm | 100 | flask | 20 | 0 | 3.3 | 4.7 (4.1-5.2) | 5.2 |
| floatinghotel | markdown_preview | warm | 100 | narrow | 12 | 0 | 6.4 | 9.4 (7.0-9.9) | 9.9 |
| floatinghotel | markdown_preview | warm | 100 | react | 20 | 0 | 4.6 | 7.0 (6.2-8.7) | 8.7 |
| floatinghotel | markdown_preview | warm | 100 | redis | 20 | 0 | 23.8 | 46.8 (28.8-54.5) | 54.5 |
| floatinghotel | markdown_preview | warm | 100 | typescript | 20 | 0 | 2.6 | 4.4 (3.5-7.6) | 7.6 |
| floatinghotel | markdown_preview | warm | 100 | wide | 120 | 0 | 8.5 | 15.3 (12.8-19.0) | 22.2 |
| floatinghotel | markdown_preview | warm | 140 | wide | 12 | 0 | 5.1 | 9.1 (5.6-9.2) | 9.2 |
| floatinghotel | markdown_preview | warm | 200 | wide | 12 | 0 | 4.4 | 8.7 (5.6-8.8) | 8.8 |
| floatinghotel | open_big | cold | 100 | flask | 5 | 0 | 18.2 | 22.1 (18.0-23.0) | 23.0 |
| floatinghotel | open_big | cold | 100 | narrow | 3 | 0 | 37.9 | 39.8 (---) | 40.0 |
| floatinghotel | open_big | cold | 100 | react | 5 | 0 | 67.4 | 119.1 (60.6-131.3) | 131.3 |
| floatinghotel | open_big | cold | 100 | redis | 5 | 0 | 4.2 | 8.9 (4.2-9.2) | 9.2 |
| floatinghotel | open_big | cold | 100 | typescript | 5 | 0 | 49.7 | 73.0 (47.2-77.6) | 77.6 |
| floatinghotel | open_big | cold | 100 | wide | 30 | 0 | 40.7 | 54.8 (49.0-74.7) | 74.7 |
| floatinghotel | open_big | cold | 140 | wide | 3 | 0 | 34.0 | 38.9 (---) | 39.4 |
| floatinghotel | open_big | cold | 200 | wide | 3 | 0 | 36.3 | 38.1 (---) | 38.3 |
| floatinghotel | open_big | warm | 100 | flask | 20 | 0 | 18.2 | 24.4 (20.5-25.6) | 25.6 |
| floatinghotel | open_big | warm | 100 | narrow | 12 | 0 | 35.2 | 44.5 (38.8-45.5) | 45.5 |
| floatinghotel | open_big | warm | 100 | react | 20 | 0 | 38.1 | 51.0 (43.6-53.4) | 53.4 |
| floatinghotel | open_big | warm | 100 | redis | 20 | 0 | 5.1 | 8.8 (6.4-10.0) | 10.0 |
| floatinghotel | open_big | warm | 100 | typescript | 20 | 0 | 35.5 | 50.6 (45.6-55.0) | 55.0 |
| floatinghotel | open_big | warm | 100 | wide | 120 | 0 | 40.7 | 56.3 (50.4-63.9) | 69.8 |
| floatinghotel | open_big | warm | 140 | wide | 12 | 0 | 33.0 | 55.7 (35.4-61.3) | 61.3 |
| floatinghotel | open_big | warm | 200 | wide | 12 | 0 | 39.0 | 43.9 (40.6-44.3) | 44.3 |
| floatinghotel | quick_open | cold | 100 | flask | 5 | 0 | 17.8 | 21.5 (17.8-22.0) | 22.0 |
| floatinghotel | quick_open | cold | 100 | narrow | 3 | 0 | 12.4 | 13.6 (---) | 13.7 |
| floatinghotel | quick_open | cold | 100 | react | 5 | 0 | 7.0 | 59.9 (7.0-71.1) | 71.1 |
| floatinghotel | quick_open | cold | 100 | redis | 5 | 0 | 44.5 | 52.6 (43.9-53.3) | 53.3 |
| floatinghotel | quick_open | cold | 100 | typescript | 5 | 0 | 39.5 | 58.1 (39.2-58.5) | 58.5 |
| floatinghotel | quick_open | cold | 100 | wide | 30 | 0 | 16.1 | 21.3 (19.3-27.2) | 27.2 |
| floatinghotel | quick_open | cold | 140 | wide | 3 | 0 | 9.9 | 10.1 (---) | 10.1 |
| floatinghotel | quick_open | cold | 200 | wide | 3 | 0 | 10.5 | 11.9 (---) | 12.0 |
| floatinghotel | quick_open | warm | 100 | flask | 20 | 0 | 20.2 | 24.5 (22.3-37.6) | 37.6 |
| floatinghotel | quick_open | warm | 100 | narrow | 12 | 0 | 11.0 | 15.9 (14.1-16.1) | 16.1 |
| floatinghotel | quick_open | warm | 100 | react | 20 | 0 | 12.2 | 19.3 (14.3-24.9) | 24.9 |
| floatinghotel | quick_open | warm | 100 | redis | 20 | 0 | 42.9 | 53.1 (48.5-62.7) | 62.7 |
| floatinghotel | quick_open | warm | 100 | typescript | 20 | 0 | 38.6 | 55.8 (53.0-57.9) | 57.9 |
| floatinghotel | quick_open | warm | 100 | wide | 120 | 0 | 14.0 | 20.8 (18.5-25.0) | 28.9 |
| floatinghotel | quick_open | warm | 140 | wide | 12 | 0 | 7.7 | 15.2 (8.3-19.9) | 19.9 |
| floatinghotel | quick_open | warm | 200 | wide | 12 | 0 | 6.9 | 12.9 (9.6-13.2) | 13.2 |
| floatinghotel | quick_open_a | cold | 100 | flask | 5 | 0 | 13.8 | 19.4 (13.6-20.0) | 20.0 |
| floatinghotel | quick_open_a | cold | 100 | react | 5 | 0 | 12.3 | 19.2 (11.5-20.8) | 20.8 |
| floatinghotel | quick_open_a | cold | 100 | redis | 5 | 0 | 46.1 | 64.0 (46.1-68.1) | 68.1 |
| floatinghotel | quick_open_a | cold | 100 | typescript | 5 | 0 | 47.5 | 59.9 (46.4-61.1) | 61.1 |
| floatinghotel | quick_open_a | warm | 100 | flask | 20 | 0 | 10.6 | 19.1 (13.4-20.3) | 20.3 |
| floatinghotel | quick_open_a | warm | 100 | react | 20 | 0 | 9.2 | 17.3 (12.6-27.9) | 27.9 |
| floatinghotel | quick_open_a | warm | 100 | redis | 20 | 0 | 46.9 | 58.6 (53.6-107.5) | 107.5 |
| floatinghotel | quick_open_a | warm | 100 | typescript | 20 | 0 | 45.1 | 65.0 (59.4-83.8) | 83.8 |
| floatinghotel | search_to_line | cold | 100 | narrow | 3 | 0 | 5.5 | 7.0 (---) | 7.2 |
| floatinghotel | search_to_line | cold | 100 | wide | 30 | 0 | 8.1 | 12.3 (11.4-14.4) | 14.4 |
| floatinghotel | search_to_line | cold | 140 | wide | 3 | 0 | 5.2 | 6.8 (---) | 7.0 |
| floatinghotel | search_to_line | cold | 200 | wide | 3 | 0 | 4.6 | 5.2 (---) | 5.3 |
| floatinghotel | search_to_line | warm | 100 | narrow | 12 | 0 | 4.6 | 6.2 (5.6-6.6) | 6.6 |
| floatinghotel | search_to_line | warm | 100 | wide | 120 | 0 | 5.7 | 14.2 (11.7-18.5) | 36.1 |
| floatinghotel | search_to_line | warm | 140 | wide | 12 | 0 | 3.4 | 5.6 (3.9-6.2) | 6.2 |
| floatinghotel | search_to_line | warm | 200 | wide | 12 | 0 | 3.4 | 5.5 (3.7-7.2) | 7.2 |
| floatinghotel | tab_a | cold | 100 | flask | 5 | 0 | 10.2 | 16.4 (10.0-16.5) | 16.5 |
| floatinghotel | tab_a | cold | 100 | narrow | 3 | 0 | 13.5 | 17.0 (---) | 17.3 |
| floatinghotel | tab_a | cold | 100 | react | 5 | 0 | 7.2 | 15.7 (7.2-16.3) | 16.3 |
| floatinghotel | tab_a | cold | 100 | redis | 5 | 0 | 38.7 | 51.3 (38.4-52.5) | 52.5 |
| floatinghotel | tab_a | cold | 100 | typescript | 5 | 0 | 42.0 | 57.2 (41.9-58.2) | 58.2 |
| floatinghotel | tab_a | cold | 100 | wide | 30 | 0 | 13.4 | 23.6 (20.0-27.0) | 27.0 |
| floatinghotel | tab_a | cold | 140 | wide | 3 | 0 | 9.2 | 16.9 (---) | 17.8 |
| floatinghotel | tab_a | cold | 200 | wide | 3 | 0 | 7.6 | 7.7 (---) | 7.7 |
| floatinghotel | tab_a | warm | 100 | flask | 20 | 0 | 11.2 | 21.4 (17.4-27.4) | 27.4 |
| floatinghotel | tab_a | warm | 100 | narrow | 12 | 0 | 11.2 | 21.1 (13.9-23.1) | 23.1 |
| floatinghotel | tab_a | warm | 100 | react | 20 | 0 | 10.3 | 21.9 (17.0-63.5) | 63.5 |
| floatinghotel | tab_a | warm | 100 | redis | 20 | 0 | 37.4 | 54.1 (47.0-58.7) | 58.7 |
| floatinghotel | tab_a | warm | 100 | typescript | 20 | 0 | 43.1 | 50.0 (48.5-56.3) | 56.3 |
| floatinghotel | tab_a | warm | 100 | wide | 120 | 0 | 14.5 | 28.6 (26.5-33.5) | 41.1 |
| floatinghotel | tab_a | warm | 140 | wide | 12 | 0 | 9.5 | 19.1 (10.7-22.0) | 22.0 |
| floatinghotel | tab_a | warm | 200 | wide | 12 | 0 | 8.7 | 61.6 (10.2-117.1) | 117.1 |
| floatinghotel | tab_a_again | cold | 100 | flask | 5 | 0 | 9.0 | 10.9 (9.0-11.1) | 11.1 |
| floatinghotel | tab_a_again | cold | 100 | narrow | 3 | 0 | 10.4 | 12.2 (---) | 12.3 |
| floatinghotel | tab_a_again | cold | 100 | react | 5 | 0 | 7.2 | 10.9 (7.0-11.0) | 11.0 |
| floatinghotel | tab_a_again | cold | 100 | redis | 5 | 0 | 35.5 | 62.9 (35.5-66.6) | 66.6 |
| floatinghotel | tab_a_again | cold | 100 | typescript | 5 | 0 | 43.5 | 58.2 (43.2-60.5) | 60.5 |
| floatinghotel | tab_a_again | cold | 100 | wide | 30 | 0 | 10.3 | 24.0 (17.2-33.2) | 33.2 |
| floatinghotel | tab_a_again | cold | 140 | wide | 3 | 0 | 7.1 | 12.4 (---) | 13.0 |
| floatinghotel | tab_a_again | cold | 200 | wide | 3 | 0 | 5.6 | 6.7 (---) | 6.9 |
| floatinghotel | tab_a_again | warm | 100 | flask | 20 | 0 | 10.1 | 18.4 (13.6-23.6) | 23.6 |
| floatinghotel | tab_a_again | warm | 100 | narrow | 12 | 0 | 9.2 | 14.4 (10.8-16.0) | 16.0 |
| floatinghotel | tab_a_again | warm | 100 | react | 20 | 0 | 8.0 | 16.6 (10.7-18.0) | 18.0 |
| floatinghotel | tab_a_again | warm | 100 | redis | 20 | 0 | 35.0 | 52.6 (41.3-61.9) | 61.9 |
| floatinghotel | tab_a_again | warm | 100 | typescript | 20 | 0 | 42.8 | 58.9 (49.3-62.7) | 62.7 |
| floatinghotel | tab_a_again | warm | 100 | wide | 120 | 0 | 12.6 | 25.4 (24.1-28.2) | 60.0 |
| floatinghotel | tab_a_again | warm | 140 | wide | 12 | 0 | 7.2 | 12.9 (8.2-16.7) | 16.7 |
| floatinghotel | tab_a_again | warm | 200 | wide | 12 | 0 | 6.3 | 11.7 (8.4-11.7) | 11.7 |
| floatinghotel | tab_b | cold | 100 | flask | 5 | 0 | 18.0 | 21.5 (17.6-21.9) | 21.9 |
| floatinghotel | tab_b | cold | 100 | narrow | 3 | 0 | 9.5 | 12.9 (---) | 13.2 |
| floatinghotel | tab_b | cold | 100 | react | 5 | 0 | 7.4 | 15.6 (7.2-17.1) | 17.1 |
| floatinghotel | tab_b | cold | 100 | redis | 5 | 0 | 35.7 | 50.5 (35.7-53.9) | 53.9 |
| floatinghotel | tab_b | cold | 100 | typescript | 5 | 0 | 41.6 | 50.1 (41.5-50.4) | 50.4 |
| floatinghotel | tab_b | cold | 100 | wide | 30 | 0 | 10.5 | 21.9 (19.4-23.7) | 23.7 |
| floatinghotel | tab_b | cold | 140 | wide | 3 | 0 | 8.0 | 10.8 (---) | 11.1 |
| floatinghotel | tab_b | cold | 200 | wide | 3 | 0 | 6.5 | 7.1 (---) | 7.2 |
| floatinghotel | tab_b | warm | 100 | flask | 20 | 0 | 18.1 | 29.8 (23.6-47.8) | 47.8 |
| floatinghotel | tab_b | warm | 100 | narrow | 12 | 0 | 11.2 | 22.6 (13.4-23.0) | 23.0 |
| floatinghotel | tab_b | warm | 100 | react | 20 | 0 | 11.0 | 18.6 (14.6-40.8) | 40.8 |
| floatinghotel | tab_b | warm | 100 | redis | 20 | 0 | 36.2 | 55.4 (40.7-64.6) | 64.6 |
| floatinghotel | tab_b | warm | 100 | typescript | 20 | 0 | 42.2 | 53.6 (48.0-55.0) | 55.0 |
| floatinghotel | tab_b | warm | 100 | wide | 120 | 0 | 13.2 | 28.6 (25.2-29.6) | 39.7 |
| floatinghotel | tab_b | warm | 140 | wide | 12 | 0 | 7.9 | 16.2 (9.5-23.2) | 23.2 |
| floatinghotel | tab_b | warm | 200 | wide | 12 | 0 | 7.4 | 12.0 (9.3-12.5) | 12.5 |
| floatinghotel | tree_open | cold | 100 | narrow | 3 | 0 | 11.3 | 15.0 (---) | 15.5 |
| floatinghotel | tree_open | cold | 100 | wide | 30 | 0 | 11.9 | 18.6 (16.5-25.4) | 25.4 |
| floatinghotel | tree_open | cold | 140 | wide | 3 | 0 | 12.1 | 14.8 (---) | 15.1 |
| floatinghotel | tree_open | cold | 200 | wide | 3 | 0 | 6.4 | 7.8 (---) | 7.9 |
| floatinghotel | tree_open | warm | 100 | narrow | 12 | 0 | 5.4 | 9.8 (6.3-11.8) | 11.8 |
| floatinghotel | tree_open | warm | 100 | wide | 120 | 0 | 6.3 | 14.6 (12.2-26.7) | 41.3 |
| floatinghotel | tree_open | warm | 140 | wide | 12 | 0 | 3.7 | 8.9 (4.3-11.7) | 11.7 |
| floatinghotel | tree_open | warm | 200 | wide | 12 | 0 | 3.8 | 6.8 (5.2-7.2) | 7.2 |
| px0 | back | cold | 100 | flask | 5 | 0 | 8.3 | 8.8 (8.3-8.8) | 8.8 |
| px0 | back | cold | 100 | narrow | 3 | 0 | 27.5 | 27.7 (---) | 27.7 |
| px0 | back | cold | 100 | react | 5 | 0 | 10.0 | 10.7 (10.0-10.8) | 10.8 |
| px0 | back | cold | 100 | redis | 5 | 0 | 9.6 | 10.3 (9.6-10.4) | 10.4 |
| px0 | back | cold | 100 | typescript | 5 | 0 | 8.7 | 9.1 (8.6-9.1) | 9.1 |
| px0 | back | cold | 100 | wide | 30 | 0 | 25.0 | 27.3 (26.7-27.8) | 27.8 |
| px0 | back | cold | 140 | wide | 3 | 0 | 26.2 | 27.5 (---) | 27.6 |
| px0 | back | cold | 200 | wide | 3 | 0 | 25.5 | 26.6 (---) | 26.7 |
| px0 | back | warm | 100 | flask | 20 | 0 | 7.7 | 13.2 (9.1-27.0) | 27.0 |
| px0 | back | warm | 100 | narrow | 12 | 0 | 26.1 | 27.6 (26.6-27.7) | 27.7 |
| px0 | back | warm | 100 | react | 20 | 0 | 10.4 | 11.7 (11.0-15.8) | 15.8 |
| px0 | back | warm | 100 | redis | 20 | 0 | 10.2 | 11.7 (11.0-13.8) | 13.8 |
| px0 | back | warm | 100 | typescript | 20 | 0 | 9.3 | 12.2 (9.8-12.7) | 12.7 |
| px0 | back | warm | 100 | wide | 120 | 0 | 25.8 | 30.8 (29.7-31.4) | 46.9 |
| px0 | back | warm | 140 | wide | 12 | 0 | 25.0 | 28.2 (25.9-29.9) | 29.9 |
| px0 | back | warm | 200 | wide | 12 | 0 | 26.6 | 29.1 (26.9-31.4) | 31.4 |
| px0 | find_later_page | cold | 100 | narrow | 3 | 0 | 203.1 | 204.3 (---) | 204.4 |
| px0 | find_later_page | cold | 100 | wide | 30 | 0 | 225.4 | 267.4 (243.1-319.8) | 319.8 |
| px0 | find_later_page | cold | 140 | wide | 3 | 0 | 199.6 | 210.1 (---) | 211.3 |
| px0 | find_later_page | cold | 200 | wide | 3 | 0 | 199.4 | 219.7 (---) | 221.9 |
| px0 | find_later_page | warm | 100 | narrow | 12 | 0 | 200.0 | 208.7 (203.2-209.7) | 209.7 |
| px0 | find_later_page | warm | 100 | wide | 120 | 0 | 207.0 | 282.4 (253.5-321.1) | 356.5 |
| px0 | find_later_page | warm | 140 | wide | 12 | 0 | 211.3 | 273.0 (220.7-283.4) | 283.4 |
| px0 | find_later_page | warm | 200 | wide | 12 | 0 | 202.9 | 256.3 (208.1-300.9) | 300.9 |
| px0 | markdown_preview | cold | 100 | flask | 5 | 0 | 10.6 | 20.4 (9.7-22.5) | 22.5 |
| px0 | markdown_preview | cold | 100 | narrow | 3 | 0 | 15.5 | 19.2 (---) | 19.6 |
| px0 | markdown_preview | cold | 100 | react | 5 | 0 | 15.4 | 26.6 (15.0-28.5) | 28.5 |
| px0 | markdown_preview | cold | 100 | redis | 5 | 0 | 20.7 | 25.4 (19.2-25.7) | 25.7 |
| px0 | markdown_preview | cold | 100 | typescript | 5 | 0 | 18.3 | 37.7 (16.3-40.6) | 40.6 |
| px0 | markdown_preview | cold | 100 | wide | 30 | 0 | 25.5 | 56.3 (36.9-379.7) | 379.7 |
| px0 | markdown_preview | cold | 140 | wide | 3 | 0 | 14.8 | 18.7 (---) | 19.1 |
| px0 | markdown_preview | cold | 200 | wide | 3 | 0 | 7.4 | 9.5 (---) | 9.7 |
| px0 | markdown_preview | warm | 100 | flask | 20 | 0 | 11.7 | 20.9 (16.5-38.9) | 38.9 |
| px0 | markdown_preview | warm | 100 | narrow | 12 | 0 | 12.0 | 25.0 (16.3-25.8) | 25.8 |
| px0 | markdown_preview | warm | 100 | react | 20 | 0 | 10.4 | 18.1 (14.0-19.1) | 19.1 |
| px0 | markdown_preview | warm | 100 | redis | 20 | 0 | 12.1 | 23.6 (17.3-30.8) | 30.8 |
| px0 | markdown_preview | warm | 100 | typescript | 20 | 0 | 11.5 | 16.3 (15.3-16.8) | 16.8 |
| px0 | markdown_preview | warm | 100 | wide | 120 | 0 | 17.0 | 53.9 (37.2-95.7) | 109.2 |
| px0 | markdown_preview | warm | 140 | wide | 12 | 0 | 12.5 | 127.8 (15.8-253.8) | 253.8 |
| px0 | markdown_preview | warm | 200 | wide | 12 | 0 | 10.3 | 20.7 (12.2-29.6) | 29.6 |
| px0 | open_big | cold | 100 | flask | 5 | 0 | 56.5 | 123.0 (55.1-135.6) | 135.6 |
| px0 | open_big | cold | 100 | narrow | 3 | 0 | 86.2 | 90.1 (---) | 90.5 |
| px0 | open_big | cold | 100 | react | 5 | 0 | 57.5 | 83.3 (57.1-89.6) | 89.6 |
| px0 | open_big | cold | 100 | redis | 5 | 0 | 1.9 | 15.1 (1.9-15.8) | 15.8 |
| px0 | open_big | cold | 100 | typescript | 5 | 0 | 49.1 | 474.8 (48.9-579.2) | 579.2 |
| px0 | open_big | cold | 100 | wide | 30 | 0 | 89.5 | 166.5 (122.5-551.5) | 551.5 |
| px0 | open_big | cold | 140 | wide | 3 | 0 | 84.9 | 85.1 (---) | 85.1 |
| px0 | open_big | cold | 200 | wide | 3 | 0 | 90.2 | 99.3 (---) | 100.3 |
| px0 | open_big | warm | 100 | flask | 20 | 0 | 58.4 | 97.5 (78.7-98.4) | 98.4 |
| px0 | open_big | warm | 100 | narrow | 12 | 0 | 78.7 | 105.0 (82.5-117.8) | 117.8 |
| px0 | open_big | warm | 100 | react | 20 | 0 | 50.2 | 102.2 (59.1-102.7) | 102.7 |
| px0 | open_big | warm | 100 | redis | 20 | 0 | 10.3 | 16.1 (14.8-16.5) | 16.5 |
| px0 | open_big | warm | 100 | typescript | 20 | 0 | 54.5 | 89.9 (71.2-285.1) | 285.1 |
| px0 | open_big | warm | 100 | wide | 120 | 0 | 90.0 | 144.3 (128.5-171.3) | 265.9 |
| px0 | open_big | warm | 140 | wide | 12 | 0 | 83.4 | 159.4 (92.5-161.9) | 161.9 |
| px0 | open_big | warm | 200 | wide | 12 | 0 | 84.5 | 104.1 (87.9-114.1) | 114.1 |
| px0 | quick_open | cold | 100 | flask | 5 | 0 | 78.1 | 154.5 (76.5-173.2) | 173.2 |
| px0 | quick_open | cold | 100 | narrow | 3 | 0 | 46.8 | 50.2 (---) | 50.6 |
| px0 | quick_open | cold | 100 | react | 5 | 0 | 51.8 | 108.4 (48.4-121.7) | 121.7 |
| px0 | quick_open | cold | 100 | redis | 5 | 0 | 63.6 | 76.7 (63.4-79.5) | 79.5 |
| px0 | quick_open | cold | 100 | typescript | 5 | 0 | 81.1 | 142.1 (80.9-150.1) | 150.1 |
| px0 | quick_open | cold | 100 | wide | 30 | 0 | 54.1 | 75.1 (65.6-245.0) | 245.0 |
| px0 | quick_open | cold | 140 | wide | 3 | 0 | 47.7 | 49.3 (---) | 49.5 |
| px0 | quick_open | cold | 200 | wide | 3 | 0 | 47.5 | 51.1 (---) | 51.5 |
| px0 | quick_open | warm | 100 | flask | 19 | 1 | 77.1 | 153.7 (142.1-162.6) | 162.6 |
| px0 | quick_open | warm | 100 | narrow | 12 | 0 | 46.3 | 61.1 (47.3-63.9) | 63.9 |
| px0 | quick_open | warm | 100 | react | 20 | 0 | 25.2 | 45.3 (35.3-52.1) | 52.1 |
| px0 | quick_open | warm | 100 | redis | 20 | 0 | 67.4 | 113.7 (89.6-128.6) | 128.6 |
| px0 | quick_open | warm | 100 | typescript | 20 | 0 | 118.5 | 170.8 (146.0-319.5) | 319.5 |
| px0 | quick_open | warm | 100 | wide | 120 | 0 | 51.1 | 101.2 (76.1-124.7) | 166.9 |
| px0 | quick_open | warm | 140 | wide | 12 | 0 | 48.7 | 66.1 (51.2-76.0) | 76.0 |
| px0 | quick_open | warm | 200 | wide | 12 | 0 | 48.8 | 57.8 (53.1-58.0) | 58.0 |
| px0 | quick_open_a | cold | 100 | flask | 5 | 0 | 67.9 | 116.0 (66.9-121.2) | 121.2 |
| px0 | quick_open_a | cold | 100 | react | 5 | 0 | 56.0 | 111.6 (54.7-123.1) | 123.1 |
| px0 | quick_open_a | cold | 100 | redis | 5 | 0 | 99.0 | 125.2 (98.1-128.4) | 128.4 |
| px0 | quick_open_a | cold | 100 | typescript | 5 | 0 | 147.1 | 195.0 (144.2-204.3) | 204.3 |
| px0 | quick_open_a | warm | 100 | flask | 20 | 0 | 61.3 | 130.0 (87.7-183.5) | 183.5 |
| px0 | quick_open_a | warm | 100 | react | 20 | 0 | 40.0 | 61.8 (49.4-79.1) | 79.1 |
| px0 | quick_open_a | warm | 100 | redis | 20 | 0 | 103.0 | 159.0 (135.1-170.4) | 170.4 |
| px0 | quick_open_a | warm | 100 | typescript | 20 | 0 | 170.3 | 390.5 (199.3-400.9) | 400.9 |
| px0 | search_api | cold | 100 | narrow | 3 | 0 | 24.3 | 29.6 (---) | 30.2 |
| px0 | search_api | cold | 100 | wide | 30 | 0 | 54.3 | 251.1 (109.1-321.9) | 321.9 |
| px0 | search_api | cold | 140 | wide | 3 | 0 | 15.1 | 24.2 (---) | 25.2 |
| px0 | search_api | cold | 200 | wide | 3 | 0 | 20.5 | 91.1 (---) | 99.0 |
| px0 | search_api | warm | 100 | narrow | 12 | 0 | 13.2 | 16.9 (13.3-20.3) | 20.3 |
| px0 | search_api | warm | 100 | wide | 120 | 0 | 14.8 | 171.7 (99.7-385.1) | 431.0 |
| px0 | search_api | warm | 140 | wide | 12 | 0 | 14.1 | 178.6 (24.2-271.3) | 271.3 |
| px0 | search_api | warm | 200 | wide | 12 | 0 | 14.5 | 34.5 (17.6-48.3) | 48.3 |
| px0 | tab_a | cold | 100 | flask | 5 | 0 | 4.9 | 5.7 (4.9-5.7) | 5.7 |
| px0 | tab_a | cold | 100 | narrow | 3 | 0 | 8.0 | 9.0 (---) | 9.1 |
| px0 | tab_a | cold | 100 | react | 5 | 0 | 5.8 | 6.8 (5.8-6.9) | 6.9 |
| px0 | tab_a | cold | 100 | redis | 5 | 0 | 6.5 | 7.3 (6.4-7.3) | 7.3 |
| px0 | tab_a | cold | 100 | typescript | 5 | 0 | 5.2 | 6.5 (5.2-6.6) | 6.6 |
| px0 | tab_a | cold | 100 | wide | 30 | 0 | 5.6 | 8.4 (7.7-9.3) | 9.3 |
| px0 | tab_a | cold | 140 | wide | 3 | 0 | 9.1 | 9.2 (---) | 9.2 |
| px0 | tab_a | cold | 200 | wide | 3 | 0 | 8.6 | 8.8 (---) | 8.8 |
| px0 | tab_a | warm | 100 | flask | 20 | 0 | 4.5 | 6.6 (6.1-6.6) | 6.6 |
| px0 | tab_a | warm | 100 | narrow | 12 | 0 | 7.7 | 9.0 (7.9-9.2) | 9.2 |
| px0 | tab_a | warm | 100 | react | 20 | 0 | 6.5 | 8.1 (7.0-8.6) | 8.6 |
| px0 | tab_a | warm | 100 | redis | 20 | 0 | 6.6 | 7.9 (7.4-8.4) | 8.4 |
| px0 | tab_a | warm | 100 | typescript | 20 | 0 | 4.6 | 6.5 (5.4-6.9) | 6.9 |
| px0 | tab_a | warm | 100 | wide | 120 | 0 | 5.2 | 9.1 (8.4-10.0) | 12.5 |
| px0 | tab_a | warm | 140 | wide | 12 | 0 | 7.3 | 8.8 (7.8-9.0) | 9.0 |
| px0 | tab_a | warm | 200 | wide | 12 | 0 | 8.4 | 9.2 (8.7-9.3) | 9.3 |
| px0 | tab_a_again | cold | 100 | flask | 5 | 0 | 6.3 | 7.5 (6.1-7.8) | 7.8 |
| px0 | tab_a_again | cold | 100 | narrow | 3 | 0 | 7.0 | 7.0 (---) | 7.0 |
| px0 | tab_a_again | cold | 100 | react | 5 | 0 | 6.6 | 7.4 (6.5-7.6) | 7.6 |
| px0 | tab_a_again | cold | 100 | redis | 5 | 0 | 7.1 | 8.5 (7.0-8.6) | 8.6 |
| px0 | tab_a_again | cold | 100 | typescript | 5 | 0 | 6.1 | 7.9 (6.0-8.3) | 8.3 |
| px0 | tab_a_again | cold | 100 | wide | 30 | 0 | 6.2 | 9.6 (7.8-10.9) | 10.9 |
| px0 | tab_a_again | cold | 140 | wide | 3 | 0 | 8.1 | 9.3 (---) | 9.4 |
| px0 | tab_a_again | cold | 200 | wide | 3 | 0 | 6.6 | 7.5 (---) | 7.6 |
| px0 | tab_a_again | warm | 100 | flask | 20 | 0 | 4.3 | 6.2 (5.6-6.6) | 6.6 |
| px0 | tab_a_again | warm | 100 | narrow | 12 | 0 | 7.1 | 8.1 (7.2-8.2) | 8.2 |
| px0 | tab_a_again | warm | 100 | react | 20 | 0 | 7.0 | 9.1 (8.0-10.3) | 10.3 |
| px0 | tab_a_again | warm | 100 | redis | 20 | 0 | 6.6 | 8.5 (7.7-11.4) | 11.4 |
| px0 | tab_a_again | warm | 100 | typescript | 20 | 0 | 5.4 | 7.0 (6.4-7.8) | 7.8 |
| px0 | tab_a_again | warm | 100 | wide | 119 | 1 | 5.9 | 10.9 (8.7-11.5) | 12.9 |
| px0 | tab_a_again | warm | 140 | wide | 12 | 0 | 6.2 | 9.7 (7.5-11.0) | 11.0 |
| px0 | tab_a_again | warm | 200 | wide | 12 | 0 | 7.8 | 9.0 (8.4-9.1) | 9.1 |
| px0 | tab_b | cold | 100 | flask | 5 | 0 | 3.4 | 5.5 (3.2-5.6) | 5.6 |
| px0 | tab_b | cold | 100 | narrow | 3 | 0 | 7.1 | 7.2 (---) | 7.2 |
| px0 | tab_b | cold | 100 | react | 5 | 0 | 6.3 | 8.1 (6.3-8.4) | 8.4 |
| px0 | tab_b | cold | 100 | redis | 5 | 0 | 7.6 | 8.0 (7.3-8.0) | 8.0 |
| px0 | tab_b | cold | 100 | typescript | 5 | 0 | 7.3 | 8.5 (7.1-8.6) | 8.6 |
| px0 | tab_b | cold | 100 | wide | 30 | 0 | 5.4 | 8.7 (7.5-9.4) | 9.4 |
| px0 | tab_b | cold | 140 | wide | 3 | 0 | 7.8 | 9.7 (---) | 9.9 |
| px0 | tab_b | cold | 200 | wide | 3 | 0 | 8.6 | 9.3 (---) | 9.4 |
| px0 | tab_b | warm | 100 | flask | 20 | 0 | 5.4 | 8.3 (6.4-10.2) | 10.2 |
| px0 | tab_b | warm | 100 | narrow | 12 | 0 | 7.0 | 7.9 (7.2-8.3) | 8.3 |
| px0 | tab_b | warm | 100 | react | 20 | 0 | 7.2 | 9.0 (7.6-9.4) | 9.4 |
| px0 | tab_b | warm | 100 | redis | 20 | 0 | 7.0 | 8.0 (7.6-8.8) | 8.8 |
| px0 | tab_b | warm | 100 | typescript | 20 | 0 | 5.9 | 8.0 (7.6-8.8) | 8.8 |
| px0 | tab_b | warm | 100 | wide | 120 | 0 | 5.5 | 9.8 (8.3-10.8) | 12.9 |
| px0 | tab_b | warm | 140 | wide | 12 | 0 | 6.4 | 8.5 (7.5-8.9) | 8.9 |
| px0 | tab_b | warm | 200 | wide | 12 | 0 | 7.5 | 9.1 (8.0-9.3) | 9.3 |
| px0 | tree_open | cold | 100 | narrow | 3 | 0 | 55.2 | 63.0 (---) | 63.9 |
| px0 | tree_open | cold | 100 | wide | 30 | 0 | 64.9 | 134.9 (91.0-403.2) | 403.2 |
| px0 | tree_open | cold | 140 | wide | 3 | 0 | 52.7 | 55.6 (---) | 55.9 |
| px0 | tree_open | cold | 200 | wide | 3 | 0 | 62.3 | 87.7 (---) | 90.5 |
| px0 | tree_open | warm | 100 | narrow | 12 | 0 | 47.7 | 58.7 (51.0-59.1) | 59.1 |
| px0 | tree_open | warm | 100 | wide | 120 | 0 | 55.3 | 123.4 (92.3-161.4) | 268.2 |
| px0 | tree_open | warm | 140 | wide | 12 | 0 | 45.5 | 160.3 (54.6-260.7) | 260.7 |
| px0 | tree_open | warm | 200 | wide | 12 | 0 | 52.3 | 84.8 (58.1-91.1) | 91.1 |

## Failures and timeouts

None.

## Frames

floatinghotel: CPU frame time, 10 blocks of 100 frames each after a long scroll. px0: animation-frame interval while scrolling (the page cannot see CPU time per frame); not comparable.

- px0: per-launch p99 17.8-19.4 ms over 30 launches
- floatinghotel: per-launch p99 3.6-264.6 ms over 30 launches

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

## Real repositories

Rows in the journey table whose window column names a repository come from these pinned shallow clones (tree, Find and search excluded; see above).

- flask 3.1.0 `ab8149664`, 249 files: a `src/flask/helpers.py`, b `src/flask/app.py`, large `CHANGES.rst`
- redis 7.4.1 `74b289a0e`, 1610 files: a `src/networking.c`, b `src/server.c`, large `src/server.c`
- react v19.0.0 `7aa5dda3b`, 6928 files: a `packages/react/src/ReactHooks.js`, b `packages/react-dom/src/client/ReactDOMRoot.js`, large `packages/react-reconciler/src/ReactFiberWorkLoop.js`
- typescript v5.7.2 `d701d908d`, 73043 files: a `src/compiler/scanner.ts`, b `src/compiler/parser.ts`, large `src/compiler/checker.ts`

## Memory (RSS, 20 tabs, scroll, close, 30 s idle)

Summed resident set of every process the app uses; shared pages are counted once per process, which overstates multi-process totals.

| app | phase | total MB | processes | largest components |
|---|---|---:|---:|---|
| floatinghotel | start | 55.7 | 1 | floatinghotel.exe 56 |
| floatinghotel | tabs_open | 71.3 | 1 | floatinghotel.exe 71 |
| floatinghotel | scrolled | 95.5 | 1 | floatinghotel.exe 95 |
| floatinghotel | closed | 95.5 | 1 | floatinghotel.exe 95 |
| floatinghotel | idle | 67.8 | 1 | floatinghotel.exe 68 |
| floatinghotel | peak | 95.5 | 1 | floatinghotel.exe 96 |
| floatinghotel | idle footprint | 121.3 | 1 | physical footprint incl. GPU |
| px0 | start | 1289.4 | 10 | Google Chrome Helper (Renderer) 695, Google Chrome Helper 340, Google Chrome 225, px0 30 |
| px0 | tabs_open | 1615.1 | 12 | Google Chrome Helper (Renderer) 1055, Google Chrome Helper 297, Google Chrome 231, px0 33 |
| px0 | scrolled | 1595.9 | 11 | Google Chrome Helper (Renderer) 991, Google Chrome Helper 283, Google Chrome 244, px0 78 |
| px0 | closed | 1595.0 | 11 | Google Chrome Helper (Renderer) 990, Google Chrome Helper 283, Google Chrome 244, px0 78 |
| px0 | idle | 1285.7 | 10 | Google Chrome Helper (Renderer) 639, Google Chrome Helper 378, Google Chrome 235, px0 34 |
| px0 | peak | 1651.7 | 13 | Google Chrome Helper (Renderer) 889, Google Chrome Helper 415, Google Chrome 243, px0 78 |
| floatinghotel | start | 56.8 | 1 | floatinghotel.exe 57 |
| floatinghotel | tabs_open | 68.8 | 1 | floatinghotel.exe 69 |
| floatinghotel | scrolled | 97.1 | 1 | floatinghotel.exe 97 |
| floatinghotel | closed | 97.1 | 1 | floatinghotel.exe 97 |
| floatinghotel | idle | 79.0 | 1 | floatinghotel.exe 79 |
| floatinghotel | peak | 97.2 | 1 | floatinghotel.exe 97 |
| floatinghotel | idle footprint | 150.4 | 1 | physical footprint incl. GPU |
| px0 | start | 1272.5 | 10 | Google Chrome Helper (Renderer) 693, Google Chrome Helper 330, Google Chrome 221, px0 28 |
| px0 | tabs_open | 1627.9 | 12 | Google Chrome Helper (Renderer) 1075, Google Chrome Helper 285, Google Chrome 234, px0 34 |
| px0 | scrolled | 1588.5 | 11 | Google Chrome Helper (Renderer) 989, Google Chrome Helper 275, Google Chrome 246, px0 79 |
| px0 | closed | 1589.8 | 11 | Google Chrome Helper (Renderer) 990, Google Chrome Helper 275, Google Chrome 246, px0 79 |
| px0 | idle | 1330.3 | 10 | Google Chrome Helper (Renderer) 643, Google Chrome Helper 382, Google Chrome 236, px0 69 |
| px0 | peak | 1715.0 | 12 | Google Chrome Helper (Renderer) 989, Google Chrome Helper 405, Google Chrome 245, px0 76 |
| floatinghotel | start | 56.7 | 1 | floatinghotel.exe 57 |
| floatinghotel | tabs_open | 72.4 | 1 | floatinghotel.exe 72 |
| floatinghotel | scrolled | 100.4 | 1 | floatinghotel.exe 100 |
| floatinghotel | closed | 100.5 | 1 | floatinghotel.exe 100 |
| floatinghotel | idle | 63.5 | 1 | floatinghotel.exe 64 |
| floatinghotel | peak | 100.5 | 1 | floatinghotel.exe 100 |
| floatinghotel | idle footprint | 122.0 | 1 | physical footprint incl. GPU |
| px0 | start | 1290.6 | 10 | Google Chrome Helper (Renderer) 695, Google Chrome Helper 336, Google Chrome 227, px0 31 |
| px0 | tabs_open | 1575.5 | 12 | Google Chrome Helper (Renderer) 1040, Google Chrome Helper 275, Google Chrome 228, px0 33 |
| px0 | scrolled | 1469.4 | 11 | Google Chrome Helper (Renderer) 893, Google Chrome Helper 273, Google Chrome 226, px0 77 |
| px0 | closed | 1485.6 | 11 | Google Chrome Helper (Renderer) 895, Google Chrome Helper 276, Google Chrome 238, px0 77 |
| px0 | idle | 1121.8 | 10 | Google Chrome Helper (Renderer) 549, Google Chrome Helper 352, Google Chrome 204, px0 17 |
| px0 | peak | 1613.6 | 12 | Google Chrome Helper (Renderer) 895, Google Chrome Helper 404, Google Chrome 238, px0 76 |
