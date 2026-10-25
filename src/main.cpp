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
#include <QTemporaryDir>
#include <QSettings>
#include "session.h"
#include "boardcontroller.h"
#include "filetransfer.h"
#include "updatecontroller.h"
#include "settingsstore.h"
#include "quickcommands.h"
#include "devicediscovery.h"
#include "terminalview.h"
#include "webdebugger.h"
#include "mqttclient.h"
#include "remotefiles.h"
#include "systemproxy.h"
#include "toolmanager.h"
#include "catalogupdater.h"

int main(int argc, char *argv[])
{
    if(argc==3 && QByteArray(argv[1])=="--remove-managed-tools") {
        QCoreApplication helper(argc,argv);
        return ToolManager::removeManaged(helper.arguments().at(2))?0:1;
    }
    if(argc==4 && QByteArray(argv[1])=="--seed-managed-tools") {
        QCoreApplication helper(argc,argv);
        return ToolManager::seedManaged(helper.arguments().at(2),helper.arguments().at(3))?0:1;
    }
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
                    {QStringLiteral("page"), QStringLiteral("页面编号 0–7"), "index", "0"},
                    {QStringLiteral("demo"), QStringLiteral("载入明确标记的离线演示")},
                    {QStringLiteral("light"), QStringLiteral("使用浅色主题")},
                    {QStringLiteral("assistant"), QStringLiteral("打开指令辅助用于界面检查")},
                    {QStringLiteral("terminal-mode"), QStringLiteral("终端系统编号 0–3，用于界面检查"), "index", "0"},
                    {QStringLiteral("compact"), QStringLiteral("按最小窗口尺寸检查界面")}});
    cli.addOption({QStringLiteral("config-dir"),QStringLiteral("指定独立配置目录"),"path"});
    cli.process(app);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qmlRegisterType<TerminalView>("SerialStudio.Terminal",1,0,"TerminalView");
    QTemporaryDir previewConfig;
    const bool preview=cli.isSet("smoke-test")||cli.isSet("capture");
    SettingsStore settings(cli.isSet("config-dir")?cli.value("config-dir"):preview?previewConfig.path():QString());
    if(preview||cli.isSet("config-dir")) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.directory());
    }
    SystemProxy systemProxy;
    ToolManager toolManager(&settings,&systemProxy,(preview||cli.isSet("config-dir"))?QDir(settings.directory()).filePath("tools"):QString());
    QuickCommands quickCommands(&settings);
    Session session(nullptr, !preview&&!cli.isSet("config-dir"));
    Session network(nullptr, false);
    CommandCatalog catalog(QCoreApplication::applicationVersion(),(preview||cli.isSet("config-dir"))?QDir(settings.directory()).filePath("catalog"):QString());
    BoardController board(&session, &network);
    board.setCatalog(&catalog);
    DeviceDiscovery discovery(nullptr,!preview);
    const bool persistLegacy=!preview && !cli.isSet("config-dir");
    board.terminal()->adb()->setPersistenceEnabled(persistLegacy);
    discovery.adb()->setPersistenceEnabled(persistLegacy);
    discovery.setSavedDevices(settings.value("devices/saved",QVariantList()).toList());
    WebDebugger web(&systemProxy);
    MqttClient mqtt(&systemProxy);
    FileTransfer transfer;
    RemoteFiles files;
    files.adb()->setPersistenceEnabled(false);
    UpdateController updater(QCoreApplication::applicationVersion());
    CatalogUpdater catalogUpdater(&catalog,QCoreApplication::applicationVersion());
    QObject::connect(&updater,&UpdateController::exitRequested,&app,&QCoreApplication::quit);
    if(!cli.isSet("smoke-test")&&!cli.isSet("capture"))QTimer::singleShot(2500,&updater,[&updater]{updater.check(false);});
    if(!preview)QTimer::singleShot(3500,&catalogUpdater,[&catalogUpdater]{catalogUpdater.check(false);});
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"backend", QVariant::fromValue(&session)},
                                 {"networkBackend", QVariant::fromValue(&network)},
                                 {"board", QVariant::fromValue(&board)},
                                 {"transfer", QVariant::fromValue(&transfer)},
                                 {"files", QVariant::fromValue(&files)},
                                 {"updater", QVariant::fromValue(&updater)},
                                 {"catalog", QVariant::fromValue(&catalog)},
                                 {"catalogUpdater", QVariant::fromValue(&catalogUpdater)},
                                 {"settings", QVariant::fromValue(&settings)},
                                 {"quickCommands", QVariant::fromValue(&quickCommands)},
                                 {"discovery", QVariant::fromValue(&discovery)},
                                 {"web", QVariant::fromValue(&web)},
                                 {"mqtt", QVariant::fromValue(&mqtt)},
                                 {"systemProxy", QVariant::fromValue(&systemProxy)},
                                 {"toolManager", QVariant::fromValue(&toolManager)},
                                 {"appVersion", QCoreApplication::applicationVersion()},
                                 {"pageIndex", qBound(0, cli.value("page").toInt(), 7)},
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
        auto *window=engine.rootObjects().first();
        window->setProperty("width",window->property("minimumWidth"));
        window->setProperty("height",window->property("minimumHeight"));
    }
    if(cli.isSet("assistant")&&!engine.rootObjects().isEmpty()){
        auto *assistant=engine.rootObjects().first()->findChild<QObject *>("commandAssistantWindow");
        if(assistant)QMetaObject::invokeMethod(assistant,"showFor",Q_ARG(QVariant,0));
    }
    if (cli.isSet("capture")) {
        const auto path = cli.value("capture");
        const bool captureAssistant=cli.isSet("assistant");
        QTimer::singleShot(1600, &app, [&engine, &settings, path,captureAssistant] {
            auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if(window&&captureAssistant)window=window->findChild<QQuickWindow *>("commandAssistantWindow");
            if (!window) { QCoreApplication::exit(2); return; }
            QDir().mkpath(QFileInfo(path).absolutePath());
            const bool ok = window->grabWindow().save(path);
            if(!settings.error().isEmpty()){qCritical().noquote()<<settings.error();QCoreApplication::exit(4);return;}
            QCoreApplication::exit(ok ? 0 : 3);
        });
    } else if (cli.isSet("smoke-test")) QTimer::singleShot(1800, &app, [&settings]{
        if(!settings.error().isEmpty()){qCritical().noquote()<<settings.error();QCoreApplication::exit(4);}
        else QCoreApplication::quit();
    });
    return app.exec();
}
