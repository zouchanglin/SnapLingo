#include "window.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <windows.h>

Window::Window(QWidget *p) : QMainWindow(p), services(this) {
    setWindowTitle("SnapLingo");
    setWindowIcon(QIcon(":/app.ico"));
    resize(680, 660);
    setMinimumSize(480, 400);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    auto *bar = new QHBoxLayout;
    auto *heading = new QVBoxLayout;
    heading->setSpacing(3);
    auto *title = new QLabel("SnapLingo");
    QFont titleFont = title->font();
    titleFont.setPointSize(16);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    heading->addWidget(title);
    auto *subtitle = new QLabel("截图识别 · 即时翻译");
    subtitle->setProperty("muted", true);
    heading->addWidget(subtitle);
    bar->addLayout(heading);
    bar->addStretch();
    auto *shot = new QPushButton("截图翻译");
    shot->setProperty("primary", true);
    auto *settingsButton = new QPushButton("设置");
    settingsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    bar->addWidget(shot);
    bar->addWidget(settingsButton);
    layout->addLayout(bar);
    auto *sourceBox = new QGroupBox("识别原文");
    auto *sl = new QVBoxLayout(sourceBox);
    source = new QPlainTextEdit;
    source->setPlaceholderText("截图识别，或在此输入需要翻译的文字…");
    source->setMaximumHeight(180);
    source->installEventFilter(this);
    sl->addWidget(source);
    auto *sb = new QHBoxLayout;
    auto *keyHint = new QLabel("Enter 翻译 · Shift+Enter 换行");
    keyHint->setProperty("muted", true);
    sb->addWidget(keyHint);
    sb->addStretch();
    auto *copy = new QPushButton("复制原文");
    auto *go = new QPushButton("翻译");
    sb->addWidget(copy);
    sb->addWidget(go);
    sl->addLayout(sb);
    layout->addWidget(sourceBox);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *container = new QWidget;
    cards = new QVBoxLayout(container);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setAlignment(Qt::AlignTop);
    scroll->setWidget(container);
    layout->addWidget(scroll, 1);
    status = new QLabel("就绪");
    status->setWordWrap(true);
    status->setProperty("muted", true);
    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(status, 1);
    connect(shot, &QPushButton::clicked, this, &Window::capture);
    connect(settingsButton, &QPushButton::clicked, this, &Window::openSettings);
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(source->toPlainText());
        status->setText("已复制");
    });
    connect(go, &QPushButton::clicked, this, [this] { translate(source->toPlainText()); });
    tray = new QSystemTrayIcon(windowIcon(), this);
    tray->setToolTip("SnapLingo");
    auto *menu = new QMenu(this);
    menu->addAction("截图翻译", this, &Window::capture);
    menu->addAction("显示结果", this, &Window::activate);
    menu->addAction("设置", this, &Window::openSettings);
    menu->addSeparator();
    menu->addAction("退出", qApp, &QApplication::quit);
    tray->setContextMenu(menu);
    connect(tray, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            activate();
    });
    tray->show();
    try {
        settings = Settings::load();
    } catch (const std::exception &e) {
        QTimer::singleShot(0, this, [this, msg = QString::fromUtf8(e.what())] {
            QMessageBox::warning(this, "读取设置失败", msg);
        });
    }
    registerHotkey();
    if (settings.models.isEmpty())
        QTimer::singleShot(0, this, &Window::openSettings);
}
Window::~Window() {
    unregisterHotkey();
    cancelJobs();
    if (overlay)
        overlay->cancel();
}
void Window::activate() {
    showNormal();
    raise();
    activateWindow();
}
void Window::unregisterHotkey() {
    UnregisterHotKey(reinterpret_cast<HWND>(winId()), 9000);
}
void Window::registerHotkey() {
    unregisterHotkey();
    if (!RegisterHotKey(reinterpret_cast<HWND>(winId()), 9000, settings.modifiers | MOD_NOREPEAT,
                        settings.key)) {
        status->setText("快捷键被占用，请关闭旧版 SnapLingo 或更改快捷键");
        tray->showMessage("SnapLingo", status->text(), QSystemTrayIcon::Warning);
    }
}
QString Window::validateHotkey(const Settings &c) {
    for (const auto &m : c.models) {
        QUrl u(m.endpoint);
        if (!u.isValid() || (u.scheme() != "https" && u.scheme() != "http") || u.host().isEmpty())
            return "模型 API 地址无效";
        if (m.model.trimmed().isEmpty())
            return "模型名称不能为空";
    }
    unregisterHotkey();
    bool ok = RegisterHotKey(reinterpret_cast<HWND>(winId()), 9001, c.modifiers | MOD_NOREPEAT, c.key);
    if (ok)
        UnregisterHotKey(reinterpret_cast<HWND>(winId()), 9001);
    registerHotkey();
    return ok ? QString{} : QStringLiteral("快捷键被占用，请换一个组合");
}
bool Window::nativeEvent(const QByteArray &, void *message, qintptr *result) {
    auto *m = static_cast<MSG *>(message);
    if (m->message == WM_HOTKEY && m->wParam == 9000) {
        capture();
        *result = 0;
        return true;
    }
    return false;
}
void Window::closeEvent(QCloseEvent *e) {
    e->ignore();
    hide();
}
bool Window::eventFilter(QObject *watched, QEvent *e) {
    if (watched == source && e->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(e);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            !(key->modifiers() & Qt::ShiftModifier)) {
            translate(source->toPlainText());
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, e);
}
void Window::cancelJobs() {
    ++generation;
    auto old = jobs;
    jobs.clear();
    for (auto j : old)
        if (j)
            j->cancel();
}
void Window::clearCards() {
    while (auto *item = cards->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}
void Window::openSettings() {
    if (dialog) {
        dialog->showNormal();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    dialog = new SettingsDialog(settings);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->validate = [this](const Settings &c) { return validateHotkey(c); };
    connect(dialog, &SettingsDialog::saved, this, [this](const Settings &s) {
        settings = s;
        registerHotkey();
        status->setText("设置已保存");
    });
    connect(dialog, &SettingsDialog::recordingHotkey, this, [this](bool active) {
        if (active)
            unregisterHotkey();
        else
            registerHotkey();
    });
    connect(dialog, &QObject::destroyed, this, [this] { registerHotkey(); });
    dialog->show();
}
void Window::capture() {
    if (capturing)
        return;
    capturing = true;
    cancelJobs();
    clearCards();
    source->clear();
    hide();
    if (dialog)
        dialog->hide();
    const auto id = generation;
    const auto config = settings.active();
    QTimer::singleShot(180, this, [=, this] {
        overlay = new Capture(this);
        connect(overlay, &Capture::captured, this, [=, this](const QImage &image) {
            capturing = false;
            overlay = nullptr;
            if (image.isNull()) {
                status->setText("已取消截图");
                return;
            }
            if (id != generation)
                return;
            status->setText("正在识别…");
            auto *job = services.recognize(config, image);
            jobs.append(job);
            auto text = std::make_shared<QString>();
            connect(job, &Job::text, this, [text](const QString &chunk) { *text += chunk; });
            connect(job, &Job::finished, this, [=, this](const QString &error) {
                if (id != generation)
                    return;
                activate();
                if (!error.isEmpty()) {
                    status->setText("识别失败：" + error);
                    return;
                }
                source->setPlainText(*text);
                if (text->trimmed().isEmpty()) {
                    status->setText("未识别到文字");
                    return;
                }
                translate(*text);
            });
        });
        try {
            overlay->start();
        } catch (const std::exception &e) {
            overlay->deleteLater();
            overlay = nullptr;
            capturing = false;
            status->setText(QString::fromUtf8(e.what()));
            activate();
        }
    });
}
void Window::translate(const QString &text) {
    if (text.trimmed().isEmpty())
        return;
    cancelJobs();
    clearCards();
    auto id = generation;
    if (settings.models.isEmpty()) {
        status->setText("请先在设置中添加翻译模型");
        return;
    }
    status->setText("正在翻译…");
    auto remaining = std::make_shared<int>(int(settings.models.size()));
    auto failed = std::make_shared<bool>(false);
    for (const auto &m : settings.models) {
        auto *box = new QGroupBox(m.name.isEmpty() ? m.model : m.name);
        auto *layout = new QVBoxLayout(box);
        auto *output = new QPlainTextEdit;
        output->setReadOnly(true);
        output->setMinimumHeight(140);
        output->setMaximumHeight(300);
        layout->addWidget(output);
        auto *copy = new QPushButton("复制译文");
        layout->addWidget(copy, 0, Qt::AlignRight);
        connect(copy, &QPushButton::clicked, this,
                [=] { QApplication::clipboard()->setText(output->toPlainText()); });
        cards->addWidget(box);
        auto *job = services.translate(m, text, settings.prompt);
        jobs.append(job);
        connect(job, &Job::text, this, [=, this](const QString &chunk) {
            if (id != generation)
                return;
            auto cursor = output->textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(chunk);
            output->setTextCursor(cursor);
            output->ensureCursorVisible();
        });
        connect(job, &Job::finished, this, [=, this](const QString &error) {
            if (id != generation)
                return;
            if (!error.isEmpty()) {
                *failed = true;
                output->appendPlainText("\n[" + error + "]");
            }
            if (--*remaining == 0)
                status->setText(*failed ? "翻译完成，部分模型出错" : "就绪");
        });
    }
}
