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
- [ ] Add an optional selection filmstrip with thumbnails and the current item
  highlighted. Bound thumbnail work and reject results from previous selections.
- [ ] Extend session position memory to text/Markdown scroll positions, Markdown
  mode, document pages where supported by public APIs, and media playback positions.
  Make any persistent history optional and provide a way to clear it.
- [ ] Support dragging the current file into other applications using file URLs.
- [ ] Play animated images supported by installed Qt plugins, with pause/resume,
  bounded decoding and frame memory, and cancellation on navigation/hide.
- [ ] Refresh changed files with debounced filesystem monitoring, preserving the
  viewing position where possible and showing clear missing/replaced-file states.

### Comparison and file triage

- [ ] Compare two images side by side, with optional linked zoom/pan, dimensions,
  and metadata differences. Evaluate text comparison as a later extension.
- [ ] Move the current file to Trash, advance through the selection, and offer
  Undo using KDE file operations. Reflect failures and restore selection correctly.
- [ ] Add explicit editing of KDE file ratings, tags, and comments in the sidebar,
  sharing metadata with Dolphin and reporting unsupported filesystems/write errors.

### More locations and formats

- [ ] Preview CSV/TSV as searchable, sortable tables with selectable cells, sensible
  column sizing, bounded parsing, and visible row/column truncation limits.
- [ ] Preview remote KIO URLs such as SFTP/SMB using bounded, cancellable temporary
  transfers with clear progress, cleanup, and no stale results after navigation.
- [ ] Evaluate native office/spreadsheet, SQLite table, and reading-position support;
  keep specialized features optional when their dependency cost is high.

### KDE polish and validation

- [ ] Adopt KDE standard actions/shortcuts where appropriate and provide configurable
  application shortcuts while preserving Space as the default preview activator.
- [ ] Audit keyboard focus, accessible names, screen-reader behaviour, and Breeze
  light/dark appearance, including readable previews in custom colour schemes.

## Integration and release priorities

- Test Plasma Wayland focus/activation, multi-monitor scaling and theme changes
  interactively on current distribution packages.
- Measure cold launch, resident show, first useful content, transitions, idle CPU,
  and peak/idle memory before making performance claims.
- Validate the desktop context-menu action across custom desktop locations and
  multiple selections. Investigate a supported desktop Space integration without
  intercepting Space in unrelated applications or replacing distribution QML.
- Extend preferences as new providers and reading-position options are added.
- Automate package builds against current Arch/CachyOS dependencies and publish
  application releases with source archives and package recipes.

## Architecture constraints

Qt owns the GUI thread and widget lifetimes. The DBus adaptor exposes only the
preview contract. Worker results are toolkit-independent where practical;
generation/cancellation tokens protect navigation from late completions.
The window is hidden and reused; active playback and heavy content are released.

Consume installed shared libraries and plugins. Avoid private KDE/Qt APIs,
vendored format engines, system-QML patches, and root-run automatic rebuild hooks.
Preserve upstream history, attribution and license notices.
