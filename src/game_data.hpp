#pragma once

#include <QMap>
#include <QString>
#include <QVector>

struct GameDataEntry {
    int id = 0;
    QString name;
    QString english;
    QString source;
    int rarity = 0;
    int maxLevel = 0;
    int generationTier = 0;
};

struct PalicoPattern {
    QString kind;
    int id = 0;
    QString sequence;
};

struct PalicoForteGrant {
    int forteId = 0;
    QString kind;
    int entryId = 0;
};

class GameData {
public:
    bool load(const QString &language = QStringLiteral("cn"));
    QString error() const { return m_error; }
    QString rootPath() const { return m_rootPath; }
    QString language() const { return m_language; }

    QVector<GameDataEntry> entries(const QString &table) const;
    GameDataEntry entry(const QString &table, int id) const;
    QString name(const QString &table, int id) const;
    bool contains(const QString &table, int id) const;
    QString equipmentTable(int type) const;
    QString palicoEquipmentTable(int rawType) const;
    QVector<PalicoPattern> patterns(const QString &kind) const;
    QVector<PalicoForteGrant> forteGrants(int forteId, const QString &kind) const;

private:
    static QStringList parseCsvLine(const QString &line);
    bool loadTable(const QString &path, const QString &table);
    QString findDataRoot() const;

    QMap<QString, QMap<int, GameDataEntry>> m_tables;
    QVector<PalicoPattern> m_patterns;
    QVector<PalicoForteGrant> m_forteGrants;
    QString m_rootPath;
    QString m_language;
    QString m_error;
};
