#include "loadout_format.hpp"

#include "game_data.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <iterator>

namespace {
constexpr int EquipmentRecordSize = 36;

bool isLowerHex(const QString &value)
{
    if (value.size() != 72) return false;
    for (const QChar ch : value)
        if (!ch.isDigit() && (ch < QLatin1Char('a') || ch > QLatin1Char('f'))) return false;
    return true;
}

bool equipmentFromJson(const QJsonObject &object, MhguEquipment *out)
{
    if (!out || !object.contains(QStringLiteral("save_type")) ||
            !object.contains(QStringLiteral("save_id")) ||
            !object.contains(QStringLiteral("record_hex"))) return false;
    const QString hex = object.value(QStringLiteral("record_hex")).toString();
    if (!isLowerHex(hex)) return false;
    const QByteArray record = QByteArray::fromHex(hex.toLatin1());
    if (record.size() != EquipmentRecordSize) return false;
    const int explicitType = object.value(QStringLiteral("save_type")).toInt(-1);
    const int explicitId = object.value(QStringLiteral("save_id")).toInt(-1);
    MhguEquipment value;
    value.record = record;
    const quint16 packed = quint16(quint8(record[0])) | (quint16(quint8(record[1])) << 8);
    value.type = quint8(packed & 0x1f);
    value.level = quint8((packed >> 5) & 0x1f);
    value.id = quint16(quint8(record[2])) | (quint16(quint8(record[3])) << 8);
    if (value.type != explicitType || value.id != explicitId) return false;
    value.appearanceId = quint16(quint8(record[4])) | (quint16(quint8(record[5])) << 8);
    for (int i = 0; i < 3; ++i)
        value.decorations[size_t(i)] = quint16(quint8(record[6 + i * 2])) |
            (quint16(quint8(record[7 + i * 2])) << 8);
    value.skill1 = quint8(record[12]);
    value.skill2 = quint8(record[13]);
    value.skill1Points = qint8(record[14]);
    value.skill2Points = qint8(record[15]);
    value.talismanSlots = quint8(record[16]);
    *out = value;
    return true;
}
}

bool parseMhguLoadoutJson(const QByteArray &json, const QString &dataVersion,
                          const GameData &data, MhguLoadoutDocument *document,
                          QStringList *errors)
{
    QStringList issues;
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject())
        issues.push_back(QStringLiteral("配装 JSON 格式无效：%1").arg(parseError.errorString()));

    MhguLoadoutDocument candidate;
    if (issues.isEmpty()) {
        const QJsonObject root = parsed.object();
        if (root.value(QStringLiteral("schema")).toString() != QStringLiteral("MH_LOADOUT") ||
                root.value(QStringLiteral("schema_version")).toInt() != 1 ||
                root.value(QStringLiteral("game")).toString() != QStringLiteral("mhgu"))
            issues.push_back(QStringLiteral("只支持 MH_LOADOUT v1 / mhgu。"));
        if (root.value(QStringLiteral("data_version")).toString() != dataVersion)
            issues.push_back(QStringLiteral("配装数据版本与当前数据库不一致。"));
        candidate.name = root.value(QStringLiteral("name")).toString().trimmed();
        candidate.gender = root.value(QStringLiteral("gender")).toString();
        const QJsonObject armor = root.value(QStringLiteral("armor")).toObject();
        const QJsonValue values[] = {root.value(QStringLiteral("weapon")),
            armor.value(QStringLiteral("head")), armor.value(QStringLiteral("chest")),
            armor.value(QStringLiteral("arms")), armor.value(QStringLiteral("waist")),
            armor.value(QStringLiteral("legs")), root.value(QStringLiteral("charm"))};
        for (int i = 0; i < 7; ++i) {
            if (!values[i].isObject() ||
                    !equipmentFromJson(values[i].toObject(), &candidate.records[i]))
                issues.push_back(QStringLiteral("第 %1 条装备记录不是完整、规范的小写 36 字节 record_hex，或显式类型/ID 不一致。").arg(i + 1));
        }
        if (issues.isEmpty()) {
            QVector<MhguEquipment> records;
            for (const MhguEquipment &record : candidate.records) records.push_back(record);
            data.validateLoadout(records, &issues, candidate.gender);
        }
    }
    if (errors) *errors = issues;
    if (!issues.isEmpty() || !document) return false;
    *document = candidate;
    return true;
}

bool validateMhguLoadoutProvenance(const MhguLoadoutDocument &document,
                                   const QVector<MhguEquipment> &knownEquipment,
                                   QStringList *errors)
{
    QStringList issues;
    static const char *names[] = {"武器", "头", "胸", "腕", "腰", "腿", "护石"};
    for (int slot = 0; slot < 7; ++slot) {
        const MhguEquipment &record = document.records[slot];
        bool exactMatch = false;
        for (const MhguEquipment &known : knownEquipment) {
            if (known.record.size() == EquipmentRecordSize && known.record == record.record) {
                exactMatch = true;
                break;
            }
        }
        if (exactMatch) continue;
        if (slot >= 1 && slot <= 5 && record.level != 0) {
            issues.push_back(QStringLiteral("%1使用了尚未确认规则的非零防具强化等级，且完整记录不在当前装备箱中；已拒绝外部生成记录。")
                .arg(QString::fromUtf8(names[slot])));
            continue;
        }
        bool canonical = record.record.size() == EquipmentRecordSize;
        if (slot != 6) {
            if (slot == 0 && record.type == 20) canonical = false;
            for (int offset = 12; canonical && offset < record.record.size(); ++offset)
                canonical = quint8(record.record[offset]) == 0;
        } else {
            static const quint8 rarityMarkers[] = {0, 97, 97, 98, 98, 99, 99, 99, 100, 100, 100};
            bool generatedForm = canonical;
            for (int offset = 17; generatedForm && offset < record.record.size(); ++offset)
                generatedForm = quint8(record.record[offset]) == 0;
            bool persistedForm = canonical && quint8(record.record[17]) == 0 &&
                record.id < std::size(rarityMarkers) &&
                quint8(record.record[18]) == rarityMarkers[record.id] &&
                quint8(record.record[19]) == 1;
            for (int offset = 20; persistedForm && offset < record.record.size(); ++offset)
                persistedForm = quint8(record.record[offset]) == 0;
            canonical = generatedForm || persistedForm;
        }
        if (!canonical)
            issues.push_back(QStringLiteral("%1包含无法由严格规则重建的扩展字节，且完整记录不在当前装备箱中；已拒绝外部记录。")
                .arg(QString::fromUtf8(names[slot])));
    }
    if (errors) *errors = issues;
    return issues.isEmpty();
}
