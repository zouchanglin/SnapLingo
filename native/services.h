#pragma once
#include "settings.h"
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSet>
#include <functional>

class Job : public QObject {
    Q_OBJECT
  public:
    explicit Job(QObject *parent = nullptr) : QObject(parent) {}
    bool complete = false;
    QSet<QNetworkReply *> replies;
    void finish(const QString &error = {});
    void cancel();
  signals:
    void text(const QString &chunk);
    void finished(const QString &error);
};
class Services : public QObject {
    Q_OBJECT
  public:
    explicit Services(QObject *parent = nullptr) : QObject(parent), network(this) {}
    Job *translate(const Model &model, const QString &text, const QString &prompt);
    Job *recognize(const OcrConfig &config, const QImage &image);
    static QJsonObject translationBody(const Model &model, const QString &text, const QString &prompt);
    static QString parseOcr(const QString &provider, const QJsonObject &root);
    static QString localOcr(QImage image);

  private:
    QNetworkAccessManager network;
    using Callback = std::function<void(const QJsonObject &)>;
    void post(Job *job, const QUrl &url, const QByteArray &body, const QByteArray &type, Callback callback,
              const QMap<QByteArray, QByteArray> &headers = {});
};
class SseDecoder {
    QByteArray pending, event;

  public:
    QList<QByteArray> feed(const QByteArray &data, bool eof = false);
};
