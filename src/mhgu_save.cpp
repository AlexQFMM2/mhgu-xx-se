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
    for (quint8 id : learned) if (id != 0 && id != 57 && id != 96) pool.insert(id);
    for (quint8 id : selected) if (id != 0 && !pool.contains(id)) return false;
    return true;
}

QString patternSequence(const QString &scope, int id)
{
    static const std::array<const char *, 7> normal{{
        "CCCCCCCC", "BBCCCC", "BBBCC", "BBBB", "ACCCCC", "ABCCC", "ABBC"
    }};
    static const std::array<const char *, 8> charisma{{
        "CCCCCCCCC", "BBCCCCC", "BBBCCC", "BBBBC", "ACCCCCC", "ABCCCC", "ABBCC", "ABBB"
    }};
    if (scope == QStringLiteral("charisma_move"))
        return id >= 0 && id < int(charisma.size()) ? QString::fromLatin1(charisma[size_t(id)]) : QString();
    return id >= 0 && id < int(normal.size()) ? QString::fromLatin1(normal[size_t(id)]) : QString();
}

QString generationGroup(bool skill, int id)
{
    static const QSet<int> moveA{2, 13, 17, 18, 19, 36, 45};
    static const QSet<int> moveB{4, 11, 15, 16, 21, 22, 28, 33, 39, 44, 49, 55, 56};
    static const QSet<int> moveC{8, 10, 14, 23, 25, 32, 34, 35, 38, 40, 41, 42, 43, 48, 50, 51, 52, 53, 54};
    static const QSet<int> skillA{7, 12, 13, 19, 21, 23, 39};
    static const QSet<int> skillB{2, 4, 6, 9, 14, 17, 20, 28, 31, 36, 40, 80, 81};
    static const QSet<int> skillC{1, 8, 15, 25, 26, 27, 29, 30, 32, 33, 34, 35, 37, 42, 47, 79};
    const QSet<int> &a = skill ? skillA : moveA;
    const QSet<int> &b = skill ? skillB : moveB;
    const QSet<int> &c = skill ? skillC : moveC;
    if (a.contains(id)) return QStringLiteral("A");
    if (b.contains(id)) return QStringLiteral("B");
    if (c.contains(id)) return QStringLiteral("C");
    return {};
}

