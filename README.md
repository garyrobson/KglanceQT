# KGlanceQT

Current prerelease: **0.3.0-alpha.1** (`qt-v0.3.0-alpha.1`). This native Qt/KDE
alpha is intended for testing; remaining features and desktop validation are
tracked in [ROADMAP.md](ROADMAP.md).

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
| CSV / TSV | Searchable, sortable, selectable read-only tables with bounded parsing |
| Markdown | Qt rendered preview with tables, task lists, local images, heading links, and a KTextEditor source view |
| Images and SVG | QImageReader and installed Qt/KDE image plugins; bounded animation playback, wheel zoom, drag to pan, Ctrl+0 to fit |
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

Image zoom, pan, and fit mode, text scroll/cursor positions, Markdown mode and
scrolling, and seekable media positions and play/pause state are remembered when
navigating the current selection. Document viewers that expose KDE's public
page navigation methods also restore their document page (supported by Okular).
Viewer support can vary. These positions stay in memory and clear when the preview
is hidden or a new selection opens. KGlanceQT does not persist this position
history; embedded viewers still manage their own settings.
**Application menu → Clear Remembered Positions** clears the selection's
saved positions and resets the current preview to its defaults.

Enable **Application menu → Show Selection Filmstrip** to show a thumbnail strip
for multiple selected files. Click a file to preview it; the current file stays
highlighted. The setting is also available in preferences. Thumbnails use installed
KDE thumbnailers, with filenames and fallback icons when unavailable. Requests are
debounced and limited to 17 nearby local files, with up to 32 thumbnails held in
memory; hiding the preview cancels requests and clears the cache. Virtual and
remote locations retain fallback icons.
Use **Application menu → Configure Keyboard Shortcuts…** to change window actions
with KDE's native shortcut editor. Application bindings are stored separately in
`$XDG_CONFIG_HOME/kglanceqtshortcutsrc`; Dolphin's activator remains on its separate
preferences page. Space remains a window-only preview toggle. When an input,
button or item view has focus, Space and arrow keys retain their normal control
behaviour; use Alt+Left/Alt+Right (or their configured replacements) to navigate
files from those controls. The filmstrip also supports Enter to activate an item.

Use **Application menu → Help → Supported Preview Types…** to see the preview
categories, detected image formats, optional viewer availability, and limitations.

Drag the **Drag file** handle in the preview toolbar into another application to
open or attach the current local file, or into a file manager to copy it. The
handle uses the file's type icon and works for folders too. It offers a standard
file URL with copy semantics; the destination decides how to use it. It is disabled
for missing files, unsupported remote locations, and unresolved desktop URLs.

Animated images play automatically when their installed Qt plugin supports it.
Use the toolbar's **Pause animation / Play animation** action to pause, resume, or
replay a finished animation. Frame delays and finite loop counts are respected;
delays below 20 ms are clamped to 20 ms. Zoom and pan remain available during
playback. Preferences can disable animation autoplay or lower its frame/cache
limits. Decoding runs in workers and stops between frames on navigation or hide;
late results are discarded. Markdown images remain still previews.

The current local file refreshes automatically after changes on disk, including
atomic saves and resolved desktop files. Save bursts are coalesced for 250 ms;
continuous writes are checked at least once per second. An inline message
distinguishes changed, replaced, missing, and inaccessible files. Missing files
reload when they reappear, including after a parent directory is recreated.
Image zoom/pan, paused animation frames, text/Markdown positions, supported
document pages, and seekable media positions are restored where possible, with
positions clamped to the new content. The current filmstrip thumbnail is refreshed
too. Folder listings retain their native live model and expansion state.
Use **Application menu → Refresh Preview** (F5) to refresh manually. Only the
current path, its parent, and any resolved symlink target are watched (at most
four paths). Hiding the preview removes watches and cancels pending refreshes;
changes in other selected files are loaded when you navigate to them. Automatic
monitoring depends on filesystem support; a warning appears if monitoring cannot
be installed.

