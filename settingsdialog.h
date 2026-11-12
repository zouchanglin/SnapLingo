#pragma once
#include "settings.h"
#include <FluentQt/TextFields.h>
#include <FluentQt/Windowing.h>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <functional>

class QKeyEvent;
class QButtonGroup;
class QStandardItemModel;
class HistoryStore;
namespace fluent::collections {
class ListView;
}

QString hotkeyText(unsigned modifiers, unsigned key);

class HotkeyEdit : public fluent::textfields::LineEdit {
    Q_OBJECT
  public:
    unsigned modifiers = 6, key = 0x41;
    explicit HotkeyEdit(QWidget *parent = nullptr);
    void refresh();
  signals:
    void recording(bool active);

  protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
};
class SettingsDialog : public fluent::windowing::Window {
    Q_OBJECT
  public:
    explicit SettingsDialog(const Settings &settings, QWidget *parent = nullptr,
                            HistoryStore *history = nullptr);
    void setPage(int index);
    void onThemeUpdated() override;
    std::function<QString(const Settings &)> validate;
    Settings draft;
  signals:
    void saved(const Settings &settings);
    void recordingHotkey(bool active);

  protected:
    void keyPressEvent(QKeyEvent *e) override;

  private:
    fluent::collections::ListView *models = nullptr;
    fluent::collections::ListView *ocrs = nullptr;
    QStandardItemModel *modelsModel = nullptr;
    QStandardItemModel *ocrsModel = nullptr;
    QLineEdit *modelName, *endpoint, *apiKey, *modelId, *ocrName;
    QCheckBox *thinking, *autoStart, *historyEnabled;
    QComboBox *provider, *historyLimit;
    QButtonGroup *themeGroup;
    QStackedWidget *credentials;
    QVector<QLineEdit *> secrets;
    fluent::textfields::TextEdit *prompt;
    HotkeyEdit *hotkey;
    fluent::collections::ListView *navList = nullptr;
    HistoryStore *history = nullptr;
    bool loading = false;
    void loadModel();
    void syncModel();
    void loadOcr();
    void syncOcr();
    void refreshOcrList();
    void refreshNavIcons();
    void showToast(const QString &message, bool success = true);
    void save();
};
