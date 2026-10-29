#include "settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <stdexcept>

static QString str(const QJsonObject &j, const char *k, const QString &fallback = {}) {
    return j.value(k).toString(fallback);
}
QJsonObject Model::json() const {
    return {{"Name", name},
            {"Endpoint", endpoint},
            {"ApiKey", key},
            {"Model", model},
            {"EnableThinking", thinking}};
}
Model Model::fromJson(const QJsonObject &j) {
    Model m;
    m.name = str(j, "Name", m.name);
    m.endpoint = str(j, "Endpoint", m.endpoint);
    m.key = str(j, "ApiKey");
    m.model = str(j, "Model", m.model);
    m.thinking = j["EnableThinking"].toBool();
    return m;
}
QJsonObject OcrConfig::json() const {
    return {{"Name", name},
            {"Provider", provider},
            {"VolcengineAccessKeyId", access},
            {"VolcengineSecretAccessKey", secret},
            {"BaiduApiKey", baiduKey},
            {"BaiduSecretKey", baiduSecret},
            {"TencentSecretId", tencentId},
            {"TencentSecretKey", tencentKey}};
}
OcrConfig OcrConfig::fromJson(const QJsonObject &j) {
    OcrConfig o;
    o.name = str(j, "Name", o.name);
    o.provider = str(j, "Provider", "Windows");
    o.access = str(j, "VolcengineAccessKeyId");
    o.secret = str(j, "VolcengineSecretAccessKey");
    o.baiduKey = str(j, "BaiduApiKey");
    o.baiduSecret = str(j, "BaiduSecretKey");
    o.tencentId = str(j, "TencentSecretId");
    o.tencentKey = str(j, "TencentSecretKey");
    return o;
}
QString Settings::defaultPrompt() {
    return QStringLiteral(
        "请根据以下规则翻译文本：\n1. 如果文本主要是中文，翻译成英文\n2. "
        "如果文本主要是英文或其他非中文语言，翻译成中文\n3. "
        "只输出翻译结果，不要添加任何解释、注释或额外内容\n4. 保持原文的格式和换行\n\n{text}");
}
QString Settings::filePath() {
    return QDir(qEnvironmentVariable("APPDATA")).filePath("SnapLingo/settings.json");
}
Settings Settings::fromJson(const QJsonObject &j) {
    Settings s;
    for (const auto &v : j["Models"].toArray())
        if (v.isObject())
            s.models.append(Model::fromJson(v.toObject()));
    if (s.models.isEmpty() && !str(j, "ApiKey").isEmpty()) {
        auto m = Model::fromJson(j);
        m.name = "默认模型";
        s.models.append(m);
    }
    s.ocrs.clear();
    for (const auto &v : j["OcrServices"].toArray())
        if (v.isObject())
            s.ocrs.append(OcrConfig::fromJson(v.toObject()));
    if (s.ocrs.isEmpty() && j["Ocr"].isObject())
        s.ocrs.append(OcrConfig::fromJson(j["Ocr"].toObject()));
    if (s.ocrs.isEmpty())
        s.ocrs.append(OcrConfig{});
    s.activeOcr = qBound(0, j["ActiveOcrIndex"].toInt(), int(s.ocrs.size() - 1));
    s.prompt = str(j, "TranslationPrompt", defaultPrompt());
    s.modifiers = unsigned(j["HotkeyModifiers"].toInt(6));
    s.key = unsigned(j["HotkeyKey"].toInt(0x41));
    s.autoStart = j["AutoStart"].toBool();
    return s;
}
Settings Settings::load(const QString &path) {
    QFile f(path);
    if (!f.exists())
        return {};
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("无法读取设置文件");
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject())
        throw std::runtime_error("设置文件损坏，请备份后修复 settings.json");
    return fromJson(d.object());
}
QJsonObject Settings::json() const {
    QJsonArray m, o;
    for (const auto &v : models)
        m.append(v.json());
    for (const auto &v : ocrs)
        o.append(v.json());
    return {{"Models", m},
            {"OcrServices", o},
            {"ActiveOcrIndex", activeOcr},
            {"TranslationPrompt", prompt},
            {"HotkeyModifiers", int(modifiers)},
            {"HotkeyKey", int(key)},
            {"AutoStart", autoStart}};
}
void Settings::save(const QString &path) const {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        throw std::runtime_error("无法创建设置目录");
    // Preserve the original WPF settings once, without logging credentials.
    if (QFile::exists(path) && !QFile::exists(path + ".pre-qt.bak")) {
        if (!QFile::copy(path, path + ".pre-qt.bak"))
            throw std::runtime_error("无法备份旧设置");
    }
    QSaveFile f(path);
    const auto bytes = QJsonDocument(json()).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
        throw std::runtime_error("保存设置失败");
}
OcrConfig Settings::active() const {
    return ocrs.isEmpty() ? OcrConfig{} : ocrs[qBound(0, activeOcr, int(ocrs.size() - 1))];
}
