# Desktop selection integration

KGlanceQT installs the standard KIO service menu `kglanceqt.desktop` with
`Exec="/usr/bin/kglanceqt" -- %U`. `%U` passes all selected URLs; `--` protects
filenames beginning with a dash. No shell is involved. Dolphin and Plasma Folder
View discover this action through KDE's installed service-menu mechanism.

Automated checks cover discovery with one file, mixed file types and multiple
`desktop:/` URLs. The window suite resolves an XDG desktop directory located
outside `~/Desktop`, including spaces and `#` in filenames, and navigates between
two selected URLs. The desktop KIO worker is optional; that resolution check skips
when it is absent. These checks do not simulate a click in the real Plasma menu.

## Space investigation

Inspected the installed Plasma 6.8.1 Folder View `FolderView.qml` on 24 October
2026 and its public integration surface. The keyboard handler owns navigation,
selection, standard file actions and type-ahead. No supported configuration hook
for binding an external service-menu action to unmodified Space was found.
Dolphin's configurable service actions are a separate integration; applying its
shortcut does not configure the desktop.

The supported desktop path remains **Preview with KGlanceQT** in the selected
files' context menu. Keyboard users can open the context menu with the Menu key
and select that action. A future desktop Space binding needs an upstream Folder
View action/shortcut extension that receives its current selection and activation
token. Do not patch distribution QML, inspect unrelated windows, or install a
global Space listener.

References: [upstream Folder View](https://invent.kde.org/plasma/plasma-desktop/-/blob/master/containments/desktop/package/contents/ui/FolderView.qml),
[KIO service menus](https://develop.kde.org/docs/apps/dolphin/service-menus/),
[Dolphin service-menu shortcuts](https://blogs.kde.org/2026/05/03/dolphin-26.04-release/).

## Human release check

With the user's usual and a custom Folder View location, select two files with
spaces/non-ASCII characters, invoke the context action, verify focus and both
navigation directions, then hide and repeat. The action should receive exactly
that selection. Repeat via the keyboard context menu. No desktop configuration
is changed by the automated suite.
