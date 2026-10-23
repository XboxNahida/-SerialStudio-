#include <QtTest>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QVideoSink>
#include <QVideoFrame>
#include "dataworkspace.h"

class MediaTests : public QObject {
    Q_OBJECT
private slots:
    void picturePreview()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QImage image(80, 60, QImage::Format_RGB32); image.fill(Qt::cyan);
        const auto path = dir.filePath("image.png"); QVERIFY(image.save(path));
        DataWorkspace workspace;
        QVERIFY(workspace.openFile(QUrl::fromLocalFile(path), "auto"));
        QCOMPARE(workspace.mediaKind(), QString("image"));
        QCOMPARE(workspace.mediaUrl(), QUrl::fromLocalFile(path));
        QFile bad(dir.filePath("bad.png")); QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("invalid image"); bad.close();
        QVERIFY(!workspace.openFile(QUrl::fromLocalFile(bad.fileName()), "image"));
    }
    void waveLoadsWithoutPlaying()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile file(dir.filePath("silence.wav")); QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4); out << quint32(16036);
        out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1)
            << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        out.writeRawData("data", 4); out << quint32(16000);
        out.writeRawData(QByteArray(16000, '\0').constData(), 16000); file.close();
        const auto url = QUrl::fromLocalFile(file.fileName());
        DataWorkspace workspace; QVERIFY(workspace.openFile(url, "auto"));
        QCOMPARE(workspace.mediaKind(), QString("audio"));
        QMediaPlayer player; player.setSource(workspace.mediaUrl());
        QTRY_VERIFY_WITH_TIMEOUT(player.mediaStatus() == QMediaPlayer::LoadedMedia || player.error() != QMediaPlayer::NoError, 10000);
        QVERIFY2(player.error() == QMediaPlayer::NoError, qPrintable(player.errorString()));
        QCOMPARE(player.duration(), qint64(1000));
        QCOMPARE(player.playbackState(), QMediaPlayer::StoppedState);
    }
    void mp4DecodesVideoFrame()
    {
        const auto path = QFINDTESTDATA("fixtures/sample.mp4"); QVERIFY(!path.isEmpty());
        DataWorkspace workspace; QVERIFY(workspace.openFile(QUrl::fromLocalFile(path), "auto"));
        QCOMPARE(workspace.mediaKind(), QString("video"));
        QMediaPlayer player; QVideoSink sink; player.setVideoSink(&sink);
        // 无 AudioOutput，测试不会播放声音。
        QSignalSpy frames(&sink, &QVideoSink::videoFrameChanged);
        player.setSource(workspace.mediaUrl()); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0 || player.error() != QMediaPlayer::NoError, 10000);
        QVERIFY2(player.error() == QMediaPlayer::NoError, qPrintable(player.errorString()));
        QVERIFY(frames.count() > 0); QVERIFY(sink.videoFrame().isValid());
        QCOMPARE(sink.videoFrame().size(), QSize(160, 90)); player.stop();
    }
};
QTEST_MAIN(MediaTests)
#include "media_tests.moc"
