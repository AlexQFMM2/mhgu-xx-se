#include "mhgu_save.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cstring>

namespace {
constexpr quint32 NameOffset = 0x23B7D;
constexpr quint32 ItemBoxOffset = 0x0278;
constexpr int ItemBoxSize = 5463;
constexpr quint32 EquipmentOffset = 0x62EE;
constexpr int EquipmentSize = 36;
constexpr quint32 EquippedOffset = 0x110;
constexpr int EquippedSize = 44;
constexpr quint32 PalicoEquipmentOffset = 0x17C2E;
constexpr quint32 PalicoOffset = 0x23BB6;
constexpr int PalicoSize = 324;

template <size_t N>
int nonZeroCount(const std::array<quint8, N> &values)
{
    return int(std::count_if(values.begin(), values.end(), [](quint8 value) { return value != 0; }));
}

template <size_t N>
bool compact(const std::array<quint8, N> &values)
{
    bool seenZero = false;
    for (quint8 value : values) {
        if (value == 0) seenZero = true;
        else if (seenZero) return false;
    }
    return true;
}

template <size_t A, size_t B>
bool subset(const std::array<quint8, A> &selected, const std::array<quint8, B> &learned)
{
    QSet<quint8> pool;
    for (quint8 id : learned) if (id != 0) pool.insert(id);
    for (quint8 id : selected) if (id != 0 && !pool.contains(id)) return false;
    return true;
}
}

