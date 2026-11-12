#pragma once
#include <FluentQt/Foundation.h>
#include <QString>

// User-selectable appearance mode shown on the settings page.
// zh_CN: 设置页面中可选的外观模式。
enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

// Persisted spelling, also accepted case-insensitively for hand-edited settings.json.
// zh_CN: 持久化名称；手工编辑 settings.json 时同样忽略大小写。
QString themeModeKey(ThemeMode mode);
ThemeMode themeModeFromKey(const QString &key);
// Localized label shown on the settings page. zh_CN: 设置页显示的名称。
QString themeModeLabel(ThemeMode mode);

// Whether Windows currently reports a dark app color scheme.
// zh_CN: Windows 当前是否处于深色应用模式。
bool systemPrefersDark();

// Resolves a mode to the concrete Fluent palette in use.
// zh_CN: 将模式解析为实际使用的 Fluent 配色。
fluent::FluentElement::Theme appThemeFor(ThemeMode mode);

// Applies the Fluent tokens, the QApplication palette and stylesheet, and remembers the mode.
// zh_CN: 应用 Fluent token、QApplication 调色板与样式表，并记住当前模式。
void applyAppTheme(ThemeMode mode);

// The mode last passed to applyAppTheme(). zh_CN: 最近一次应用的模式。
ThemeMode currentThemeMode();

// Re-applies the theme only while the user is following the system setting.
// zh_CN: 仅在「跟随系统」时按系统配色重新应用主题。
void refreshFollowedSystemTheme();
