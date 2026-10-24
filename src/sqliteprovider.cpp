// SPDX-License-Identifier: AGPL-3.0-only
#include "sqlitepreview.h"
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <fcntl.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>
namespace
{
struct Budget
{
    std::stop_token stop;
    QDeadlineTimer deadline{5000};
    bool expired() const { return stop.stop_requested() || deadline.hasExpired(); }
};
QString quoted(QString name)
{
    name.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + name + QLatin1Char('"');
}
} // namespace
SQLiteResult readSQLite(const QString &path, const QString &table, std::stop_token stop,
                        int maxRows)
{
    SQLiteResult result;
    Budget budget{stop};
    auto fail = [&](const QString &error)
    {
        result.error = error;
        return result;
    };
    QFile source(path);
    const QFileInfo before(path);
    if (!before.isFile() || before.size() > 512LL * 1024 * 1024)
        return fail(QObject::tr("SQLite preview requires a regular database of at most 512 MiB."));
    if (!source.open(QIODevice::ReadOnly))
        return fail(source.errorString());
    // Hold SQLite's shared/pending lock region while copying. Never create a journal.
    struct flock lock{};
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0x40000000;
    lock.l_len = 512;
    if (fcntl(source.handle(), F_OFD_SETLK, &lock) == -1)
        return fail(QObject::tr("The database is locked or cannot be safely snapshotted."));
    const auto unlock = qScopeGuard(
        [&]
        {
            lock.l_type = F_UNLCK;
            fcntl(source.handle(), F_OFD_SETLK, &lock);
        });
    const auto header = source.peek(100);
    if (!header.startsWith(QByteArray("SQLite format 3\0", 16)))
        return fail(QObject::tr("This is not a supported SQLite database."));
    auto journals = [&]
    {
        return QFileInfo::exists(path + QStringLiteral("-wal")) ||
               QFileInfo::exists(path + QStringLiteral("-journal"));
    };
    if (header.size() < 100 || header[18] == 2 || header[19] == 2 || journals())
        return fail(QObject::tr("Live WAL or journal databases are not previewed. Close the writer "
                                "and export a standalone rollback-journal database."));
    QTemporaryDir directory;
    QFile snapshot(directory.filePath(QStringLiteral("preview.db")));
    if (!directory.isValid() || !snapshot.open(QIODevice::WriteOnly))
        return fail(QObject::tr("Cannot create a private database snapshot."));
    qint64 copied = 0;
    while (!source.atEnd())
    {
        if (budget.expired())
            return fail(
                QObject::tr("SQLite preview cancelled or exceeded its five-second deadline."));
        auto bytes = source.read(65536);
        copied += bytes.size();
        if (bytes.isEmpty() || copied > 512LL * 1024 * 1024 ||
            snapshot.write(bytes) != bytes.size())
            return fail(QObject::tr("Cannot copy the database within the preview limits."));
    }
    snapshot.close();
    const QFileInfo after(path);
    struct stat openedStat{}, pathStat{};
    if (fstat(source.handle(), &openedStat) != 0 ||
        stat(QFile::encodeName(path).constData(), &pathStat) != 0 ||
        openedStat.st_ino != pathStat.st_ino || openedStat.st_dev != pathStat.st_dev)
        return fail(QObject::tr("The database was replaced while snapshotting."));
    if (journals() || before.size() != after.size() ||
        before.lastModified() != after.lastModified())
        return fail(QObject::tr(
            "The database changed during snapshotting. Retry after closing its writer."));
    sqlite3 *db = nullptr;
    const int opened = sqlite3_open_v2(QFile::encodeName(snapshot.fileName()).constData(), &db,
                                       SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    const auto close = qScopeGuard(
        [&]
        {
            if (db)
                sqlite3_close(db);
        });
    if (opened != SQLITE_OK)
        return fail(QObject::tr("Cannot open the read-only database snapshot."));
    sqlite3_progress_handler(
        db, 1000, [](void *p) { return int(static_cast<Budget *>(p)->expired()); }, &budget);
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_enable_load_extension(db, 0);
    sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 1024 * 1024);
    sqlite3_limit(db, SQLITE_LIMIT_SQL_LENGTH, 65536);
    sqlite3_limit(db, SQLITE_LIMIT_COLUMN, 256);
    sqlite3_limit(db, SQLITE_LIMIT_EXPR_DEPTH, 32);
    sqlite3_limit(db, SQLITE_LIMIT_VDBE_OP, 100000);
    sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
    sqlite3_limit(db, SQLITE_LIMIT_WORKER_THREADS, 0);
    sqlite3_exec(db, "PRAGMA cache_size=-2048; PRAGMA mmap_size=0;", nullptr, nullptr, nullptr);
    auto query = [&](const QString &sql, auto consume)
    {
        sqlite3_stmt *statement = nullptr;
        const auto finalize = qScopeGuard([&] { sqlite3_finalize(statement); });
        if (budget.expired() ||
            sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &statement, nullptr) != SQLITE_OK)
            return false;
        int code;
        while ((code = sqlite3_step(statement)) == SQLITE_ROW)
        {
            if (budget.expired())
                return false;
            consume(statement);
        }
        return code == SQLITE_DONE;
    };
    // table_list identifies virtual/shadow tables without parsing user schema SQL.
    if (!query(QStringLiteral("PRAGMA main.table_list"),
               [&](sqlite3_stmt *s)
               {
                   const auto name =
                       QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 1)));
                   const auto type =
                       QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(s, 2)));
                   if (type == QLatin1String("table") &&
                       !name.startsWith(QLatin1String("sqlite_")) && result.tables.size() < 128 &&
                       sqlite3_column_bytes(s, 1) <= 1024)
                       result.tables << name;
               }))
        return fail(QObject::tr("SQLite schema is corrupt, unsupported, too large or timed out."));
    result.tables.sort();
    result.notice = QObject::tr(
        "Read-only snapshot; up to 128 ordinary tables. Views and virtual tables are excluded.");
    if (table.isEmpty())
        return result;
    if (!result.tables.contains(table))
        return fail(QObject::tr("The selected ordinary table is no longer available."));
    if (!query(QStringLiteral("PRAGMA main.table_xinfo(%1)").arg(quoted(table)),
               [&](sqlite3_stmt *s)
               {
                   if (sqlite3_column_int(s, 6) == 0 && result.headers.size() < 256 &&
                       sqlite3_column_bytes(s, 1) <= 1024)
                       result.headers << QString::fromUtf8(
                           reinterpret_cast<const char *>(sqlite3_column_text(s, 1)));
               }) ||
        result.headers.isEmpty())
        return fail(QObject::tr("No supported stored columns were found."));
    // Deny schema expressions, functions, views, writes, and all other operations.
    sqlite3_set_authorizer(
        db,
        [](void *, int action, const char *, const char *, const char *, const char *origin)
        {
            return !origin && (action == SQLITE_SELECT || action == SQLITE_READ) ? SQLITE_OK
                                                                                 : SQLITE_DENY;
        },
        nullptr);
    QStringList columns;
    for (const auto &name : result.headers)
        columns << quoted(name);
    maxRows = qBound(1, maxRows, 10000);
    const int limit = qMin(maxRows, 100000 / int(columns.size()));
    qint64 textBytes = 0;
    if (!query(QStringLiteral("SELECT %1 FROM main.%2 LIMIT %3")
                   .arg(columns.join(QLatin1Char(',')), quoted(table))
                   .arg(limit + 1),
               [&](sqlite3_stmt *s)
               {
                   if (result.rows.size() >= limit || textBytes >= 16 * 1024 * 1024)
                       return;
                   QStringList row;
                   for (int i = 0; i < columns.size(); ++i)
                   {
                       QString value;
                       const auto type = sqlite3_column_type(s, i);
                       if (type == SQLITE_NULL)
                           value = QStringLiteral("NULL");
                       else if (type == SQLITE_BLOB)
                           value = QObject::tr("[BLOB: %1 bytes]").arg(sqlite3_column_bytes(s, i));
                       else
                       {
                           const auto *data = sqlite3_column_text(s, i);
                           const int bytes = sqlite3_column_bytes(s, i);
                           value = QString::fromUtf8(reinterpret_cast<const char *>(data),
                                                     qMin(bytes, 4096));
                           if (bytes > 4096)
                               value += QObject::tr("… [truncated]");
                       }
                       textBytes += value.size() * 2;
                       row << value;
                   }
                   result.rows << row;
               }))
        return fail(QObject::tr("SQLite preview failed: %1. The database may be corrupt, "
                                "oversized, unsupported or timed out.")
                        .arg(QString::fromUtf8(sqlite3_errmsg(db))));
    result.columns = result.headers.size();
    result.notice += QObject::tr(" Showing up to %1 rows / 100,000 cells / 16 MiB of text; text "
                                 "cells are capped at 4 KiB and blobs summarized.")
                         .arg(limit);
    return result;
}