Markdown opens in **Preview** by default; **Source** shows the original text.
Use Ctrl+F to search the rendered preview and Ctrl++ / Ctrl+- to zoom. External
links open through the default application. Referenced local images load in
bounded workers; remote images are not downloaded and embedded HTML is disabled.

The **Information** toolbar action (F11) opens an optional sidebar with file
details and Dolphin's read-only metadata widget. Available properties depend on
the file and installed KDE extractors, including image dimensions, camera data,
media details, and document properties. Detailed extraction is limited to regular
files up to 256 MiB; folders and larger files retain basic file information.
Choose **Edit Ratings, Tags and Comment…** for an explicit metadata form. Ratings
(including half stars), one tag per line, and comments use KDE user metadata,
shared with Dolphin. Commas are reserved by KDE’s tag storage; the editor rejects
them inside a tag. Unsupported filesystems and save errors appear in the form.
Each editor stays bound to its displayed file, survives sidebar refresh, and
refuses saving if that file was replaced. A partially failed save is reported;
successful fields may already have been written.

Choose **Compare Images…** in the application menu to open two local images side
by side. Pick from the selection or choose other images. **Link zoom and pan**
synchronizes magnification and the relative viewing centre; **Fit both images**
resets the views. The metadata table highlights differences in dimensions,
format, size, colour information and available embedded text. Each pane decodes
in a worker, watches for changes and uses at most 32 megapixels (or the lower
configured limit). Animated comparisons show the first frame.

**Move to Trash** in the application menu uses KDE Trash operations and advances
to the next selected file. **Undo Move to Trash** (Ctrl+Z, also offered in the
operation banner) restores the file and its original selection entries. An
occupied original location produces an error rather than overwriting its new
contents. Restoring after a different selection opens restores the file without
inserting it into the new selection. This application never falls back to
permanent deletion.

CSV and TSV files open as read-only tables. Search filters all columns, column
numbers sort numerically where possible, and selected cells copy as TSV with
Ctrl+C (up to 4 Mi characters per copy). All rows, including a possible header row,
are data. Parsing handles
quoted delimiters, doubled quotes and multiline fields in a worker. Limits are
16 MiB input (or the lower text limit), 10,000 rows, 256 columns, 100,000 cells and
64 Ki characters per field. A visible notice identifies partial results; incomplete final rows are
omitted at the input limit. Column widths sample at most 100 rows. Query, sort,
current cell and scroll positions are remembered for the selection, including
automatic refresh. Hide, a new selection or Clear Remembered Positions clears them.

Native Office and SQLite options have been [evaluated](docs/NATIVE_FORMATS.md).
Rendered Office previews are available only when a compatible native document
viewer is installed; this machine currently uses thumbnails or an external app.
SQLite remains a separate optional-provider task with cancellation and read-only
requirements. The Supported Preview Types dialog reports native Office support
on the current machine.

Open **Configure KGlanceQT…** from the toolbar (Ctrl+Shift+,) for audio/video/animation
autoplay, default volume, Markdown's initial view, sidebar visibility, and text,
image, animation, table input, remote download and archive limits. Preview
defaults apply to the next file; volume,
sidebar, and filmstrip visibility update immediately. The Dolphin shortcut page has
a separate **Apply shortcut to Dolphin** button, preserves conflicting assignments, and
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
XmlGui, ConfigWidgets, ColorScheme, IconThemes 6.3+, WidgetsAddons, BalooWidgets (`baloo-widgets`) and FileMetaData 6.2+ (`kfilemetadata`). Tests
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

Animations exceeding the configured limits show their first frame with an
explanation. Office formats may have only a thumbnail.
Office spreadsheet/SQLite tables, persistent reading history,
custom shell actions and 3D previews are not implemented. This is a replacement
project, not a claim of complete feature parity with the old application. See [ROADMAP.md](ROADMAP.md).

