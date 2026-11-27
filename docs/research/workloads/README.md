# K210 workload evidence

This package replays the analysis of archived board trials for UART Logger,
Object Gate and Sky Glide. Each [indexed trial](trials.jsonl) names the raw
file and its one-based JSONL line. The [summary](summary.json) fingerprints
the inputs and analysis script. Text is hashed with LF line endings and
gzip data is hashed as stored. Each summary group fixes the board, workload,
mode, implementation path, and image/script fingerprint.

| Workload | Boards | Archived runs | Valid runs | Recorded failures |
| --- | --- | ---: | ---: | ---: |
| UART Logger | Maix Cube, SEN0305 | 65 | 62 | 3 |
| Object Gate | Maix Cube, SEN0305 | 20 | 20 | 0 |
| Sky Glide | Maix Cube, SEN0305 | 140 | 140 | 0 |

The UART Logger archive includes two single-run memory profiles. The three
failed setup attempts on Maix Cube remain in the index and in their group's
attempt count. Measurements from valid runs determine the median, median
absolute deviation (MAD), minimum and maximum. The summary records a null
MAD for groups with one valid run. Display and input latencies use one
within-trial median as a sample for each run. The summaries describe the
observed runs and their dispersion.

## Reproduce

Run the following from the repository root to regenerate the two files.

```text
python tools/reproduce_workloads.py build/workloads
```

The original studies specify setup, warm-up policy and workload details.

- UART Logger measures a 4,096-packet parser replay, live plot and SD
  recording. Live trials fill the history during warm-up and measure 30
  seconds. [Study and hardware procedure](../../UART_LOGGER_STUDY.md)
- Object Gate replays the same diagnostic input tensors on each board.
  It skips the first four frames and records the following 24.
  [Study and hardware procedure](../../OBJECT_GATE_STUDY.md)
- Sky Glide replays fixed-point logic and measures frame transfer,
  debounced-input response and memory. Paced runs warm up for two seconds.
  Burst runs warm up for 50 completed frames.
  [Study and hardware procedure](../../SKY_GLIDE_STUDY.md)

The parse, tensor and logic replay digests agree across implementation
paths in each of six board/workload groups. These are workload-level
results. Renderer, VM, scheduling and provider effects all contribute to
application latency. The full studies describe additional smoke and
restoration checks outside the repeated trials.

UART Logger and Object Gate carry per-trial image and script identifiers.
Sky Glide records the diagnostic image by board and the script in the
study metadata. The archive does not establish an exact firmware commit
or compiler identity for every trial. These runs predate the paper
candidate. Paper-revision ablation and watchdog recovery remain separate
experiments.
