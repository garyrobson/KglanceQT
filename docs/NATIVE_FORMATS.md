# Native formats and position support

Evaluation on 24 October 2026: Qt 6.12.0, KDE Frameworks 6.31,
Okular 26.08.2, SQLite 3.54.0 on CachyOS/Arch.

| Format | Installed public component | Decision |
| --- | --- | --- |
| PDF / supported document formats | Okular read-only KPart | Keep native embedding; page memory uses public scriptable page methods only. |
| ODT / ODS / DOCX / XLSX | KParts discovery advertises `arkpart` on this machine; no compatible document-viewer KPart | Archive support is not a rendered Office viewer. Keep installed thumbnails and Open in application. The supported-types dialog checks for a compatible Okular provider at runtime. |
| CSV / TSV | Existing bounded worker parser and Qt table | Keep native table preview; add session memory for search, sort, current cell and both scroll positions. |
| SQLite | Installed Qt SQL driver and SQLite shared library | Implemented behind `KGLANCEQT_WITH_SQLITE` (upstream default OFF; local builds and Arch package enable it). Uses the installed SQLite library. |

Discovery used `KParts::PartLoader::partsForMimeType()` for the four Office
MIME types and PDF. PDF reported `okularpart`; the Office formats reported only
`arkpart`. Distribution plugins can change these results. Installing a full
Office suite alone does not provide an embeddable Qt 6 read-only KPart.
No Office conversion subprocess, editor embedding or new format engine is added.

## SQLite implementation decision

The optional provider uses the installed SQLite library (3.41+) with a five-second
progress deadline and navigation/hide cancellation. It runs on the existing
bounded worker pool; QObject/widget creation and results stay on the GUI thread.
Destroyed views disconnect results, so late completions cannot replace content.

Only local, standalone rollback-journal databases are supported. The worker opens
the source read-only and takes an open-file-description shared lock across
SQLite's lock region before reading. This detects conflicting writer locks and
avoids process-wide lock release when another worker closes a file. It copies at
most 512 MiB to a private temporary directory, checks file identity, size, time
and journal presence, then opens only the copy with `SQLITE_OPEN_READONLY`.
Temporary storage is removed on success, cancellation and failure. The source is
never opened by SQLite and no source journal/SHM is created.

WAL-mode headers and existing WAL/rollback journals cause an explicit unsupported
message. Export a standalone rollback-journal copy with the database's owning
application. Do not remove journals manually or use SQLite's immutable flag on
live source files. Remote SQLite downloads are rejected because related journals
may be missing. Snapshotting uses the normal SQLite locking convention on Linux;
filesystems that cannot honor that lock return an error.

`table_list` excludes views, virtual and shadow tables. At most 128 table names
and 256 stored columns are retained (names over 1 KiB are omitted); generated/hidden columns are excluded.
Identifiers are quoted, table choices are checked against the discovered list,
and no user SQL is accepted. An authorizer allows only SELECT and direct READ
operations for the table query. Extensions and trusted-schema execution are
disabled; defensive mode is enabled.

Limits: 1,000 rows by default (configurable 1–10,000), 100,000 cells, 16 MiB total
text, 4 KiB per text cell and blob byte-count summaries. Engine limits cap value
length at 1 MiB, SQL at 64 KiB, expression depth at 32 and compiled operations at
100,000, with a 2 MiB page cache and mmap/attached databases/internal workers
disabled. SQLite rejects rows containing larger values even if they would have
been displayed as a blob summary. Limit notices and errors are visible. This is
a preview of the bounded first rows; sorting/searching apply to those rows only.
The five-second deadline is cooperative and cannot interrupt an OS-blocked read.

Tests cover quoted identifiers, generated columns, hostile views/virtual tables,
large text/blobs, row limits, corruption, cancellation, WAL/journal rejection,
conflicting source locks, byte preservation, table switching and hide during work.
API references: [progress callback](https://www.sqlite.org/c3ref/progress_handler.html),
[authorizer](https://www.sqlite.org/c3ref/set_authorizer.html),
[WAL read-only constraints](https://www.sqlite.org/wal.html).

## Reading positions

Positions stay in memory for the current selection. Text/Markdown, image views,
media, and public Okular page positions already use that policy. CSV/TSV now also
restore query, sort, current cell and horizontal/vertical scroll. Restoration
waits for parsing and clamps indices after changes. Clear Remembered Positions,
Hide and a new selection clear these positions; no persistent reading history is
created. Embedded viewers may retain their own settings independently.

Preferences can disable session position memory entirely. Disabling it clears
remembered positions and subsequent previews start at their defaults. No
persistent history is written. SQLite table selection/scroll positions currently
reset on navigation; CSV/TSV session memory remains available when enabled.
