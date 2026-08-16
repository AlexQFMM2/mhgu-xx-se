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
    m_weaponLevelSlots.clear();
    m_talismanSkillLimits.clear();
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
    if (table == QStringLiteral("weapon_level_slots")) {
        while (!stream.atEnd()) {
            const QStringList fields = parseCsvLine(stream.readLine());
            if (fields.size() < header.size()) continue;
            const int type = fields.value(columns.value(QStringLiteral("equipment_type"), -1)).toInt();
            const int weaponId = fields.value(columns.value(QStringLiteral("weapon_id"), -1)).toInt();
            const int saveLevel = fields.value(columns.value(QStringLiteral("save_level"), -1)).toInt();
            const int slotCount = fields.value(columns.value(QStringLiteral("slots"), -1)).toInt();
            const quint32 key = (quint32(type) << 24) | (quint32(weaponId) << 8) | quint32(saveLevel);
            m_weaponLevelSlots.insert(key, slotCount);
        }
        return true;
    }
    if (table == QStringLiteral("talisman_skill_limits")) {
        while (!stream.atEnd()) {
            const QStringList fields = parseCsvLine(stream.readLine());
            if (fields.size() < header.size()) continue;
            const int talismanId = fields.value(columns.value(QStringLiteral("talisman_id"), -1)).toInt();
            const int skillId = fields.value(columns.value(QStringLiteral("skill_id"), -1)).toInt();
            TalismanSkillLimit limit;
            limit.skill1Min = fields.value(columns.value(QStringLiteral("skill1_min"), -1)).toInt();
            limit.skill1Max = fields.value(columns.value(QStringLiteral("skill1_max"), -1)).toInt();
            limit.skill2Min = fields.value(columns.value(QStringLiteral("skill2_min"), -1)).toInt();
            limit.skill2Max = fields.value(columns.value(QStringLiteral("skill2_max"), -1)).toInt();
            const quint32 key = (quint32(talismanId) << 16) | quint32(skillId);
            m_talismanSkillLimits.insert(key, limit);
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
        if (columns.contains(QStringLiteral("slot_cost")))
            entry.slotCost = value(QStringLiteral("slot_cost")).toInt();
        if (columns.contains(QStringLiteral("slots")))
            entry.nativeSlots = value(QStringLiteral("slots")).toInt();
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
        {11, QStringLiteral("weapon_heavy_bowgun")}, {13, QStringLiteral("weapon_light_bowgun")},
        {14, QStringLiteral("weapon_long_sword")}, {15, QStringLiteral("weapon_switch_axe")},
        {16, QStringLiteral("weapon_gunlance")}, {17, QStringLiteral("weapon_bow")},
        {18, QStringLiteral("weapon_dual_blades")}, {19, QStringLiteral("weapon_hunting_horn")},
        {20, QStringLiteral("weapon_insect_glaive")}, {21, QStringLiteral("weapon_charge_blade")}
    };
    return tables.value(type);
}

int GameData::weaponSlots(int type, int weaponId, int saveLevel, bool *found) const
{
    const quint32 key = (quint32(type) << 24) | (quint32(weaponId) << 8) | quint32(saveLevel);
    const auto it = m_weaponLevelSlots.constFind(key);
    if (found) *found = it != m_weaponLevelSlots.constEnd();
    return it == m_weaponLevelSlots.constEnd() ? 0 : it.value();
}

int GameData::armorSlots(int type, int armorId, bool *found) const
{
    const QString tableName = equipmentTable(type);
    const auto table = m_tables.constFind(tableName);
    if (type < 1 || type > 5 || table == m_tables.constEnd()) {
        if (found) *found = false;
        return 0;
    }
    const auto it = table.value().constFind(armorId);
    const bool valid = it != table.value().constEnd() && it.value().nativeSlots >= 0;
    if (found) *found = valid;
    return valid ? it.value().nativeSlots : 0;
}

int GameData::decorationSlotCost(int itemId, bool *found) const
{
    if (itemId == 0) {
        if (found) *found = true;
        return 0;
    }
    const auto table = m_tables.constFind(QStringLiteral("decorations"));
    if (table == m_tables.constEnd()) {
        if (found) *found = false;
        return 0;
    }
    const QMap<int, GameDataEntry> &rows = table.value();
    const auto it = rows.constFind(itemId);
    const bool valid = it != rows.constEnd() && it.value().slotCost >= 0;
    if (found) *found = valid;
    return valid ? it.value().slotCost : 0;
}

bool GameData::talismanSkillRange(int talismanId, int skillId, int position,
                                  int *minimum, int *maximum) const
{
    if (position != 1 && position != 2) return false;
    const quint32 key = (quint32(talismanId) << 16) | quint32(skillId);
    const auto it = m_talismanSkillLimits.constFind(key);
    if (it == m_talismanSkillLimits.constEnd()) return false;
    if (minimum) *minimum = position == 1 ? it.value().skill1Min : it.value().skill2Min;
    if (maximum) *maximum = position == 1 ? it.value().skill1Max : it.value().skill2Max;
    return true;
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
