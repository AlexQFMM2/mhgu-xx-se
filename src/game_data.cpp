#include "game_data.hpp"
#include "mhgu_save.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>

namespace {
QString sqlitePath(const QString &root) { return QDir(root).filePath(QStringLiteral("mhgu.sqlite")); }
}

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
    m_tableColumns.clear();
    m_tableRowCounts.clear();
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
    const QString sqlite = sqlitePath(m_rootPath);
    if (QFileInfo::exists(sqlite)) {
        QFile manifestFile(QDir(m_rootPath).filePath(QStringLiteral("mhgu-sqlite-manifest.json")));
        QFile databaseFile(sqlite);
        if (!manifestFile.open(QIODevice::ReadOnly) || !databaseFile.open(QIODevice::ReadOnly)) {
            m_error = QStringLiteral("SQLite 数据库或 manifest 无法读取。");
            return false;
        }
        const QJsonObject sqliteManifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        const QString expectedHash = sqliteManifest.value(QStringLiteral("database")).toObject().value(QStringLiteral("sha256")).toString();
        QCryptographicHash hasher(QCryptographicHash::Sha256);
        while (!databaseFile.atEnd()) hasher.addData(databaseFile.read(1024 * 1024));
        if (sqliteManifest.value(QStringLiteral("format")).toString() != QStringLiteral("mhgu-save-editor-data-v1") ||
                expectedHash.size() != 64 || QString::fromLatin1(hasher.result().toHex()) != expectedHash) {
            m_error = QStringLiteral("mhgu.sqlite 与 manifest 校验值不一致，已拒绝加载。 ");
            return false;
        }
        const QString connection = QStringLiteral("mhgu_data_%1").arg(quintptr(this));
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(sqlite);
        if (db.open()) {
            const QString prefix = m_language + QStringLiteral("__");
            QSqlQuery tables(db);
            if (tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name LIKE '%1%' ORDER BY name").arg(prefix))) {
                while (tables.next()) {
                    const QString physical = tables.value(0).toString();
                    if (!physical.startsWith(prefix)) continue;
                    const QString table = physical.mid(prefix.size());
                    QSqlQuery query(db);
                    if (!query.exec(QStringLiteral("SELECT * FROM \"%1\"").arg(physical))) continue;
                    const QSqlRecord record = query.record();
                    QSet<QString> columnNames;
                    for (int column = 0; column < record.count(); ++column)
                        columnNames.insert(record.fieldName(column));
                    QMap<int, GameDataEntry> rows;
                    int rowCount = 0;
                    while (query.next()) {
                        ++rowCount;
                        const int idIndex = record.indexOf(QStringLiteral("id"));
                        if (idIndex < 0) continue;
                        GameDataEntry entry;
                        entry.id = query.value(idIndex).toInt();
                        auto value = [&query, &record](const QString &name) {
                            const int index = record.indexOf(name);
                            return index >= 0 ? query.value(index).toString() : QString();
                        };
                        entry.name = value(QStringLiteral("name"));
                        entry.english = value(QStringLiteral("english"));
                        entry.source = value(QStringLiteral("source"));
                        entry.rarity = value(QStringLiteral("rarity")).toInt();
                        entry.maxLevel = value(QStringLiteral("max_level")).toInt();
                        entry.generationTier = value(QStringLiteral("generation_tier")).toInt();
                        entry.generationGroup = value(QStringLiteral("generation_group"));
                        entry.role = value(QStringLiteral("role"));
                        const QString teachable = value(QStringLiteral("teachable"));
                        entry.teachable = teachable == QStringLiteral("1") ? 1 : teachable == QStringLiteral("0") ? 0 : -1;
                        const QString memoryCost = value(QStringLiteral("memory_cost"));
                        entry.memoryCost = memoryCost.isEmpty() ? -1 : memoryCost.toInt();
                        entry.generationWeights = value(QStringLiteral("generation_weights"));
                        entry.slotCost = value(QStringLiteral("slot_cost")).toInt();
                        entry.nativeSlots = value(QStringLiteral("slots")).toInt();
                        for (int column = 0; column < record.count(); ++column)
                            entry.attributes.insert(record.fieldName(column), query.value(column).toString());
                        rows.insert(entry.id, entry);
                    }
                    if (!rows.isEmpty()) {
                        m_tables.insert(table, rows);
                        m_tableColumns.insert(table, columnNames);
                        m_tableRowCounts.insert(table, rowCount);
                    }
                }
            }
            // Special tables have no id column and are loaded explicitly.
            QSqlQuery q(db);
            const QString weaponSql = QStringLiteral("SELECT equipment_type,weapon_id,save_level,slots FROM \"%1weapon_level_slots\"").arg(prefix);
            if (q.exec(weaponSql)) while (q.next()) {
                const quint32 key = (quint32(q.value(0).toInt()) << 24) | (quint32(q.value(1).toInt()) << 8) | quint32(q.value(2).toInt());
                m_weaponLevelSlots.insert(key, q.value(3).toInt());
            }
            q = QSqlQuery(db);
            if (q.exec(QStringLiteral("SELECT talisman_id,skill_id,skill1_min,skill1_max,skill2_min,skill2_max FROM \"%1talisman_skill_limits\"").arg(prefix)))
                while (q.next()) {
                    TalismanSkillLimit limit{q.value(2).toInt(), q.value(3).toInt(), q.value(4).toInt(), q.value(5).toInt()};
                    const quint32 key = (quint32(q.value(0).toInt()) << 16) | quint32(q.value(1).toInt());
                    m_talismanSkillLimits.insert(key, limit);
                }
            q = QSqlQuery(db);
            if (q.exec(QStringLiteral("SELECT scope,pattern_id,sequence,valid_length,weight FROM \"%1palico_generation_patterns\"").arg(prefix)))
                while (q.next()) m_patterns.push_back({q.value(0).toString(), q.value(1).toInt(), q.value(2).toString(), q.value(3).toInt(), q.value(4).toInt()});
            q = QSqlQuery(db);
            if (q.exec(QStringLiteral("SELECT forte_id,kind,role,entry_id FROM \"%1palico_forte_grants\"").arg(prefix)))
                while (q.next()) m_forteGrants.push_back({q.value(0).toInt(), q.value(1).toString(), q.value(2).toString(), q.value(3).toInt()});
            q = QSqlQuery();
            tables = QSqlQuery();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connection);
            if (m_tables.contains(QStringLiteral("items")) && m_tables.contains(QStringLiteral("equipment_types"))) {
                m_error.clear();
                return true;
            }
            m_tables.clear(); m_tableColumns.clear(); m_tableRowCounts.clear(); m_weaponLevelSlots.clear(); m_talismanSkillLimits.clear();
        }
        db = QSqlDatabase();
        if (QSqlDatabase::contains(connection)) QSqlDatabase::removeDatabase(connection);
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
    m_tableColumns.insert(table, QSet<QString>(header.cbegin(), header.cend()));
    QMap<QString, int> columns;
    for (int i = 0; i < header.size(); ++i) columns.insert(header[i], i);
    if (table == QStringLiteral("palico_generation_patterns")) {
        int rowCount = 0;
        while (!stream.atEnd()) {
            const QStringList fields = parseCsvLine(stream.readLine());
            if (fields.size() < header.size()) continue;
            PalicoPattern pattern;
            pattern.scope = fields.value(columns.value(QStringLiteral("scope"), -1));
            pattern.id = fields.value(columns.value(QStringLiteral("pattern_id"), -1)).toInt();
            pattern.sequence = fields.value(columns.value(QStringLiteral("sequence"), -1));
            pattern.validLength = fields.value(columns.value(QStringLiteral("valid_length"), -1)).toInt();
            pattern.weight = fields.value(columns.value(QStringLiteral("weight"), -1)).toInt();
            m_patterns.push_back(pattern);
            ++rowCount;
        }
        m_tableRowCounts.insert(table, rowCount);
        return true;
    }
    if (table == QStringLiteral("palico_forte_grants")) {
        int rowCount = 0;
        while (!stream.atEnd()) {
            const QStringList fields = parseCsvLine(stream.readLine());
            if (fields.size() < header.size()) continue;
            PalicoForteGrant grant;
            grant.forteId = fields.value(columns.value(QStringLiteral("forte_id"), -1)).toInt();
            grant.kind = fields.value(columns.value(QStringLiteral("kind"), -1));
            grant.role = fields.value(columns.value(QStringLiteral("role"), -1));
            grant.entryId = fields.value(columns.value(QStringLiteral("entry_id"), -1)).toInt();
            m_forteGrants.push_back(grant);
            ++rowCount;
        }
        m_tableRowCounts.insert(table, rowCount);
        return true;
    }
    if (table == QStringLiteral("weapon_level_slots")) {
        int rowCount = 0;
        while (!stream.atEnd()) {
            const QStringList fields = parseCsvLine(stream.readLine());
            if (fields.size() < header.size()) continue;
            const int type = fields.value(columns.value(QStringLiteral("equipment_type"), -1)).toInt();
            const int weaponId = fields.value(columns.value(QStringLiteral("weapon_id"), -1)).toInt();
            const int saveLevel = fields.value(columns.value(QStringLiteral("save_level"), -1)).toInt();
            const int slotCount = fields.value(columns.value(QStringLiteral("slots"), -1)).toInt();
            const quint32 key = (quint32(type) << 24) | (quint32(weaponId) << 8) | quint32(saveLevel);
            m_weaponLevelSlots.insert(key, slotCount);
            ++rowCount;
        }
        m_tableRowCounts.insert(table, rowCount);
        return true;
    }
    if (table == QStringLiteral("talisman_skill_limits")) {
        int rowCount = 0;
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
            ++rowCount;
        }
        m_tableRowCounts.insert(table, rowCount);
        return true;
    }
    if (!columns.contains(QStringLiteral("id"))) return true;
    QMap<int, GameDataEntry> rows;
    int rowCount = 0;
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
        entry.generationGroup = value(QStringLiteral("generation_group"));
        entry.role = value(QStringLiteral("role"));
        const QString teachable = value(QStringLiteral("teachable"));
        entry.teachable = teachable == QStringLiteral("1") ? 1 : teachable == QStringLiteral("0") ? 0 : -1;
        const QString memoryCost = value(QStringLiteral("memory_cost"));
        entry.memoryCost = memoryCost.isEmpty() ? -1 : memoryCost.toInt();
        entry.generationWeights = value(QStringLiteral("generation_weights"));
        if (columns.contains(QStringLiteral("slot_cost")))
            entry.slotCost = value(QStringLiteral("slot_cost")).toInt();
        if (columns.contains(QStringLiteral("slots")))
            entry.nativeSlots = value(QStringLiteral("slots")).toInt();
        for (auto column = columns.cbegin(); column != columns.cend(); ++column)
            entry.attributes.insert(column.key(), value(column.key()));
        rows.insert(entry.id, entry);
        ++rowCount;
    }
    m_tables.insert(table, rows);
    m_tableRowCounts.insert(table, rowCount);
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
    GameDataEntry fallback;
    fallback.id = id;
    fallback.name = QStringLiteral("未知 #%1").arg(id);
    return m_tables.value(table).value(id, fallback);
}

