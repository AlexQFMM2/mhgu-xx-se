#include "game_data.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

QStringList GameData::parseCsvLine(const QString &line)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < line.size() && line[i + 1] == QLatin1Char('"')) {
                    field += ch;
                    ++i;
                } else quoted = false;
            } else field += ch;
        } else if (ch == QLatin1Char(',')) {
            fields.push_back(field);
            field.clear();
        } else if (ch == QLatin1Char('"') && field.isEmpty()) {
            quoted = true;
        } else {
            field += ch;
        }
    }
    fields.push_back(field);
    return fields;
}

QString GameData::findDataRoot() const
{
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(app).filePath(QStringLiteral("data")),
        QDir(app).filePath(QStringLiteral("../data")),
        QDir::current().filePath(QStringLiteral("data")),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("manifest.json"))))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

bool GameData::load(const QString &language)
{
    m_tables.clear();
    m_patterns.clear();
    m_forteGrants.clear();
    m_language = language == QStringLiteral("en") ? QStringLiteral("en") : QStringLiteral("cn");
    m_rootPath = findDataRoot();
    if (m_rootPath.isEmpty()) {
        m_error = QStringLiteral("找不到 data/manifest.json。请将 data 文件夹放在程序旁边。");
        return false;
    }
    const QDir directory(QDir(m_rootPath).filePath(m_language));
    const QStringList files = directory.entryList({QStringLiteral("*.csv")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        const QString table = QFileInfo(file).completeBaseName();
        if (!loadTable(directory.filePath(file), table)) return false;
    }
    if (!m_tables.contains(QStringLiteral("items")) || !m_tables.contains(QStringLiteral("equipment_types"))) {
        m_error = QStringLiteral("data 目录不完整，缺少必要的数据表。");
        return false;
    }
    m_error.clear();
    return true;
}

bool GameData::loadTable(const QString &path, const QString &table)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    if (stream.atEnd()) return true;
    QString headerLine = stream.readLine();
    if (!headerLine.isEmpty() && headerLine[0] == QChar(0xFEFF)) headerLine.remove(0, 1);
    const QStringList header = parseCsvLine(headerLine);
    QMap<QString, int> columns;
    for (int i = 0; i < header.size(); ++i) columns.insert(header[i], i);
    if (table == QStringLiteral("palico_generation_patterns")) {
        while (!stream.atEnd()) {
            const QStringList fields = parseCsvLine(stream.readLine());
            if (fields.size() < header.size()) continue;
            PalicoPattern pattern;
            pattern.kind = fields.value(columns.value(QStringLiteral("kind"), -1));
            pattern.id = fields.value(columns.value(QStringLiteral("pattern_id"), -1)).toInt();
            pattern.sequence = fields.value(columns.value(QStringLiteral("sequence"), -1));
            m_patterns.push_back(pattern);
        }
        return true;
    }
    if (table == QStringLiteral("palico_forte_grants")) {
        while (!stream.atEnd()) {
            const QStringList fields = parseCsvLine(stream.readLine());
            if (fields.size() < header.size()) continue;
            PalicoForteGrant grant;
            grant.forteId = fields.value(columns.value(QStringLiteral("forte_id"), -1)).toInt();
            grant.kind = fields.value(columns.value(QStringLiteral("kind"), -1));
            grant.entryId = fields.value(columns.value(QStringLiteral("entry_id"), -1)).toInt();
            m_forteGrants.push_back(grant);
        }
        return true;
    }
    if (!columns.contains(QStringLiteral("id"))) return true;
    QMap<int, GameDataEntry> rows;
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.isEmpty()) continue;
        const QStringList fields = parseCsvLine(line);
        auto value = [&fields, &columns](const QString &key) {
            const int index = columns.value(key, -1);
            return index >= 0 ? fields.value(index) : QString();
        };
        GameDataEntry entry;
        entry.id = value(QStringLiteral("id")).toInt();
        entry.name = value(QStringLiteral("name"));
        entry.english = value(QStringLiteral("english"));
        entry.source = value(QStringLiteral("source"));
        entry.rarity = value(QStringLiteral("rarity")).toInt();
        entry.maxLevel = value(QStringLiteral("max_level")).toInt();
        entry.generationTier = value(QStringLiteral("generation_tier")).toInt();
        rows.insert(entry.id, entry);
    }
    m_tables.insert(table, rows);
    return true;
}

QVector<GameDataEntry> GameData::entries(const QString &table) const
{
    QVector<GameDataEntry> result;
    const auto rows = m_tables.value(table);
    result.reserve(rows.size());
    for (const GameDataEntry &entry : rows) result.push_back(entry);
    return result;
}

GameDataEntry GameData::entry(const QString &table, int id) const
{
    return m_tables.value(table).value(id, GameDataEntry{id, QStringLiteral("未知 #%1").arg(id), {}, {}});
}

QString GameData::name(const QString &table, int id) const
{
    if (id == 0 && !m_tables.value(table).contains(0)) return QStringLiteral("无");
    const GameDataEntry item = entry(table, id);
    return item.name.isEmpty() ? QStringLiteral("未知 #%1").arg(id) : item.name;
}

bool GameData::contains(const QString &table, int id) const
{
    return id == 0 || m_tables.value(table).contains(id);
}

QString GameData::equipmentTable(int type) const
{
    static const QMap<int, QString> tables = {
        {1, QStringLiteral("armor_head")}, {2, QStringLiteral("armor_chest")},
        {3, QStringLiteral("armor_arms")}, {4, QStringLiteral("armor_waist")},
        {5, QStringLiteral("armor_legs")}, {6, QStringLiteral("talismans")},
        {7, QStringLiteral("weapon_great_sword")}, {8, QStringLiteral("weapon_sword_and_shield")},
        {9, QStringLiteral("weapon_hammer")}, {10, QStringLiteral("weapon_lance")},
        {11, QStringLiteral("weapon_light_bowgun")}, {12, QStringLiteral("weapon_heavy_bowgun")},
        {13, QStringLiteral("weapon_long_sword")}, {14, QStringLiteral("weapon_switch_axe")},
        {15, QStringLiteral("weapon_gunlance")}, {16, QStringLiteral("weapon_bow")},
        {17, QStringLiteral("weapon_dual_blades")}, {18, QStringLiteral("weapon_hunting_horn")},
        {19, QStringLiteral("weapon_insect_glaive")}, {20, QStringLiteral("weapon_charge_blade")}
    };
    return tables.value(type);
}

QString GameData::palicoEquipmentTable(int rawType) const
{
    if (rawType == 22) return QStringLiteral("palico_weapons");
    if (rawType == 23) return QStringLiteral("palico_head");
    if (rawType == 24) return QStringLiteral("palico_armor");
    return {};
}

QVector<PalicoPattern> GameData::patterns(const QString &kind) const
{
    QVector<PalicoPattern> result;
    for (const PalicoPattern &pattern : m_patterns) if (pattern.kind == kind) result.push_back(pattern);
    return result;
}

QVector<PalicoForteGrant> GameData::forteGrants(int forteId, const QString &kind) const
{
    QVector<PalicoForteGrant> result;
    for (const PalicoForteGrant &grant : m_forteGrants)
        if (grant.forteId == forteId && grant.kind == kind) result.push_back(grant);
    return result;
}
