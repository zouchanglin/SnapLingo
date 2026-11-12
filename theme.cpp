#include "theme.h"
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

namespace {
ThemeMode g_mode = ThemeMode::System;

// Widget-level rules that cannot be expressed with Fluent tokens alone. They reference
// palette() roles so re-installing them after a palette swap repaints plain Qt widgets.
// zh_CN: 仅靠 Fluent token 无法覆盖的控件规则；颜色取自 palette()，换色后重新安装即可刷新。
const char *kStyleSheet = R"(
        QToolTip { padding: 6px 8px; border: 1px solid palette(mid); border-radius: 4px; }
        QPlainTextEdit, QTextEdit {
            border: 1px solid palette(mid); border-radius: 4px; padding: 6px;
            background: palette(base); selection-background-color: palette(highlight);
            selection-color: palette(highlighted-text);
        }
        QListWidget { background: transparent; border: none; outline: 0; }
        QListWidget::item { padding: 8px 10px; border-radius: 4px; margin: 1px 0; }
        QListWidget::item:hover { background: palette(alternate-base); }
        QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QScrollArea, QScrollArea > QWidget > QWidget { border: none; background: transparent; }
        QScrollBar:vertical { width: 10px; margin: 2px 0; background: transparent; }
        QScrollBar::handle:vertical { min-height: 28px; border-radius: 4px; background: palette(mid); }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { height: 10px; margin: 0 2px; background: transparent; }
        QScrollBar::handle:horizontal { min-width: 28px; border-radius: 4px; background: palette(mid); }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
    )";

QPalette fluentPalette(fluent::FluentElement::Theme theme) {
    const auto c = fluent::ThemeRegistry::instance().colors(theme);
    QPalette palette;
    palette.setColor(QPalette::Window, c.bgCanvas);
    palette.setColor(QPalette::WindowText, c.textPrimary);
    palette.setColor(QPalette::Base, c.bgLayer);
    palette.setColor(QPalette::AlternateBase, c.bgLayerAlt);
    palette.setColor(QPalette::Text, c.textPrimary);
    palette.setColor(QPalette::Button, c.controlDefault);
    palette.setColor(QPalette::ButtonText, c.textPrimary);
    palette.setColor(QPalette::BrightText, c.textOnAccent);
    palette.setColor(QPalette::Highlight, c.accentDefault);
    palette.setColor(QPalette::HighlightedText, c.textOnAccent);
    palette.setColor(QPalette::Link, c.textAccentPrimary);
    palette.setColor(QPalette::LinkVisited, c.textAccentPrimary);
    palette.setColor(QPalette::PlaceholderText, c.textTertiary);
    palette.setColor(QPalette::Mid, c.strokeDefault);
    palette.setColor(QPalette::Midlight, c.strokeSecondary);
    palette.setColor(QPalette::Dark, c.strokeSecondary);
    palette.setColor(QPalette::Shadow, c.strokeStrong);
    palette.setColor(QPalette::ToolTipBase, c.bgLayerAlt);
    palette.setColor(QPalette::ToolTipText, c.textPrimary);
    for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText, QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled, role, c.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, c.accentDisabled);
    return palette;
}

// QStyleSheetStyle caches rules resolved from palette(...), so an unchanged stylesheet string
// would keep the previous colors. Toggling it empty first guarantees a fresh resolve.
// zh_CN: QStyleSheetStyle 会缓存 palette(...) 解析结果，字符串未变时不会重新取色，故先清空再安装。
void reinstallStyleSheet(QApplication &app) {
    if (!app.styleSheet().isEmpty())
        app.setStyleSheet(QString());
    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}

#ifdef Q_OS_WIN
// FluentQt asks DWM for a dark caption frame, but changing the application palette makes Qt re-derive
// its own frame attributes from the Windows color scheme and drop that request a moment later. The
// theme therefore owns the frame: every relevant window event re-asserts it, so a switch can never
// leave a window with a stale light title bar or navigation pane. New windows created after a switch
// (message boxes, the history window) are corrected when they are first shown.
// zh_CN: FluentQt 会向 DWM 申请深色标题栏，但修改应用调色板会让 Qt 按系统配色重算原生窗框并把该请求
// 丢掉。因此由主题模块统管窗框：任何相关窗口事件都会重新确认一次，切换后不可能残留浅色标题栏或导航栏；
// 切换之后才创建的窗口（消息框、历史窗口）在首次显示时也会被纠正。
BOOL nativeFrameWantsDark() {
    return g_mode == ThemeMode::System ? systemPrefersDark() : g_mode == ThemeMode::Dark;
}

