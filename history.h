#pragma once
#include <QDateTime>
#include <QImage>
#include <QString>
#include <QVector>

struct HistoryEntry {
    QString id;
    QDateTime created;
    QString imageFile;
    QString text;
    QString ocrName;
};

class HistoryStore {
  public:
    explicit HistoryStore(const QString &directory = QString());

    QString directory() const { return m_directory; }
    QString indexPath() const;
    const QVector<HistoryEntry> &entries() const { return m_entries; }

    void reload();
    HistoryEntry add(const QImage &image, const QString &text, const QString &ocrName, int limit = 100);
    bool remove(const QString &id);
    void clear();

    QImage image(const HistoryEntry &entry) const;
    QString imagePath(const HistoryEntry &entry) const;

  private:
    void save();
    void prune(int limit);

    QString m_directory;
    QVector<HistoryEntry> m_entries;
};
