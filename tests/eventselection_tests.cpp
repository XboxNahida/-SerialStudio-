#include <QtTest>
#include "eventmodel.h"

class EventSelectionTests : public QObject {
    Q_OBJECT
    static QString idAt(EventModel &model, int row)
    { return model.data(model.index(row), model.roleNames().key("eventId", -1)).toString(); }
    static bool choose(EventModel &model, const QString &id, bool selected = true)
    {
        bool result = false;
        QMetaObject::invokeMethod(&model, "setEventSelected", Q_RETURN_ARG(bool, result), Q_ARG(QString, id), Q_ARG(bool, selected));
        return result;
    }
    static QString text(EventModel &model, bool hex = false, bool detailed = false, bool protocol = false)
    {
        QString result;
        QMetaObject::invokeMethod(&model, "selectedText", Q_RETURN_ARG(QString, result), Q_ARG(bool, hex), Q_ARG(bool, detailed), Q_ARG(bool, protocol));
        return result;
    }
private slots:
    void orderingAndDisplayedFormats()
    {
        EventModel model(4);
        model.append({{"10:01", "RX", "接收", "3 字节", QByteArray::fromHex("e4b8ad"), true},
                      {"10:02", "TX", "发送", "2 字节", QByteArray("ok"), true}});
        QVERIFY2(model.property("selectedCount").isValid(), "消息模型尚未提供多选状态");
        const auto first = idAt(model, 0), second = idAt(model, 1);
        QVERIFY(!first.isEmpty()); QVERIFY(first != second);
        QVERIFY(choose(model, second)); QVERIFY(choose(model, first));
        QCOMPARE(model.property("selectedCount").toInt(), 2);
        QCOMPARE(text(model), QStringLiteral("RX\n中\nTX\nok"));
        QCOMPARE(text(model, true), QStringLiteral("RX\nE4 B8 AD\nTX\n6F 6B"));
        QCOMPARE(text(model, false, true), QStringLiteral("10:01 RX\n接收 · 3 字节\n中\n10:02 TX\n发送 · 2 字节\nok"));
        QCOMPARE(text(model, false, false, true), QStringLiteral("接收\n3 字节\n发送\n2 字节"));
        QCOMPARE(text(model, false, true, true), QStringLiteral("10:01 接收\n3 字节\nE4 B8 AD\n10:02 发送\n2 字节\n6F 6B"));
    }
    void rolloverPreservesIdentityAndClearsExpiredSelection()
    {
        EventModel model(2);
        model.append({{"1", "RX", "a", "", "a", true}, {"2", "RX", "b", "", "b", true}});
        QVERIFY2(model.property("selectedCount").isValid(), "消息模型尚未提供多选状态");
        const auto first = idAt(model, 0), second = idAt(model, 1);
        QVERIFY(choose(model, first)); QVERIFY(choose(model, second));
        QSignalSpy changed(&model, SIGNAL(selectionChanged()));
        model.append({{"3", "RX", "c", "", "c", true}});
        QCOMPARE(idAt(model, 0), second);
        QVERIFY(idAt(model, 1) != first); QVERIFY(idAt(model, 1) != second);
        QCOMPARE(model.property("selectedCount").toInt(), 1);
        QCOMPARE(text(model), QStringLiteral("RX\nb"));
        QVERIFY(changed.count() > 0);
        QVERIFY(!choose(model, first)); QVERIFY(!choose(model, "不存在"));
        model.clear(); QCOMPARE(model.property("selectedCount").toInt(), 0);
        model.append({{"4", "RX", "d", "", "d", true}});
        QVERIFY(idAt(model, 0) != first); QVERIFY(idAt(model, 0) != second);
        QCOMPARE(text(model), QString());
    }
    void allAndNoneIncludeOnlyCurrentMessages()
    {
        EventModel model(2);
        model.append({{"1", "RX", "a", "", "a", true}, {"2", "RX", "b", "", "b", true}, {"3", "RX", "c", "", "c", true}});
        QVERIFY2(model.property("selectedCount").isValid(), "消息模型尚未提供多选状态");
        QVERIFY(QMetaObject::invokeMethod(&model, "selectAll"));
        QCOMPARE(model.property("selectedCount").toInt(), 2);
        QCOMPARE(text(model), QStringLiteral("RX\nb\nRX\nc"));
        model.append({{"4", "DEMO", "演示", "仅离线", "d", true}});
        QCOMPARE(model.property("selectedCount").toInt(), 1);
        QCOMPARE(text(model), QStringLiteral("RX\nc"));
        QVERIFY(QMetaObject::invokeMethod(&model, "selectAll"));
        QCOMPARE(text(model), QStringLiteral("RX\nc\nDEMO\n演示 · 仅离线\nd"));
        QVERIFY(choose(model, idAt(model, 0), false));
        QCOMPARE(text(model, true, false, true), QStringLiteral("演示 · 离线演示\n仅离线"));
        QVERIFY(QMetaObject::invokeMethod(&model, "clearSelection"));
        QCOMPARE(model.property("selectedCount").toInt(), 0);
        QCOMPARE(text(model), QString());
    }
};
QTEST_GUILESS_MAIN(EventSelectionTests)
#include "eventselection_tests.moc"
