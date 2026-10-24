"""Run inside dbus-run-session; never contacts the user's normal preview service."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1]).resolve()
service = "org.kglanceqt.Preview"
object_path = "/org/kglanceqt/Preview"


def call(method, *arguments):
    return subprocess.check_output(
        ["qdbus6", service, object_path, f"{service}.{method}", *arguments], text=True
    ).strip()


with tempfile.TemporaryDirectory(prefix="kglanceqt-smoke-") as temporary:
    root = Path(temporary)
    sample = root / "sample with spaces.cpp"
    sample.write_text("int main() { return 0; }\n")
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=str(root / "config"),
                       XDG_CACHE_HOME=str(root / "cache"))
    with (root / "daemon.log").open("w+") as log:
        daemon = subprocess.Popen([str(binary), "--daemon"], env=environment, stdout=log, stderr=log)
        try:
            subprocess.run(["gdbus", "wait", "--session", "--timeout=10", service], check=True)
            assert call("IsVisible") == "false"
            subprocess.run([str(binary), "--", str(sample)], env=environment, check=True)
            assert call("IsVisible") == "true"
            subprocess.run([str(binary), "--", str(sample)], env=environment, check=True)
            assert call("IsVisible") == "false"
            subprocess.run([str(binary), "--", sample.as_uri()], env=environment, check=True)
            assert call("IsVisible") == "true"
            call("HidePreview")
            assert call("IsVisible") == "false"
            subprocess.run([str(binary), "--quit"], env=environment, check=True)
            assert daemon.wait(timeout=10) == 0
        except BaseException:
            log.seek(0)
            print(log.read(), file=sys.stderr)
            raise
        finally:
            if daemon.returncode is None:
                daemon.terminate()
                daemon.wait(timeout=10)
print("DBus activation, path/URL handling, toggling, hide, and shutdown passed.")
