#include "mhgu_save.hpp"
#include "game_data.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
void write16(QByteArray &bytes, int offset, quint16 value)
{
    bytes[offset] = char(value & 0xFF);
    bytes[offset + 1] = char(value >> 8);
}

void write32(QByteArray &bytes, int offset, quint32 value)
{
    for (int i = 0; i < 4; ++i) bytes[offset + i] = char((value >> (8 * i)) & 0xFF);
}

void writeString(QByteArray &bytes, int offset, int length, const QString &value)
{
    QByteArray encoded = value.toUtf8().left(length - 1);
    for (int i = 0; i < length; ++i) bytes[offset + i] = i < encoded.size() ? encoded[i] : 0;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QByteArray syntheticSystem()
{
    QByteArray bytes(int(MhguSave::FileSize), char(0));
    const quint32 bases[] = {0x1000, 0x140000, 0x280000};
    const QString names[] = {QStringLiteral("Slot One"), QStringLiteral("存档二"), QStringLiteral("Slot Three")};
    for (int i = 0; i < 3; ++i) {
        bytes[0x04 + i] = 1;
        write32(bytes, 0x10 + i * 4, bases[i]);
        writeString(bytes, int(bases[i] + 0x23B7D), 32, names[i]);
        write32(bytes, int(bases[i] + 0x20), 3600u * quint32(i + 1));
        write32(bytes, int(bases[i] + 0x24), 1000u * quint32(i + 1));
        write16(bytes, int(bases[i] + 0x28), quint16(10 + i));
    }
    const int boxEntry = int(bases[1] + 0x62EE + 5 * 36);
    const int equippedHead = int(bases[1] + 0x110 + 1 * 44);
    write16(bytes, boxEntry, quint16(1 | (4 << 5)));
    write16(bytes, boxEntry + 2, 77);
    write16(bytes, boxEntry + 4, 88);
    write16(bytes, boxEntry + 6, 9);
    for (int i = 0; i < 12; ++i) bytes[equippedHead + i] = bytes[boxEntry + i];
    return bytes;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory");

    GameData data;
    require(data.load(QStringLiteral("cn")), "load generated Chinese game data");
    require(data.name(QStringLiteral("equipment_types"), 1) == QStringLiteral("头甲"), "equipment type 1 is head");
    require(data.name(QStringLiteral("equipment_types"), 5) == QStringLiteral("腿甲"), "equipment type 5 is legs");
    require(data.name(QStringLiteral("armor_chest"), 1281) == QStringLiteral("飞龙装束･天"), "armor uses real chest save ID");
    require(data.name(QStringLiteral("armor_arms"), 1080) == QStringLiteral("祖龙护肘"), "armor save ID is mapped per part");
    require(data.entries(QStringLiteral("palico_weapons")).size() == 509, "Palico weapon count");
    require(data.entries(QStringLiteral("palico_head")).size() == 502, "Palico head count");
    require(data.entries(QStringLiteral("palico_armor")).size() == 524, "Palico armor count");
    require(data.entries(QStringLiteral("palico_support_moves")).size() == 58, "Palico support move count");
    require(data.entries(QStringLiteral("palico_skills")).size() == 97, "Palico skill count");
    require(!data.contains(QStringLiteral("palico_head"), 503) && data.contains(QStringLiteral("palico_head"), 504)
            && !data.contains(QStringLiteral("palico_armor"), 524) && data.contains(QStringLiteral("palico_armor"), 525),
            "non-contiguous Palico equipment IDs");
    require(!data.patterns(QStringLiteral("move")).isEmpty() && !data.forteGrants(1, QStringLiteral("move")).isEmpty(), "Palico patterns and forte grants");

    MhguSave save;
    const QString wrong = temp.filePath(QStringLiteral("wrong"));
    require(writeFile(wrong, QByteArray(10, char(0))), "write wrong-size file");
    require(!save.open(wrong), "reject wrong-size file");
    const QString backup = temp.filePath(QStringLiteral("headered-input"));
    require(writeFile(backup, QByteArray(int(MhguSave::BackupFileSize), char(0))), "write backup file");
    require(!save.open(backup) && save.error().contains(QStringLiteral("system_backup")), "reject headered backup");

    const QString path = temp.filePath(QStringLiteral("system"));
    const QByteArray original = syntheticSystem();
    require(writeFile(path, original), "write synthetic system");
    require(save.open(path), "open synthetic system");
    const QVector<MhguSlotInfo> slotList = save.slotInfos();
    require(slotList.size() == 3 && slotList[0].name == QStringLiteral("Slot One"), "parse slot one");
    require(slotList[1].name == QStringLiteral("存档二") && slotList[2].hunterRank == 12, "parse all three slots");
    require(save.selectSlot(1), "select second slot");
    require(save.character().money == 2000 && save.character().playTime == 7200, "parse character fields");
    QByteArray invalidSystem = original;
    write32(invalidSystem, 0x14, quint32(MhguSave::FileSize - 8));
    const QString invalidPath = temp.filePath(QStringLiteral("invalid-system"));
    require(writeFile(invalidPath, invalidSystem), "write invalid full-size system");
    require(!save.open(invalidPath), "reject invalid full-size system");
    require(save.isOpen() && save.path() == path && save.selectedSlot() == 1
            && save.character().money == 2000, "failed open preserves current system and slot");
    require(!save.isDirty(), "clean immediately after open");
    require(save.save(), "save unchanged system");
    require(readFile(path) == original, "unchanged save is byte-identical");

    require(save.setItem(0, MhguItem{9, 99}), "set bit-packed item");
    require(save.items()[0].id == 9 && save.items()[0].count == 99, "round-trip bit-packed item");
    require(save.items()[1].id == 0, "adjacent item unchanged");
    QVector<MhguItem> bulkItems = save.items();
    bulkItems[10] = MhguItem{321, 45};
    require(save.setItems(bulkItems) && save.items()[10].id == 321 && save.items()[10].count == 45,
            "bulk item import round-trip");

    MhguEquipment equipment;
    equipment.type = 1;
    equipment.level = 7;
    equipment.id = 123;
    equipment.appearanceId = 456;
    equipment.decorations = {{10, 11, 12}};
    require(save.setEquipment(0, equipment), "set hunter equipment");
    const MhguEquipment decoded = save.equipment(0);
    require(decoded.type == 1 && decoded.id == 123 && decoded.appearanceId == 456, "round-trip hunter equipment");

    MhguEquipment equippedSource = save.equipment(5);
    equippedSource.appearanceId = 99;
    QString cacheWarning;
    require(save.setEquipment(5, equippedSource, &cacheWarning) && cacheWarning.isEmpty(), "edit unique equipped box source");
    const QByteArray afterCacheSync = save.bytes();
    const int equippedHead = 0x140000 + 0x110 + 44;
    require(quint8(afterCacheSync[equippedHead + 4]) == 99 && quint8(afterCacheSync[equippedHead + 5]) == 0,
            "synchronize uniquely matched equipped cache");

    MhguPalicoEquipment palicoEquipment;
    palicoEquipment.rawType = 23;
    palicoEquipment.id = 501;
    palicoEquipment.appearanceId = 502;
    require(save.setPalicoEquipment(0, palicoEquipment), "set Palico equipment");
    require(save.palicoEquipment(0).id == 501, "round-trip Palico equipment");

    MhguPalico cat = save.palico(0);
    cat.name = QStringLiteral("Test Cat");
    cat.level = 20;
    cat.forte = 1;
    cat.target = 4;
    cat.learnedActions[0] = 1;
    cat.equippedActions[0] = 1;
    cat.learnedSkills[0] = 3;
    cat.equippedSkills[0] = 3;
    QString validationError;
    require(save.setPalico(0, cat, &validationError), "set legal Palico");
    const MhguPalico decodedCat = save.palico(0);
    require(decodedCat.name == QStringLiteral("Test Cat") && decodedCat.experience == 18645, "Palico level synchronizes EXP");
    cat.equippedActions[1] = 2;
    require(save.setPalico(1, cat, &validationError), "allow user-defined Palico configuration");
    require(!validationError.isEmpty(), "report Palico legality as advisory status");

    require(save.save(), "save edited system");
    require(readFile(path).size() == MhguSave::FileSize, "saved size remains fixed");
    require(!save.isDirty(), "clean after save");
    require(!QFileInfo::exists(temp.filePath(QStringLiteral("system_backup"))) &&
            !QFileInfo::exists(path + QStringLiteral(".bak")), "saving creates no backup files");

    const QStringList args = app.arguments();
    const int sampleArg = args.indexOf(QStringLiteral("--sample"));
    if (sampleArg >= 0 && sampleArg + 1 < args.size()) {
        MhguSave privateSample;
        require(privateSample.open(args[sampleArg + 1]), "open private sample");
        const QVector<MhguSlotInfo> sampleSlots = privateSample.slotInfos();
        require(sampleSlots[0].used && sampleSlots[0].name == QStringLiteral("QM_Hammer"), "private sample slot/name");
        require(sampleSlots[0].hunterRank == 119, "private sample HR");
        require(!sampleSlots[1].used && !sampleSlots[2].used, "private sample unused slots");
        require(privateSample.selectSlot(0) && privateSample.character().money == 9999999, "private sample money");
        int unknownEquipment = 0;
        for (int i = 0; i < MhguSave::EquipmentCount; ++i) {
            const MhguEquipment entry = privateSample.equipment(i);
            if (entry.type > 0 && entry.type <= 20) {
                const QString table = data.equipmentTable(entry.type);
                if (!data.contains(table, entry.id) || (entry.appearanceId && !data.contains(table, entry.appearanceId)))
                    ++unknownEquipment;
            }
        }
        for (int i = 0; i < MhguSave::PalicoEquipmentCount; ++i) {
            const MhguPalicoEquipment entry = privateSample.palicoEquipment(i);
            if (entry.rawType == 22 || entry.rawType == 23 || entry.rawType == 24) {
                const QString table = data.palicoEquipmentTable(entry.rawType);
                if (!data.contains(table, entry.id) || (entry.appearanceId && !data.contains(table, entry.appearanceId)))
                    ++unknownEquipment;
            }
        }
        require(unknownEquipment == 0, "all used sample equipment IDs resolve to names");
    }

    std::cout << "All MHGU core tests passed.\n";
    return 0;
}
