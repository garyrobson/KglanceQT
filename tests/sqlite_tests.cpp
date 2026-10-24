// SPDX-License-Identifier: AGPL-3.0-only
#include "sqlitepreview.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>
class SQLiteTests : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void snapshotAndHostileSchema()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("database.db");
        sqlite3 *db = nullptr;
        QCOMPARE(sqlite3_open(QFile::encodeName(path).constData(), &db), SQLITE_OK);
        QCOMPARE(
            sqlite3_exec(
                db,
                "CREATE TABLE \"odd\"\"name\"(id, text, payload, generated AS (id+1)); INSERT INTO "
                "\"odd\"\"name\" VALUES(1,'hello',zeroblob(20000)); CREATE VIEW unsafe AS SELECT "
                "load_extension('oops'); CREATE VIRTUAL TABLE search USING fts5(body);",
                nullptr, nullptr, nullptr),
            SQLITE_OK);
        sqlite3_close(db);
        QFile source(path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto original = source.readAll();
        auto schema = readSQLite(path);
        QVERIFY2(schema.error.isEmpty(), qPrintable(schema.error));
        QCOMPARE(schema.tables, QStringList{QStringLiteral("odd\"name")});
        auto data = readSQLite(path, schema.tables.first());
        QVERIFY2(data.error.isEmpty(), qPrintable(data.error));
        QCOMPARE(data.columns, 3);
        QCOMPARE(data.rows.first().first(), QStringLiteral("1"));
        QVERIFY(data.rows.first().last().contains("20000"));
        QVERIFY(!readSQLite(path, "unsafe").error.isEmpty());
        source.seek(0);
        QCOMPARE(source.readAll(), original);
        QVERIFY(!QFile::exists(path + "-wal"));
        QVERIFY(!QFile::exists(path + "-shm"));
        QVERIFY(!QFile::exists(path + "-journal"));
    }
    void limitsCancellationAndCorruption()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("limits.db");
        sqlite3 *db = nullptr;
        QCOMPARE(sqlite3_open(QFile::encodeName(path).constData(), &db), SQLITE_OK);
        QCOMPARE(sqlite3_exec(db,
                              "CREATE TABLE data(value); INSERT INTO data "
                              "VALUES(printf('%.*c',6000,'x')),('two'),('three');",
                              nullptr, nullptr, nullptr),
                 SQLITE_OK);
        sqlite3_close(db);
        const auto limited = readSQLite(path, "data", {}, 1);
        QVERIFY2(limited.error.isEmpty(), qPrintable(limited.error));
        QCOMPARE(limited.rows.size(), 1);
        QVERIFY(limited.rows.first().first().contains("truncated"));
        std::stop_source stop;
        stop.request_stop();
        QVERIFY(!readSQLite(path, {}, stop.get_token()).error.isEmpty());
        QFile corrupt(dir.filePath("bad.db"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("not sqlite");
        corrupt.close();
        QVERIFY(!readSQLite(corrupt.fileName()).error.isEmpty());
        QCOMPARE(sqlite3_open(QFile::encodeName(path).constData(), &db), SQLITE_OK);
        QCOMPARE(sqlite3_exec(db, "INSERT INTO data VALUES(zeroblob(2000000));", nullptr, nullptr,
                              nullptr),
                 SQLITE_OK);
        sqlite3_close(db);
        QVERIFY(!readSQLite(path, "data").error.isEmpty());
    }
    void walAndJournalRejected()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("wal.db");
        sqlite3 *db = nullptr;
        QCOMPARE(sqlite3_open(QFile::encodeName(path).constData(), &db), SQLITE_OK);
        QCOMPARE(
            sqlite3_exec(
                db,
                "PRAGMA journal_mode=WAL; CREATE TABLE data(value); INSERT INTO data VALUES(42);",
                nullptr, nullptr, nullptr),
            SQLITE_OK);
        QVERIFY(readSQLite(path).error.contains("WAL"));
        sqlite3_close(db);
        QVERIFY(readSQLite(path).error.contains("WAL"));
        QCOMPARE(sqlite3_open(QFile::encodeName(path).constData(), &db), SQLITE_OK);
        QCOMPARE(sqlite3_exec(db, "PRAGMA journal_mode=DELETE;", nullptr, nullptr, nullptr),
                 SQLITE_OK);
        sqlite3_close(db);
        QFile journal(path + "-journal");
        QVERIFY(journal.open(QIODevice::WriteOnly));
        journal.close();
        QVERIFY(readSQLite(path).error.contains("journal"));
    }
    void lockedSource()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("locked.db");
        sqlite3 *db = nullptr;
        QCOMPARE(sqlite3_open(QFile::encodeName(path).constData(), &db), SQLITE_OK);
        QCOMPARE(sqlite3_exec(db, "CREATE TABLE data(value)", nullptr, nullptr, nullptr),
                 SQLITE_OK);
        sqlite3_close(db);
        int ready[2], finish[2];
        QCOMPARE(pipe(ready), 0);
        QCOMPARE(pipe(finish), 0);
        const auto pid = fork();
        QVERIFY(pid >= 0);
        if (pid == 0)
        {
            close(ready[0]);
            close(finish[1]);
            sqlite3_open(QFile::encodeName(path).constData(), &db);
            const char ok =
                sqlite3_exec(db, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK;
            write(ready[1], &ok, 1);
            char done;
            read(finish[0], &done, 1);
            sqlite3_close(db);
            _exit(0);
        }
        close(ready[1]);
        close(finish[0]);
        char ok = 0;
        const auto count = read(ready[0], &ok, 1);
        const auto result = readSQLite(path);
        write(finish[1], "x", 1);
        close(ready[0]);
        close(finish[1]);
        waitpid(pid, nullptr, 0);
        QCOMPARE(count, 1);
        QVERIFY(ok);
        QVERIFY(result.error.contains("locked"));
    }
};
QTEST_GUILESS_MAIN(SQLiteTests)
#include "sqlite_tests.moc"
