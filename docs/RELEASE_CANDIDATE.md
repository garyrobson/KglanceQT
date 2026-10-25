# KGlanceQT 0.3.0-rc.1

Release tag: `qt-v0.3.0-rc.1`. GitHub prerelease, intended for testing.
Publication was explicitly requested on 25 October 2026, with the remaining
human validation checks disclosed below. They are not marked as passed.

## Changes since alpha.1

- Selection filmstrip, drag-to-application, animated image playback and session
  viewing positions, with optional position memory and a clear action.
- Automatic refresh after file changes, image comparison, native Trash/Undo,
  and KDE ratings, tags and comments shared with Dolphin.
- Searchable/sortable CSV and TSV tables with cell copy, and cancellable remote
  KIO downloads with bounded temporary storage.
- Optional read-only SQLite table previews from bounded local snapshots, with
  provider and row-limit preferences. Live WAL/journal databases, remote SQLite,
  views and virtual tables are excluded; generated columns are not evaluated.
- Original Breeze-compatible application icon, configurable KDE shortcuts,
  improved input focus/accessibility names and live light/dark/custom palettes.
- Arch/CachyOS CI with SQLite enabled/disabled, makepkg package checks, source
  archives and verified recipes. Reproducible local performance measurements.

Space remains Dolphin's default preview action; no global Space is registered.
Rendered Office-document support still depends on compatible installed viewers.
AGPL-3.0-only licensing and original-project attribution are retained.

## Validation and known limits

Local build, seven enabled test suites, six SQLite-disabled suites, formatting,
makepkg, current-display Wayland and isolated mixed-scale checks have passed.
The four-way Arch/CachyOS matrix passed on the implementation checkpoint; the
release-specific run is recorded with the GitHub release assets.

These human checks remain outstanding:

- Plasma context-menu invocation in usual/custom desktop folders: multiple
  selections, real input activation/focus and keyboard invocation.
- Physical mixed-DPI hot-plugging, docking and suspend/resume.
- Spoken Orca output and assistive-technology keyboard traversal.

Only one physical panel was available and Orca was not installed. Automated
virtual-output and accessible-role checks do not establish those human results.
SQLite previews deliberately reject live WAL/journal databases; export a
standalone rollback-journal copy using the owning application.

See [desktop procedure](DESKTOP_INTEGRATION.md),
[hardware/accessibility checks](PLASMA_VALIDATION.md),
[SQLite limits](NATIVE_FORMATS.md) and [measurement notes](PERFORMANCE.md).

## Source and packaging

The release supplies a tagged source archive, SHA256SUMS, PKGBUILD, .SRCINFO and
separate Arch/CachyOS x86_64 packages. Install only one distribution package.
Use the recipe attached to the release or current main: the recipe embedded in
the immutable source tag may refer to the preceding release, since the tagged
archive checksum can only be filled after the tag exists. Package scripts do not
change user settings or automatically start a service.

CI runs on branch pushes, PRs, schedules and manual dispatch. Tag pushes do not
start a duplicate matrix; the final branch recipe update tests the same app source
as the tag. The only post-tag source differences are packaging metadata.
