# Roadmap

The native application is the only active implementation. Do not reintroduce the
old Rust/Iced tree or parallel UI dependencies to implement missing features.

## Next priorities

- Test Plasma Wayland focus/activation, multi-monitor scaling and theme changes
  interactively on current distribution packages.
- Measure cold launch, resident show, first useful content, transitions, idle CPU,
  and peak/idle memory before making performance claims.
- Validate the desktop context-menu action across custom desktop locations and
  multiple selections. Investigate a supported desktop Space integration without
  intercepting Space in unrelated applications or replacing distribution QML.
- Add native Markdown rendering and image animation while retaining bounded work.
- Evaluate Qt/KDE options for metadata, office/spreadsheet previews and reading
  positions. Keep specialized features optional when their dependency cost is high.
- Add a native preferences interface for shortcuts and preview limits.
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
