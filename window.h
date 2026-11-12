#pragma once
#include "capture.h"
#include "history.h"
#include "services.h"
#include "settings.h"
#include "settingsdialog.h"
#include <FluentQt/Windowing.h>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

class HistoryDialog;

namespace fluent::status_info {
class ProgressRing;
}
namespace fluent::textfields {
class Label;
}

class Window : public fluent::windowing::Window {
    Q_OBJECT
  public:
    explicit Window(QWidget *parent = nullptr);
    ~Window() override;
    void capture();
    void openSettings();
    void openHistory();

  protected:
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    Settings settings;
    Services services;
    HistoryStore history;
    QSystemTrayIcon *tray;
    fluent::textfields::Label *status;
    fluent::textfields::Label *hotkeyHint;
    fluent::status_info::ProgressRing *busy;
    QPlainTextEdit *source;
    QVBoxLayout *cards;
    QStackedWidget *resultsPages;
    QPointer<SettingsDialog> dialog;
    QPointer<HistoryDialog> historyDialog;
    QPointer<Capture> overlay;
    QVector<QPointer<Job>> jobs;
    quint64 generation = 0;
    bool capturing = false;
    void activate();
    void cancelJobs();
    void clearCards();
    void translate(const QString &text);
    void registerHotkey();
    void unregisterHotkey();
    void updateHotkeyHint();
    QString validateHotkey(const Settings &candidate);
};