bool MhguSave::open(const QString &path)
{
    const QFileInfo input(path);
    if (input.fileName().compare(QStringLiteral("system_backup"), Qt::CaseInsensitive) == 0) {
        m_error = QStringLiteral("为避免破坏游戏备份，不能直接编辑 system_backup；请选择 system。");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("无法读取文件：%1").arg(file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    QByteArray header;
    QByteArray raw;
    if (bytes.size() == FileSize) {
        raw = bytes;
    } else if (bytes.size() == HeaderedFileSize) {
        header = bytes.left(int(HeaderSize));
        raw = bytes.mid(int(HeaderSize));
    } else {
        m_error = QStringLiteral("文件大小不正确：应为 %1 字节（无头）或 %2 字节（带 36 字节头），实际为 %3 字节。")
                      .arg(FileSize).arg(HeaderedFileSize).arg(bytes.size());
        return false;
    }

    const QByteArray previousRaw = m_raw;
    const QByteArray previousHeader = m_header;
    const QString previousPath = m_path;
    const int previousSlot = m_selectedSlot;
    const bool previousDirty = m_dirty;
    m_raw = raw;
    m_header = header;
    m_path = QFileInfo(path).absoluteFilePath();
    m_selectedSlot = -1;
    m_dirty = false;
    QString validationError;
    if (!validate(&validationError)) {
        m_raw = previousRaw;
        m_header = previousHeader;
        m_path = previousPath;
        m_selectedSlot = previousSlot;
        m_dirty = previousDirty;
        m_error = validationError;
        return false;
    }
    m_error.clear();
    return true;
}

bool MhguSave::save()
{
    if (!isOpen()) {
        m_error = QStringLiteral("尚未打开 system。");
        return false;
    }
    QString validationError;
    if (!validate(&validationError)) {
        m_error = validationError;
        return false;
    }
    const QFileInfo target(m_path);
    const QByteArray output = m_header + m_raw;
    if (!target.exists() || target.size() != output.size()) {
        m_error = QStringLiteral("磁盘上的 system 已被删除或尺寸发生变化；为避免覆盖错误文件，已停止保存。");
        return false;
    }
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = QStringLiteral("无法写入 system：%1").arg(file.errorString());
        return false;
    }
    if (file.write(output) != output.size()) {
        m_error = QStringLiteral("system 没有完整写入：%1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        m_error = QStringLiteral("无法替换原 system：%1").arg(file.errorString());
        return false;
    }
    m_dirty = false;
    m_error.clear();
    return true;
}

void MhguSave::close()
{
    m_raw.clear();
    m_header.clear();
    m_path.clear();
    m_selectedSlot = -1;
    m_dirty = false;
}

QVector<MhguSlotInfo> MhguSave::slotInfos() const
{
    QVector<MhguSlotInfo> result;
    for (int index = 0; index < 3; ++index) {
        MhguSlotInfo info;
        info.index = index;
        info.used = isOpen() && quint8(m_raw[0x04 + index]) != 0;
        info.base = isOpen() ? read32(0x10 + index * 4) : 0;
        if (info.used && rangeOk(quint64(info.base) + NameOffset, 32)) {
            info.name = readUtf8(quint64(info.base) + NameOffset, 32);
            info.hunterRank = read16(quint64(info.base) + 0x28);
            info.playTime = read32(quint64(info.base) + 0x20);
        }
        result.push_back(info);
    }
    return result;
}

bool MhguSave::selectSlot(int index)
{
    const QVector<MhguSlotInfo> all = slotInfos();
    if (index < 0 || index >= all.size() || !all[index].used) {
        m_error = QStringLiteral("所选存档槽未使用。");
        return false;
    }
    m_selectedSlot = index;
    m_error.clear();
    return true;
}

MhguCharacter MhguSave::character() const
{
    MhguCharacter out;
    const quint32 base = selectedBase();
    if (!base) return out;
    out.name = readUtf8(quint64(base) + NameOffset, 32);
    out.playTime = read32(quint64(base) + 0x20);
    out.money = read32(quint64(base) + 0x24);
    out.hunterRank = read16(quint64(base) + 0x28);
    out.hunterRankPoints = read32(quint64(base) + 0x280B);
    out.academyPoints = read32(quint64(base) + 0x2817);
    out.bhernaPoints = read32(quint64(base) + 0x281B);
    out.kokotoPoints = read32(quint64(base) + 0x281F);
    out.pokkePoints = read32(quint64(base) + 0x2823);
    out.yukumoPoints = read32(quint64(base) + 0x2827);
    return out;
}

bool MhguSave::setCharacter(const MhguCharacter &value)
{
    const quint32 base = selectedBase();
    if (!base) return false;
    writeUtf8(quint64(base) + NameOffset, 32, value.name);
    write32(quint64(base) + 0x20, value.playTime);
    write32(quint64(base) + 0x24, value.money);
    write16(quint64(base) + 0x28, value.hunterRank);
    write32(quint64(base) + 0x280B, value.hunterRankPoints);
    write32(quint64(base) + 0x2817, value.academyPoints);
    write32(quint64(base) + 0x281B, value.bhernaPoints);
    write32(quint64(base) + 0x281F, value.kokotoPoints);
    write32(quint64(base) + 0x2823, value.pokkePoints);
    write32(quint64(base) + 0x2827, value.yukumoPoints);
    return true;
}

QVector<MhguItem> MhguSave::decodeItems() const
{
    QVector<MhguItem> result(ItemCount);
    const quint32 base = selectedBase();
    if (!base || !rangeOk(quint64(base) + ItemBoxOffset, ItemBoxSize)) return result;
    QByteArray bytes = m_raw.mid(int(base + ItemBoxOffset), ItemBoxSize);
    std::reverse(bytes.begin(), bytes.end());
    auto bit = [&bytes](int position) -> int {
        return (quint8(bytes[position / 8]) >> (7 - position % 8)) & 1;
    };
    int position = 4;
    for (int slot = ItemCount - 1; slot >= 0; --slot) {
        quint16 count = 0;
        quint16 id = 0;
        for (int i = 0; i < 7; ++i) count = quint16((count << 1) | bit(position++));
        for (int i = 0; i < 12; ++i) id = quint16((id << 1) | bit(position++));
        result[slot] = MhguItem{id, quint8(count)};
    }
    return result;
}

QVector<MhguItem> MhguSave::items() const { return decodeItems(); }

void MhguSave::encodeItems(const QVector<MhguItem> &items)
{
    QByteArray bytes(ItemBoxSize, char(0));
    int position = 4;
    auto put = [&bytes, &position](quint16 value, int width) {
        for (int i = width - 1; i >= 0; --i, ++position) {
            if ((value >> i) & 1) bytes[position / 8] = char(quint8(bytes[position / 8]) | (1u << (7 - position % 8)));
        }
    };
    for (int slot = ItemCount - 1; slot >= 0; --slot) {
        put(items[slot].count & 0x7F, 7);
        put(items[slot].id & 0x0FFF, 12);
    }
    std::reverse(bytes.begin(), bytes.end());
    markIfChanged(quint64(selectedBase()) + ItemBoxOffset, bytes);
}

bool MhguSave::setItem(int index, const MhguItem &value)
{
    if (index < 0 || index >= ItemCount || value.id > 0x0FFF || value.count > 0x7F || !selectedBase()) return false;
    QVector<MhguItem> all = decodeItems();
    all[index] = value.id == 0 ? MhguItem{} : value;
    encodeItems(all);
    return true;
}

bool MhguSave::setItems(const QVector<MhguItem> &values)
{
    if (!selectedBase() || values.size() != ItemCount) return false;
    for (const MhguItem &value : values)
        if (value.id > 0x0FFF || value.count > 0x7F) return false;
    QVector<MhguItem> normalized = values;
    for (MhguItem &value : normalized)
        if (value.id == 0) value = MhguItem{};
    encodeItems(normalized);
    return true;
}

MhguEquipment MhguSave::equipment(int index) const
{
    MhguEquipment out;
    const quint64 offset = quint64(selectedBase()) + EquipmentOffset + quint64(index) * EquipmentSize;
    if (index < 0 || index >= EquipmentCount || !rangeOk(offset, EquipmentSize)) return out;
    const quint16 packed = read16(offset);
    out.type = packed & 0x1F;
    out.level = (packed >> 5) & 0x1F;
    out.id = read16(offset + 2);
    out.appearanceId = read16(offset + 4);
    for (int i = 0; i < 3; ++i) out.decorations[i] = read16(offset + 6 + i * 2);
    out.skill1 = quint8(m_raw[int(offset + 12)]);
    out.skill2 = quint8(m_raw[int(offset + 13)]);
    out.skill1Points = qint8(m_raw[int(offset + 14)]);
    out.skill2Points = qint8(m_raw[int(offset + 15)]);
    out.talismanSlots = quint8(m_raw[int(offset + 16)]);
    return out;
}

bool MhguSave::setEquipment(int index, const MhguEquipment &value, QString *warning)
{
    const quint32 base = selectedBase();
    const quint64 offset = quint64(base) + EquipmentOffset + quint64(index) * EquipmentSize;
    if (!base || index < 0 || index >= EquipmentCount || value.type > 20 || value.level > 31 || !rangeOk(offset, EquipmentSize)) return false;

    const QByteArray oldKey = m_raw.mid(int(offset), 12);
    QVector<quint64> matchingLoadouts;
    for (int i = 0; i < 7; ++i) {
        const quint64 loadout = quint64(base) + EquippedOffset + quint64(i) * EquippedSize;
        if (m_raw.mid(int(loadout), 12) == oldKey) matchingLoadouts.push_back(loadout);
    }
    int identicalBoxEntries = 0;
    for (int i = 0; i < EquipmentCount; ++i) {
        const quint64 candidate = quint64(base) + EquipmentOffset + quint64(i) * EquipmentSize;
        if (m_raw.mid(int(candidate), 12) == oldKey) ++identicalBoxEntries;
    }

    auto writeEntry = [this, &value](quint64 target) {
        quint16 packed = read16(target);
        packed = quint16((packed & 0xFC00) | ((value.level & 0x1F) << 5) | (value.type & 0x1F));
        write16(target, packed);
        write16(target + 2, value.id);
        write16(target + 4, value.appearanceId);
        for (int i = 0; i < 3; ++i) write16(target + 6 + i * 2, value.decorations[i]);
        if (value.type == 6) {
            static const quint8 rarityMarkers[] = {0, 97, 97, 98, 98, 99, 99, 99, 100, 100, 100};
            QByteArray talisman(24, char(0));
            talisman[0] = char(value.skill1);
            talisman[1] = char(value.skill2);
            talisman[2] = char(value.skill1Points);
            talisman[3] = char(value.skill2Points);
            talisman[4] = char(value.talismanSlots);
            talisman[5] = 0;
            talisman[6] = char(value.id < std::size(rarityMarkers) ? rarityMarkers[value.id] : 0);
            talisman[7] = 1;
            markIfChanged(target + 12, talisman);
        }
    };
    writeEntry(offset);
    if (!matchingLoadouts.isEmpty()) {
        if (identicalBoxEntries == 1 && matchingLoadouts.size() == 1) writeEntry(matchingLoadouts.first());
        else if (warning) *warning = QStringLiteral("装备箱中存在多个相同记录，无法确认当前穿戴来源；已修改装备箱，但没有同步穿戴缓存。");
    }
    return true;
}

MhguPalicoEquipment MhguSave::palicoEquipment(int index) const
{
    MhguPalicoEquipment out;
    const quint64 offset = quint64(selectedBase()) + PalicoEquipmentOffset + quint64(index) * EquipmentSize;
    if (index < 0 || index >= PalicoEquipmentCount || !rangeOk(offset, EquipmentSize)) return out;
    out.rawType = quint8(m_raw[int(offset)]);
    out.id = read16(offset + 2);
    out.appearanceId = read16(offset + 4);
    return out;
}

bool MhguSave::setPalicoEquipment(int index, const MhguPalicoEquipment &value)
{
    const quint64 offset = quint64(selectedBase()) + PalicoEquipmentOffset + quint64(index) * EquipmentSize;
    if (index < 0 || index >= PalicoEquipmentCount || !rangeOk(offset, EquipmentSize)) return false;
    if (value.rawType != 0 && value.rawType != 22 && value.rawType != 23 && value.rawType != 24) return false;
    QByteArray lead = m_raw.mid(int(offset), 6);
    lead[0] = char(value.rawType);
    if (value.rawType == 0) {
        lead[1] = lead[2] = lead[3] = lead[4] = lead[5] = 0;
    } else {
        lead[2] = char(value.id & 0xFF);
        lead[3] = char(value.id >> 8);
        lead[4] = char(value.appearanceId & 0xFF);
        lead[5] = char(value.appearanceId >> 8);
    }
    markIfChanged(offset, lead);
    return true;
}

MhguPalico MhguSave::palico(int index) const
{
    MhguPalico out;
    const quint64 p = quint64(selectedBase()) + PalicoOffset + quint64(index) * PalicoSize;
    if (index < 0 || index >= PalicoCount || !rangeOk(p, PalicoSize)) return out;
    out.name = readUtf8(p, 32);
    out.experience = read32(p + 0x20);
    out.level = quint8(m_raw[int(p + 0x24)]) + 1;
    out.forte = quint8(m_raw[int(p + 0x25)]);
    out.enthusiasm = quint8(m_raw[int(p + 0x26)]);
    out.target = quint8(m_raw[int(p + 0x27)]);
    for (int i = 0; i < 8; ++i) out.equippedActions[i] = quint8(m_raw[int(p + 0x28 + i)]);
    for (int i = 0; i < 8; ++i) out.equippedSkills[i] = quint8(m_raw[int(p + 0x30 + i)]);
    for (int i = 0; i < 16; ++i) out.learnedActions[i] = quint8(m_raw[int(p + 0x38 + i)]);
    for (int i = 0; i < 12; ++i) out.learnedSkills[i] = quint8(m_raw[int(p + 0x48 + i)]);
    out.actionPattern = quint8(m_raw[int(p + 0x54)]);
    out.actionSeed = quint8(m_raw[int(p + 0x55)]);
    out.skillPattern = quint8(m_raw[int(p + 0x56)]);
    out.skillSeed = quint8(m_raw[int(p + 0x57)]);
    out.received = false;
    for (int i = 0; i < 7; ++i) out.received |= quint8(m_raw[int(p + 0x58 + i)]) != 0;
    out.greeting = readUtf8(p + 0x60, 60);
    out.nameGiver = readUtf8(p + 0x9C, 32);
    out.previousOwner = readUtf8(p + 0xBC, 32);
    out.status = quint8(m_raw[int(p + 0xE0)]);
    out.trainingState = quint8(m_raw[int(p + 0xE1)]);
    out.assignment = quint8(m_raw[int(p + 0xE2)]);
    out.prowlerSelected = quint8(m_raw[int(p + 0xE3)]);
    out.assignmentReferences = m_raw.mid(int(p + 0x100), 6);
    out.voice = quint8(m_raw[int(p + 0x10F)]);
    out.eyes = quint8(m_raw[int(p + 0x110)]);
    out.clothing = quint8(m_raw[int(p + 0x111)]);
    out.coat = quint8(m_raw[int(p + 0x114)]);
    out.ears = quint8(m_raw[int(p + 0x115)]);
    out.tail = quint8(m_raw[int(p + 0x116)]);
    out.coatColor = m_raw.mid(int(p + 0x11A), 4);
    out.rightEyeColor = m_raw.mid(int(p + 0x11E), 4);
    out.leftEyeColor = m_raw.mid(int(p + 0x122), 4);
    out.vestColor = m_raw.mid(int(p + 0x126), 4);
    return out;
}

bool MhguSave::setPalico(int index, const MhguPalico &value, QString *validationError)
{
    const quint64 p = quint64(selectedBase()) + PalicoOffset + quint64(index) * PalicoSize;
    if (index < 0 || index >= PalicoCount || !rangeOk(p, PalicoSize)) return false;
    // Legality is advisory. GU may reset unusual combinations, but this editor
    // deliberately lets experienced users test them after the UI warning.
    validatePalico(value, validationError);
    const quint8 oldActionSeed = quint8(m_raw[int(p + 0x55)]);
    const quint8 oldSkillSeed = quint8(m_raw[int(p + 0x57)]);
    writeUtf8(p, 32, value.name);
    const quint8 previousLevel = quint8(m_raw[int(p + 0x24)]) + 1;
    write32(p + 0x20, previousLevel == value.level ? value.experience : experienceForLevel(value.level));
    QByteArray basic(4, char(0));
    basic[0] = char(std::clamp<int>(value.level, 1, 60) - 1);
    basic[1] = char(value.forte);
    basic[2] = char(value.enthusiasm);
    basic[3] = char(value.target);
    markIfChanged(p + 0x24, basic);
    QByteArray actions(8, char(0)), skills(8, char(0)), learnedActions(16, char(0)), learnedSkills(12, char(0));
    for (int i = 0; i < 8; ++i) actions[i] = char(value.equippedActions[i]);
    for (int i = 0; i < 8; ++i) skills[i] = char(value.equippedSkills[i]);
    for (int i = 0; i < 16; ++i) learnedActions[i] = char(value.learnedActions[i]);
    for (int i = 0; i < 12; ++i) learnedSkills[i] = char(value.learnedSkills[i]);
    markIfChanged(p + 0x28, actions);
    markIfChanged(p + 0x30, skills);
    markIfChanged(p + 0x38, learnedActions);
    markIfChanged(p + 0x48, learnedSkills);
    QByteArray patterns(4, char(0));
    patterns[0] = char(value.actionPattern);
    patterns[1] = char(oldActionSeed);
    patterns[2] = char(value.skillPattern);
    patterns[3] = char(oldSkillSeed);
    markIfChanged(p + 0x54, patterns);
    writeUtf8(p + 0x60, 60, value.greeting);
    writeUtf8(p + 0x9C, 32, value.nameGiver);
    writeUtf8(p + 0xBC, 32, value.previousOwner);
    markIfChanged(p + 0x10F, QByteArray(1, char(value.voice)));
    markIfChanged(p + 0x110, QByteArray(1, char(value.eyes)));
    markIfChanged(p + 0x111, QByteArray(1, char(value.clothing)));
    markIfChanged(p + 0x114, QByteArray(1, char(value.coat)));
    markIfChanged(p + 0x115, QByteArray(1, char(value.ears)));
    markIfChanged(p + 0x116, QByteArray(1, char(value.tail)));
    auto color = [this](quint64 offset, QByteArray value) {
        value = value.left(4);
        value.append(QByteArray(4 - value.size(), char(0)));
        markIfChanged(offset, value);
    };
    color(p + 0x11A, value.coatColor);
    color(p + 0x11E, value.rightEyeColor);
    color(p + 0x122, value.leftEyeColor);
    color(p + 0x126, value.vestColor);
    return true;
}

quint32 MhguSave::experienceForLevel(int displayedLevel)
{
    struct Anchor { int level; quint32 exp; };
    const Anchor anchors[] = {{1, 0}, {20, 18645}, {21, 19805}, {31, 56065}, {35, 77367}, {58, 270415}, {60, 285415}};
    const int level = std::clamp(displayedLevel, 1, 60);
    for (size_t i = 1; i < std::size(anchors); ++i) {
        if (level <= anchors[i].level) {
            const Anchor a = anchors[i - 1], b = anchors[i];
            return a.exp + quint32((quint64(b.exp - a.exp) * quint64(level - a.level)) / quint64(b.level - a.level));
        }
    }
    return anchors[std::size(anchors) - 1].exp;
}

bool MhguSave::validatePalico(const MhguPalico &value, QString *error)
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (value.level < 1 || value.level > 60) return fail(QStringLiteral("猫猫等级必须在 1 到 60 之间。"));
    if (value.forte > 7 || value.target > 5) return fail(QStringLiteral("猫猫倾向或目标值无效。"));
    if (!compact(value.equippedActions) || !compact(value.equippedSkills) ||
        !compact(value.learnedActions) || !compact(value.learnedSkills))
        return fail(QStringLiteral("行动和技能必须从第一个槽开始连续排列，空位应放在末尾。"));
    const int actionLimit = std::min(6, 1 + int(value.level) / 10);
    if (nonZeroCount(value.equippedActions) > actionLimit)
        return fail(QStringLiteral("当前等级最多装备 %1 个支援行动。").arg(actionLimit));
    if (nonZeroCount(value.equippedSkills) > 4) return fail(QStringLiteral("最多装备 4 个被动技能。"));
    if (nonZeroCount(value.learnedActions) > 10) return fail(QStringLiteral("最多保留 10 个已学支援行动。"));
    if (nonZeroCount(value.learnedSkills) > 8) return fail(QStringLiteral("最多保留 8 个已学被动技能。"));
    if (!subset(value.equippedActions, value.learnedActions)) return fail(QStringLiteral("装备的支援行动必须存在于已学行动中。"));
    if (!subset(value.equippedSkills, value.learnedSkills)) return fail(QStringLiteral("装备的被动技能必须存在于已学技能中。"));
    if (error) error->clear();
    return true;
}

bool MhguSave::validate(QString *error) const
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (m_raw.size() != FileSize) return fail(QStringLiteral("system 缓冲区尺寸不正确。"));
    for (const MhguSlotInfo &slot : slotInfos()) {
        if (!slot.used) continue;
        if (slot.base == 0) return fail(QStringLiteral("存档槽 %1 的角色基址为 0。").arg(slot.index + 1));
        const quint64 knownEnd = quint64(slot.base) + PalicoOffset + quint64(PalicoCount) * PalicoSize;
        if (knownEnd > quint64(m_raw.size())) return fail(QStringLiteral("存档槽 %1 的角色数据越界。").arg(slot.index + 1));
    }
    if (m_selectedSlot >= 0) {
        const QVector<MhguSlotInfo> all = slotInfos();
        if (m_selectedSlot >= all.size() || !all[m_selectedSlot].used) return fail(QStringLiteral("当前存档槽已无效。"));
    }
    if (error) error->clear();
    return true;
}

