# Attribution and project lineage

KGlanceQT is Gary Robson's independently maintained Qt 6 / KDE Frameworks 6
successor to [Kglance (Oxiview)](https://github.com/Mintori09/kglance), created by
**Mintori (Mintori09)**. The original project established the Quick Look-style
workflow, file-manager integration, and client/daemon design that inspired this
replacement.

This repository was created as a GitHub fork and retains the original Git history
and author information. The active source tree now contains the Qt/KDE
implementation; the former Rust/Iced implementation remains available in history.
KGlanceQT is not an upstream Kglance release and is maintained separately.

The original project's **GNU Affero General Public License v3.0 only
(AGPL-3.0-only)** is retained. See [LICENSE](LICENSE). Existing copyright and
attribution notices in retained material must be preserved. New source files use
the same license unless explicitly marked otherwise.

Qt, KDE Frameworks, Okular, KTextEditor, and installed format plugins are separately
maintained dependencies with their own authors and licenses. They are consumed
through distribution-provided shared libraries and plugins, not vendored here.

The document-and-eye application icon and its small-size variant are original
artwork created for KGlanceQT, distributed under AGPL-3.0-only. PNG sizes are
rendered from the SVG sources with the distribution's librsvg tools.
