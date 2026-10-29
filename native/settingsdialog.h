#pragma once
#include "settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <functional>

class HotkeyEdit : public QLineEdit {
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
class SettingsDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsDialog(const Settings &settings, QWidget *parent = nullptr);
    std::function<QString(const Settings &)> validate;
    Settings draft;
  signals:
    void saved(const Settings &settings);
    void recordingHotkey(bool active);

  private:
    QListWidget *models, *ocrs;
    QLineEdit *modelName, *endpoint, *apiKey, *modelId, *ocrName;
    QCheckBox *thinking, *autoStart;
    QComboBox *provider;
    QStackedWidget *credentials;
    QVector<QLineEdit *> secrets;
    QPlainTextEdit *prompt;
    HotkeyEdit *hotkey;
    bool loading = false;
    void loadModel();
    void syncModel();
    void loadOcr();
    void syncOcr();
    void refreshOcrList();
    void save();
};