quint32 MhguSave::selectedBase() const
{
    if (m_selectedSlot < 0 || m_selectedSlot > 2 || !isOpen()) return 0;
    return read32(0x10 + m_selectedSlot * 4);
}

bool MhguSave::rangeOk(quint64 offset, quint64 length) const
{
    return offset <= quint64(m_raw.size()) && length <= quint64(m_raw.size()) - offset;
}

quint16 MhguSave::read16(quint64 offset) const
{
    if (!rangeOk(offset, 2)) return 0;
    return quint16(quint8(m_raw[int(offset)])) | (quint16(quint8(m_raw[int(offset + 1)])) << 8);
}

quint32 MhguSave::read32(quint64 offset) const
{
    if (!rangeOk(offset, 4)) return 0;
    return quint32(quint8(m_raw[int(offset)])) |
           (quint32(quint8(m_raw[int(offset + 1)])) << 8) |
           (quint32(quint8(m_raw[int(offset + 2)])) << 16) |
           (quint32(quint8(m_raw[int(offset + 3)])) << 24);
}

void MhguSave::write16(quint64 offset, quint16 value)
{
    QByteArray bytes(2, char(0));
    bytes[0] = char(value & 0xFF);
    bytes[1] = char(value >> 8);
    markIfChanged(offset, bytes);
}

