#include "terminalscreen.h"
#include <QHash>
#include <algorithm>
#include <limits>

TerminalScreen::TerminalScreen(QObject *parent):QObject(parent) {
    m_terminal=vterm_new(m_rows,m_columns);
    vterm_set_utf8(m_terminal,1);m_screen=vterm_obtain_screen(m_terminal);
    static const VTermScreenCallbacks callbacks={nullptr,nullptr,moveCursor,property,nullptr,nullptr,pushLine,popLine,nullptr};
    vterm_screen_set_callbacks(m_screen,&callbacks,this);
    vterm_screen_enable_altscreen(m_screen,1);
    vterm_screen_enable_reflow(m_screen,true);
    vterm_output_set_callback(m_terminal,[](const char *data,size_t length,void *user){
        emit static_cast<TerminalScreen*>(user)->output(QByteArray(data,qsizetype(length)));
    },this);
    VTermColor fg,bg;vterm_color_rgb(&fg,224,230,238);vterm_color_rgb(&bg,18,22,30);
    vterm_screen_set_default_colors(m_screen,&fg,&bg);
    vterm_screen_reset(m_screen,1);
}
TerminalScreen::~TerminalScreen(){vterm_free(m_terminal);}
void TerminalScreen::feed(const QByteArray &bytes) {
    vterm_input_write(m_terminal,bytes.constData(),size_t(bytes.size()));
    vterm_screen_flush_damage(m_screen);emit changed();
}
void TerminalScreen::resize(int columns,int rows) {
    columns=qBound(10,columns,400);rows=qBound(3,rows,160);
    if(columns==m_columns && rows==m_rows)return;
    m_columns=columns;m_rows=rows;m_offset=0;
    vterm_set_size(m_terminal,rows,columns);emit sizeChanged(columns,rows);emit changed();
}
void TerminalScreen::reset() {
    m_history.clear();m_offset=0;vterm_screen_reset(m_screen,1);emit changed();
}
void TerminalScreen::scroll(int lines) {
    m_offset=m_alternate?0:qBound(0,m_offset+lines,historySize());emit changed();
}
int TerminalScreen::moveCursor(VTermPos pos,VTermPos,int visible,void *user) {
    auto *self=static_cast<TerminalScreen*>(user);self->m_cursor=pos;self->m_visible=visible!=0;return 1;
}
int TerminalScreen::property(VTermProp property,VTermValue *value,void *user) {
    auto *self=static_cast<TerminalScreen*>(user);
    if(property==VTERM_PROP_ALTSCREEN){self->m_alternate=value->boolean!=0;self->m_offset=0;}
    if(property==VTERM_PROP_CURSORVISIBLE)self->m_visible=value->boolean!=0;
    return 1;
}
int TerminalScreen::pushLine(int columns,const VTermScreenCell *cells,void *user) {
    auto *self=static_cast<TerminalScreen*>(user);
    self->m_history.emplace_back(cells,cells+columns);
    if(self->m_history.size()>10000)self->m_history.pop_front();
    if(self->m_offset)self->m_offset=qMin(self->m_offset+1,self->historySize());
    return 1;
}
int TerminalScreen::popLine(int columns,VTermScreenCell *cells,void *user) {
    auto *self=static_cast<TerminalScreen*>(user);if(self->m_history.empty())return 0;
    const auto &line=self->m_history.back();
    for(int i=0;i<columns;++i)cells[i]=i<int(line.size())?line[size_t(i)]:VTermScreenCell{};
    self->m_history.pop_back();return 1;
}
VTermScreenCell TerminalScreen::cell(int row,int column) const {
    VTermScreenCell result{};if(row<0||row>=m_rows||column<0||column>=m_columns)return result;
    const int absolute=historySize()+row-m_offset;
    if(absolute<historySize()) {
        const auto &line=m_history[size_t(absolute)];
        return column<int(line.size())?line[size_t(column)]:result;
    }
    vterm_screen_get_cell(m_screen,{absolute-historySize(),column},&result);return result;
}
QString TerminalScreen::text(const VTermScreenCell &cell) {
    QString result;
    if(cell.chars[0]==std::numeric_limits<uint32_t>::max())return result;
    for(const auto code:cell.chars){if(!code)break;const char32_t scalar=char32_t(code);result+=QString::fromUcs4(&scalar,1);}
    return result;
}
QString TerminalScreen::lineText(int row) const {
    QString result;
    for(int column=0;column<m_columns;++column){const auto value=cell(row,column);const auto part=text(value);result+=part.isEmpty()?(value.chars[0]==0?QString(" "):QString()):part;}
    while(result.endsWith(' '))result.chop(1);
    return result;
}
QVariantList TerminalScreen::cells() const {
    QVariantList result;result.reserve(m_rows*m_columns);
    for(int row=0;row<m_rows;++row)for(int column=0;column<m_columns;++column){
        auto value=cell(row,column);if(value.chars[0]==std::numeric_limits<uint32_t>::max())continue;
        vterm_screen_convert_color_to_rgb(m_screen,&value.fg);vterm_screen_convert_color_to_rgb(m_screen,&value.bg);
        QColor fg(value.fg.rgb.red,value.fg.rgb.green,value.fg.rgb.blue),bg(value.bg.rgb.red,value.bg.rgb.green,value.bg.rgb.blue);
        if(value.attrs.reverse)std::swap(fg,bg);
        result.append(QVariantMap{{"row",row},{"column",column},{"text",value.attrs.conceal?QString():text(value)},
            {"width",qMax(1,int(value.width))},{"foreground",fg},{"background",bg},{"bold",bool(value.attrs.bold)},
            {"italic",bool(value.attrs.italic)},{"underline",bool(value.attrs.underline)}});
    }return result;
}
QString TerminalScreen::selection(int startRow,int startColumn,int endRow,int endColumn) const {
    if(startRow>endRow || (startRow==endRow && startColumn>endColumn)){std::swap(startRow,endRow);std::swap(startColumn,endColumn);}
    QStringList lines;
    for(int row=qMax(0,startRow);row<=qMin(endRow,m_rows-1);++row){
        QString line;
        for(int column=row==startRow?qMax(0,startColumn):0;column<=(row==endRow?qMin(endColumn,m_columns-1):m_columns-1);++column){
            const auto value=cell(row,column);const auto part=text(value);line+=part.isEmpty()?(value.chars[0]==0?QString(" "):QString()):part;
        }
        while(line.endsWith(' '))line.chop(1);
        lines.append(line);
    }return lines.join('\n');
}
void TerminalScreen::key(int key,int modifiers,const QString &text) {
    m_offset=0;
    const QHash<int,VTermKey> keys={{Qt::Key_Return,VTERM_KEY_ENTER},{Qt::Key_Enter,VTERM_KEY_ENTER},{Qt::Key_Tab,VTERM_KEY_TAB},
        {Qt::Key_Backtab,VTERM_KEY_TAB},{Qt::Key_Backspace,VTERM_KEY_BACKSPACE},{Qt::Key_Escape,VTERM_KEY_ESCAPE},
        {Qt::Key_Up,VTERM_KEY_UP},{Qt::Key_Down,VTERM_KEY_DOWN},{Qt::Key_Left,VTERM_KEY_LEFT},{Qt::Key_Right,VTERM_KEY_RIGHT},
        {Qt::Key_Insert,VTERM_KEY_INS},{Qt::Key_Delete,VTERM_KEY_DEL},{Qt::Key_Home,VTERM_KEY_HOME},{Qt::Key_End,VTERM_KEY_END},
        {Qt::Key_PageUp,VTERM_KEY_PAGEUP},{Qt::Key_PageDown,VTERM_KEY_PAGEDOWN}};
    VTermModifier mods=VTermModifier(((modifiers&Qt::ShiftModifier)?VTERM_MOD_SHIFT:0)|((modifiers&Qt::AltModifier)?VTERM_MOD_ALT:0)|((modifiers&Qt::ControlModifier)?VTERM_MOD_CTRL:0));
    if(keys.contains(key))vterm_keyboard_key(m_terminal,keys.value(key),mods);
    else if(key>=Qt::Key_F1 && key<=Qt::Key_F35)vterm_keyboard_key(m_terminal,VTermKey(VTERM_KEY_FUNCTION(key-Qt::Key_F1+1)),mods);
    else if(modifiers&Qt::ControlModifier && key>=Qt::Key_A && key<=Qt::Key_Z)vterm_keyboard_unichar(m_terminal,uint32_t('a'+key-Qt::Key_A),mods);
    else for(const auto scalar:text.toUcs4())vterm_keyboard_unichar(m_terminal,scalar,mods);
    emit changed();
}
void TerminalScreen::paste(const QString &text) {
    m_offset=0;vterm_keyboard_start_paste(m_terminal);
    for(const auto scalar:text.toUcs4())vterm_keyboard_unichar(m_terminal,scalar,VTERM_MOD_NONE);
    vterm_keyboard_end_paste(m_terminal);emit changed();
}
