# Arch / CachyOS package

From the repository root run `makepkg --dir packaging/arch -si`, or enter this
directory and run `makepkg -si`. Build as your normal user; pacman requests
administrator authentication for dependency installation and package installation.
Run `kglanceqt --configure-dolphin` as the desktop user afterward to assign Space.

This recipe downloads the tagged native source archive and verifies its SHA-256
checksum. It does not build the removed Rust application, install user settings,
start a daemon automatically, or patch Plasma. Existing settings are retained on
package removal. Runtime viewers/image plugins are declared as optional packages;
Okular and DBus tools are required for the full test suite during builds.
The information sidebar uses the installed `baloo-widgets` metadata component;
native preferences use `kwidgetsaddons`. Both are declared runtime dependencies.

`kglanceqt` conflicts with packaged `kglance`/`kglance-git`. Earlier manual copies
under `~/.local` are not known to pacman and must be moved to Trash separately.
The package installs only its own executable, application entry, KIO service
action, license/attribution, and documentation under `/usr`.

For a new application version, test and publish a new `qt-vVERSION` source tag,
update `pkgver`, `_tag`, and the verified archive checksum, reset `pkgrel` to 1,
then regenerate `.SRCINFO` with `makepkg --printsrcinfo`. Prereleases use Arch
version notation, for example `0.3.0alpha1` for tag `qt-v0.3.0-alpha.1`, so the
eventual stable version sorts newer. Use the exact source directory name from
the downloaded archive. For packaging-only changes increment
`pkgrel`. Do not move existing release tags or use unverified source checksums.

The recipe on `main` and the recipe attached to a GitHub release are updated after
its source tag is published, when the archive checksum can be verified. Use those
recipes to build the release; a recipe inside a tagged source archive can still
refer to the preceding release.

Normal Qt 6/KF6 updates are consumed through the system shared libraries after
restarting the app. Rebuild/test when a dependency changes ABI or toolchain
compatibility, and when producing an updated application package. A locally
installed package does not receive application updates from pacman until a
repository serves those versions (or you build/install a new package yourself).

## Working-tree packages and CI

Run `just package-snapshot` for unpublished local work. The snapshot helper keeps
the tagged recipe intact, archives the current source (including uncommitted
implementation), excludes build/VCS/cache directories and local settings, writes
a separate PKGBUILD with a real checksum and `.SRCINFO`, and runs makepkg as the
normal user. Artifacts remain under `pkg/snapshot-*`. Use `--prepare-only` to
review the archive/recipe before running makepkg. Never run this helper as root.

The distribution package enables `KGLANCEQT_WITH_SQLITE` and declares
`sqlite>=3.41`; direct CMake builds keep it optional. CI checks both feature
settings against `archlinux:base-devel` and the distribution's
[`cachyos/cachyos:latest`](https://hub.docker.com/r/cachyos/cachyos) image on pushes,
PRs, weekly and manual runs. Package builds run as an unprivileged builder and
retain packages, source tarballs and recipes as workflow artifacts. Current
rolling dependency versions are resolved on each run. CI itself must run on
GitHub to validate container-specific behaviour.

Release publication remains deliberate: review the built artifacts and tests,
then create the authorized immutable source tag and attach the verified source
archive, package and recipe to the release. This workflow does not automatically
publish or update installed systems.
