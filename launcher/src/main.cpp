// ps2-native desktop launcher: manages the user's BIOS dumps, console NVM and
// disc images. Play runs `ps2n play <disc>`: PS2Recomp recompiles the game and
// its runtime executes it in its own window.
#include "automation.hpp"
#include "icon_provider.hpp"
#include "launcher.hpp"
#include "translation.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QStyleHints>
#include <QUrl>
#include <QTimer>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--automate") ps2launcher::install_automation_message_handler();
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("ps2-native"));
    QGuiApplication::setOrganizationName(QStringLiteral("ps2-native"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ps2-native"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PS2N_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/ps2native/assets/app-icon.svg")));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ps2-native desktop launcher"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption data_dir({QStringLiteral("data-dir")},
                                      QStringLiteral("Library and settings directory (default: the user's application data)."),
                                      QStringLiteral("dir"));
    const QCommandLineOption automate({QStringLiteral("automate")},
                                      QStringLiteral("Scripted run: import, visit every page, verify persistence, exit."));
    const QCommandLineOption import_option({QStringLiteral("import")}, QStringLiteral("File or folder to import (automation)."),
                                           QStringLiteral("path"));
    const QCommandLineOption screenshots({QStringLiteral("screenshots")}, QStringLiteral("Capture every page to PNG files (automation)."),
                                         QStringLiteral("dir"));
    const QCommandLineOption expect_entries({QStringLiteral("expect-entries")}, QStringLiteral("Persisted entries expected (automation)."),
                                            QStringLiteral("n"));
    const QCommandLineOption expect_valid({QStringLiteral("expect-valid")}, QStringLiteral("Valid entries expected (automation)."),
                                          QStringLiteral("n"));
    const QCommandLineOption window_size({QStringLiteral("window-size")}, QStringLiteral("Initial window size WxH."),
                                         QStringLiteral("size"));
    parser.addOptions({data_dir, automate, import_option, screenshots, expect_entries, expect_valid, window_size});
    parser.process(app);

    QString data = parser.value(data_dir);
    if (data.isEmpty()) data = qEnvironmentVariable("PS2N_DATA_DIR");
    if (data.isEmpty()) data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    data = QDir(data).absolutePath();

    ps2launcher::Launcher launcher(data);
    qmlRegisterSingletonInstance("PS2Native.Backend", 1, 0, "Launcher", &launcher);
    qmlRegisterUncreatableType<ps2launcher::EntryListModel>("PS2Native.Backend", 1, 0, "EntryListModel", QStringLiteral("backend"));
    qmlRegisterUncreatableType<ps2launcher::EntryFilterModel>("PS2Native.Backend", 1, 0, "EntryFilterModel", QStringLiteral("backend"));

    QQmlApplicationEngine engine;
    ps2launcher::Translation translation;
    QString appliedLanguage;
    const auto translate = [&] {
        const auto language = launcher.language();
        if (language == appliedLanguage) return;
        appliedLanguage = language;
        translation.setPortuguese(language == "pt-BR" || (language == "auto" && QLocale::system().language() == QLocale::Portuguese));
        engine.retranslate();
    };
    app.installTranslator(&translation);
    QObject::connect(&launcher, &ps2launcher::Launcher::settingsChanged, &engine, translate);
    translate();
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.addImageProvider(QStringLiteral("icon"), new ps2launcher::IconProvider);
    QSize initial(1360, 860);
    if (parser.isSet(window_size)) {
        const auto parts = parser.value(window_size).split(QLatin1Char('x'));
        if (parts.size() == 2) initial = QSize(parts[0].toInt(), parts[1].toInt());
    }
    engine.rootContext()->setContextProperty(QStringLiteral("initialWindowSize"), initial);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PS2Native/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &launcher, [&launcher] { launcher.flush(); });

    launcher.initialize();
    std::unique_ptr<ps2launcher::Automation> automation;
    if (parser.isSet(automate)) {
        ps2launcher::AutomationOptions options;
        options.imports = parser.values(import_option);
        options.screenshot_dir = parser.value(screenshots);
        options.expect_entries = parser.isSet(expect_entries) ? parser.value(expect_entries).toInt() : -1;
        options.expect_valid = parser.isSet(expect_valid) ? parser.value(expect_valid).toInt() : -1;
        automation = std::make_unique<ps2launcher::Automation>(launcher, engine, options);
        automation->start();
    }
    return app.exec();
}
