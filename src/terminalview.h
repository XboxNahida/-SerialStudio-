#pragma once
#include <QQuickPaintedItem>
#include <QPointer>
#include "terminalscreen.h"

class TerminalView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(TerminalScreen* screen READ screen WRITE setScreen NOTIFY screenChanged)
public:
    explicit TerminalView(QQuickItem *parent=nullptr);
    TerminalScreen *screen() const {return m_screen;}
    void setScreen(TerminalScreen *screen);
    void paint(QPainter *painter) override;
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
signals:
    void screenChanged();
protected:
    void geometryChange(const QRectF&,const QRectF&) override;
    void keyPressEvent(QKeyEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    void resizeScreen();
    QPoint cellAt(QPointF point) const;
    QPointer<TerminalScreen> m_screen;
    QFont m_font;
    qreal m_cellWidth=9,m_cellHeight=19,m_ascent=15;
    QPoint m_start{-1,-1},m_end{-1,-1};
};
