# KGlanceQT

> **AI CODING DISCLAIMER:** Both the original Kglance (Oxiview) project and this
> KGlanceQT rewrite were developed with AI coding assistance.

A Qt 6 and KDE Frameworks 6 file preview application for Plasma and Dolphin.
Select files in Dolphin and press **Space** to preview them. The application keeps
a resident process and reuses its preview window; Space or Esc hides it.

KGlanceQT is an independently maintained successor to Mintori09's
[Kglance (Oxiview)](https://github.com/Mintori09/kglance). The original history,
attribution, and AGPL-3.0-only license are preserved. See [ATTRIBUTION.md](ATTRIBUTION.md).
The active tree contains only the native Qt/KDE implementation and its build,
packaging, documentation, and tests. No Rust/Iced runtime or Cargo dependencies
are required.

## Install on Arch Linux / CachyOS

The repository's `packaging/arch/PKGBUILD` builds a normal pacman package, declaring runtime,
build, and optional dependencies. From the repository root:

```sh
makepkg --dir packaging/arch -si
kglanceqt --configure-dolphin
```

Restart Dolphin when convenient to load the service action and shortcut. The
setup command backs up its per-user XML configuration, assigns **Space**, removes
the old Kglance/test shortcut entries, and preserves unrelated customizations.
It refuses to take a shortcut assigned to an unrelated action. A different key
can be selected with `--configure-dolphin --shortcut Ctrl+Alt+Space`.

Package installation does not edit user settings or stop Dolphin. Before moving
from the earlier manual installations, stop them and move their binaries and
desktop files out of `~/.local`; otherwise those files can shadow the package.
Keep user documents and old settings unless you deliberately want to discard them.

System Qt/KDE libraries and viewers update through the normal package manager.
Restart KGlanceQT after those updates. Application releases or an incompatible
library/toolchain change require a package update/rebuild; the PKGBUILD does not
automatically rebuild on each KDE upgrade. Test current distribution packages
before distributing a new build.

## Desktop files

Plasma's Folder View desktop can invoke **Preview with KGlanceQT** from a selected
file's context menu. `desktop:/` URLs are resolved asynchronously through KIO to
their local files. The same action also works in Dolphin.

The Dolphin Space shortcut does not register Space globally. Plasma controls
desktop keyboard handling independently; desktop **Space-to-preview is not
implemented**. A supported desktop keyboard integration remains on the roadmap.

## Preview providers and controls

| Content | Native component |
| --- | --- |
| Text / source code | Read-only KTextEditor, including highlighting, search, selection and copy |
| Markdown | Qt rendered preview with tables, task lists, local images, heading links, and a KTextEditor source view |
| Images and SVG | QImageReader and installed Qt/KDE image plugins; wheel zoom, drag to pan, Ctrl+0 to fit |
| PDF and supported documents | Installed Okular read-only KPart |
| Fonts | Installed font-viewer KPart, where available |
| Audio / video | Qt Multimedia, with play/pause and seeking |
| Folders | QFileSystemModel and Qt tree view |
| ZIP / TAR / 7z | KArchive listing without extraction |
| Other formats | Installed KIO thumbnailers, then an open-in-application fallback |

Space / Esc hides the preview. With multiple files selected, Left / Right moves
through the supplied files in selection order, wrapping at either end, even when
a document viewer has focus. Alt+Left / Alt+Right also moves through the supplied
selection, Ctrl+O opens the file in its associated application, and Ctrl+Q exits
KGlanceQT. Audio and video play automatically; click the video to pause or resume.
Audio previews show centered playback controls with a scrubber, elapsed/total
time, volume, and mute. Files without a visual preview show a subdued file-type
icon on the dark preview background. Codec coverage follows the distribution's
Qt Multimedia backend.

The application menu contains **Open With…**, **Show in File Manager**, and
**Copy Location**. **Quit KGlanceQT** (Ctrl+Q) also lives there and stops the
background preview service; Space, Esc, and closing the window normally just hide
the preview. To start the service again without opening a preview, run
`kglanceqt --daemon`. Previewing a file through the Dolphin action also starts it.
Image zoom, pan, and fit mode are remembered when navigating the current selection;
this memory is cleared when the preview is hidden or a new selection opens.
Use **Application menu → Help → Supported Preview Types…** to see the preview
categories, detected image formats, optional viewer availability, and limitations.

Markdown opens in **Preview** by default; **Source** shows the original text.
Use Ctrl+F to search the rendered preview and Ctrl++ / Ctrl+- to zoom. External
links open through the default application. Referenced local images load in
bounded workers; remote images are not downloaded and embedded HTML is disabled.

The **Information** toolbar action (F11) opens an optional sidebar with file
details and Dolphin's read-only metadata widget. Available properties depend on
the file and installed KDE extractors, including image dimensions, camera data,
media details, and document properties. Detailed extraction is limited to regular
files up to 256 MiB; folders and larger files retain basic file information.

Open **Configure KGlanceQT…** from the toolbar (Ctrl+Shift+,) for audio/video
autoplay, default volume, Markdown's initial view, sidebar visibility, and text,
image, and archive limits. Preview defaults apply to the next file; volume and
sidebar visibility update immediately. The Dolphin shortcut page has a separate
**Apply shortcut to Dolphin** button, preserves conflicting assignments, and
backs up existing configuration. Normal preference changes do not modify Dolphin.

```sh
kglanceqt -- /path/to/file another-file
kglanceqt --standalone -- /path/to/file
kglanceqt --daemon
kglanceqt --quit
```

The binary is `kglanceqt`; its DBus service/interface is `org.kglanceqt.Preview`,
with object `/org/kglanceqt/Preview`. Per-user window settings live in
`$XDG_CONFIG_HOME/kglanceqt/KGlanceQT.conf` (normally under `~/.config`).

## Build and verify

Required: a C++20 compiler, CMake, Ninja, Qt 6.6+ Widgets, XML, DBus, Concurrent
and Multimedia, plus KF6 Parts, TextEditor, KIO, Archive, WindowSystem,
WidgetsAddons and BalooWidgets (`baloo-widgets`). Tests
also use Qt Test, Python, DBus tools and Okular. Python is not an application
runtime dependency. Image format plugins and thumbnails are optional enhancements.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

Equivalent `just build`, `just test`, and `just fmt-check` recipes are provided.
The DBus integration test uses a private session bus and needs permission to
create Unix sockets; it does not address the user's live preview process.

## Current limits

Animated images display a static frame, and Office formats may have only a
thumbnail. Spreadsheet/SQLite tables, reading positions, trash/undo, custom shell actions and
3D previews are not implemented. This is a replacement project, not a claim of
complete feature parity with the old application. See [ROADMAP.md](ROADMAP.md).

Text previews are limited to 16 MiB; images to 64 megapixels and a 256 MiB reader
allocation budget; archive listings to 10,000 entries. Preferences can lower
these limits. Markdown rendering is capped at 256 KiB and 4,000 lines, with larger
documents shown as source within the text limit. Up to 16 local Markdown images
of 4 megapixels each can load, displayed at up to 1024 pixels per side.
Worker jobs use two threads
and request generations prevent stale results. Archive traversal checks
cancellation, but an image decode already running cannot be interrupted. KParts
load in-process; their responsiveness and resource use still require benchmarks.
Remote network URLs are not downloaded by the application.