void MhguSave::write32(quint64 offset, quint32 value)
{
    QByteArray bytes(4, char(0));
    for (int i = 0; i < 4; ++i) bytes[i] = char((value >> (i * 8)) & 0xFF);
    markIfChanged(offset, bytes);
}

QString MhguSave::readUtf8(quint64 offset, int length) const
{
    if (!rangeOk(offset, length)) return {};
    QByteArray bytes = m_raw.mid(int(offset), length);
    const int end = bytes.indexOf('\0');
    if (end >= 0) bytes.truncate(end);
    return QString::fromUtf8(bytes);
}

void MhguSave::writeUtf8(quint64 offset, int length, const QString &value)
{
    QString safe = value;
    QByteArray bytes = safe.toUtf8();
    while (bytes.size() >= length && !safe.isEmpty()) {
        safe.chop(1);
        bytes = safe.toUtf8();
    }
    bytes.append(QByteArray(length - bytes.size(), char(0)));
    markIfChanged(offset, bytes);
}

void MhguSave::markIfChanged(quint64 offset, const QByteArray &value)
{
    if (!rangeOk(offset, value.size())) return;
    if (m_raw.mid(int(offset), value.size()) == value) return;
    std::memcpy(m_raw.data() + int(offset), value.constData(), size_t(value.size()));
    m_dirty = true;
}