QString GameData::name(const QString &table, int id) const
{
    if (id == 0 && !m_tables.value(table).contains(0)) return QStringLiteral("无");
    const GameDataEntry item = entry(table, id);
    return item.name.isEmpty() ? QStringLiteral("未知 #%1").arg(id) : item.name;
}

bool GameData::contains(const QString &table, int id) const
{
    // ID 0 is represented by an explicit "无" row only where the source
    // table actually defines it.  Treating zero as universally valid masks
    // corrupt non-empty equipment (for example a Palico item with id 0).
    return id >= 0 && m_tables.value(table).contains(id);
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

int GameData::talismanMaxSlots(int talismanId, bool *found) const
{
    const auto talismans = m_tables.constFind(QStringLiteral("talismans"));
    if (talismans == m_tables.constEnd()) {
        if (found) *found = false;
        return 0;
    }
    const auto charm = talismans->constFind(talismanId);
    const bool valid = charm != talismans->constEnd() &&
        charm->attributes.contains(QStringLiteral("max_slots"));
    if (found) *found = valid;
    return valid ? charm->attributes.value(QStringLiteral("max_slots")).toInt() : 0;
}

bool GameData::talismanSlotsLegal(int talismanId, int skill1Id, int skill1Points,
                                  int skill2Id, int skill2Points, int slotCount) const
{
    bool found = false;
    const int absoluteMaximum = talismanMaxSlots(talismanId, &found);
    if (!found || slotCount < 0 || slotCount > absoluteMaximum) return false;
    if (!skill1Id && !skill2Id) return true;
    if (!skill1Id || skill1Points <= 0) return false;
    int firstMinimum = 0, firstMaximum = 0;
    if (!talismanSkillRange(talismanId, skill1Id, 1, &firstMinimum, &firstMaximum) ||
            firstMaximum <= 0) return false;
    double fulfillmentValue = double(skill1Points) * 10.0 / double(firstMaximum);
    if (skill2Id) {
        int secondMinimum = 0, secondMaximum = 0;
        if (!talismanSkillRange(talismanId, skill2Id, 2, &secondMinimum, &secondMaximum) ||
                secondMaximum <= 0) return false;
        if (skill2Points > 0)
            fulfillmentValue += double(skill2Points) * 10.0 / double(secondMaximum);
    }
    int fulfillment = int(fulfillmentValue);
    fulfillment = qBound(1, fulfillment, 20);
    const int rarityMarker = entry(QStringLiteral("talismans"), talismanId).rarity;
    for (const GameDataEntry &row : entries(QStringLiteral("talisman_slot_thresholds"))) {
        if (row.attributes.value(QStringLiteral("rarity_marker")).toInt() != rarityMarker ||
                row.attributes.value(QStringLiteral("fulfillment")).toInt() != fulfillment)
            continue;
        int possibleMaximum = 0;
        if (row.attributes.value(QStringLiteral("slot1_threshold")).toInt() < 100) possibleMaximum = 1;
        if (row.attributes.value(QStringLiteral("slot2_threshold")).toInt() < 100) possibleMaximum = 2;
        if (row.attributes.value(QStringLiteral("slot3_threshold")).toInt() < 100) possibleMaximum = 3;
        return slotCount <= qMin(absoluteMaximum, possibleMaximum);
    }
    return false;
}

QString GameData::palicoEquipmentTable(int rawType) const
{
    if (rawType == 22) return QStringLiteral("palico_weapons");
    if (rawType == 23) return QStringLiteral("palico_head");
    if (rawType == 24) return QStringLiteral("palico_armor");
    return {};
}

QVector<PalicoPattern> GameData::patterns(const QString &scope) const
{
    QVector<PalicoPattern> result;
    for (const PalicoPattern &pattern : m_patterns) if (pattern.scope == scope) result.push_back(pattern);
    return result;
}

PalicoPattern GameData::pattern(const QString &scope, int id) const
{
    for (const PalicoPattern &pattern : m_patterns)
        if (pattern.scope == scope && pattern.id == id) return pattern;
    return {};
}

QVector<PalicoForteGrant> GameData::forteGrants(int forteId, const QString &kind,
                                                const QString &role) const
{
    QVector<PalicoForteGrant> result;
    for (const PalicoForteGrant &grant : m_forteGrants)
        if ((grant.forteId == forteId || grant.forteId == -1) && grant.kind == kind &&
            (role.isEmpty() || grant.role == role)) result.push_back(grant);
    return result;
}

bool GameData::validateHunterEquipment(const MhguEquipment &entry, QString *error) const
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!entry.record.isEmpty()) {
        if (entry.record.size() != 36)
            return fail(QStringLiteral("装备原始记录不是完整的 36 字节。"));
        const quint16 packed = quint16(quint8(entry.record[0])) |
            (quint16(quint8(entry.record[1])) << 8);
        if ((packed & 0xfc00) != 0)
            return fail(QStringLiteral("装备记录包含未确认的类型标志位。"));
    }
    if (entry.type == 0) {
        if (entry.id || entry.appearanceId || entry.level || entry.skill1 || entry.skill2 ||
                entry.skill1Points || entry.skill2Points || entry.talismanSlots ||
                entry.decorations[0] || entry.decorations[1] || entry.decorations[2])
            return fail(QStringLiteral("空装备记录包含非零字段。"));
        return true;
    }
    const bool armor = entry.type >= 1 && entry.type <= 5;
    const bool weapon = entry.type >= 7 && entry.type <= 21 && entry.type != 12;
    const QString table = equipmentTable(entry.type);
    if (entry.id == 0 && entry.type != 0)
        return fail(QStringLiteral("非空装备不能使用空 ID。"));
    if (table.isEmpty() || !contains(table, entry.id))
        return fail(QStringLiteral("装备类型 %1 与 ID %2 不匹配或不存在。").arg(entry.type).arg(entry.id));
    if (armor && entry.appearanceId && !contains(table, entry.appearanceId))
        return fail(QStringLiteral("防具幻化 ID %1 不属于相同部位。").arg(entry.appearanceId));
    if (weapon && entry.appearanceId)
        return fail(QStringLiteral("MHGU/XX 不允许武器幻化。"));

    bool found = false;
    int available = 0;
    if (armor) available = armorSlots(entry.type, entry.id, &found);
    else if (weapon) available = weaponSlots(entry.type, entry.id, entry.level, &found);
    else if (entry.type == 6) {
        if (entry.level || entry.appearanceId)
            return fail(QStringLiteral("护石记录不能包含强化等级或幻化 ID。"));
        found = talismanSlotsLegal(entry.id, entry.skill1, entry.skill1Points,
                                   entry.skill2, entry.skill2Points, entry.talismanSlots);
        available = entry.talismanSlots;
        if (entry.id < 1 || entry.id > 10) return fail(QStringLiteral("护石品级不存在。"));
        if (entry.skill1 && entry.skill1 == entry.skill2)
            return fail(QStringLiteral("护石的两个技能不能相同。"));
        if (!entry.skill1 && entry.skill2)
            return fail(QStringLiteral("护石不能在第一技能为空时设置第二技能。"));
        const auto skill = [this, error, &entry](int position, int id, int points) {
            if (id == 0) {
                if (points == 0) return true;
                if (error) *error = QStringLiteral("护石空技能的点数必须为 0。");
                return false;
            }
            if (!contains(QStringLiteral("skills"), id)) {
                if (error) *error = QStringLiteral("护石包含不存在的技能 ID %1。").arg(id);
                return false;
            }
            int minimum = 0, maximum = 0;
            if (!talismanSkillRange(entry.id, id, position, &minimum, &maximum) ||
                    (minimum == 0 && maximum == 0) || points < minimum || points > maximum) {
                if (error) *error = QStringLiteral("护石技能 %1 的第 %2 技能点数 %3 不在确认范围 %4～%5。")
                    .arg(id).arg(position).arg(points).arg(minimum).arg(maximum);
                return false;
            }
            return true;
        };
        if (!skill(1, entry.skill1, entry.skill1Points) || !skill(2, entry.skill2, entry.skill2Points)) return false;
        if (!found) return fail(QStringLiteral("该护石的技能点组合不能自然生成 %1 个孔。")
                                .arg(entry.talismanSlots));
    }
    if (!found) return fail(QStringLiteral("原生数据中不存在此装备等级或孔位记录。"));
    int used = 0;
    for (quint16 id : entry.decorations) {
        if (!id) continue;
        bool decorationFound = false;
        const int cost = decorationSlotCost(id, &decorationFound);
        if (!decorationFound || cost <= 0) return fail(QStringLiteral("装饰珠 ID %1 不是已确认的合法珠子。").arg(id));
        used += cost;
    }
    if (used > available) return fail(QStringLiteral("装饰珠占用 %1 孔，装备只有 %2 孔。").arg(used).arg(available));
    if (error) error->clear();
    return true;
}

