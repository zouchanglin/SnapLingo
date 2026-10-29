#pragma once
#include <QImage>
#include <QObject>
#include <QScreen>
#include <QVector>
#include <QWidget>

struct Monitor {
    QString name;
    QRect pixels;
};
class Capture : public QObject {
    Q_OBJECT
  public:
    explicit Capture(QObject *parent = nullptr);
    ~Capture() override;
    void start();
    void move();
    void begin();
    void end();
    void cancel();
    QRect desktop;
    QImage snapshot;
    QVector<QWidget *> overlays;
    QPoint anchor, cursor;
    bool selecting = false, finished = false;
    static QVector<Monitor> monitors();
    static QRect screenPixels(QScreen *screen);
    static QImage grabDesktop(QRect &bounds);
    static QRect selection(QPoint a, QPoint b, QRect bounds);
  signals:
    void captured(const QImage &image);
};
