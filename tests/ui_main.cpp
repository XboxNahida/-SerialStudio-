#include <QtQuickTest/quicktest.h>
#include <QQuickStyle>

class UiSetup : public QObject {
    Q_OBJECT
public slots:
    void applicationAvailable() { QQuickStyle::setStyle(QStringLiteral("Basic")); }
};

QUICK_TEST_MAIN_WITH_SETUP(SerialStudioUI, UiSetup)
#include "ui_main.moc"
