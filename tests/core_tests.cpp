#include "mhgu_save.hpp"
#include "game_data.hpp"
#include "transfer_forms.hpp"

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

bool changesOnlyIn(const QByteArray &before, const QByteArray &after, int offset, int length)
{
    if (before.size() != after.size()) return false;
    for (int i = 0; i < before.size(); ++i)
        if (before[i] != after[i] && (i < offset || i >= offset + length)) return false;
    return true;
}

std::array<quint32, 3> syntheticBases(MhguSaveFormat format)
{
    if (format == MhguSaveFormat::Mhxx3ds) return {{0x126474, 0x244C34, 0x3633F4}};
    return {{0x18CC78, 0x2AC53C, 0x3CBE00}};
}

QByteArray syntheticSystem(MhguSaveFormat format = MhguSaveFormat::MhguNsRaw)
{
    const qint64 size = format == MhguSaveFormat::Mhxx3ds ? MhguSave::MhxxFileSize : MhguSave::MhguFileSize;
    QByteArray bytes(int(size), char(0));
    const std::array<quint32, 3> bases = syntheticBases(format);
    const QString names[] = {QStringLiteral("Slot One"), QStringLiteral("存档二"), QStringLiteral("Slot Three")};
    write32(bytes, 0, 0xC6);
    write32(bytes, 0x08, 0x1C);
    for (int i = 0; i < 3; ++i) {
        bytes[0x04 + i] = 1;
        write32(bytes, 0x10 + i * 4, bases[size_t(i)]);
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

MhguPalico regularCat(bool charisma, int actionPattern, int skillPattern)
{
    static const char *normal[] = {"CCCCCCCC", "BBCCCC", "BBBCC", "BBBB", "ACCCCC", "ABCCC", "ABBC"};
    static const char *leader[] = {"CCCCCCCCC", "BBCCCCC", "BBBCCC", "BBBBC", "ACCCCCC", "ABCCCC", "ABBCC", "ABBB"};
    MhguPalico cat;
    cat.level = 60;
    cat.forte = charisma ? 0 : 1;
    cat.target = 3;
    cat.learnedActions.fill(57);
    cat.learnedSkills.fill(96);
    int pos = 0;
    if (charisma) {
        cat.learnedActions[size_t(pos++)] = 31;
        cat.learnedActions[size_t(pos++)] = 9;
        cat.learnedActions[size_t(pos++)] = 1;
    } else {
        cat.learnedActions[size_t(pos++)] = 29;
        cat.learnedActions[size_t(pos++)] = 6;
        cat.learnedActions[size_t(pos++)] = 9;
        cat.learnedActions[size_t(pos++)] = 1;
    }
    const QByteArray actionSequence(charisma ? leader[actionPattern] : normal[actionPattern]);
    for (char group : actionSequence)
        cat.learnedActions[size_t(pos++)] = group == 'A' ? 2 : group == 'B' ? 4 : 8;
    cat.learnedActions[size_t(pos++)] = 0;
    cat.learnedActions[size_t(pos++)] = 0;
    if (charisma) cat.learnedActions[size_t(pos++)] = 0;
    cat.actionPattern = quint8(actionPattern);
    cat.actionValidLength = quint8(pos);
    pos = 0;
    cat.learnedSkills[size_t(pos++)] = charisma ? 45 : 3;
    cat.learnedSkills[size_t(pos++)] = charisma ? 38 : 10;
    const QByteArray skillSequence(normal[skillPattern]);
    for (char group : skillSequence)
        cat.learnedSkills[size_t(pos++)] = group == 'A' ? 7 : group == 'B' ? 2 : 1;
    cat.learnedSkills[size_t(pos++)] = 0;
    cat.learnedSkills[size_t(pos++)] = 0;
    cat.skillPattern = quint8(skillPattern);
    cat.skillValidLength = quint8(pos);
    cat.equippedActions[0] = cat.learnedActions[0];
    cat.equippedSkills[0] = cat.learnedSkills[0];
    return cat;
}

bool hasIssue(const QVector<PalicoValidationIssue> &issues, const QString &code)
{
    for (const PalicoValidationIssue &issue : issues) if (issue.code == code) return true;
    return false;
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
    require(data.name(QStringLiteral("equipment_types"), 11) == QStringLiteral("重弩"), "save type 11 is HBG");
    require(data.name(QStringLiteral("equipment_types"), 13) == QStringLiteral("轻弩"), "save type 13 is LBG");
    require(data.name(QStringLiteral("equipment_types"), 14) == QStringLiteral("太刀"), "save type 14 is Long Sword");
    require(data.name(QStringLiteral("equipment_types"), 21) == QStringLiteral("盾斧"), "save type 21 is Charge Blade");
    require(data.equipmentTable(12).isEmpty(), "save type 12 remains reserved");
    bool weaponSlotsFound = false;
    require(data.weaponSlots(7, 1, 0, &weaponSlotsFound) == 0 && weaponSlotsFound,
            "weapon save level zero maps to native displayed level one");
    require(data.weaponSlots(14, 1, 5, &weaponSlotsFound) == 1 && weaponSlotsFound,
            "Long Sword native level slot count");
    data.weaponSlots(7, 1, 11, &weaponSlotsFound);
    require(!weaponSlotsFound, "weapon level above native maximum is absent");
    bool armorSlotsFound = false;
    require(data.armorSlots(1, 1066, &armorSlotsFound) == 2 && armorSlotsFound,
            "Royal Crown native armor slot count");
    data.armorSlots(1, 65535, &armorSlotsFound);
    require(!armorSlotsFound, "unknown armor ID has no native slot rule");
    bool decorationFound = false;
    require(data.decorationSlotCost(2638, &decorationFound) == 1 && decorationFound,
            "one-slot decoration cost comes from native decoData");
    require(data.decorationSlotCost(2639, &decorationFound) == 2 && decorationFound,
            "two-slot decoration cost comes from native decoData");
    data.decorationSlotCost(2889, &decorationFound);
    require(!decorationFound, "extra DUMMY decoration is not a native legal jewel");
    int skillMinimum = 0;
    int skillMaximum = 0;
    require(data.name(QStringLiteral("skills"), 70) == QStringLiteral("达人"),
            "save skill ID 70 is Expert, not the Dex ID");
    require(data.name(QStringLiteral("skills"), 71) == QStringLiteral("痛击"),
            "save skill ID 71 is Tenderizer");
    require(data.name(QStringLiteral("skills"), 72) == QStringLiteral("连击"),
            "save skill ID 72 is Chain Crit");
    require(data.name(QStringLiteral("skills"), 203) == QStringLiteral("身体系统加倍"),
            "Torso Up is moved to native save skill ID 203");
    require(data.talismanSkillRange(10, 70, 1, &skillMinimum, &skillMaximum)
            && skillMinimum == 0 && skillMaximum == 0,
            "Creator Talisman cannot generate Expert in skill position one");
    require(data.talismanSkillRange(10, 70, 2, &skillMinimum, &skillMaximum)
            && skillMinimum == -7 && skillMaximum == 10,
            "Creator Talisman Expert position-two range");
    require(data.talismanSkillRange(10, 72, 1, &skillMinimum, &skillMaximum)
            && skillMinimum == 1 && skillMaximum == 5,
            "Creator Talisman Chain Crit position-one range");
    require(!data.talismanSkillRange(10, 999, 1, &skillMinimum, &skillMaximum),
            "unknown talisman skill has no legal range");
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
    require(data.patterns(QStringLiteral("normal_move")).size() == 7 &&
            data.patterns(QStringLiteral("charisma_move")).size() == 8 &&
            data.patterns(QStringLiteral("skill")).size() == 7, "Palico native pattern sets");
    require(data.entry(QStringLiteral("palico_support_moves"), 2).generationGroup == QStringLiteral("A") &&
            data.entry(QStringLiteral("palico_support_moves"), 4).generationGroup == QStringLiteral("B") &&
            data.entry(QStringLiteral("palico_support_moves"), 8).generationGroup == QStringLiteral("C"),
            "Palico support A/B/C groups");
    require(data.entry(QStringLiteral("palico_skills"), 7).memoryCost == -1,
            "unconfirmed Palico memory cost stays unknown");

    MhguSave save;
    const QString wrong = temp.filePath(QStringLiteral("wrong"));
    require(writeFile(wrong, QByteArray(10, char(0))), "write wrong-size file");
    require(!save.open(wrong), "reject wrong-size file");
    const QString path = temp.filePath(QStringLiteral("system"));
    const QByteArray original = syntheticSystem();
    require(writeFile(path, original), "write synthetic system");
    require(save.open(path), "open synthetic system");
    require(save.format() == MhguSaveFormat::MhguNsRaw && save.formatName().contains(QStringLiteral("MHGU")),
            "detect raw MHGU system");
    require(!save.setPalico(0, regularCat(false, 6, 6)), "Palico write requires a selected slot");
    const QVector<MhguSlotInfo> slotList = save.slotInfos();
    require(slotList.size() == 3 && slotList[0].name == QStringLiteral("Slot One"), "parse slot one");
    require(slotList[1].name == QStringLiteral("存档二") && slotList[2].hunterRank == 12, "parse all three slots");
    require(save.selectSlot(1), "select second slot");
    require(save.character().money == 2000 && save.character().playTime == 7200, "parse character fields");
    QByteArray invalidSystem = original;
    write32(invalidSystem, 0x14, quint32(MhguSave::MhguFileSize - 8));
    const QString invalidPath = temp.filePath(QStringLiteral("invalid-system"));
    require(writeFile(invalidPath, invalidSystem), "write invalid full-size system");
    require(!save.open(invalidPath), "reject invalid full-size system");
    require(save.isOpen() && save.path() == path && save.selectedSlot() == 1
            && save.character().money == 2000, "failed open preserves current system and slot");
    QByteArray invalidMagic = original;
    write32(invalidMagic, 0, 0);
    const QString invalidMagicPath = temp.filePath(QStringLiteral("invalid-magic-system"));
    require(writeFile(invalidMagicPath, invalidMagic) && !save.open(invalidMagicPath),
            "reject correct-size file with invalid system marker");
    require(save.path() == path && save.selectedSlot() == 1, "invalid marker preserves current system");
    require(!save.isDirty(), "clean immediately after open");
    require(save.save(), "save unchanged system");
    require(readFile(path) == original, "unchanged save is byte-identical");

    QByteArray header(int(MhguSave::HeaderSize), char(0));
    for (int i = 0; i < header.size(); ++i) header[i] = char((i * 17 + 3) & 0xFF);
    const QByteArray headeredOriginal = header + original;
    const QString headeredPath = temp.filePath(QStringLiteral("system-headered"));
    require(writeFile(headeredPath, headeredOriginal), "write headered system");
    require(save.open(headeredPath), "open headered system");
    require(save.format() == MhguSaveFormat::MhguNsHeadered, "detect headered MHGU system");
    require(save.selectSlot(0), "select slot in headered system");
    require(save.save(), "save unchanged headered system");
    require(readFile(headeredPath) == headeredOriginal, "headered unchanged save is byte-identical");
    MhguCharacter headeredCharacter = save.character();
    headeredCharacter.money = 7654321;
    require(save.setCharacter(headeredCharacter) && save.save(), "edit and save headered system");
    const QByteArray headeredEdited = readFile(headeredPath);
    require(headeredEdited.size() == MhguSave::HeaderedFileSize, "headered save keeps original size");
    require(headeredEdited.left(int(MhguSave::HeaderSize)) == header, "header bytes remain unchanged");

    const QString backup = temp.filePath(QStringLiteral("system_backup"));
    require(writeFile(backup, headeredOriginal), "write named backup file");
    require(!save.open(backup) && save.error().contains(QStringLiteral("system_backup")), "reject named system_backup");
    require(save.isOpen() && save.path() == headeredPath && save.character().money == 7654321,
            "rejected backup preserves current headered system");
    require(QFile::remove(backup), "remove synthetic named backup after rejection test");

    const QString xxPath = temp.filePath(QStringLiteral("mhxx-system"));
    const QByteArray xxOriginal = syntheticSystem(MhguSaveFormat::Mhxx3ds);
    require(writeFile(xxPath, xxOriginal), "write synthetic MHXX system");
    MhguSave xxSave;
    require(xxSave.open(xxPath) && xxSave.format() == MhguSaveFormat::Mhxx3ds,
            "detect synthetic MHXX 3DS system");
    require(xxSave.formatName() == QStringLiteral("MHXX（3DS）"), "report MHXX platform name");
    require(xxSave.selectSlot(1) && xxSave.character().name == QStringLiteral("存档二")
            && xxSave.character().hunterRank == 11, "parse MHXX slot with common relative offsets");
    require(xxSave.save() && readFile(xxPath) == xxOriginal, "unchanged MHXX save is byte-identical");

    MhguCharacter xxCharacter = xxSave.character();
    const QByteArray beforeCharacter = xxSave.bytes();
    xxCharacter.money = 424242;
    require(xxSave.setCharacter(xxCharacter), "edit MHXX character");
    require(changesOnlyIn(beforeCharacter, xxSave.bytes(), 0x244C34 + 0x24, 4),
            "MHXX character edit changes only money bytes");
    const QByteArray beforeItems = xxSave.bytes();
    require(xxSave.setItem(17, MhguItem{1234, 77}), "edit MHXX bit-packed item");
    require(xxSave.items()[17].id == 1234 && xxSave.items()[17].count == 77,
            "round-trip MHXX item");
    require(changesOnlyIn(beforeItems, xxSave.bytes(), 0x244C34 + 0x278, 5463),
            "MHXX item edit stays in item box");
    MhguEquipment xxEquipment;
    xxEquipment.type = 14; xxEquipment.id = 22; xxEquipment.level = 3;
    const QByteArray beforeEquipment = xxSave.bytes();
    require(xxSave.setEquipment(0, xxEquipment), "edit MHXX hunter equipment");
    require(xxSave.equipment(0).type == 14 && xxSave.equipment(0).id == 22,
            "round-trip MHXX hunter equipment");
    require(changesOnlyIn(beforeEquipment, xxSave.bytes(), 0x244C34 + 0x62EE, 36),
            "MHXX hunter equipment edit stays in target record");
    MhguPalicoEquipment xxCatEquipment;
    xxCatEquipment.rawType = 22; xxCatEquipment.id = 100; xxCatEquipment.appearanceId = 101;
    const QByteArray beforeCatEquipment = xxSave.bytes();
    require(xxSave.setPalicoEquipment(0, xxCatEquipment), "edit MHXX Palico equipment");
    require(xxSave.palicoEquipment(0).id == 100, "round-trip MHXX Palico equipment");
    require(changesOnlyIn(beforeCatEquipment, xxSave.bytes(), 0x244C34 + 0x17C2E, 36),
            "MHXX Palico equipment edit stays in target record");
    MhguPalico xxCat = regularCat(false, 6, 6);
    xxCat.name = QStringLiteral("XX Cat");
    const QByteArray beforeCat = xxSave.bytes();
    require(xxSave.setPalico(0, xxCat), "edit MHXX Palico record");
    require(xxSave.palico(0).name == QStringLiteral("XX Cat"), "round-trip MHXX Palico record");
    require(changesOnlyIn(beforeCat, xxSave.bytes(), 0x244C34 + 0x23BB6, 324),
            "MHXX Palico edit stays in target record");
    require(xxSave.save() && readFile(xxPath).size() == MhguSave::MhxxFileSize,
            "MHXX save keeps original size");

    require(save.open(path) && save.selectSlot(1), "return to unheadered system");

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
    equipment.type = 12;
    require(!save.setEquipment(1, equipment), "reject reserved hunter equipment type 12");
    equipment.type = 21;
    require(save.setEquipment(1, equipment) && save.equipment(1).type == 21,
            "round-trip Charge Blade save type 21");

    MhguEquipment equippedSource = save.equipment(5);
    equippedSource.appearanceId = 99;
    QString cacheWarning;
    require(save.setEquipment(5, equippedSource, &cacheWarning) && cacheWarning.isEmpty(), "edit unique equipped box source");
    const QByteArray afterCacheSync = save.bytes();
    const int equippedHead = 0x2AC53C + 0x110 + 44;
    require(quint8(afterCacheSync[equippedHead + 4]) == 99 && quint8(afterCacheSync[equippedHead + 5]) == 0,
            "synchronize uniquely matched equipped cache");

    MhguPalicoEquipment palicoEquipment;
    palicoEquipment.rawType = 23;
    palicoEquipment.id = 501;
    palicoEquipment.appearanceId = 502;
    require(save.setPalicoEquipment(0, palicoEquipment), "set Palico equipment");
    require(save.palicoEquipment(0).id == 501, "round-trip Palico equipment");

    const QByteArray itemForm = MhxxGuTransfer::exportItems(save.items());
    require(itemForm.startsWith("MHXX_GU_TRANSFER,1,ITEM_BOX\r\n"), "versioned item transfer preamble");
    QVector<MhxxGuTransfer::ItemUpdate> itemUpdates;
    QString transferError;
    require(MhxxGuTransfer::parseItems(itemForm, &itemUpdates, &transferError)
            && itemUpdates.size() == MhguSave::ItemCount, "parse versioned item transfer");
    QVector<MhguItem> xxImportedItems = xxSave.items();
    for (const MhxxGuTransfer::ItemUpdate &update : itemUpdates) xxImportedItems[update.index] = update.value;
    require(xxSave.setItems(xxImportedItems) && xxSave.items()[0].id == save.items()[0].id,
            "transfer item box GU to XX through logical values");
    require(MhxxGuTransfer::parseItems(QByteArray("slot,id,count\n2,77,6\n"), &itemUpdates, &transferError)
            && itemUpdates.size() == 1 && itemUpdates[0].index == 1,
            "accept legacy unversioned GU item form");
    require(!MhxxGuTransfer::parseItems(QByteArray("slot,id,count\n2,77,6\n2,88,7\n"),
                                        &itemUpdates, &transferError),
            "reject duplicate item transfer slot");

    QVector<MhguEquipmentUpdate> exportHunter;
    QVector<MhguPalicoEquipmentUpdate> exportPalico;
    MhguEquipmentUpdate exportHunterRow; exportHunterRow.index = 0; exportHunterRow.value = save.equipment(0);
    MhguPalicoEquipmentUpdate exportPalicoRow; exportPalicoRow.index = 0; exportPalicoRow.value = save.palicoEquipment(0);
    exportHunter.push_back(exportHunterRow); exportPalico.push_back(exportPalicoRow);
    const QByteArray equipmentForm = MhxxGuTransfer::exportEquipment(exportHunter, exportPalico);
    require(equipmentForm.startsWith("MHXX_GU_TRANSFER,1,EQUIPMENT_BOX\r\n"),
            "versioned equipment transfer preamble");
    QVector<MhguEquipmentUpdate> importedHunter;
    QVector<MhguPalicoEquipmentUpdate> importedPalico;
    require(MhxxGuTransfer::parseEquipment(equipmentForm, &importedHunter, &importedPalico, &transferError)
            && importedHunter.size() == 1 && importedPalico.size() == 1,
            "parse versioned equipment transfer");
    require(xxSave.setEquipmentBatch(importedHunter, importedPalico)
            && xxSave.equipment(0).id == save.equipment(0).id
            && xxSave.palicoEquipment(0).id == save.palicoEquipment(0).id,
            "transfer equipment GU to XX through logical values");
    const QByteArray beforeNoOpBatch = xxSave.bytes();
    require(xxSave.setEquipmentBatch(importedHunter, importedPalico) && xxSave.bytes() == beforeNoOpBatch,
            "unchanged equipment batch preserves unknown record bytes");
    const QByteArray legacyEquipment(
        "domain,slot,type,id,appearance_id,level,decoration_1,decoration_2,decoration_3,skill_1,skill_1_points,skill_2,skill_2_points,talisman_slots\n"
        "hunter,3,14,10,0,2,0,0,0,0,0,0,0,0\n");
    require(MhxxGuTransfer::parseEquipment(legacyEquipment, &importedHunter, &importedPalico, &transferError)
            && importedHunter.size() == 1 && importedHunter[0].index == 2,
            "accept legacy unversioned GU equipment form");
    const QByteArray duplicateEquipment = legacyEquipment
        + QByteArray("hunter,3,14,11,0,2,0,0,0,0,0,0,0,0\n");
    require(!MhxxGuTransfer::parseEquipment(duplicateEquipment, &importedHunter, &importedPalico, &transferError),
            "reject duplicate equipment transfer slot");
    MhguEquipmentUpdate stagedHunter; stagedHunter.index = 8; stagedHunter.value = save.equipment(0);
    MhguPalicoEquipmentUpdate invalidPalico; invalidPalico.index = MhguSave::PalicoEquipmentCount; invalidPalico.value = palicoEquipment;
    const QByteArray beforeFailedBatch = save.bytes();
    require(!save.setEquipmentBatch({stagedHunter}, {invalidPalico}) && save.bytes() == beforeFailedBatch,
            "failed equipment batch leaves memory byte-identical");
    MhguEquipment reverseEquipment = xxSave.equipment(0);
    reverseEquipment.type = 14; reverseEquipment.id = 222;
    require(xxSave.setEquipment(0, reverseEquipment), "prepare reverse XX equipment transfer");
    MhguEquipmentUpdate reverseRow; reverseRow.index = 0; reverseRow.value = xxSave.equipment(0);
    const QByteArray reverseForm = MhxxGuTransfer::exportEquipment({reverseRow}, {});
    require(MhxxGuTransfer::parseEquipment(reverseForm, &importedHunter, &importedPalico, &transferError)
            && save.setEquipmentBatch(importedHunter, importedPalico) && save.equipment(0).id == 222,
            "transfer equipment XX to GU through logical values");

    for (int pattern = 0; pattern < 7; ++pattern) {
        const MhguPalico patternCat = regularCat(false, pattern, pattern);
        require(MhguSave::decodePalicoStructure(patternCat).recognized &&
                MhguSave::validatePalico(patternCat).isEmpty(), "all normal Palico patterns validate");
    }
    for (int pattern = 0; pattern < 8; ++pattern) {
        const MhguPalico patternCat = regularCat(true, pattern, pattern % 7);
        require(MhguSave::decodePalicoStructure(patternCat).recognized &&
                MhguSave::validatePalico(patternCat).isEmpty(), "all Charisma Palico patterns validate");
    }
    const MhguPalico leader020c = regularCat(true, 2, 3);
    require(leader020c.actionPattern == 0x02 && leader020c.actionValidLength == 0x0C &&
            leader020c.skillPattern == 0x03 && leader020c.skillValidLength == 0x08,
            "real Charisma 020C/0308 pattern-byte semantics");
    MhguPalico receivedLeader = leader020c;
    receivedLeader.received = true;
    require(MhguSave::decodePalicoStructure(receivedLeader).recognized,
            "origin flags do not hide a recognizable Palico structure");
    const int primaryByForte[] = {31, 29, 30, 20, 3, 12, 37, 46};
    const int secondaryByForte[] = {0, 6, 7, 5, 5, 6, 26, 47};
    const int innateByForte[][2] = {{45,38}, {3,10}, {16,18}, {41,43}, {5,46}, {24,11}, {44,22}, {77,78}};
    for (int forte = 1; forte < 8; ++forte) {
        MhguPalico forteCat = regularCat(false, 6, 6);
        forteCat.forte = quint8(forte);
        forteCat.learnedActions[0] = quint8(primaryByForte[forte]);
        forteCat.learnedActions[1] = quint8(secondaryByForte[forte]);
        forteCat.learnedSkills[0] = quint8(innateByForte[forte][0]);
        forteCat.learnedSkills[1] = quint8(innateByForte[forte][1]);
        forteCat.equippedActions[0] = forteCat.learnedActions[0];
        forteCat.equippedSkills[0] = forteCat.learnedSkills[0];
        require(MhguSave::decodePalicoStructure(forteCat).recognized &&
                MhguSave::validatePalico(forteCat).isEmpty(), "all eight Palico fortes validate");
    }

    MhguPalico cat = regularCat(false, 6, 6);
    cat.name = QStringLiteral("Test Cat");
    cat.level = 20;
    cat.target = 4;
    require(save.setPalico(0, cat), "set legal Palico");
    const MhguPalico decodedCat = save.palico(0);
    require(decodedCat.name == QStringLiteral("Test Cat") && decodedCat.experience == 18645 &&
            decodedCat.actionValidLength == cat.actionValidLength &&
            decodedCat.skillValidLength == cat.skillValidLength, "Palico level and all pattern bytes round-trip");
    cat.learnedActions[4] = 4;
    cat.actionValidLength = 1;
    cat.skillPattern = 8;
    cat.equippedActions[1] = 56;
    const QVector<PalicoValidationIssue> illegal = MhguSave::validatePalico(cat);
    require(hasIssue(illegal, QStringLiteral("action.length.mismatch")) &&
            hasIssue(illegal, QStringLiteral("skill.pattern.unknown")) &&
            hasIssue(illegal, QStringLiteral("action.equipped.not_learned")),
            "illegal Palico structure reports concrete advisory reasons");
    require(save.setPalico(1, cat), "illegal Palico configuration still writes");
    const MhguPalico illegalRoundTrip = save.palico(1);
    require(illegalRoundTrip.actionValidLength == 1 && illegalRoundTrip.skillPattern == 8 &&
            illegalRoundTrip.learnedActions[4] == 4, "advanced illegal bytes round-trip unchanged");

    MhguPalico special = regularCat(false, 6, 6);
    special.skillPattern = 8;
    special.skillValidLength = 11;
    special.learnedSkills[10] = 95;
    special.learnedSkills[11] = 96;
    special.received = true;
    require(save.setPalico(2, special), "write special Palico fixture");
    MhguPalico renamedSpecial = save.palico(2);
    const auto specialActions = renamedSpecial.learnedActions;
    const auto specialSkills = renamedSpecial.learnedSkills;
    const quint8 specialActionPattern = renamedSpecial.actionPattern;
    const quint8 specialActionLength = renamedSpecial.actionValidLength;
    const quint8 specialSkillPattern = renamedSpecial.skillPattern;
    const quint8 specialSkillLength = renamedSpecial.skillValidLength;
    renamedSpecial.name = QStringLiteral("Only Name Changed");
    require(save.setPalico(2, renamedSpecial), "rename special Palico without rebuilding");
    const MhguPalico specialAfterRename = save.palico(2);
    require(specialAfterRename.learnedActions == specialActions && specialAfterRename.learnedSkills == specialSkills &&
            specialAfterRename.actionPattern == specialActionPattern && specialAfterRename.actionValidLength == specialActionLength &&
            specialAfterRename.skillPattern == specialSkillPattern && specialAfterRename.skillValidLength == specialSkillLength,
            "special Palico action/skill bytes stay unchanged on unrelated edit");

    require(save.save(), "save edited system");
    require(readFile(path).size() == MhguSave::FileSize, "saved size remains fixed");
    MhguSave persistedPalicos;
    require(persistedPalicos.open(path) && persistedPalicos.selectSlot(1), "reopen saved Palico fixture");
    const MhguPalico persistedIllegal = persistedPalicos.palico(1);
    require(persistedIllegal.actionValidLength == 1, "illegal Palico effective length persists to disk");
    require(persistedIllegal.skillPattern == 8, "illegal Palico pattern persists to disk");
    require(persistedIllegal.learnedActions[4] == 4, "illegal Palico array byte persists to disk");
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
            if (entry.type > 0 && entry.type <= 21 && entry.type != 12) {
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

    const int mhxxSampleArg = args.indexOf(QStringLiteral("--mhxx-sample"));
    if (mhxxSampleArg >= 0 && mhxxSampleArg + 1 < args.size()) {
        const QByteArray privateMhxxBytes = readFile(args[mhxxSampleArg + 1]);
        MhguSave privateMhxx;
        require(privateMhxx.open(args[mhxxSampleArg + 1]), "open private MHXX sample");
        require(privateMhxx.format() == MhguSaveFormat::Mhxx3ds, "private sample detected as MHXX 3DS");
        const QVector<MhguSlotInfo> mhxxSlots = privateMhxx.slotInfos();
        require(mhxxSlots[0].used && mhxxSlots[0].name == QStringLiteral("sdd")
                && mhxxSlots[0].hunterRank == 0, "private MHXX slot/name/HR");
        require(!mhxxSlots[1].used && !mhxxSlots[2].used, "private MHXX unused slots");
        require(privateMhxx.selectSlot(0), "select private MHXX slot");
        const int weaponTypes[] = {7, 14, 8, 18, 9, 19, 10, 16, 15, 21, 20, 13, 11, 17};
        for (int i = 0; i < 14; ++i)
            require(privateMhxx.equipment(i).type == weaponTypes[i] && privateMhxx.equipment(i).id == 1,
                    "private MHXX starter weapon record");
        for (int i = 0; i < 5; ++i)
            require(privateMhxx.equipment(14 + i).type == i + 1 && privateMhxx.equipment(14 + i).id == 3,
                    "private MHXX starter armor record");
        const QString privateCopyPath = temp.filePath(QStringLiteral("private-mhxx-system-copy"));
        require(writeFile(privateCopyPath, privateMhxxBytes), "copy private MHXX sample for save test");
        MhguSave privateCopy;
        require(privateCopy.open(privateCopyPath) && privateCopy.selectSlot(0) && privateCopy.save(),
                "unchanged private MHXX sample save");
        require(readFile(privateCopyPath) == privateMhxxBytes, "private MHXX no-op save is byte-identical");
    }

    std::cout << "All MHGU / MHXX core tests passed.\n";
    return 0;
}
