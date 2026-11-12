#include "window.h"
#include "historydialog.h"
#include "theme.h"
#include <FluentQt/BasicInput.h>
#include <FluentQt/Layout.h>
#include <FluentQt/StatusInfo.h>
#include <FluentQt/TextFields.h>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QScrollArea>
#include <QTimer>
#include <windows.h>

using fluent::basicinput::Button;
using fluent::layout::Card;
using fluent::status_info::ProgressRing;
using fluent::textfields::Label;

namespace {
Label *makeLabel(const QString &text, Typography::FontRole role,
                 Label::TextColorRole color = Label::TextColorRole::Default) {
    auto *label = new Label(text);
    label->setFluentTypography(role);
    label->setTextColorRole(color);
    return label;
}
Button *makeButton(const QString &text, bool accent = false) {
    auto *button = new Button(text);
    button->setFluentStyle(accent ? Button::Accent : Button::Standard);
    return button;
}
void makeTextSurface(QPlainTextEdit *edit) {
    edit->setFrameShape(QFrame::NoFrame);
    edit->setStyleSheet("QPlainTextEdit { background: transparent; border: none; padding: 0; }");
}
} // namespace

Window::Window(QWidget *p) : fluent::windowing::Window(p), services(this) {
    setWindowTitle("SnapLingo");
    setWindowIcon(QIcon(":/app.ico"));
    setCaptionButtonToolTips("最小化", "最大化", "关闭", "还原");
    resize(800, 700);
    setMinimumSize(620, 520);

    auto *titleContent = new QWidget;
    auto *titleLayout = new QHBoxLayout(titleContent);
    titleLayout->setContentsMargins(10, 0, 0, 0);
    titleLayout->addWidget(makeLabel("SnapLingo", Typography::FontRole::Caption,
                                     Label::TextColorRole::Secondary));
    titleLayout->addStretch();
    titleBar()->setContentWidget(titleContent);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 18, 12, 12);
    layout->setSpacing(16);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(12);
    auto *logo = new QLabel;
    logo->setPixmap(QIcon(":/app.ico").pixmap(42, 42));
    logo->setFixedSize(42, 42);
    auto *heading = new QVBoxLayout;
    heading->setSpacing(1);
    heading->addWidget(makeLabel("SnapLingo", Typography::FontRole::Title));
    heading->addWidget(makeLabel("截图识别 · 多模型并发翻译", Typography::FontRole::Caption,
                                 Label::TextColorRole::Secondary));
    bar->addWidget(logo);
    bar->addLayout(heading);
    bar->addStretch();
    auto *historyButton = makeButton("历史");
    historyButton->setIconGlyph(Typography::Icons::History, 16);
    auto *settingsButton = makeButton("设置");
    settingsButton->setIconGlyph(Typography::Icons::Settings, 16);
    auto *shot = makeButton("截图翻译", true);
    shot->setIconGlyph(Typography::Icons::Camera, 16);
    bar->addWidget(historyButton);
    bar->addWidget(settingsButton);
    bar->addWidget(shot);
    layout->addLayout(bar);

    auto *sourceCard = new Card;
    auto *sl = new QVBoxLayout(sourceCard);
    sl->setContentsMargins(18, 14, 18, 16);
    sl->setSpacing(10);
    auto *sourceHead = new QHBoxLayout;
    sourceHead->addWidget(makeLabel("识别原文", Typography::FontRole::BodyStrong));
    sourceHead->addStretch();
    hotkeyHint = makeLabel(QString{}, Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    sourceHead->addWidget(hotkeyHint);
    sl->addLayout(sourceHead);
    source = new QPlainTextEdit;
    source->setPlaceholderText("截图识别，或在此输入需要翻译的文字…");
    source->setFrameShape(QFrame::NoFrame);
    source->setMinimumHeight(92);
    source->setMaximumHeight(168);
    source->installEventFilter(this);
    sl->addWidget(source);
    auto *sourceActions = new QHBoxLayout;
    auto *copy = makeButton("复制原文");
    copy->setFluentStyle(Button::Subtle);
    copy->setIconGlyph(Typography::Icons::Copy, 14);
    auto *go = makeButton("翻译", true);
    go->setIconGlyph(Typography::Icons::Send, 16);
    sourceActions->addWidget(copy);
    sourceActions->addStretch();
    sourceActions->addWidget(go);
    sl->addLayout(sourceActions);
    layout->addWidget(sourceCard);

    auto *resultsCard = new Card;
    auto *resultsLayout = new QVBoxLayout(resultsCard);
    resultsLayout->setContentsMargins(18, 14, 18, 16);
    resultsLayout->setSpacing(10);
    auto *resultsHead = new QHBoxLayout;
    resultsHead->addWidget(makeLabel("翻译结果", Typography::FontRole::BodyStrong));
    resultsHead->addStretch();
    resultsHead->addWidget(makeLabel("多模型并行流式输出", Typography::FontRole::Caption,
                                     Label::TextColorRole::Secondary));
    resultsLayout->addLayout(resultsHead);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *container = new QWidget;
    cards = new QVBoxLayout(container);
    cards->setContentsMargins(0, 0, 6, 0);
    cards->setSpacing(12);
    cards->setAlignment(Qt::AlignTop);
    scroll->setWidget(container);

    resultsPages = new QStackedWidget;
    auto *emptyHost = new QWidget;
    auto *emptyHostLayout = new QVBoxLayout(emptyHost);
    emptyHostLayout->setContentsMargins(0, 0, 0, 0);
    emptyHostLayout->addStretch(1);
    auto *emptyTitle = makeLabel("还没有翻译结果", Typography::FontRole::BodyStrong);
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyHint = makeLabel("截图或输入文字后点击“翻译”，结果会显示在这里。",
                                Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHostLayout->addWidget(emptyTitle);
    emptyHostLayout->addWidget(emptyHint);
    emptyHostLayout->addStretch(1);
    resultsPages->addWidget(emptyHost);
    resultsPages->addWidget(scroll);
    resultsLayout->addWidget(resultsPages, 1);
    layout->addWidget(resultsCard, 1);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    busy = new ProgressRing;
    busy->setRingSize(ProgressRing::ProgressRingSize::Small);
    busy->setIsIndeterminate(true);
    busy->setIsActive(false);
    status = makeLabel("就绪", Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    status->setWordWrap(true);
    footer->addWidget(busy);
    footer->addWidget(status, 1);
    layout->addLayout(footer);

    setContentWidget(central);

    connect(shot, &Button::clicked, this, &Window::capture);
    connect(historyButton, &Button::clicked, this, &Window::openHistory);
    connect(settingsButton, &Button::clicked, this, &Window::openSettings);
    connect(copy, &Button::clicked, this, [this] {
        QApplication::clipboard()->setText(source->toPlainText());
        status->setText("已复制原文");
    });
    connect(go, &Button::clicked, this, [this] { translate(source->toPlainText()); });
    tray = new QSystemTrayIcon(windowIcon(), this);
    tray->setToolTip("SnapLingo");
    auto *menu = new QMenu(this);
    menu->addAction("截图翻译", this, &Window::capture);
    menu->addAction("显示结果", this, &Window::activate);
    menu->addAction("历史记录", this, &Window::openHistory);
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
    updateHotkeyHint();
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
void Window::updateHotkeyHint() {
    const auto combo = hotkeyText(settings.modifiers, settings.key);
    hotkeyHint->setText(combo.isEmpty()
                            ? "Enter 翻译 · Shift+Enter 换行"
                            : combo + " 截图 · Enter 翻译 · Shift+Enter 换行");
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
bool Window::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
    auto *m = static_cast<MSG *>(message);
    if (m->message == WM_HOTKEY && m->wParam == 9000) {
        capture();
        *result = 0;
        return true;
    }
    return fluent::windowing::Window::nativeEvent(type, message, result);
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
    return fluent::windowing::Window::eventFilter(watched, e);
}
void Window::cancelJobs() {
    ++generation;
    auto old = jobs;
    jobs.clear();
    for (auto j : old)
        if (j)
            j->cancel();
    if (busy)
        busy->setIsActive(false);
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
    dialog = new SettingsDialog(settings, nullptr, &history);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->validate = [this](const Settings &c) { return validateHotkey(c); };
    connect(dialog, &SettingsDialog::saved, this, [this](const Settings &s) {
        settings = s;
        applyAppTheme(themeModeFromKey(s.theme));
        updateHotkeyHint();
        registerHotkey();
        status->setText("设置已保存");
    });
    connect(dialog, &SettingsDialog::recordingHotkey, this, [this](bool active) {
        if (active)
            unregisterHotkey();
        else
            registerHotkey();
    });
    connect(dialog, &QObject::destroyed, this, [this] {
        registerHotkey();
        // Drop an unsaved appearance preview so the dialog never leaves a temporary theme behind.
        // zh_CN: 放弃未保存的外观预览，避免对话框关闭后残留临时主题。
        applyAppTheme(themeModeFromKey(settings.theme));
    });
    dialog->show();
}
void Window::openHistory() {
    if (historyDialog) {
        historyDialog->reload();
        historyDialog->showNormal();
        historyDialog->raise();
        historyDialog->activateWindow();
        return;
    }
    historyDialog = new HistoryDialog(history);
    historyDialog->setAttribute(Qt::WA_DeleteOnClose);
    historyDialog->show();
}
void Window::capture() {
    if (capturing)
        return;
    capturing = true;
    cancelJobs();
    clearCards();
    resultsPages->setCurrentIndex(0);
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
            busy->setIsActive(true);
            auto *job = services.recognize(config, image);
            jobs.append(job);
            auto text = std::make_shared<QString>();
            connect(job, &Job::text, this, [text](const QString &chunk) { *text += chunk; });
            connect(job, &Job::finished, this, [=, this](const QString &error) {
                if (id != generation)
                    return;
                busy->setIsActive(false);
                activate();
                if (!error.isEmpty()) {
                    status->setText("识别失败：" + error);
                    return;
                }
                if (settings.historyEnabled && !image.isNull()) {
                    history.add(image, *text, config.name, settings.historyLimit);
                    if (historyDialog)
                        historyDialog->reload();
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
            busy->setIsActive(false);
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
    resultsPages->setCurrentIndex(1);
    status->setText("正在翻译…");
    busy->setIsActive(true);
    auto remaining = std::make_shared<int>(int(settings.models.size()));
    auto failed = std::make_shared<bool>(false);
    for (const auto &m : settings.models) {
        auto *box = new Card;
        box->setAppearance(Card::LayerAlt);
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(18, 14, 18, 16);
        layout->setSpacing(8);
        auto *head = new QHBoxLayout;
        head->addWidget(makeLabel(m.name.isEmpty() ? m.model : m.name,
                                  Typography::FontRole::BodyStrong));
        head->addStretch();
        auto *copy = makeButton("复制译文");
        copy->setFluentStyle(Button::Subtle);
        copy->setFluentSize(Button::Small);
        copy->setIconGlyph(Typography::Icons::Copy, 14);
        head->addWidget(copy);
        layout->addLayout(head);
        auto *output = new QPlainTextEdit;
        output->setReadOnly(true);
        output->setMinimumHeight(110);
        output->setMaximumHeight(240);
        makeTextSurface(output);
        layout->addWidget(output);
        cards->addWidget(box);
        connect(copy, &Button::clicked, this,
                [=] { QApplication::clipboard()->setText(output->toPlainText()); });
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
            if (--*remaining == 0) {
                busy->setIsActive(false);
                status->setText(*failed ? "翻译完成，部分模型出错" : "就绪");
            }
        });
    }
}
