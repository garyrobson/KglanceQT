# Roadmap

The native application is the only active implementation. Do not reintroduce the
old Rust/Iced tree or parallel UI dependencies to implement missing features.

## Planned usability milestones

The first usability milestone now includes native Markdown preview/source views,
an optional KDE file information sidebar, and native preferences for playback,
preview defaults, limits, and Dolphin shortcut configuration.

### Everyday previewing

- [x] Move Quit out of the preview toolbar into an application menu. Keep Hide
  readily accessible and explain that Quit stops the background preview service.
- [x] Add native Open With, Show in File Manager, and Copy Location actions.
- [x] Remember image zoom, pan, and fit mode while navigating a selection. Keep
  this state in memory and clear it when the preview is hidden or a new selection opens.
- [x] Add an optional selection filmstrip with thumbnails and the current item
  highlighted. Bound thumbnail work and reject results from previous selections.
- [x] Extend session position memory to text/Markdown scroll positions, Markdown
  mode, document pages where supported by public APIs, and media playback positions.
  Make any persistent history optional and provide a way to clear it.
  Implemented as session-only memory, with a Clear Remembered Positions action;
  no persistent position history is stored.
- [x] Support dragging the current file into other applications using file URLs.
- [x] Play animated images supported by installed Qt plugins, with pause/resume,
  bounded decoding and frame memory, and cancellation on navigation/hide.
- [x] Refresh changed files with debounced filesystem monitoring, preserving the
  viewing position where possible and showing clear missing/replaced-file states.

### Comparison and file triage

- [x] Compare two images side by side, with optional linked zoom/pan, dimensions,
  and metadata differences. Evaluate text comparison as a later extension.
- [x] Move the current file to Trash, advance through the selection, and offer
  Undo using KDE file operations. Reflect failures and restore selection correctly.
- [x] Add explicit editing of KDE file ratings, tags, and comments in the sidebar,
  sharing metadata with Dolphin and reporting unsupported filesystems/write errors.

### More locations and formats

- [x] Preview CSV/TSV as searchable, sortable tables with selectable cells, sensible
  column sizing, bounded parsing, and visible row/column truncation limits.
- [x] Preview remote KIO URLs such as SFTP/SMB using bounded, cancellable temporary
  transfers with clear progress, cleanup, and no stale results after navigation.
- [x] Evaluate native office/spreadsheet, SQLite table, and reading-position support;
  keep specialized features optional when their dependency cost is high.
  Evaluation and decisions: [native formats](docs/NATIVE_FORMATS.md). Table reading
  positions and the optional SQLite provider are implemented; rendered Office
  previews remain separate work.
- [x] Implement an optional bounded, read-only SQLite provider. Local standalone
  snapshots are supported; live WAL/journal databases are explicitly rejected.
  See [provider policy and limits](docs/NATIVE_FORMATS.md).

### KDE polish and validation

- [x] **High priority:** Create a distinctive application icon that communicates
  quick file previewing and looks at home in Plasma's Breeze theme. Provide a
  scalable icon with clear small-size variants, verify it against light and dark
  panels, and use it consistently in the launcher, task switcher, and window.
- [x] Adopt KDE standard actions/shortcuts where appropriate and provide configurable
  application shortcuts while preserving Space as the default preview activator.
- [x] Audit keyboard focus, accessible names, screen-reader behaviour, and Breeze
  light/dark appearance, including readable previews in custom colour schemes.

## Integration and release priorities

- [x] Validate Plasma Wayland focus/activation, monitor transitions, mixed scaling
  and theme changes on current packages. Automated native runs cover the actual
  2× display and an isolated two-output KWin at 1×/1.5×; see
  [validation coverage](docs/PLASMA_VALIDATION.md).
- [ ] Repeat multi-monitor checks with physical mixed-DPI outputs/hot-plugging
  and verify spoken output with Orca before release.
- [x] Add repeatable local measurements for daemon startup, resident show, first
  useful content, transitions, idle CPU and RSS. Recorded samples and measurement
  limits: [performance](docs/PERFORMANCE.md).
- [x] Validate KIO context-menu discovery across single/multiple selections and
  custom desktop URL navigation. Investigate supported desktop Space integration;
  no exposed Folder View hook was found. [Details](docs/DESKTOP_INTEGRATION.md).
- [ ] Complete the human Plasma menu/input-activation checks described in the
  desktop integration notes.
- [x] Add SQLite enable/row limits and optional session position memory to preferences.
- [x] Automate working-tree makepkg builds and configure scheduled Arch/CachyOS
  CI with SQLite enabled/disabled, retaining source archives and package recipes.
- [x] Run the expanded Arch/CachyOS × SQLite ON/OFF matrix on GitHub, including
  unprivileged package builds and uploaded artifacts. Passed for `63788ca` in
  [run 40134115688](https://github.com/garyrobson/KglanceQT/actions/runs/40134115688).
- [ ] Complete the remaining human desktop/hardware/accessibility checks before
  a stable release. RC1 is published for testing with these limits disclosed in
  the [release notes](docs/RELEASE_CANDIDATE.md).

## Architecture constraints

Qt owns the GUI thread and widget lifetimes. The DBus adaptor exposes only the
preview contract. Worker results are toolkit-independent where practical;
generation/cancellation tokens protect navigation from late completions.
The window is hidden and reused; active playback and heavy content are released.

Consume installed shared libraries and plugins. Avoid private KDE/Qt APIs,
vendored format engines, system-QML patches, and root-run automatic rebuild hooks.
Preserve upstream history, attribution and license notices.
