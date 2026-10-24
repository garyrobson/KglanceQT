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
update `pkgver` and the archive checksum, reset `pkgrel` to 1, then regenerate
`.SRCINFO` with `makepkg --printsrcinfo`. For packaging-only changes increment
`pkgrel`. Do not move existing release tags or use unverified source checksums.

Normal Qt 6/KF6 updates are consumed through the system shared libraries after
restarting the app. Rebuild/test when a dependency changes ABI or toolchain
compatibility, and when producing an updated application package. A locally
installed package does not receive application updates from pacman until a
repository serves those versions (or you build/install a new package yourself).
