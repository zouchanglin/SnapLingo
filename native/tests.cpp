#include "capture.h"
#include "services.h"
#include "settings.h"
#include "settingsdialog.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class Tests : public QObject {
    Q_OBJECT
  private slots:
    void desktopCaptureCancel() {
        if (qEnvironmentVariable("SNAPLINGO_DESKTOP_TEST") != "1")
            QSKIP("Desktop capture check is opt-in");
        auto *capture = new Capture;
        QSignalSpy done(capture, &Capture::captured);
        capture->start();
        QVERIFY(!capture->snapshot.isNull());
        QCOMPARE(capture->overlays.size(), QGuiApplication::screens().size());
        QVERIFY(!capture->overlays.isEmpty());
        auto *window = capture->overlays.front();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(qvariant_cast<QImage>(done[0][0]).isNull());
    }
    void liveDeepSeek() {
        const auto key = qEnvironmentVariable("SNAPLINGO_TEST_API_KEY");
        if (key.isEmpty())
            QSKIP("Live API check requires explicit test credentials");
        QVERIFY(QSslSocket::setActiveBackend("schannel"));
        Services services;
        Model m;
        m.key = key;
        m.thinking = false;
        auto *job = services.translate(m, "Hello, world!",
                                       "Translate to Chinese. Output only the translation: {text}");
        QSignalSpy chunks(job, &Job::text), done(job, &Job::finished);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 65000);
        QVERIFY2(done[0][0].toString().isEmpty(), qPrintable(done[0][0].toString()));
        QString result;
        for (const auto &args : chunks)
            result += args[0].toString();
        QVERIFY(result.contains("你好"));
    }
    void settingsRoundTrip() {
        QTemporaryDir dir;
        auto path = dir.filePath("settings.json");
        Settings s;
        Model m;
        m.key = "test-only-key";
        m.thinking = true;
        s.models.append(m);
        s.ocrs[0].provider = "Tencent";
        s.ocrs[0].tencentKey = "test-secret";
        s.save(path);
        auto loaded = Settings::load(path);
        QCOMPARE(loaded.json(), s.json());
        auto copy = s;
        copy.models[0].key = "changed";
        QCOMPARE(s.models[0].key, QString("test-only-key"));
        copy.save(path);
        QVERIFY(QFile::exists(path + ".pre-qt.bak"));
        QCOMPARE(Settings::load(path + ".pre-qt.bak").models[0].key, s.models[0].key);
    }
    void legacyMigration() {
        auto s = Settings::fromJson(
            {{"ApiKey", "old-key"},
             {"Endpoint", "https://example.com/v1/chat/completions"},
             {"Model", "old-model"},
             {"ActiveOcrIndex", 99},
             {"Ocr", QJsonObject{{"Provider", "Volcengine"}, {"VolcengineAccessKeyId", "old-access"}}}});
        QCOMPARE(s.models.size(), 1);
        QCOMPARE(s.models[0].model, QString("old-model"));
        QCOMPARE(s.activeOcr, 0);
        QCOMPARE(s.active().access, QString("old-access"));
        QVERIFY(!s.models[0].thinking);
    }
    void corruptedSettings() {
        QTemporaryDir d;
        QFile f(d.filePath("settings.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("invalid");
        f.close();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Settings::load(f.fileName()));
    }
    void thinkingPayload() {
        Model m;
        for (bool enabled : {false, true}) {
            m.thinking = enabled;
            auto body = Services::translationBody(m, "hello", "translate: {text}");
            QCOMPARE(body["thinking"].toObject()["type"].toString(),
                     enabled ? QString("enabled") : QString("disabled"));
            QVERIFY(!body.contains("enable_thinking"));
            QVERIFY(body["stream"].toBool());
        }
        m.endpoint = "https://example.com/v1/chat/completions";
        m.model = "qwen-plus";
        auto body = Services::translationBody(m, "hello", "translate");
        QVERIFY(body["enable_thinking"].toBool());
        QVERIFY(!body.contains("thinking"));
        QVERIFY(body["messages"].toArray()[0].toObject()["content"].toString().endsWith("hello"));
    }
    void fragmentedSse() {
        SseDecoder parser;
        QByteArray data = "data:" + QString("你好").toUtf8() +
                          "\r\n\r\n: keepalive\n\ndata: one\ndata: two\n\ndata: [DONE]\n\n";
        QList<QByteArray> events;
        for (char c : data)
            events.append(parser.feed(QByteArray(1, c)));
        QCOMPARE(events.size(), 3);
        QCOMPARE(events[0], QString("你好").toUtf8());
        QCOMPARE(events[1], QByteArray("one\ntwo"));
        QCOMPARE(events[2], QByteArray("[DONE]"));
    }
    void ocrResponses() {
        QCOMPARE(Services::parseOcr("Baidu", {{"words_result", QJsonArray{QJsonObject{{"words", "hello"}},
                                                                          QJsonObject{{"words", "world"}}}}}),
                 QString("hello\nworld"));
        QCOMPARE(Services::parseOcr(
                     "Tencent",
                     {{"Response",
                       QJsonObject{{"TextDetections", QJsonArray{QJsonObject{{"DetectedText", "hello"}}}}}}}),
                 QString("hello"));
        QCOMPARE(Services::parseOcr(
                     "Volcengine",
                     {{"code", 10000},
                      {"data", QJsonObject{{"ocr_infos", QJsonArray{QJsonObject{{"text", "hello"}}}}}}}),
                 QString("hello"));
        QVERIFY_THROWS_EXCEPTION(
            std::runtime_error,
            Services::parseOcr("Baidu", {{"error_code", 1}, {"error_msg", "test error"}}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Services::parseOcr("Tencent", {}));
    }
    void selectionCoordinates() {
        QCOMPARE(Capture::selection({-500, -200}, {100, 100}, {-400, -100, 1200, 800}),
                 QRect(-400, -100, 501, 201));
        QCOMPARE(Capture::selection({100, 100}, {-500, -200}, {-400, -100, 1200, 800}),
                 QRect(-400, -100, 501, 201));
    }
    void settingsEditor() {
        Settings s;
        Model a;
        a.name = "A";
        Model b;
        b.name = "B";
        s.models = {a, b};
        SettingsDialog dialog(s);
        auto *list = dialog.findChild<QListWidget *>("models");
        auto *name = dialog.findChild<QLineEdit *>("modelName");
        auto *thinking = dialog.findChild<QCheckBox *>("thinking");
        thinking->setChecked(true);
        list->setCurrentRow(1);
        QVERIFY(!thinking->isChecked());
        list->setCurrentRow(0);
        QVERIFY(thinking->isChecked());
        QVERIFY(!s.models[0].thinking);
        QPushButton *remove = nullptr;
        for (auto *button : list->parentWidget()->findChildren<QPushButton *>())
            if (button->text() == "删除")
                remove = button;
        QVERIFY(remove);
        remove->click();
        QCOMPARE(name->text(), QString("B"));
        QCOMPARE(dialog.draft.models.size(), 1);
        remove->click();
        QVERIFY(name->text().isEmpty());
        QVERIFY(!name->isEnabled());
    }
    void streamingAndCancel() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray request;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                request += socket->readAll();
                if (!request.contains("\r\n\r\n"))
                    return;
                socket->disconnect(this);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: "
                              "close\r\n\r\ndata: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}\n\n");
                socket->flush();
            });
        });
        Services services;
        Model m;
        m.key = "local-test";
        m.endpoint = QString("http://127.0.0.1:%1").arg(server.serverPort());
        auto *job = services.translate(m, "test", "{text}");
        QSignalSpy chunks(job, &Job::text), done(job, &Job::finished);
        QTRY_COMPARE(chunks.size(), 1);
        QCOMPARE(chunks[0][0].toString(), QString("hello"));
        QVERIFY(request.contains("/chat/completions"));
        job->cancel();
        QCOMPARE(done.size(), 1);
        QVERIFY(!done[0][0].toString().isEmpty());
    }
    void apiError() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                socket->write("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        });
        Services services;
        Model m;
        m.key = "test";
        m.endpoint = QString("http://127.0.0.1:%1").arg(server.serverPort());
        auto *job = services.translate(m, "test", "{text}");
        QSignalSpy done(job, &Job::finished);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done[0][0].toString().contains("401"));
    }
};
QTEST_MAIN(Tests)
#include "tests.moc"
