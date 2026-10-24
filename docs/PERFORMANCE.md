# Preview performance measurements

Run `just benchmark` (offscreen) or `python3 tests/benchmark.py build/window_tests
--platform wayland --output build/benchmark-wayland.json` on the current display.
Each run uses fresh private config/cache directories and DBus sessions. It never
contacts the user's running preview service. Desktop portals are disabled for
these generated local-file measurements. Five runs are the default; raw samples
and medians are retained in JSON.

The real executable is launched with `--daemon` to measure process launch through
DBus service readiness, hidden idle CPU and hidden RSS/high-water RSS. Readiness
includes the `gdbus wait` observer's overhead; a fresh process is not a cold disk.
The generated preview workload separately measures window construction,
ShowPreview dispatch, first useful text, resident show/content and a text-to-image
transition using the actual PreviewWindow. First-content checks use QtTest's
polling granularity. The fresh-process first-text figure includes QtTest and
fixture creation, so is not an exact production CLI-to-paint number.

The workload is a generated 1,000-line C++ file and 1920×1080 PNG. CPU is sampled
from Linux process counters during one second of idle, in user+system clock
ticks; memory comes from `/proc/PID/status`. Peak RSS is the observed process high
water mark, not a universal bound. The visible-idle sample follows a transition
back to text; the separate daemon sample has never shown content. OS caches are
not dropped. Results depend on load, libraries, renderer, scale and codecs.

## Recorded local run

Measured on 24 October 2026 on this CachyOS system, Qt 6.12/KF6 6.31,
Plasma Wayland 6.8.1, 3840×2160 panel at 2× scale. Five fresh-process runs,
RelWithDebInfo build; concurrent build activity may affect these samples.
These are local observations rather than release performance guarantees. A release run
should additionally record package/toolchain versions, hardware and compositor,
repeat on representative PDFs/media/large tables, and measure actual input to
compositor presentation with an appropriate external tracing tool.

| Metric | Median |
| --- | ---: |
| Actual daemon launch to DBus ready | 58.73 ms |
| Window construction | 15.43 ms |
| Show dispatch | 4.29 ms |
| First text after ShowPreview | 26.67 ms |
| Fresh test process through first text (includes fixture) | 112.00 ms |
| Resident show dispatch | 0.78 ms |
| Resident text content | 7.28 ms |
| Text to image | 10.93 ms |
| Visible idle CPU over one second | 1.00 % |
| Hidden daemon idle CPU over one second | 0.00 % |
| Visible RSS | 153.4 MiB |
| Process high-water RSS | 153.4 MiB |
| Hidden daemon RSS | 70.3 MiB |

Raw samples: `build/benchmark-wayland.json`. CPU values are sampled at the
OS clock-tick resolution; zero measured ticks does not prove zero CPU use.