bool GameData::validateLoadout(const QVector<MhguEquipment> &entries,
                               QStringList *errors, const QString &gender) const
{
    QStringList issues;
    if (entries.size() != 7) {
        issues.push_back(QStringLiteral("配装必须包含武器、五件防具和护石共 7 条记录。"));
    } else {
        const int expectedTypes[] = {0, 1, 2, 3, 4, 5, 6};
        const char *labels[] = {"武器", "头", "胸", "腕", "腰", "腿", "护石"};
        for (int i = 0; i < entries.size(); ++i) {
            const MhguEquipment &entry = entries.at(i);
            if (entry.type == 0) {
                issues.push_back(QStringLiteral("%1未选择装备。 ").arg(QString::fromUtf8(labels[i])));
                continue;
            }
            const bool typeMatches = i == 0
                ? (entry.type >= 7 && entry.type <= 21 && entry.type != 12)
                : entry.type == expectedTypes[i];
            if (!typeMatches) {
                issues.push_back(QStringLiteral("%1的装备类型不匹配。 ").arg(QString::fromUtf8(labels[i])));
                continue;
            }
            QString issue;
            if (!validateHunterEquipment(entry, &issue))
                issues.push_back(QStringLiteral("%1：%2").arg(QString::fromUtf8(labels[i]), issue));
        }
        if (gender != QStringLiteral("male") && gender != QStringLiteral("female") && !gender.isEmpty())
            issues.push_back(QStringLiteral("配装性别必须为 male 或 female。"));
        if ((gender == QStringLiteral("male") || gender == QStringLiteral("female")) &&
                entries[0].type >= 7) {
            const int expectedGender = gender == QStringLiteral("male") ? 0 : 1;
            const int expectedCombat = entries[0].type == 11 || entries[0].type == 13 ||
                entries[0].type == 17 ? 1 : 0;
            for (int part = 0; part < 5; ++part) {
                bool matched = false;
                for (const LoadoutArmorData &armor : loadoutArmor(part + 1)) {
                    if (armor.id != entries[part + 1].id) continue;
                    matched = true;
                    if (armor.gender != 2 && armor.gender != expectedGender)
                        issues.push_back(QStringLiteral("%1与所选性别不兼容。").arg(QString::fromUtf8(labels[part + 1])));
                    if (armor.combat != 2 && armor.combat != expectedCombat)
                        issues.push_back(QStringLiteral("%1与武器的近战/远程类型不兼容。").arg(QString::fromUtf8(labels[part + 1])));
                    break;
                }
                if (!matched)
                    issues.push_back(QStringLiteral("%1缺少已验证的性别与近战/远程属性。").arg(QString::fromUtf8(labels[part + 1])));
            }
        }
    }
    if (errors) *errors = issues;
    return issues.isEmpty();
}

