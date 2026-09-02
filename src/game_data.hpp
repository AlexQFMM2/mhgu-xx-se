#pragma once

#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

struct MhguEquipment;
struct MhguPalicoEquipment;

struct GameDataEntry {
    int id = 0;
    QString name;
    QString english;
    QString source;
    int rarity = 0;
    int maxLevel = 0;
    int generationTier = 0;
    QString generationGroup;
    QString role;
    int teachable = -1;
    int memoryCost = -1;
    QString generationWeights;
    int slotCost = -1;
    int nativeSlots = -1;
    QMap<QString, QString> attributes;
};

struct PalicoPattern {
    QString scope;
    int id = 0;
    QString sequence;
    int validLength = 0;
    int weight = 0;
};

struct PalicoForteGrant {
    int forteId = 0;
    QString kind;
    QString role;
    int entryId = 0;
};

struct TalismanSkillLimit {
    int skill1Min = 0;
    int skill1Max = 0;
    int skill2Min = 0;
    int skill2Max = 0;
};

struct LoadoutArmorData {
    int type = 0;
    int id = 0;
    QString name;
    int gender = 2; // source convention: 0 male, 1 female, 2 both
    int combat = 2; // 0 blade, 1 gunner, 2 both
    int slotCount = 0;
    int baseDefense = 0;
    int maxDefense = 0;
    int fireRes = 0;
    int waterRes = 0;
    int thunderRes = 0;
    int iceRes = 0;
    int dragonRes = 0;
    QMap<int, int> skills;
};

struct LoadoutDecorationData {
    int id = 0;
    QString name;
    int slotCount = 0;
    QMap<int, int> skills;
};

struct ActiveSkillData {
    int id = 0;
    int skillId = 0;
    int points = 0;
    QString name;
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
    int weaponSlots(int type, int weaponId, int saveLevel, bool *found = nullptr) const;
    int armorSlots(int type, int armorId, bool *found = nullptr) const;
    int decorationSlotCost(int itemId, bool *found = nullptr) const;
    bool talismanSkillRange(int talismanId, int skillId, int position,
                            int *minimum, int *maximum) const;
    int talismanMaxSlots(int talismanId, bool *found = nullptr) const;
    bool talismanSlotsLegal(int talismanId, int skill1Id, int skill1Points,
                            int skill2Id, int skill2Points, int slotCount) const;
    QVector<PalicoPattern> patterns(const QString &scope) const;
    PalicoPattern pattern(const QString &scope, int id) const;
    QVector<PalicoForteGrant> forteGrants(int forteId, const QString &kind,
                                          const QString &role = {}) const;
    bool validateHunterEquipment(const MhguEquipment &entry, QString *error = nullptr) const;
    // Validate a complete seven-piece set without mutating any caller-owned
    // state.  This is the common gate used by the loadout editor, JSON
    // import and future search-result回填 paths.
    bool validateLoadout(const QVector<MhguEquipment> &entries,
                         QStringList *errors = nullptr,
                         const QString &gender = {}) const;
    bool validatePalicoEquipment(const MhguPalicoEquipment &entry, QString *error = nullptr) const;
    QString dataVersion() const;
    bool automaticLoadoutDataComplete(QStringList *missing = nullptr) const;
    QVector<LoadoutArmorData> loadoutArmor(int type) const;
    QVector<LoadoutDecorationData> loadoutDecorations() const;
    QVector<ActiveSkillData> activeSkills() const;

private:
    static QStringList parseCsvLine(const QString &line);
    bool loadTable(const QString &path, const QString &table);
    QString findDataRoot() const;

    QMap<QString, QMap<int, GameDataEntry>> m_tables;
    QMap<QString, QSet<QString>> m_tableColumns;
    QMap<QString, int> m_tableRowCounts;
    QMap<quint32, int> m_weaponLevelSlots;
    QMap<quint32, TalismanSkillLimit> m_talismanSkillLimits;
    QVector<PalicoPattern> m_patterns;
    QVector<PalicoForteGrant> m_forteGrants;
    QString m_rootPath;
    QString m_language;
    QString m_error;
};
