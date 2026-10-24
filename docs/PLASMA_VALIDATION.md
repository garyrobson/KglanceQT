# Plasma and accessibility validation

Validation target: CachyOS/Arch on 24 October 2026. Installed versions:
Qt 6.12.0, KDE Frameworks 6.31, Plasma/KWin/KScreen 6.8.1.

## Coverage

| Check | Method | Scope |
| --- | --- | --- |
| Window show, navigation and hide | Native Wayland Qt test on the current session | Existing 3840×2160 panel, 1920×1080 logical area, 2× scale; no monitor reconfiguration. |
| Activation-token handoff | KDE public token API, Hide, then ShowPreview with that token | Focus restoration asserted on the current session and nested KWin. Initial test-window focus is set by an owned-PID KWin script. |
| Output transitions | Isolated KWin, two virtual outputs, public KWin scripting API | Preview moved to each output; Qt screen association, exposure and window icon checked. |
| Fractional scaling | Private KScreen configuration, then the same native lifecycle checks | Uniform 1× followed by mixed 1×/1.5×. Qt may report integer output scale while the window uses fractional scale. |
| Theme changes | Breeze style, native Light/Dark schemes and a custom navy/yellow palette; Light/Dark changes on native Wayland too | Preview messages, table/search and native toolbar text/icons reviewed; live palette inheritance checked. |
| Accessibility | Qt accessible interfaces and keyboard input tests | Search field EditableText role/name, table name; named image/metadata controls; native widget keyboard behaviour. |
| Shortcut configuration | Native KDE editor, isolated config and remapped refresh tests | Window shortcuts, protected Space, configuration persistence and compatibility with embedded text views. |
| Reading positions | Navigate away/back, then clear session positions | CSV query, sort, current cell and horizontal/vertical scrolling. |
| Icon | Native-size contact sheet on light/dark backgrounds; window icon and package metadata | Scalable source, simple small variant, PNGs at 16–256 pixels; shared desktop/window icon name. |

KDE icon-theme initialization runs before QApplication so native icon recoloring
works even without a KDE platform-theme plugin. Preview surfaces and audio
controls inherit the current palette rather than capturing fixed colours.

## Keyboard findings and fixes

The previous window-level Space shortcut could hide the preview while a search
field was being edited. Plain arrows could also navigate files while table cells
were focused. Inputs, buttons and item views now retain native Space/arrow
behaviour. File navigation remains available through Alt+Left/Alt+Right and the
configurable Previous/Next actions. The window-only Space toggle and Dolphin's
separate default Space activator remain intact; no global Space is installed.

The filmstrip supports Enter activation. Native controls provide focus indicators
and accessibility roles. Image previews and metadata inputs have explicit names;
table search exposes an EditableText role and a meaningful name.

## Reproduction and isolation

Run `just wayland-test` for nested validation or `just wayland-current-test` for
the existing display. Both require the installed KDE/Wayland tools described in
the README; ordinary `just test` uses offscreen rendering and skips the explicit
native compositor case.

The nested wrapper creates its private runtime/config before starting its DBus
session, so activated helpers inherit that isolation too. It stages the current
desktop file and icon PNGs, verifies the KWin DBus owner's PID before changing
virtual output scales, and disables unrelated desktop portal access for the
headless renderer. A test-created document-portal FUSE mount is detached during
cleanup if one exists. Current-display validation does not change output settings
or contact the running preview daemon. The normal KWin renderer is required here:
a forced raster renderer on KWin 6.8.1 crashed in its startup-feedback path when
requesting an activation token for a known desktop application. This is avoided
by leaving renderer selection to KWin. KWin scripts select only the test PID and
are unloaded immediately after activation/movement.

Generated theme screenshots live in `build/polish-screenshots/` when
`KGLANCEQT_TEST_SCREENSHOTS` is set. The nested KWin log is retained under the
binary's `test-wayland-nested/` directory. Tests do not record desktop screenshots
or windows belonging to other applications.

## Remaining release checks

- Physical mixed-DPI monitor hot-plugging, docking, rotated outputs and suspend /
  resume require hardware validation beyond the current single physical panel.
- Qt accessibility roles/names were audited programmatically; spoken Orca output
  and complete assistive-technology navigation still need a human session.
- The activation check exercises the native token path after compositor focus.
  Distribution-specific file-manager/desktop launchers must also be checked with
  real input serials and their own activation tokens before release.

These limits are retained as release work; virtual-output tests do not establish
physical hot-plug or spoken screen-reader behaviour.

## API references

The integration uses KDE's [public KWin scripting API](https://develop.kde.org/docs/plasma/kwin/api/)
for owned-window activation and output movement, and the installed public
KWaylandExtras activation-token API. No private Qt/KWin APIs or system scripts
are patched.

## Current batch hardware and assistive-technology checks

The implementation batch also checks installed tools: Orca is not installed on
this system. No screen-reader settings were changed or speech session started.
Physical hot-plugging and spoken output remain explicit release gates; software
checks cannot substitute for either observation.

For the physical check, keep this app visible while connecting a second output,
set different scales in Plasma Display Configuration, move the preview between
them, unplug/reconnect the second display and repeat after suspend/resume. Check
placement, focus, readable text/icons, no lost window and no stale preview. Record
output models, scales and results here when hardware is available.

For Orca, use an enabled human speech session: open text, a table and SQLite;
Tab/Shift+Tab through toolbar, table chooser, search and cells; check names, roles,
cell contents, selection changes and error messages, then Hide/reopen. Confirm
that typing Space in search does not hide the window. Mark speech coverage only
after a listener confirms these announcements.
