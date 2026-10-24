# SPDX-License-Identifier: AGPL-3.0-only
"""Repeatable local measurements; isolated bus/config, no contact with the running app."""
import argparse
import json
import os
from pathlib import Path
import statistics
import sys
import subprocess
import tempfile
import time


def daemon_sample(binary, environment):
    start = time.monotonic()
    with tempfile.TemporaryFile() as log:
        daemon = subprocess.Popen([str(binary.parent / "kglanceqt"), "--daemon"],
                                  env=environment, stdout=log, stderr=log)
        try:
            subprocess.run(["gdbus", "wait", "--session", "--timeout=10", "org.kglanceqt.Preview"],
                           env=environment, stdout=subprocess.DEVNULL, check=True)
            ready = (time.monotonic() - start) * 1000
            stat_path = Path(f"/proc/{daemon.pid}/stat")
            def ticks():
                fields = stat_path.read_text().rsplit(")", 1)[1].split()
                return int(fields[11]) + int(fields[12])
            first = ticks()
            idle_start = time.monotonic()
            time.sleep(1)
            cpu = (ticks() - first) / os.sysconf("SC_CLK_TCK") / (time.monotonic() - idle_start) * 100
            result = {"daemon_cold_ready_ms": ready, "daemon_hidden_idle_cpu_percent": cpu}
            for line in Path(f"/proc/{daemon.pid}/status").read_text().splitlines():
                if line.startswith(("VmRSS:", "VmHWM:")):
                    result["daemon_hidden_" + line.split(":")[0] + "_KiB"] = int(line.split()[1])
            subprocess.run([str(binary.parent / "kglanceqt"), "--quit"], env=environment,
                           stdout=subprocess.DEVNULL, stderr=log, check=True)
            daemon.wait(timeout=10)
            return result
        finally:
            if daemon.poll() is None:
                daemon.terminate()
                daemon.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path, help="window_tests executable")
    parser.add_argument("--daemon-sample", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--platform", choices=("offscreen", "wayland"), default="offscreen")
    parser.add_argument("--output", type=Path, default=Path("build/benchmark.json"))
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not 1 <= args.runs <= 20:
        parser.error("--runs must be between 1 and 20")
    if args.daemon_sample:
        print(json.dumps(daemon_sample(binary, os.environ)))
        return
    reports = []
    # Window tests require configs under the executable directory as a safety assertion.
    with tempfile.TemporaryDirectory(prefix="benchmark-", dir=binary.parent) as temporary:
        root = Path(temporary)
        for run in range(args.runs):
            config = root / str(run)
            config.mkdir()
            report = config / "report.json"
            env = dict(os.environ, QT_QPA_PLATFORM=args.platform, QT_NO_XDG_DESKTOP_PORTAL="1", XDG_CONFIG_HOME=str(config),
                       XDG_DATA_HOME=str(config / "data"), XDG_CACHE_HOME=str(config / "cache"),
                       KGLANCEQT_BENCHMARK_REPORT=str(report),
                       KGLANCEQT_BENCHMARK_START_MS=str(time.time_ns() // 1000000))
            (config / "kwalletrc").write_text("[Wallet]\nEnabled=false\n")
            start = time.monotonic()
            subprocess.run(["dbus-run-session", "--", str(binary), "previewPerformance"],
                           env=env, check=True, timeout=45, stdout=subprocess.DEVNULL)
            result = json.loads(report.read_text())
            result["whole_process_ms"] = (time.monotonic() - start) * 1000
            result["idle_cpu_percent_1s"] = result["idle_cpu_ticks_1s"] / os.sysconf("SC_CLK_TCK") * 100
            memory = result.pop("memory_status")
            for line in memory.splitlines():
                if line.startswith(("VmRSS:", "VmHWM:")):
                    result[line.split(":")[0] + "_KiB"] = int(line.split()[1])
            sampled = subprocess.check_output(["dbus-run-session", "--", sys.executable,
                                               str(Path(__file__).resolve()), str(binary), "--daemon-sample"],
                                              env=env, text=True, timeout=20)
            result.update(json.loads(sampled))
            reports.append(result)
    medians = {key: statistics.median(row[key] for row in reports)
               for key, value in reports[0].items() if isinstance(value, (int, float))}
    output = {"scope": "Local generated 1000-line C++ text and 1920x1080 PNG; fresh process/config, OS caches not dropped. First-content checks have QtTest polling granularity; whole process includes idle sampling and teardown. No cold-disk or compositor-paint claim.",
              "platform": args.platform, "runs": reports, "medians": medians}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps(medians, indent=2))


if __name__ == "__main__":
    main()
