# SPDX-License-Identifier: AGPL-3.0-only
"""Native Wayland checks; nested runs never reconfigure the desktop's outputs."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--current", action="store_true", help="Test on the existing compositor without changing its outputs")
    parser.add_argument("--private", action="store_true", help=argparse.SUPPRESS)
    options = parser.parse_args()
    binary = options.binary.resolve()
    config = binary.parent / ("test-wayland-current" if options.current else "test-wayland-nested")
    config.mkdir(exist_ok=True)
    (config / "kwalletrc").write_text("[Wallet]\nEnabled=false\n")
    environment = dict(os.environ, QT_QPA_PLATFORM="wayland", XDG_CONFIG_HOME=str(config),
                       XDG_DATA_HOME=str(config / "data"), XDG_CACHE_HOME=str(config / "cache"),
                       KGLANCEQT_NATIVE_DISPLAY_TEST="1", QTEST_DISABLE_STACK_DUMP="1",
                       KGLANCEQT_TEST_SCREENSHOTS=str(config / "screenshots"))
    environment.pop("QT_SCALE_FACTOR", None)
    environment.pop("QT_SCREEN_SCALE_FACTORS", None)
    command = [str(binary), "nativeWaylandLifecycle"]
    if options.current:
        environment["KGLANCEQT_KWIN_PID"] = compositor_pid(environment)
        return subprocess.run(command, env=environment, timeout=60).returncode
    environment["QT_NO_XDG_DESKTOP_PORTAL"] = "1"
    stage_desktop_metadata(binary, config)
    if not options.private:
        # The bus daemon and every activated helper inherit the private runtime
        # and configuration too, rather than only the Qt client inheriting them.
        with tempfile.TemporaryDirectory(prefix="kglanceqt-wayland-") as temporary:
            runtime = Path(temporary)
            runtime.chmod(0o700)
            environment.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY="kglanceqt-test",
                               KGLANCEQT_OWNED_RUNTIME=str(runtime))
            try:
                return subprocess.call(["dbus-run-session", "--", sys.executable, __file__, str(binary), "--private"], env=environment)
            finally:
                # A document portal may have mounted FUSE storage in this owned
                # runtime. Detach that test mount before deleting the directory.
                if shutil.which("fusermount3"):
                    subprocess.run(["fusermount3", "-u", "-z", str(runtime / "doc")],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    runtime = Path(environment.get("KGLANCEQT_OWNED_RUNTIME", ""))
    if not runtime.is_dir() or str(runtime) != environment.get("XDG_RUNTIME_DIR"):
        raise RuntimeError("The nested test must be launched through its private session wrapper")
    compositor_environment = dict(environment, QT_QPA_PLATFORM="offscreen")
    compositor_environment.pop("KWIN_COMPOSE", None)
    with (config / "kwin.log").open("w+") as log:
        compositor = subprocess.Popen(["kwin_wayland", "--virtual", "--output-count", "2", "--width", "1920",
                                        "--height", "1200", "--no-lockscreen", "--no-global-shortcuts",
                                        "--no-kactivities", "--socket", "kglanceqt-test"],
                                       env=compositor_environment, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 20
            while not (runtime / "kglanceqt-test").exists():
                if compositor.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("Nested KWin failed to create its Wayland socket")
                time.sleep(0.05)
            subprocess.run(["gdbus", "wait", "--session", "--timeout=15", "org.kde.KWin"], env=environment, check=True)
            owner = compositor_pid(environment)
            if owner != str(compositor.pid):
                raise RuntimeError("Refusing output changes: KWin is not the owned nested compositor")
            environment["KGLANCEQT_KWIN_PID"] = owner
            subprocess.run(["kscreen-doctor", "output.1.scale.1", "output.2.scale.1"], env=environment, check=True, timeout=15)
            subprocess.run(command, env=environment, check=True, timeout=60)
            # Reached only after checking the private bus owner against our PID.
            subprocess.run(["kscreen-doctor", "output.1.scale.1", "output.2.scale.1.5"], env=environment, check=True, timeout=15)
            subprocess.run(command, env=environment, check=True, timeout=60)
        except BaseException:
            print(f"Nested compositor return code: {compositor.poll()}; log: {config / 'kwin.log'}", file=sys.stderr)
            log.seek(0)
            print(log.read()[-4000:], file=sys.stderr)
            raise
        finally:
            if compositor.poll() is None:
                compositor.terminate()
                try:
                    compositor.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    compositor.kill()
                    compositor.wait()
    return 0


def stage_desktop_metadata(binary, config):
    desktop = config / "data/applications"
    desktop.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary.parent / "org.kglanceqt.KGlanceQT.desktop", desktop)
    icons = Path(__file__).resolve().parent.parent / "data/icons"
    for size in (16, 22, 24, 32, 48, 64, 128, 256):
        destination = config / f"data/icons/hicolor/{size}x{size}/apps"
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(icons / f"org.kglanceqt.KGlanceQT-{size}.png", destination / "org.kglanceqt.KGlanceQT.png")


def compositor_pid(environment):
    return subprocess.check_output(["qdbus6", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                    "org.freedesktop.DBus.GetConnectionUnixProcessID", "org.kde.KWin"],
                                   env=environment, text=True, timeout=10).strip()


if __name__ == "__main__":
    raise SystemExit(main())