void addIssue(QVector<PalicoValidationIssue> &issues, const QString &field,
              const QString &code, const QString &message,
              PalicoIssueSeverity severity = PalicoIssueSeverity::Error)
{
    issues.push_back({severity, field, code, message});
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
    const bool validType = value.type <= 11 || (value.type >= 13 && value.type <= 21);
    if (!base || index < 0 || index >= EquipmentCount || !validType || value.level > 31 || !rangeOk(offset, EquipmentSize)) return false;

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
    out.actionValidLength = quint8(m_raw[int(p + 0x55)]);
    out.skillPattern = quint8(m_raw[int(p + 0x56)]);
    out.skillValidLength = quint8(m_raw[int(p + 0x57)]);
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

bool MhguSave::setPalico(int index, const MhguPalico &value)
{
    const quint32 base = selectedBase();
    if (base == 0) return false;
    const quint64 p = quint64(base) + PalicoOffset + quint64(index) * PalicoSize;
    if (index < 0 || index >= PalicoCount || !rangeOk(p, PalicoSize)) return false;
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
    patterns[1] = char(value.actionValidLength);
    patterns[2] = char(value.skillPattern);
    patterns[3] = char(value.skillValidLength);
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

MhguPalicoStructure MhguSave::decodePalicoStructure(const MhguPalico &value)
{
    MhguPalicoStructure out;
    out.actionScope = value.forte == 0 ? QStringLiteral("charisma_move") : QStringLiteral("normal_move");
    out.actionFixedCount = value.forte == 0 ? 3 : 4;
    out.actionTransferCount = value.forte == 0 ? 3 : 2;
    out.actionSequence = patternSequence(out.actionScope, value.actionPattern);
    out.skillSequence = patternSequence(QStringLiteral("skill"), value.skillPattern);
    const int expectedActionLength = out.actionFixedCount + out.actionSequence.size() + out.actionTransferCount;
    const int expectedSkillLength = out.skillFixedCount + out.skillSequence.size() + out.skillTransferCount;
    bool boundaries = value.actionValidLength <= value.learnedActions.size() &&
                      value.skillValidLength <= value.learnedSkills.size();
    for (int i = value.actionValidLength; boundaries && i < int(value.learnedActions.size()); ++i)
        boundaries = value.learnedActions[size_t(i)] == 57;
    for (int i = value.skillValidLength; boundaries && i < int(value.learnedSkills.size()); ++i)
        boundaries = value.learnedSkills[size_t(i)] == 96;
    bool groups = true;
    for (int i = 0; groups && i < out.actionSequence.size(); ++i)
        groups = generationGroup(false, value.learnedActions[size_t(out.actionFixedCount + i)]) == out.actionSequence.mid(i, 1);
    for (int i = 0; groups && i < out.skillSequence.size(); ++i)
        groups = generationGroup(true, value.learnedSkills[size_t(out.skillFixedCount + i)]) == out.skillSequence.mid(i, 1);
    auto compactRegion = [](const auto &array, int begin, int end) {
        bool empty = false;
        for (int i = begin; i < end; ++i) {
            if (array[size_t(i)] == 0) empty = true;
            else if (empty) return false;
        }
        return true;
    };
    groups = groups && compact(value.equippedActions) && compact(value.equippedSkills) &&
        compactRegion(value.learnedActions, out.actionFixedCount + out.actionSequence.size(), expectedActionLength) &&
        compactRegion(value.learnedSkills, out.skillFixedCount + out.skillSequence.size(), expectedSkillLength);
    bool fixed = value.forte <= 7;
    if (fixed) {
        static const std::array<int, 8> primary{{31, 29, 30, 20, 3, 12, 37, 46}};
        static const std::array<std::array<int, 2>, 8> secondary{{
            {{-1, -1}}, {{6, 26}}, {{7, 24}}, {{5, 24}},
            {{5, 7}}, {{6, 27}}, {{26, 27}}, {{47, -1}}
        }};
        static const std::array<std::array<int, 2>, 8> innateSkills{{
            {{45, 38}}, {{3, 10}}, {{16, 18}}, {{41, 43}},
            {{5, 46}}, {{24, 11}}, {{44, 22}}, {{77, 78}}
        }};
        const int forte = value.forte;
        fixed = value.learnedActions[0] == primary[size_t(forte)];
        if (forte == 0) fixed = fixed && value.learnedActions[1] == 9 && value.learnedActions[2] == 1;
        else fixed = fixed && (value.learnedActions[1] == secondary[size_t(forte)][0] ||
                               value.learnedActions[1] == secondary[size_t(forte)][1]) &&
                     value.learnedActions[2] == 9 && value.learnedActions[3] == 1;
        fixed = fixed && value.learnedSkills[0] == innateSkills[size_t(forte)][0] &&
                value.learnedSkills[1] == innateSkills[size_t(forte)][1];
    }
    out.recognized = fixed && groups && boundaries && !out.actionSequence.isEmpty() &&
        !out.skillSequence.isEmpty() && value.actionValidLength == expectedActionLength &&
        value.skillValidLength == expectedSkillLength;
    return out;
}

QVector<PalicoValidationIssue> MhguSave::validatePalico(const MhguPalico &value)
{
    QVector<PalicoValidationIssue> issues;
    if (value.level < 1 || value.level > 60)
        addIssue(issues, QStringLiteral("level"), QStringLiteral("level.range"),
                 QStringLiteral("猫猫等级不在 1 到 60 之间。"));
    if (value.forte > 7)
        addIssue(issues, QStringLiteral("forte"), QStringLiteral("forte.unknown"),
                 QStringLiteral("猫猫倾向无法识别。"));
    if (value.target > 5)
        addIssue(issues, QStringLiteral("target"), QStringLiteral("target.unknown"),
                 QStringLiteral("目标偏好无法识别。"));

    const MhguPalicoStructure structure = decodePalicoStructure(value);
    if (structure.actionSequence.isEmpty())
        addIssue(issues, QStringLiteral("actionPattern"), QStringLiteral("action.pattern.unknown"),
                 QStringLiteral("行动模式编号不属于当前倾向的原生 A/B/C 组合。"));
    if (structure.skillSequence.isEmpty())
        addIssue(issues, QStringLiteral("skillPattern"), QStringLiteral("skill.pattern.unknown"),
                 QStringLiteral("技能模式编号不属于普通猫的七种原生组合。"));

    const int expectedActionLength = structure.actionFixedCount + structure.actionSequence.size() + structure.actionTransferCount;
    const int expectedSkillLength = structure.skillFixedCount + structure.skillSequence.size() + structure.skillTransferCount;
    if (!structure.actionSequence.isEmpty() && value.actionValidLength != expectedActionLength)
        addIssue(issues, QStringLiteral("actionValidLength"), QStringLiteral("action.length.mismatch"),
                 QStringLiteral("行动有效长度为 %1，当前组合应为 %2。")
                    .arg(value.actionValidLength).arg(expectedActionLength));
    if (!structure.skillSequence.isEmpty() && value.skillValidLength != expectedSkillLength)
        addIssue(issues, QStringLiteral("skillValidLength"), QStringLiteral("skill.length.mismatch"),
                 QStringLiteral("技能有效长度为 %1，当前组合应为 %2。")
                    .arg(value.skillValidLength).arg(expectedSkillLength));

    auto checkBoundary = [&issues](const auto &array, int length, int sentinel,
                                   const QString &field, const QString &label) {
        if (length < 0 || length > int(array.size())) {
            addIssue(issues, field, field + QStringLiteral(".range"),
                     QStringLiteral("%1有效长度超过物理数组边界。 ").arg(label));
            return;
        }
        for (int i = length; i < int(array.size()); ++i) {
            if (array[size_t(i)] != sentinel) {
                addIssue(issues, field, field + QStringLiteral(".sentinel"),
                         QStringLiteral("%1第 %2 格位于有效区域外，应为尾部哨兵 %3。")
                            .arg(label).arg(i + 1).arg(sentinel));
                break;
            }
        }
    };
    checkBoundary(value.learnedActions, value.actionValidLength, 57,
                  QStringLiteral("learnedActions"), QStringLiteral("已学行动"));
    checkBoundary(value.learnedSkills, value.skillValidLength, 96,
                  QStringLiteral("learnedSkills"), QStringLiteral("已学技能"));

    if (value.forte <= 7) {
        static const std::array<int, 8> primary{{31, 29, 30, 20, 3, 12, 37, 46}};
        static const std::array<std::array<int, 2>, 8> secondary{{
            {{-1, -1}}, {{6, 26}}, {{7, 24}}, {{5, 24}},
            {{5, 7}}, {{6, 27}}, {{26, 27}}, {{47, -1}}
        }};
        static const std::array<std::array<int, 2>, 8> innateSkills{{
            {{45, 38}}, {{3, 10}}, {{16, 18}}, {{41, 43}},
            {{5, 46}}, {{24, 11}}, {{44, 22}}, {{77, 78}}
        }};
        const int forte = value.forte;
        if (value.learnedActions[0] != primary[size_t(forte)])
            addIssue(issues, QStringLiteral("learnedActions"), QStringLiteral("action.primary.mismatch"),
                     QStringLiteral("固有行动 ID %1 与当前倾向不一致，应为 ID %2。")
                        .arg(value.learnedActions[0]).arg(primary[size_t(forte)]));
        if (forte == 0) {
            if (value.learnedActions[1] != 9 || value.learnedActions[2] != 1)
                addIssue(issues, QStringLiteral("learnedActions"), QStringLiteral("action.common.mismatch"),
                         QStringLiteral("领袖猫的共通行动应为小桶爆弹与药草笛。"));
        } else {
            const int secondaryId = value.learnedActions[1];
            const auto allowed = secondary[size_t(forte)];
            if (secondaryId != allowed[0] && secondaryId != allowed[1])
                addIssue(issues, QStringLiteral("learnedActions"), QStringLiteral("action.secondary.mismatch"),
                         QStringLiteral("准固有行动 ID %1 不属于当前倾向候选。").arg(secondaryId));
            if (value.learnedActions[2] != 9 || value.learnedActions[3] != 1)
                addIssue(issues, QStringLiteral("learnedActions"), QStringLiteral("action.common.mismatch"),
                         QStringLiteral("共通行动应为小桶爆弹与药草笛。"));
        }
        for (int i = 0; i < 2; ++i) {
            if (value.learnedSkills[size_t(i)] != innateSkills[size_t(forte)][size_t(i)])
                addIssue(issues, QStringLiteral("learnedSkills"), QStringLiteral("skill.innate.mismatch"),
                         QStringLiteral("第 %1 个固有技能 ID %2 与当前倾向不一致，应为 ID %3。")
                            .arg(i + 1).arg(value.learnedSkills[size_t(i)])
                            .arg(innateSkills[size_t(forte)][size_t(i)]));
        }
    }

    if (!structure.actionSequence.isEmpty()) {
        for (int i = 0; i < structure.actionSequence.size(); ++i) {
            const int position = structure.actionFixedCount + i;
            if (position >= int(value.learnedActions.size())) break;
            const int id = value.learnedActions[size_t(position)];
            const QString expected = structure.actionSequence.mid(i, 1);
            const QString actual = generationGroup(false, id);
            if (actual != expected)
                addIssue(issues, QStringLiteral("learnedActions"), QStringLiteral("action.group.mismatch"),
                         QStringLiteral("行动 %1 槽放入了 ID %2（%3组），应为%1组。")
                            .arg(expected).arg(id).arg(actual.isEmpty() ? QStringLiteral("非生成") : actual));
        }
    }
    if (!structure.skillSequence.isEmpty()) {
        for (int i = 0; i < structure.skillSequence.size(); ++i) {
            const int position = structure.skillFixedCount + i;
            if (position >= int(value.learnedSkills.size())) break;
            const int id = value.learnedSkills[size_t(position)];
            const QString expected = structure.skillSequence.mid(i, 1);
            const QString actual = generationGroup(true, id);
            if (actual != expected)
                addIssue(issues, QStringLiteral("learnedSkills"), QStringLiteral("skill.group.mismatch"),
                         QStringLiteral("技能 %1 槽放入了 ID %2（%3组），应为%1组。")
                            .arg(expected).arg(id).arg(actual.isEmpty() ? QStringLiteral("非生成") : actual));
        }
    }
    if (!compact(value.equippedActions))
        addIssue(issues, QStringLiteral("equippedActions"), QStringLiteral("action.equipped.gap"),
                 QStringLiteral("已装备行动中间存在空槽；游戏通常要求前置紧凑。"));
    if (!compact(value.equippedSkills))
        addIssue(issues, QStringLiteral("equippedSkills"), QStringLiteral("skill.equipped.gap"),
                 QStringLiteral("已装备技能中间存在空槽；游戏通常要求前置紧凑。"));
    if (!subset(value.equippedActions, value.learnedActions))
        addIssue(issues, QStringLiteral("equippedActions"), QStringLiteral("action.equipped.not_learned"),
                 QStringLiteral("已装备行动包含不在已学行动池中的项目。"));
    if (!subset(value.equippedSkills, value.learnedSkills))
        addIssue(issues, QStringLiteral("equippedSkills"), QStringLiteral("skill.equipped.not_learned"),
                 QStringLiteral("已装备技能包含不在已学技能池中的项目。"));
    if (value.received)
        addIssue(issues, QStringLiteral("origin"), QStringLiteral("origin.received"),
                 QStringLiteral("外来、配信或联动猫可能被游戏重置行动和技能。"),
                 PalicoIssueSeverity::Warning);
    return issues;
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