void syncNativeFrame(QWidget *widget) {
    if (!widget || !widget->isWindow() || !widget->windowHandle())
        return;
    switch (widget->windowType()) {
    case Qt::ToolTip:
    case Qt::Popup:
    case Qt::Tool:
        return; // Transient native helpers have no caption to theme. zh_CN: 临时原生窗口没有标题栏。
    default:
        break;
    }
    const auto hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd)
        return;
    const BOOL dark = nativeFrameWantsDark();
    BOOL current = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &current, sizeof(current)))
        && current == dark)
        return;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void syncNativeFrames() {
    for (auto *widget : QApplication::topLevelWidgets())
        syncNativeFrame(widget);
}

// Application-wide watchdog: Qt's own frame handling can run after a palette change, so the correct
// value is re-asserted whenever a window paints, is shown, or becomes active.
// zh_CN: 应用级看门狗：Qt 的窗框处理可能晚于调色板变化发生，因此窗口绘制、显示或激活时都会重新确认。
class NativeFrameGuard final : public QObject {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (auto *widget = qobject_cast<QWidget *>(watched); widget && widget->isWindow()) {
            switch (event->type()) {
            case QEvent::ApplicationPaletteChange:
            case QEvent::Show:
            case QEvent::WindowActivate:
            case QEvent::Paint:
                syncNativeFrame(widget);
                break;
            default:
                break;
            }
        }
        return false;
    }
};
#endif
} // namespace

QString themeModeKey(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light:
        return "Light";
    case ThemeMode::Dark:
        return "Dark";
    case ThemeMode::System:
        break;
    }
    return "System";
}
ThemeMode themeModeFromKey(const QString &key) {
    const auto value = key.trimmed().toLower();
    if (value == "light")
        return ThemeMode::Light;
    if (value == "dark")
        return ThemeMode::Dark;
    return ThemeMode::System;
}
QString themeModeLabel(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light:
        return "亮色";
    case ThemeMode::Dark:
        return "暗色";
    case ThemeMode::System:
        break;
    }
    return "跟随系统";
}
bool systemPrefersDark() {
    if (auto *hints = QGuiApplication::styleHints())
        return hints->colorScheme() == Qt::ColorScheme::Dark;
    return false;
}
fluent::FluentElement::Theme appThemeFor(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light:
        return fluent::FluentElement::Light;
    case ThemeMode::Dark:
        return fluent::FluentElement::Dark;
    case ThemeMode::System:
        break;
    }
    return systemPrefersDark() ? fluent::FluentElement::Dark : fluent::FluentElement::Light;
}
void applyAppTheme(ThemeMode mode) {
    g_mode = mode;
    const auto theme = appThemeFor(mode);
    // Plain Qt widgets follow the application palette and its stylesheet, while Fluent widgets repaint
    // from tokens. Refresh the palette and stylesheet first, then announce the Fluent theme.
    // zh_CN: 普通 Qt 控件跟随应用调色板与样式表，Fluent 控件按 token 重绘。先换调色板与样式表，
    // 再广播 Fluent 主题。
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        app->setPalette(fluentPalette(theme));
        reinstallStyleSheet(*app);
    }
    fluent::FluentElement::setTheme(theme);
#ifdef Q_OS_WIN
    // Keep the native frame in step with the Fluent theme. The guard is installed once and corrects
    // windows whenever Qt re-derives their frame; the immediate and delayed passes cover the switch
    // itself and Qt's late palette handling.
    // zh_CN: 让原生窗框跟随 Fluent 主题。看门狗只安装一次，在 Qt 重算窗框时纠正；立即与延后两次同步
    // 覆盖切换本身以及 Qt 稍晚的调色板处理。
    static bool frameGuardInstalled = false;
    if (!frameGuardInstalled) {
        frameGuardInstalled = true;
        qApp->installEventFilter(new NativeFrameGuard(qApp));
    }
    syncNativeFrames();
    QTimer::singleShot(0, qApp, [] { syncNativeFrames(); });
    QTimer::singleShot(250, qApp, [] { syncNativeFrames(); });
#endif
}
ThemeMode currentThemeMode() {
    return g_mode;
}
void refreshFollowedSystemTheme() {
    if (g_mode == ThemeMode::System)
        applyAppTheme(ThemeMode::System);
}