Text previews are limited to 16 MiB; images to 64 megapixels and a 256 MiB reader
allocation budget; archive listings to 10,000 entries. Preferences can lower
these limits. Markdown rendering is capped at 256 KiB and 4,000 lines, with larger
documents shown as source within the text limit. Up to 16 local Markdown images
of 4 megapixels each can load, displayed at up to 1024 pixels per side.
Animation previews cache at most 256 frames and 64 MiB of decoded frames per
preview, with a five-second decoding budget checked between frames. Preferences
can lower the frame and memory limits. The cache budget counts at least four
bytes per pixel; image-reader working memory and the displayed pixmap are
additional. A decode already in progress cannot be interrupted, so an individual
frame can overrun the time budget before playback falls back to a still image.
Worker jobs use two threads
and request generations prevent stale results. Archive traversal checks
cancellation, but an image decode already running cannot be interrupted. KParts
load in-process; their responsiveness and resource use still require benchmarks.
Remote previews download at most 128 MiB into private temporary storage, with a
60-second transfer deadline. Navigation, hiding and cancellation stop transfers;
temporary files are removed after viewers release them. Installed KIO workers
provide SFTP, SMB and other protocols; failures and authentication requirements
come from KIO. Remote previews do not monitor the server for changes; use Refresh.
Open, Open With and Copy Location retain the original URL. Trash, image comparison
and metadata editing currently require local files. Embedded Markdown remote images
remain disabled.


## Plasma appearance and validation

The document-and-eye application icon ships as scalable SVG and PNG variants
from 16 through 256 pixels. The 16/22/24-pixel versions use simpler artwork.
Launcher, Dolphin action and preview windows share `org.kglanceqt.KGlanceQT`.
The window also has an embedded PNG fallback for uninstalled development builds
and systems without the optional Qt SVG plugin.

Preview messages and audio controls follow the current system palette, including
live changes. Table/search, image and metadata controls expose accessible names;
Qt's native controls provide their accessibility roles and keyboard behaviour.
See [the validation record](docs/PLASMA_VALIDATION.md) for coverage and limits.

For a system with KWin Wayland, KScreen, Qt Wayland, DBus tools and Breeze installed:

```sh
just wayland-test          # isolated KWin; two outputs, including mixed scaling
just wayland-current-test  # existing display; output settings are preserved
```

The current-display check briefly opens and activates its own validation window.
Nested output changes require verifying that the private DBus KWin owner is the
compositor PID launched by the test. These tools never install global shortcuts,
change the real monitor configuration or contact the running preview service.

### SQLite, measurement and package automation

Local `just build` and Arch packages enable the optional SQLite preview. Direct
CMake builds can select `-DKGLANCEQT_WITH_SQLITE=ON` (default OFF), requiring the
installed SQLite 3.41+ development library. Choose a table, then search, sort or
copy cells. Configure previews and the row limit in preferences; session position
memory can also be disabled there. Only standalone local databases are accepted;
live WAL/journal files show an explanation. See [native format limits](docs/NATIVE_FORMATS.md).

`just benchmark` records five isolated process runs in `build/benchmark.json`.
For the current display use `python3 tests/benchmark.py build/window_tests
--platform wayland`. It measures fresh-process first text, window construction,
resident dispatch/content, a text-to-image transition, idle CPU and current/peak
RSS. OS caches are not dropped and these are local observations, not general
performance claims. See [measurement notes](docs/PERFORMANCE.md).

`just package-snapshot` packages the current working tree with makepkg, a local
source archive and its SHA-256 checksum, retaining artifacts under `pkg/`. It does
not install or publish. Scheduled/manual CI checks current Arch/CachyOS images,
SQLite ON/OFF, tests and formatting, and retains package/source/recipe artifacts.
See [packaging](packaging/arch/README.md) and [desktop integration](docs/DESKTOP_INTEGRATION.md).
