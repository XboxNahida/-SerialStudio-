#include "terminalview.h"
#include <QPainter>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QWheelEvent>

TerminalView::TerminalView(QQuickItem *parent):QQuickPaintedItem(parent) {
    setFlag(ItemAcceptsInputMethod,true);setFlag(ItemIsFocusScope,true);
    setAcceptedMouseButtons(Qt::LeftButton|Qt::RightButton);setActiveFocusOnTab(true);setClip(true);
    m_font=QFont("Consolas");m_font.setPixelSize(14);
    const QFontMetricsF metrics(m_font);m_cellWidth=metrics.horizontalAdvance('M');
    m_cellHeight=qCeil(metrics.height())+2;m_ascent=metrics.ascent()+1;
}
void TerminalView::setScreen(TerminalScreen *screen) {
    if(m_screen==screen)return;
    if(m_screen)disconnect(m_screen,nullptr,this,nullptr);
    m_screen=screen;
    if(screen)connect(screen,&TerminalScreen::changed,this,[this]{update();});
    resizeScreen();update();emit screenChanged();
}
void TerminalView::geometryChange(const QRectF &now,const QRectF &before) {
    QQuickPaintedItem::geometryChange(now,before);resizeScreen();
}
void TerminalView::resizeScreen() {
    if(m_screen && width()>0 && height()>0)m_screen->resize(int((width()-12)/m_cellWidth),int((height()-10)/m_cellHeight));
}
void TerminalView::paint(QPainter *painter) {
    painter->fillRect(boundingRect(),QColor(18,22,30));if(!m_screen)return;
    painter->setRenderHint(QPainter::TextAntialiasing);painter->setFont(m_font);
    QPoint first=m_start,last=m_end;
    if(first.y()>last.y()||(first.y()==last.y()&&first.x()>last.x()))std::swap(first,last);
    const auto cells=m_screen->cells();
    for(const auto &entry:cells){
        const auto cell=entry.toMap();const int row=cell.value("row").toInt(),column=cell.value("column").toInt();
        QRectF rect(6+column*m_cellWidth,5+row*m_cellHeight,cell.value("width").toInt()*m_cellWidth,m_cellHeight);
        const bool selected=first.x()>=0 && row>=first.y() && row<=last.y() &&
            (row!=first.y()||column>=first.x())&&(row!=last.y()||column<=last.x());
        painter->fillRect(rect,selected?QColor(52,80,110):cell.value("background").value<QColor>());
        auto font=m_font;font.setBold(cell.value("bold").toBool());font.setItalic(cell.value("italic").toBool());
        font.setUnderline(cell.value("underline").toBool());painter->setFont(font);
        painter->setPen(cell.value("foreground").value<QColor>());
        painter->drawText(QPointF(rect.x(),rect.y()+m_ascent),cell.value("text").toString());
    }
    if(m_screen->cursorVisible()){
        painter->setPen(QColor(220,230,240));
        painter->drawRect(QRectF(6+m_screen->cursorColumn()*m_cellWidth,5+m_screen->cursorRow()*m_cellHeight,m_cellWidth,m_cellHeight-1));
    }
}
void TerminalView::keyPressEvent(QKeyEvent *event) {
    if(!m_screen)return;
    if(event->key()==Qt::Key_Control||event->key()==Qt::Key_Shift||event->key()==Qt::Key_Alt||event->key()==Qt::Key_Meta){event->accept();return;}
    if(event->modifiers().testFlag(Qt::ControlModifier)&&event->modifiers().testFlag(Qt::ShiftModifier)){
        if(event->key()==Qt::Key_C){copy();event->accept();return;}
        if(event->key()==Qt::Key_V){paste();event->accept();return;}
    }
    m_start=m_end=QPoint(-1,-1);m_screen->key(event->key(),int(event->modifiers()),event->text());event->accept();
}
void TerminalView::inputMethodEvent(QInputMethodEvent *event) {
    if(m_screen&&!event->commitString().isEmpty())m_screen->paste(event->commitString());
    event->accept();
}
QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const {
    if(query==Qt::ImEnabled)return true;
    if(query==Qt::ImCursorRectangle && m_screen)return QRectF(6+m_screen->cursorColumn()*m_cellWidth,5+m_screen->cursorRow()*m_cellHeight,m_cellWidth,m_cellHeight);
    return QQuickPaintedItem::inputMethodQuery(query);
}
QPoint TerminalView::cellAt(QPointF point) const {
    return m_screen?QPoint(qBound(0,int((point.x()-6)/m_cellWidth),m_screen->columns()-1),qBound(0,int((point.y()-5)/m_cellHeight),m_screen->rows()-1)):QPoint(-1,-1);
}
void TerminalView::mousePressEvent(QMouseEvent *event) {
    forceActiveFocus();if(event->button()==Qt::LeftButton){m_start=m_end=cellAt(event->position());update();}event->accept();
}
void TerminalView::mouseMoveEvent(QMouseEvent *event) {
    if(event->buttons().testFlag(Qt::LeftButton)){m_end=cellAt(event->position());update();}event->accept();
}
void TerminalView::wheelEvent(QWheelEvent *event) {
    if(m_screen)m_screen->scroll(event->angleDelta().y()/40);
    event->accept();
}
void TerminalView::copy() {
    if(m_screen&&m_start.x()>=0)QGuiApplication::clipboard()->setText(m_screen->selection(m_start.y(),m_start.x(),m_end.y(),m_end.x()));
}
void TerminalView::paste() {
    const auto value=QGuiApplication::clipboard()->text();
    if(m_screen&&value.size()<=65536)m_screen->paste(value);
}
