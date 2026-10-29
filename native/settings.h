#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>

struct Model {
    QString name = "模型", endpoint = "https://api.deepseek.com/chat/completions", key,
            model = "deepseek-flash";
    bool thinking = false;
    QJsonObject json() const;
    static Model fromJson(const QJsonObject &j);
};
struct OcrConfig {
    QString name = "Windows 本地 OCR", provider = "Windows";
    QString access, secret, baiduKey, baiduSecret, tencentId, tencentKey;
    QJsonObject json() const;
    static OcrConfig fromJson(const QJsonObject &j);
};
struct Settings {
    QVector<Model> models;
    QVector<OcrConfig> ocrs{OcrConfig{}};
    int activeOcr = 0;
    QString prompt = defaultPrompt();
    unsigned modifiers = 6, key = 0x41;
    bool autoStart = false;
    static QString defaultPrompt();
    static QString filePath();
    static Settings load(const QString &path = filePath());
    static Settings fromJson(const QJsonObject &j);
    QJsonObject json() const;
    void save(const QString &path = filePath()) const;
    OcrConfig active() const;
};
