// SPDX-License-Identifier: AGPL-3.0-only

#include "dolphinsetup.h"

#include <QDomDocument>
#include <QFile>
#include <QMap>
#include <QTemporaryDir>
#include <QtTest>

class ShortcutTests final : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void migrateAndPreserveOtherActions()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("dolphinui.rc"));
        const QByteArray original =
            "<!DOCTYPE gui SYSTEM 'kpartgui.dtd'><gui name='dolphin' version='49'>"
            "<ActionProperties scheme='Default'>"
            "<Action name='servicemenu_kglance-rust.desktop::previewWithRust' shortcut='Space'/>"
            "<Action name='servicemenu_kglanceqt.desktop::previewWithKGlanceQT' "
            "shortcut='Ctrl+Alt+Space'/>"
            "<Action name='keep' shortcut='Ctrl+P' priority='0'/>"
            "</ActionProperties></gui>";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), original.size());
        file.close();
        const auto result = configureDolphin(path, QStringLiteral("Space"));
        QCOMPARE(result.error, QString{});
        QVERIFY(!result.backup.isEmpty());
        QFile backup(result.backup);
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), original);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QDomDocument document;
        QVERIFY(document.setContent(file.readAll()));
        file.close();
        QMap<QString, QString> bindings;
        const auto actions = document.elementsByTagName(QStringLiteral("Action"));
        for (int i = 0; i < actions.size(); ++i)
        {
            const auto action = actions.at(i).toElement();
            bindings.insert(action.attribute(QStringLiteral("name")),
                            action.attribute(QStringLiteral("shortcut")));
        }
        const QMap<QString, QString> expected{
            {QStringLiteral("keep"), QStringLiteral("Ctrl+P")},
            {QStringLiteral("servicemenu_kglanceqt.desktop::previewWithKGlanceQT"),
             QStringLiteral("Space")}};
        QCOMPARE(bindings, expected);
        const auto repeated = configureDolphin(path, QStringLiteral("Space"));
        QCOMPARE(repeated.error, QString{});
        QCOMPARE(repeated.backup, QString{});
    }

    void conflictingShortcutDoesNotWrite()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("dolphinui.rc"));
        const QByteArray original =
            "<gui name='dolphin'><Action name='other' shortcut='Space'/></gui>";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), original.size());
        file.close();
        const auto result = configureDolphin(path, QStringLiteral("Space"));
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.backup, QString{});
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }
};

QTEST_GUILESS_MAIN(ShortcutTests)
#include "shortcut_tests.moc"
