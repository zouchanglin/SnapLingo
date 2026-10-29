#pragma once
#include "capture.h"
#include "services.h"
#include "settings.h"
#include "settingsdialog.h"
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

class Window : public QMainWindow {
    Q_OBJECT
  public:
    explicit Window(QWidget *parent = nullptr);
    ~Window() override;
    void capture();
    void openSettings();

  protected:
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    Settings settings;
    Services services;
    QSystemTrayIcon *tray;
    QLabel *status;
    QPlainTextEdit *source;
    QVBoxLayout *cards;
    QPointer<SettingsDialog> dialog;
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
    QString validateHotkey(const Settings &candidate);
};
