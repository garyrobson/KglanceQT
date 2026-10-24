// SPDX-License-Identifier: AGPL-3.0-only

#include "dolphinsetup.h"
#include "previewservice.h"
#include "previewwindow.h"

#include <KIconTheme>
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QImageReader>
#include <QStandardPaths>
#include <QTextStream>
#include <QThreadPool>

int main(int argc, char **argv)
{
    KIconTheme::initTheme();
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("KGlanceQT"));
    app.setOrganizationName(QStringLiteral("kglanceqt"));
    app.setApplicationVersion(QStringLiteral(KGLANCEQT_VERSION));
    app.setDesktopFileName(QStringLiteral("org.kglanceqt.KGlanceQT"));
    app.setQuitOnLastWindowClosed(false);
    QImageReader::setAllocationLimit(256);
    QThreadPool::globalInstance()->setMaxThreadCount(2);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("KGlanceQT — Qt 6 / KDE file preview"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(
        {QStringLiteral("configure-dolphin"),
         QStringLiteral(
             "Configure the current user's Dolphin preview shortcut (default: Space).")});
    parser.addOption({QStringLiteral("shortcut"),
                      QStringLiteral("Shortcut to use with --configure-dolphin."),
                      QStringLiteral("key"), QStringLiteral("Space")});
    parser.addOption({QStringLiteral("daemon"),
                      QStringLiteral("Wait for preview requests without opening a window.")});
    parser.addOption({QStringLiteral("standalone"),
                      QStringLiteral("Use an independent window and exit when closed.")});
    parser.addOption(
        {QStringLiteral("quit"), QStringLiteral("Stop the KGlanceQT preview service.")});
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("Local paths or file URLs to preview."),
                                 QStringLiteral("[files…]"));
    parser.process(app);
    if (parser.isSet(QStringLiteral("configure-dolphin")))
    {
        const auto path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                          QStringLiteral("/kxmlgui5/dolphin/dolphinui.rc");
        const auto result = configureDolphin(path, parser.value(QStringLiteral("shortcut")));
        if (!result.error.isEmpty())
        {
            QTextStream(stderr) << result.error << '\n';
            return 1;
        }
        QTextStream(stdout) << "Configured Dolphin shortcut. Restart Dolphin when convenient.\n";
        if (!result.backup.isEmpty())
        {
            QTextStream(stdout) << "Backup: " << result.backup << '\n';
        }
        return 0;
    }
    const bool standalone = parser.isSet(QStringLiteral("standalone"));
    auto bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("org.kglanceqt.Preview");
    const QString object = QStringLiteral("/org/kglanceqt/Preview");
    QStringList paths;
    for (const auto &argument : parser.positionalArguments())
    {
        const QUrl url(argument);
        paths.append(url.scheme().isEmpty() ? QDir::current().absoluteFilePath(argument)
                                            : argument);
    }
    const QString activation = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    if (!standalone && bus.isConnected() && bus.interface()->isServiceRegistered(service))
    {
        QDBusInterface remote(service, object, service, bus);
        const auto reply =
            parser.isSet(QStringLiteral("quit")) ? remote.call(QStringLiteral("Quit"))
            : paths.isEmpty() ? QDBusMessage{}
                              : remote.call(QStringLiteral("ShowPreview"), paths, activation);
        if (reply.type() == QDBusMessage::ErrorMessage)
        {
            QTextStream(stderr) << reply.errorMessage() << '\n';
            return 1;
        }
        return 0;
    }
    if (parser.isSet(QStringLiteral("quit")))
    {
        return 0;
    }
    if (paths.isEmpty() && !parser.isSet(QStringLiteral("daemon")))
    {
        parser.showHelp(1);
    }
    PreviewWindow window(standalone);
    if (!standalone)
    {
        new PreviewService(&window);
        if (!bus.registerObject(object, &window, QDBusConnection::ExportAdaptors) ||
            !bus.registerService(service))
        {
            QTextStream(stderr) << "Cannot register the KGlanceQT DBus service.\n";
            return 1;
        }
    }
    if (!paths.isEmpty())
    {
        window.ShowPreview(paths, activation);
    }
    return app.exec();
}
