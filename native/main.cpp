#include "window.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QSslSocket>
#include <QTimer>
#include <QtConcurrent>
#include <windows.h>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("SnapLingo");
    app.setApplicationVersion("0.3.0");
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(QIcon(":/app.ico"));
    QSslSocket::setActiveBackend("schannel");
    app.setStyle("Fusion");
    QFont font("Microsoft YaHei UI", 10);
    app.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#f1f3f5"));
    palette.setColor(QPalette::WindowText, QColor("#253039"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#f5f7f8"));
    palette.setColor(QPalette::Text, QColor("#253039"));
    palette.setColor(QPalette::Button, QColor("#f7f8f9"));
    palette.setColor(QPalette::ButtonText, QColor("#253039"));
    palette.setColor(QPalette::Highlight, QColor("#287e88"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, QColor("#216f79"));
    palette.setColor(QPalette::PlaceholderText, QColor("#879198"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#99a1a7"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#99a1a7"));
    app.setPalette(palette);
    app.setStyleSheet(R"(
        QToolTip { padding: 5px; }
        QLineEdit { padding: 5px 6px; }
        QPlainTextEdit { padding: 7px; }
        QComboBox { padding: 5px 8px; }
        QPushButton { padding: 6px 14px; min-height: 18px; }
        QPushButton[primary="true"] {
            background: #287e88; color: white; border: 1px solid #236f78;
            border-radius: 3px; padding: 7px 16px;
        }
        QPushButton[primary="true"]:hover { background: #318d98; }
        QPushButton[primary="true"]:pressed { background: #206873; }
        QPushButton[primary="true"]:focus { border: 2px solid #164d54; padding: 6px 15px; }
        QPushButton[primary="true"]:disabled { background: #c8d5d7; border-color: #bcc9cb; color: #69777a; }
        QTabBar::tab { padding: 9px 17px; }
        QGroupBox { margin-top: 12px; padding: 12px 8px 6px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QListWidget::item { padding: 7px 6px; }
        QLabel[muted="true"] { color: #748089; }
        QStatusBar { border-top: 1px solid #d7dce0; }
    )");
    const auto args = app.arguments();
    if (args.contains("--preview-settings")) {
        const int index = args.indexOf("--preview-settings");
        if (index + 1 >= args.size())
            return 1;
        Settings example;
        Model model;
        model.name = "DeepSeek";
        example.models.append(model);
        SettingsDialog dialog(example);
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
    int result = app.exec();
    CloseHandle(mutex);
    return result;
}
