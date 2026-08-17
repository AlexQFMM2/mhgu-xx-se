#include "transfer_forms.hpp"

#include <QSet>

namespace {
const QByteArray Magic("MHXX_GU_TRANSFER");
const QByteArray Version("1");
const QByteArray ItemKind("ITEM_BOX");
const QByteArray EquipmentKind("EQUIPMENT_BOX");
const QByteArray ItemHeader("slot,id,count");
const QByteArray EquipmentHeader("domain,slot,type,id,appearance_id,level,decoration_1,decoration_2,decoration_3,skill_1,skill_1_points,skill_2,skill_2_points,talisman_slots");

QByteArray cleanLine(QByteArray line)
{
    line = line.trimmed();
    if (line.startsWith("\xEF\xBB\xBF")) line.remove(0, 3);
    return line;
}

bool nextLine(const QList<QByteArray> &lines, int *position, QByteArray *line, int *lineNumber)
{
    while (*position < lines.size()) {
        const int current = (*position)++;
        const QByteArray candidate = cleanLine(lines[current]);
        if (candidate.isEmpty()) continue;
        *line = candidate;
        if (lineNumber) *lineNumber = current + 1;
        return true;
    }
    return false;
}

bool validatePreamble(const QList<QByteArray> &lines, const QByteArray &kind, const QByteArray &header,
                      int *position, QString *error)
{
    QByteArray line;
    int lineNumber = 0;
    if (!nextLine(lines, position, &line, &lineNumber)) {
        if (error) *error = QStringLiteral("表单为空。");
        return false;
    }
    if (line.startsWith(Magic + ',')) {
        if (line != Magic + ',' + Version + ',' + kind) {
            if (error) *error = QStringLiteral("第 %1 行：表单类型或版本不受支持。").arg(lineNumber);
            return false;
        }
        if (!nextLine(lines, position, &line, &lineNumber)) {
            if (error) *error = QStringLiteral("表单缺少字段标题。");
            return false;
        }
    }
    if (line != header) {
        if (error) *error = QStringLiteral("第 %1 行：字段标题不正确。").arg(lineNumber);
        return false;
    }
    return true;
}

bool integer(const QByteArray &field, qint64 minimum, qint64 maximum, qint64 *value)
{
    const QByteArray text = field.trimmed();
    if (text.isEmpty()) return false;
    int start = 0;
    if (text[0] == '-') {
        if (minimum >= 0 || text.size() == 1) return false;
        start = 1;
    }
    for (int i = start; i < text.size(); ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    bool ok = false;
    const qint64 parsed = text.toLongLong(&ok, 10);
    if (!ok || parsed < minimum || parsed > maximum) return false;
    *value = parsed;
    return true;
}

QString rowError(int lineNumber)
{
    return QStringLiteral("第 %1 行格式、数值或格号无效，未修改存档。").arg(lineNumber);
}
}

namespace MhxxGuTransfer {

QByteArray exportItems(const QVector<MhguItem> &items)
{
    QByteArray csv = Magic + ',' + Version + ',' + ItemKind + "\r\n" + ItemHeader + "\r\n";
    for (int i = 0; i < items.size(); ++i)
        csv += QByteArray::number(i + 1) + ',' + QByteArray::number(items[i].id) + ','
            + QByteArray::number(items[i].count) + "\r\n";
    return csv;
}

bool parseItems(const QByteArray &form, QVector<ItemUpdate> *updates, QString *error)
{
    if (!updates) return false;
    updates->clear();
    if (error) error->clear();
    const QList<QByteArray> lines = form.split('\n');
    int position = 0;
    if (!validatePreamble(lines, ItemKind, ItemHeader, &position, error)) return false;
    QSet<int> seen;
    for (; position < lines.size(); ++position) {
        const QByteArray line = cleanLine(lines[position]);
        if (line.isEmpty()) continue;
        const QList<QByteArray> fields = line.split(',');
        qint64 slot = 0, id = 0, count = 0;
        if (fields.size() != 3 || !integer(fields[0], 1, MhguSave::ItemCount, &slot)
            || !integer(fields[1], 0, 0x0FFF, &id) || !integer(fields[2], 0, 0x7F, &count)
            || seen.contains(int(slot))) {
            if (error) *error = rowError(position + 1);
            updates->clear();
            return false;
        }
        seen.insert(int(slot));
        ItemUpdate update;
        update.index = int(slot) - 1;
        update.value = id == 0 ? MhguItem{} : MhguItem{quint16(id), quint8(count)};
        updates->push_back(update);
    }
    if (updates->isEmpty()) {
        if (error) *error = QStringLiteral("表单中没有可导入的道具格。");
        return false;
    }
    return true;
}

QByteArray exportEquipment(const QVector<MhguEquipmentUpdate> &hunter,
                           const QVector<MhguPalicoEquipmentUpdate> &palico)
{
    QByteArray csv = Magic + ',' + Version + ',' + EquipmentKind + "\r\n" + EquipmentHeader + "\r\n";
    auto append = [&csv](const QByteArray &domain, const QList<qint64> &values) {
        csv += domain;
        for (qint64 value : values) csv += ',' + QByteArray::number(value);
        csv += "\r\n";
    };
    for (const MhguEquipmentUpdate &row : hunter) {
        const MhguEquipment &e = row.value;
        append("hunter", {row.index + 1, e.type, e.id, e.appearanceId, e.level,
            e.decorations[0], e.decorations[1], e.decorations[2], e.skill1, e.skill1Points,
            e.skill2, e.skill2Points, e.talismanSlots});
    }
    for (const MhguPalicoEquipmentUpdate &row : palico) {
        const MhguPalicoEquipment &e = row.value;
        append("palico", {row.index + 1, e.rawType, e.id, e.appearanceId, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    }
    return csv;
}

bool parseEquipment(const QByteArray &form, QVector<MhguEquipmentUpdate> *hunter,
                    QVector<MhguPalicoEquipmentUpdate> *palico, QString *error)
{
    if (!hunter || !palico) return false;
    hunter->clear();
    palico->clear();
    if (error) error->clear();
    const QList<QByteArray> lines = form.split('\n');
    int position = 0;
    if (!validatePreamble(lines, EquipmentKind, EquipmentHeader, &position, error)) return false;
    QSet<QString> seen;
    for (; position < lines.size(); ++position) {
        const QByteArray line = cleanLine(lines[position]);
        if (line.isEmpty()) continue;
        const QList<QByteArray> fields = line.split(',');
        if (fields.size() != 14 || (fields[0].trimmed() != "hunter" && fields[0].trimmed() != "palico")) {
            if (error) *error = rowError(position + 1);
            hunter->clear(); palico->clear();
            return false;
        }
        const bool isPalico = fields[0].trimmed() == "palico";
        qint64 value[13]{};
        const qint64 minimum[13] = {1, 0, 0, 0, 0, 0, 0, 0, 0, -128, 0, -128, 0};
        const qint64 maximum[13] = {isPalico ? MhguSave::PalicoEquipmentCount : MhguSave::EquipmentCount,
            255, 0xFFFF, 0xFFFF, 31, 0xFFFF, 0xFFFF, 0xFFFF, 0xFF, 127, 0xFF, 127, 0xFF};
        bool valid = true;
        for (int i = 0; i < 13; ++i) valid &= integer(fields[i + 1], minimum[i], maximum[i], &value[i]);
        const QString key = QString::fromLatin1(fields[0].trimmed()) + QLatin1Char(':') + QString::number(value[0]);
        const bool typeValid = isPalico ? (value[1] == 0 || value[1] == 22 || value[1] == 23 || value[1] == 24)
                                         : (value[1] <= 11 || (value[1] >= 13 && value[1] <= 21));
        if (!valid || !typeValid || seen.contains(key)) {
            if (error) *error = rowError(position + 1);
            hunter->clear(); palico->clear();
            return false;
        }
        seen.insert(key);
        if (isPalico) {
            MhguPalicoEquipmentUpdate update;
            update.index = int(value[0]) - 1;
            update.value.rawType = quint8(value[1]);
            update.value.id = quint16(value[2]);
            update.value.appearanceId = quint16(value[3]);
            palico->push_back(update);
        } else {
            MhguEquipmentUpdate update;
            update.index = int(value[0]) - 1;
            MhguEquipment &e = update.value;
            e.type = quint8(value[1]); e.id = quint16(value[2]); e.appearanceId = quint16(value[3]);
            e.level = quint8(value[4]); e.decorations = {{quint16(value[5]), quint16(value[6]), quint16(value[7])}};
            e.skill1 = quint8(value[8]); e.skill1Points = qint8(value[9]);
            e.skill2 = quint8(value[10]); e.skill2Points = qint8(value[11]); e.talismanSlots = quint8(value[12]);
            hunter->push_back(update);
        }
    }
    if (hunter->isEmpty() && palico->isEmpty()) {
        if (error) *error = QStringLiteral("表单中没有可导入的装备格。");
        return false;
    }
    return true;
}

}
