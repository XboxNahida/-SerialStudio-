#include <QtQuickTest/quicktest.h>
#include <QQuickStyle>
#include "terminalview.h"
#include "settingsstore.h"
#include "quickcommands.h"
#include <QTemporaryDir>
#include <QDir>
#include <QQmlEngine>
#include <QQmlContext>
#include <QClipboard>
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include "eventmodel.h"

class MessageFixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(EventModel *model READ model CONSTANT)
    EventModel m_model{3};
public:
    EventModel *model() { return &m_model; }
    Q_INVOKABLE void reset() {
        m_model.clear();
        m_model.append({{"10:01","RX","接收","首条","一",true},
                        {"10:02","TX","发送","第二条","二",true},
                        {"10:03","DEMO","示例","离线","三",true}});
    }
    Q_INVOKABLE void append() { m_model.append({{"10:04","RX","新接收","第四条","四",true}}); }
    Q_INVOKABLE void clear() { m_model.clear(); }
    Q_INVOKABLE QString idAt(int row) { return m_model.data(m_model.index(row), EventModel::EventIdRole).toString(); }
    Q_INVOKABLE QString clipboard() { return QGuiApplication::clipboard()->text(); }
    Q_INVOKABLE void clearClipboard() { QGuiApplication::clipboard()->clear(); }
};

class UiSetup : public QObject {
    Q_OBJECT
    QTemporaryDir m_directory;
    SettingsStore m_settings{m_directory.path()};
    QuickCommands m_commands{&m_settings};
    MessageFixture m_messages;
public slots:
    void applicationAvailable() { QQuickStyle::setStyle(QStringLiteral("Basic"));qmlRegisterType<TerminalView>("SerialStudio.Terminal",1,0,"TerminalView"); }
    void qmlEngineAvailable(QQmlEngine *engine) {
        QFile catalog(QCoreApplication::applicationDirPath()+"/../data/terminal_commands.json");
        if(catalog.open(QIODevice::ReadOnly))
            engine->rootContext()->setContextProperty("dictionaryEntries",QJsonDocument::fromJson(catalog.readAll()).array().toVariantList());
        engine->rootContext()->setContextProperty("commandRepository",&m_commands);
        engine->rootContext()->setContextProperty("messageFixture",&m_messages);
        engine->rootContext()->setContextProperty("formSettings",&m_settings);
        engine->rootContext()->setContextProperty("quickExportUrl",QUrl::fromLocalFile(m_directory.filePath("quick.json")));
        const auto evidence=QCoreApplication::applicationDirPath()+"/formal-settings";
        QDir().mkpath(evidence);
        engine->rootContext()->setContextProperty("quickEvidenceDirectory",evidence);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(SerialStudioUI, UiSetup)
#include "ui_main.moc"
