#pragma once
#include <QObject>
#include <QColor>
#include <QVariantList>
#include <deque>
#include <vector>
#include <vterm.h>

class TerminalScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cells READ cells NOTIFY changed)
    Q_PROPERTY(int columns READ columns NOTIFY changed)
    Q_PROPERTY(int rows READ rows NOTIFY changed)
    Q_PROPERTY(int cursorRow READ cursorRow NOTIFY changed)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY changed)
    Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY changed)
    Q_PROPERTY(bool alternate READ alternate NOTIFY changed)
    Q_PROPERTY(int historySize READ historySize NOTIFY changed)
    Q_PROPERTY(int scrollOffset READ scrollOffset NOTIFY changed)
public:
    explicit TerminalScreen(QObject *parent=nullptr);
    ~TerminalScreen() override;
    int columns() const {return m_columns;}
    int rows() const {return m_rows;}
    int cursorRow() const {return m_cursor.row;}
    int cursorColumn() const {return m_cursor.col;}
    bool cursorVisible() const {return m_visible && !m_offset;}
    bool alternate() const {return m_alternate;}
    int historySize() const {return int(m_history.size());}
    int scrollOffset() const {return m_offset;}
    QVariantList cells() const;
    QString lineText(int row) const;
    void feed(const QByteArray &bytes);
    Q_INVOKABLE void resize(int columns,int rows);
    Q_INVOKABLE void reset();
    Q_INVOKABLE void scroll(int lines);
    Q_INVOKABLE QString selection(int startRow,int startColumn,int endRow,int endColumn) const;
    Q_INVOKABLE void key(int key,int modifiers,const QString &text);
    Q_INVOKABLE void paste(const QString &text);
signals:
    void changed();
    void output(const QByteArray &bytes);
    void sizeChanged(int columns,int rows);
private:
    VTermScreenCell cell(int row,int column) const;
    static QString text(const VTermScreenCell &cell);
    static int moveCursor(VTermPos,VTermPos,int,void*);
    static int property(VTermProp,VTermValue*,void*);
    static int pushLine(int,const VTermScreenCell*,void*);
    static int popLine(int,VTermScreenCell*,void*);
    VTerm *m_terminal=nullptr;
    VTermScreen *m_screen=nullptr;
    int m_columns=100,m_rows=30,m_offset=0;
    bool m_visible=true,m_alternate=false;
    VTermPos m_cursor{};
    std::deque<std::vector<VTermScreenCell>> m_history;
};
