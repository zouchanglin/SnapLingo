#include "history.h"
#include <algorithm>
#include <utility>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <stdexcept>

HistoryStore::HistoryStore(const QString &directory)
    : m_directory(directory.isEmpty()
                      ? QDir(qEnvironmentVariable("APPDATA")).filePath("SnapLingo/history")
                      : directory) {
    QDir().mkpath(m_directory);
    reload();
}

QString HistoryStore::indexPath() const {
    return QDir(m_directory).filePath("history.json");
}

QString HistoryStore::imagePath(const HistoryEntry &entry) const {
    return entry.imageFile.isEmpty() ? QString{} : QDir(m_directory).filePath(entry.imageFile);
}

QImage HistoryStore::image(const HistoryEntry &entry) const {
    const auto path = imagePath(entry);
    return path.isEmpty() ? QImage{} : QImage(path);
}

void HistoryStore::reload() {
    m_entries.clear();
    QFile file(indexPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray())
        return;
    for (const auto &value : doc.array()) {
        if (!value.isObject())
            continue;
        const auto obj = value.toObject();
        HistoryEntry entry;
        entry.id = obj["Id"].toString();
        entry.created = QDateTime::fromString(obj["Created"].toString(), Qt::ISODateWithMs);
        entry.imageFile = obj["Image"].toString();
        entry.text = obj["Text"].toString();
        entry.ocrName = obj["Ocr"].toString();
        if (!entry.id.isEmpty() && entry.created.isValid())
            m_entries.append(entry);
    }
}

HistoryEntry HistoryStore::add(const QImage &image, const QString &text, const QString &ocrName, int limit) {
    HistoryEntry entry;
    entry.created = QDateTime::currentDateTime();
    entry.text = text;
    entry.ocrName = ocrName;

    const auto base = entry.created.toString("yyyyMMdd-HHmmss-zzz");
    entry.id = base;
    for (int suffix = 2; std::any_of(m_entries.cbegin(), m_entries.cend(),
                                     [&](const HistoryEntry &e) { return e.id == entry.id; });
         ++suffix)
        entry.id = base + '-' + QString::number(suffix);

    if (!image.isNull()) {
        entry.imageFile = entry.id + ".png";
        if (!image.save(imagePath(entry)))
            entry.imageFile.clear();
    }

    m_entries.prepend(entry);
    prune(limit);
    save();
    return entry;
}

bool HistoryStore::remove(const QString &id) {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id != id)
            continue;
        const auto path = imagePath(m_entries[i]);
        if (!path.isEmpty())
            QFile::remove(path);
        m_entries.removeAt(i);
        save();
        return true;
    }
    return false;
}

void HistoryStore::clear() {
    for (const auto &entry : std::as_const(m_entries)) {
        const auto path = imagePath(entry);
        if (!path.isEmpty())
            QFile::remove(path);
    }
    m_entries.clear();
    save();
}

void HistoryStore::prune(int limit) {
    if (limit <= 0)
        return;
    while (m_entries.size() > limit) {
        const auto entry = m_entries.takeLast();
        const auto path = imagePath(entry);
        if (!path.isEmpty())
            QFile::remove(path);
    }
}

void HistoryStore::save() {
    QDir().mkpath(m_directory);
    QJsonArray array;
    for (const auto &entry : std::as_const(m_entries)) {
        array.append(QJsonObject{{"Id", entry.id},
                                 {"Created", entry.created.toString(Qt::ISODateWithMs)},
                                 {"Image", entry.imageFile},
                                 {"Text", entry.text},
                                 {"Ocr", entry.ocrName}});
    }
    QSaveFile file(indexPath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    file.commit();
}
