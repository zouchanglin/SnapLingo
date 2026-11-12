#include "historydialog.h"
#include "theme.h"
#include "window.h"
#include <FluentQt/FluentQt.h>
#include <QTemporaryDir>
#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QStyleHints>
#include <QSslSocket>
#include <QTimer>
#include <QtConcurrent>
#include <windows.h>

int main(int argc, char **argv) {
    fluent::prepareHighDpiApplication();
    QApplication app(argc, argv);
    app.setApplicationName("SnapLingo");
    app.setApplicationVersion("0.0.1");
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(QIcon(":/app.ico"));
    QSslSocket::setActiveBackend("schannel");
    app.setStyle("Fusion");
    const auto args = app.arguments();

    fluent::initializeResources();
    Settings startup;
    try {
        startup = Settings::load();
    } catch (const std::exception &) {
        // The main window reports the broken settings file with details.
        // zh_CN: 设置文件损坏时，主窗口会给出具体提示。
    }
    auto themeMode = themeModeFromKey(startup.theme);
    if (args.contains("--dark"))
        themeMode = ThemeMode::Dark;
    else if (args.contains("--light"))
        themeMode = ThemeMode::Light;
    applyAppTheme(themeMode);
    app.setFont(Typography::fontStyle(Typography::FontRole::Body).toQFont());
    // Follow Windows personalization changes while the user keeps "跟随系统".
    // zh_CN: 用户在「跟随系统」时，随 Windows 个性化配色变化自动切换。
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [](Qt::ColorScheme) { refreshFollowedSystemTheme(); });

    if (args.contains("--preview-main")) {
        const int index = args.indexOf("--preview-main");
        if (index + 1 >= args.size())
            return 1;
        Window window;
        window.show();
        app.processEvents();
        return window.grab().save(args[index + 1]) ? 0 : 1;
    }
    if (args.contains("--preview-settings")) {
        const int index = args.indexOf("--preview-settings");
        if (index + 1 >= args.size())
            return 1;
        Settings example;
        Model model;
        model.name = "DeepSeek";
        example.models.append(model);
        QTemporaryDir historyDir;
        HistoryStore previewHistory(historyDir.filePath("history"));
        SettingsDialog dialog(example, nullptr, &previewHistory);
        if (index + 2 < args.size())
            dialog.setPage(args[index + 2].toInt());
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.show();
        app.processEvents();
        return dialog.grab().save(args[index + 1]) ? 0 : 1;
    }
    if (args.contains("--preview-history")) {
        const int index = args.indexOf("--preview-history");
        if (index + 1 >= args.size())
            return 1;
        QTemporaryDir historyDir;
        HistoryStore store(historyDir.filePath("history"));
        if (!(index + 2 < args.size() && args[index + 2] == "empty")) {
            QImage sample(640, 360, QImage::Format_RGB32);
            sample.fill(QColor("#eef2f7"));
            QPainter painter(&sample);
            painter.setPen(QColor("#253039"));
            painter.setFont(QFont("Segoe UI", 17));
            painter.drawText(sample.rect().adjusted(24, 0, -24, 0), Qt::AlignCenter,
                             "SnapLingo OCR sample\nCMake Tools provides a full-featured workflow.");
            painter.end();
            store.add(sample, "CMake Tools provides a full-featured workflow for CMake based projects.",
                      "Windows OCR");
            store.add(sample, "截图与识别历史示例文字，只保存在本机。", "Windows OCR");
        }
        HistoryDialog dialog(store);
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.show();
        app.processEvents();
        return dialog.grab().save(args[index + 1]) ? 0 : 1;
    }
    if (args.contains("--smoke-test")) {
        QJsonObject report{{"qt", qVersion()},
                           {"tls", QSslSocket::supportsSsl()},
                           {"tlsBackend", QSslSocket::activeBackend()}};
        QImage image(640, 120, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Arial", 36));
        painter.drawText(image.rect(), Qt::AlignCenter, "HELLO WORLD");
        painter.end();
        auto result = QtConcurrent::run([image] {
                          try {
                              return Services::localOcr(image);
                          } catch (...) {
                              return QString{};
                          }
                      }).result();
        report["ocr"] = result;
        bool matched = true;
        try {
            for (auto *screen : QGuiApplication::screens())
                if (Capture::screenPixels(screen).isEmpty())
                    matched = false;
        } catch (...) {
            matched = false;
        }
        report["monitorMapping"] = matched;
        auto i = args.indexOf("--report");
        if (i >= 0 && i + 1 < args.size()) {
            QFile f(args[i + 1]);
            if (f.open(QIODevice::WriteOnly))
                f.write(QJsonDocument(report).toJson());
        }
        return report["tls"].toBool() && !result.isEmpty() && matched ? 0 : 2;
    }
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\SnapLingo.Qt.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        QMessageBox::information(nullptr, "SnapLingo", "SnapLingo 已在系统托盘运行。");
        CloseHandle(mutex);
        return 0;
    }
    Window window;
    if (args.contains("--show"))
        window.show();
    if (args.contains("--settings"))
        window.openSettings();
    if (args.contains("--history"))
        window.openHistory();
    int result = app.exec();
    CloseHandle(mutex);
    return result;
}
