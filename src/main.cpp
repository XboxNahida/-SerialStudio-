#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QImage>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QFile>
#include "session.h"
#include "boardcontroller.h"
#include "filetransfer.h"
#include "updatecontroller.h"

int main(int argc, char *argv[])
{
    for(int i=1;i<argc;++i)if(QByteArray(argv[i])=="--terminal-input"){
        QCoreApplication helper(argc,argv);QFile input;
        if(!input.open(stdin,QIODevice::ReadOnly))return 1;
        return TerminalController::inputHelper(input.read(32769));
    }
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SerialStudio"));
    QCoreApplication::setOrganizationName(QStringLiteral("SerialWorkbench"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SERIALSTUDIO_VERSION));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOptions({{QStringLiteral("smoke-test"), QStringLiteral("无设备启动检查并退出")},
                    {QStringLiteral("capture"), QStringLiteral("保存界面截图并退出"), "path"},
                    {QStringLiteral("page"), QStringLiteral("页面编号 0–5"), "index", "0"},
                    {QStringLiteral("demo"), QStringLiteral("载入明确标记的离线演示")},
                    {QStringLiteral("light"), QStringLiteral("使用浅色主题")},
                    {QStringLiteral("assistant"), QStringLiteral("打开指令辅助用于界面检查")},
                    {QStringLiteral("terminal-mode"), QStringLiteral("终端系统编号 0–3，用于界面检查"), "index", "0"},
                    {QStringLiteral("compact"), QStringLiteral("按最小窗口尺寸检查界面")}});
    cli.process(app);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    Session session(nullptr, !cli.isSet("smoke-test") && !cli.isSet("capture"));
    Session network(nullptr, false);
    BoardController board(&session, &network);
    FileTransfer transfer;
    UpdateController updater(QCoreApplication::applicationVersion());
    QObject::connect(&updater,&UpdateController::exitRequested,&app,&QCoreApplication::quit);
    if(!cli.isSet("smoke-test")&&!cli.isSet("capture"))QTimer::singleShot(2500,&updater,[&updater]{updater.check(false);});
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"backend", QVariant::fromValue(&session)},
                                 {"networkBackend", QVariant::fromValue(&network)},
                                 {"board", QVariant::fromValue(&board)},
                                 {"transfer", QVariant::fromValue(&transfer)},
                                 {"updater", QVariant::fromValue(&updater)},
                                 {"appVersion", QCoreApplication::applicationVersion()},
                                 {"pageIndex", qBound(0, cli.value("page").toInt(), 5)},
                                 {"lightMode", cli.isSet("light")}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("SerialWorkbench"), QStringLiteral("Main"));
    if(cli.isSet("terminal-mode")&&!engine.rootObjects().isEmpty()){
        auto *mode=engine.rootObjects().first()->findChild<QObject *>("terminalMode");
        if(mode)mode->setProperty("currentIndex",qBound(0,cli.value("terminal-mode").toInt(),3));
    }
    if (cli.isSet("demo")) {
        if (cli.value("page") == "1") session.configureParser(2, {}, {});
        QTimer::singleShot(200, &session, &Session::demo);
    }
    if (cli.isSet("compact") && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("width", 1120);
        engine.rootObjects().first()->setProperty("height", 760);
    }
    if(cli.isSet("assistant")&&!engine.rootObjects().isEmpty()){
        auto *assistant=engine.rootObjects().first()->findChild<QObject *>("commandAssistantWindow");
        if(assistant)QMetaObject::invokeMethod(assistant,"showFor",Q_ARG(QVariant,0));
    }
    if (cli.isSet("capture")) {
        const auto path = cli.value("capture");
        const bool captureAssistant=cli.isSet("assistant");
        QTimer::singleShot(1600, &app, [&engine, path,captureAssistant] {
            auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if(window&&captureAssistant)window=window->findChild<QQuickWindow *>("commandAssistantWindow");
            if (!window) { QCoreApplication::exit(2); return; }
            QDir().mkpath(QFileInfo(path).absolutePath());
            const bool ok = window->grabWindow().save(path);
            QCoreApplication::exit(ok ? 0 : 3);
        });
    } else if (cli.isSet("smoke-test")) QTimer::singleShot(1800, &app, &QCoreApplication::quit);
    return app.exec();
}
