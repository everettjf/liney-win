# Terminal benchmark evidence

## 2026-07-27 local Windows 11 run

Hardware: AMD Ryzen 7 3700X, NVIDIA GeForce GTX 1650 SUPER.

Method: each terminal opens a fresh window and runs the same PowerShell payload
that writes 20,000 fixed-width lines. Time starts at process invocation and ends
when the producer writes a completion marker. The harness polls at 10 ms.

| Terminal | Samples | Median | p95 |
|---|---:|---:|---:|
| Liney | 10 | 499 ms | 545 ms |
| Windows Terminal | 10 | 1,422 ms | 1,534 ms |
| WezTerm | — | not installed | not installed |
| Alacritty | — | not installed | not installed |

Raw Liney samples (ms): 545, 515, 505, 493, 485, 502, 494, 499, 483, 488.

Raw Windows Terminal samples (ms): 1383, 1534, 1455, 1420, 1377, 1341,
1487, 1505, 1383, 1422.

This is a startup-plus-output/backpressure test. It is useful and reproducible,
but it is not a frame-latency, dropped-frame, input-latency or steady-state GPU
benchmark. Do not use it to claim that Liney's renderer is universally 2.8×
faster. Re-run with `tools/competitor-benchmark.ps1`; installed WezTerm and
Alacritty builds are included automatically.

The separate renderer gate on this machine completed the 20,000-line workload
at 53 MiB peak working set with frame p95 5.52 ms and p99 14.82 ms. Those
timings include the synchronized `Present(1)` call and therefore measure the
whole render/present interval rather than shader execution alone.