bool GameData::validatePalicoEquipment(const MhguPalicoEquipment &entry, QString *error) const
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (entry.rawType == 0) return entry.id == 0 && entry.appearanceId == 0
        ? true : fail(QStringLiteral("空猫装备记录包含非零字段。"));
    if (entry.id == 0) return fail(QStringLiteral("非空猫装备不能使用空 ID。"));
    const QString table = palicoEquipmentTable(entry.rawType);
    if (table.isEmpty() || !contains(table, entry.id)) return fail(QStringLiteral("猫装备类型与 ID 不匹配。"));
    if (entry.rawType == 22 && entry.appearanceId) return fail(QStringLiteral("MHGU/XX 不允许猫武器幻化。"));
    if ((entry.rawType == 23 || entry.rawType == 24) && entry.appearanceId && !contains(table, entry.appearanceId))
        return fail(QStringLiteral("猫防具幻化 ID 不属于相同部位。"));
    if (error) error->clear();
    return true;
}

QString GameData::dataVersion() const
{
    QFile file(QDir(m_rootPath).filePath(QStringLiteral("mhgu-sqlite-manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("csv-fallback");
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    return root.value(QStringLiteral("database")).toObject().value(QStringLiteral("sha256")).toString();
}

bool GameData::automaticLoadoutDataComplete(QStringList *missing) const
{
    QStringList value;
    const QMap<QString, QSet<QString>> required = {
        {QStringLiteral("armor_skill_points"), {QStringLiteral("equipment_type"), QStringLiteral("armor_id"),
            QStringLiteral("skill_id"), QStringLiteral("points")}},
        {QStringLiteral("decoration_skill_points"), {QStringLiteral("decoration_id"),
            QStringLiteral("skill_id"), QStringLiteral("points")}},
        {QStringLiteral("active_skills"), {QStringLiteral("id"), QStringLiteral("skill_id"),
            QStringLiteral("points"), QStringLiteral("name")}},
        {QStringLiteral("armor_stats"), {QStringLiteral("equipment_type"), QStringLiteral("armor_id"),
            QStringLiteral("gender"), QStringLiteral("combat"), QStringLiteral("base_defense"),
            QStringLiteral("max_defense"), QStringLiteral("fire_res"), QStringLiteral("water_res"),
            QStringLiteral("thunder_res"), QStringLiteral("ice_res"), QStringLiteral("dragon_res")}},
        {QStringLiteral("talismans"), {QStringLiteral("id"), QStringLiteral("rarity"),
            QStringLiteral("max_slots")}},
        {QStringLiteral("talisman_slot_thresholds"), {QStringLiteral("rarity_marker"),
            QStringLiteral("fulfillment"), QStringLiteral("slot1_threshold"),
            QStringLiteral("slot2_threshold"), QStringLiteral("slot3_threshold")}}
    };
    for (auto table = required.cbegin(); table != required.cend(); ++table) {
        if (!m_tables.contains(table.key()) || m_tableRowCounts.value(table.key()) <= 0) {
            value.push_back(table.key());
            continue;
        }
        const QSet<QString> absent = table.value() - m_tableColumns.value(table.key());
        if (!absent.isEmpty())
            value.push_back(QStringLiteral("%1(缺字段:%2)").arg(table.key(),
                QStringList(absent.cbegin(), absent.cend()).join(QLatin1Char(','))));
    }
    if (missing) *missing = value;
    return value.isEmpty();
}

QVector<LoadoutArmorData> GameData::loadoutArmor(int type) const
{
    QVector<LoadoutArmorData> result;
    if (type < 1 || type > 5 || !automaticLoadoutDataComplete()) return result;
    QMap<int, LoadoutArmorData> byId;
    for (const GameDataEntry &row : entries(QStringLiteral("armor_stats"))) {
        if (row.attributes.value(QStringLiteral("equipment_type")).toInt() != type) continue;
        LoadoutArmorData value;
        value.type = type;
        value.id = row.attributes.value(QStringLiteral("armor_id")).toInt();
        const GameDataEntry source = entry(equipmentTable(type), value.id);
        if (source.name.startsWith(QStringLiteral("未知"))) continue;
        value.name = source.name; value.slotCount = source.nativeSlots;
        value.gender = row.attributes.value(QStringLiteral("gender")).toInt();
        value.combat = row.attributes.value(QStringLiteral("combat")).toInt();
        value.baseDefense = row.attributes.value(QStringLiteral("base_defense")).toInt();
        value.maxDefense = row.attributes.value(QStringLiteral("max_defense")).toInt();
        value.fireRes = row.attributes.value(QStringLiteral("fire_res")).toInt();
        value.waterRes = row.attributes.value(QStringLiteral("water_res")).toInt();
        value.thunderRes = row.attributes.value(QStringLiteral("thunder_res")).toInt();
        value.iceRes = row.attributes.value(QStringLiteral("ice_res")).toInt();
        value.dragonRes = row.attributes.value(QStringLiteral("dragon_res")).toInt();
        byId.insert(value.id, value);
    }
    for (const GameDataEntry &row : entries(QStringLiteral("armor_skill_points"))) {
        if (row.attributes.value(QStringLiteral("equipment_type")).toInt() != type) continue;
        const int armorId = row.attributes.value(QStringLiteral("armor_id")).toInt();
        if (!byId.contains(armorId)) continue;
        byId[armorId].skills.insert(row.attributes.value(QStringLiteral("skill_id")).toInt(),
                                    row.attributes.value(QStringLiteral("points")).toInt());
    }
    result = QVector<LoadoutArmorData>::fromList(byId.values());
    return result;
}

QVector<LoadoutDecorationData> GameData::loadoutDecorations() const
{
    QVector<LoadoutDecorationData> result;
    if (!automaticLoadoutDataComplete()) return result;
    QMap<int, LoadoutDecorationData> byId;
    for (const GameDataEntry &source : entries(QStringLiteral("decorations"))) {
        bool found = false;
        const int slotCount = decorationSlotCost(source.id, &found);
        if (!source.id || !found || slotCount <= 0) continue;
        LoadoutDecorationData value; value.id = source.id; value.name = source.name; value.slotCount = slotCount;
        byId.insert(value.id, value);
    }
    for (const GameDataEntry &row : entries(QStringLiteral("decoration_skill_points"))) {
        const int decorationId = row.attributes.value(QStringLiteral("decoration_id")).toInt();
        if (!byId.contains(decorationId)) continue;
        byId[decorationId].skills.insert(row.attributes.value(QStringLiteral("skill_id")).toInt(),
            row.attributes.value(QStringLiteral("points")).toInt());
    }
    result = QVector<LoadoutDecorationData>::fromList(byId.values());
    return result;
}

QVector<ActiveSkillData> GameData::activeSkills() const
{
    QVector<ActiveSkillData> result;
    if (!automaticLoadoutDataComplete()) return result;
    for (const GameDataEntry &row : entries(QStringLiteral("active_skills"))) {
        ActiveSkillData value;
        value.id = row.id;
        value.skillId = row.attributes.value(QStringLiteral("skill_id")).toInt();
        value.points = row.attributes.value(QStringLiteral("points")).toInt();
        value.name = row.name;
        if (value.id > 0 && value.skillId > 0 && value.points > 0) result.push_back(value);
    }
    return result;
}
