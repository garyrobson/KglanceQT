#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Package the current working tree with makepkg and a real source checksum."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("pkg"))
    parser.add_argument("--pkgrel", default="1.4")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[1-9][0-9]*(?:\.[0-9]+)?", args.pkgrel):
        parser.error("invalid package release")
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="snapshot-", dir=output))
    recipe = (root / "packaging/arch/PKGBUILD").read_text()
    version = re.search(r"^pkgver=(.*)$", recipe, re.M)[1]
    tag = "local-snapshot"
    archive = work / f"kglanceqt-{version}.tar.gz"
    excluded = {".git", "build", "pkg", "src-cache", ".codex", ".agents", ".aws"}
    with tarfile.open(archive, "w:gz") as tar:
        def add(directory):
            for path in sorted(directory.iterdir()):
                relative = path.relative_to(root)
                if path.resolve() == output or path.name in excluded or "__pycache__" in relative.parts or path.name == "AGENTS.local.md":
                    continue
                if relative.parts[:2] == ("packaging", "arch") and path.name in {"src", "pkg"}:
                    continue
                if path.is_symlink() or path.name.endswith((".log", ".tar.gz", ".pkg.tar.zst")):
                    continue
                if path.is_dir():
                    add(path)
                elif path.is_file():
                    tar.add(path, arcname=f"KglanceQT-{tag}/{relative}", recursive=False)
        add(root)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    recipe = re.sub(r"^_tag=.*$", f"_tag={tag}", recipe, flags=re.M)
    recipe = re.sub(r"^pkgrel=.*$", f"pkgrel={args.pkgrel}", recipe, flags=re.M)
    recipe = re.sub(r"^source=.*$", f'source=("kglanceqt-{version}.tar.gz")', recipe, flags=re.M)
    recipe = re.sub(r"^sha256sums=.*$", f"sha256sums=('{checksum}')", recipe, flags=re.M)
    (work / "PKGBUILD").write_text(recipe)
    with (work / ".SRCINFO").open("w") as target:
        subprocess.run(["makepkg", "--printsrcinfo"], cwd=work, stdout=target, check=True)
    print(work, flush=True)
    if not args.prepare_only:
        subprocess.run(["makepkg", "--force", "--clean"], cwd=work, check=True)


if __name__ == "__main__":
    main()
