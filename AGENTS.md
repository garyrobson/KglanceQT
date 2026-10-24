# KGlanceQT development

Read AGENTS.local.md if present. This is the independent Qt 6 / KF6 successor to
Mintori09's Kglance. The old implementation is preserved in Git history only.

- Use C++20, Qt 6 and KDE Frameworks 6 public APIs. Do not add Rust/Iced code or
  dependencies from the removed implementation.
- Keep widgets and QObject lifetimes on the GUI thread. Use bounded workers for
  decoding and archive work, and reject stale results after navigation/hide.
- Prefer installed Qt/KDE components over new format engines or custom controls.
- Keep modules small, ownership explicit, and errors visible. Avoid blocking
  subprocesses and heavy parsing on the GUI or DBus dispatch path.
- Preserve the AGPL-3.0-only license and ATTRIBUTION.md.
- Preserve CLI/DBus/config contracts unless the user approves changes.
- Space is the default file-manager/preview activator. Do not require Meta or
  install a global unmodified Space shortcut.
- Never commit or publish unless requested. Never discard user changes.
- Prefer Trash for obsolete files. Never run cargo clean.
- Run `just build`, `just test`, and `just fmt-check` for functional changes.
  Do not poll background jobs; continue independent work or wait for completion
  output from the execution harness.
- Package with makepkg and declared dependencies. Package scripts must not edit
  per-user settings, terminate Dolphin, or silently start a GUI session service.
- Report files changed, validation results and documentation updates.
