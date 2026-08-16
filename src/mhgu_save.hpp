#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <array>

struct MhguSlotInfo {
    int index = -1;
    bool used = false;
    quint32 base = 0;
    QString name;
    quint16 hunterRank = 0;
    quint32 playTime = 0;
};

struct MhguCharacter {
    QString name;
    quint32 playTime = 0;
    quint32 money = 0;
    quint16 hunterRank = 0;
    quint32 hunterRankPoints = 0;
    quint32 academyPoints = 0;
    quint32 bhernaPoints = 0;
    quint32 kokotoPoints = 0;
    quint32 pokkePoints = 0;
    quint32 yukumoPoints = 0;
};

struct MhguItem {
    quint16 id = 0;
    quint8 count = 0;
};

struct MhguEquipment {
    quint8 type = 0;
    quint8 level = 0;
    quint16 id = 0;
    quint16 appearanceId = 0;
    std::array<quint16, 3> decorations{{0, 0, 0}};
    quint8 skill1 = 0;
    quint8 skill2 = 0;
    qint8 skill1Points = 0;
    qint8 skill2Points = 0;
    quint8 talismanSlots = 0;
};

struct MhguPalicoEquipment {
    quint8 rawType = 0;
    quint16 id = 0;
    quint16 appearanceId = 0;
};

struct MhguPalico {
    QString name;
    quint32 experience = 0;
    quint8 level = 1;
    quint8 forte = 0;
    quint8 enthusiasm = 0;
    quint8 target = 0;
    std::array<quint8, 8> equippedActions{};
    std::array<quint8, 8> equippedSkills{};
    std::array<quint8, 16> learnedActions{};
    std::array<quint8, 12> learnedSkills{};
    quint8 actionPattern = 0;
    quint8 actionValidLength = 0;
    quint8 skillPattern = 0;
    quint8 skillValidLength = 0;
    bool received = false;
    QString greeting;
    QString nameGiver;
    QString previousOwner;
    quint8 status = 0;
    quint8 trainingState = 0;
    quint8 assignment = 0;
    quint8 prowlerSelected = 0;
    QByteArray assignmentReferences;
    quint8 voice = 0;
    quint8 eyes = 0;
    quint8 clothing = 0;
    quint8 coat = 0;
    quint8 ears = 0;
    quint8 tail = 0;
    QByteArray coatColor;
    QByteArray rightEyeColor;
    QByteArray leftEyeColor;
    QByteArray vestColor;
};

enum class PalicoIssueSeverity { Info, Warning, Error };

struct PalicoValidationIssue {
    PalicoIssueSeverity severity = PalicoIssueSeverity::Error;
    QString field;
    QString code;
    QString message;
};

struct MhguPalicoStructure {
    bool recognized = false;
    QString actionScope;
    QString actionSequence;
    QString skillSequence;
    int actionFixedCount = 0;
    int actionTransferCount = 0;
    int skillFixedCount = 2;
    int skillTransferCount = 2;
};

class MhguSave {
public:
    static constexpr qint64 FileSize = 5159064;
    static constexpr qint64 HeaderSize = 36;
    static constexpr qint64 HeaderedFileSize = FileSize + HeaderSize;
    static constexpr int ItemCount = 2300;
    static constexpr int EquipmentCount = 2000;
    static constexpr int PalicoEquipmentCount = 1000;
    static constexpr int PalicoCount = 84;

    bool open(const QString &path);
    bool save();
    void close();

    bool isOpen() const { return !m_raw.isEmpty(); }
    bool isDirty() const { return m_dirty; }
    QString path() const { return m_path; }
    QString error() const { return m_error; }
    QVector<MhguSlotInfo> slotInfos() const;
    bool selectSlot(int index);
    int selectedSlot() const { return m_selectedSlot; }

    MhguCharacter character() const;
    bool setCharacter(const MhguCharacter &value);

    QVector<MhguItem> items() const;
    bool setItem(int index, const MhguItem &value);
    bool setItems(const QVector<MhguItem> &values);

    MhguEquipment equipment(int index) const;
    bool setEquipment(int index, const MhguEquipment &value, QString *warning = nullptr);
    MhguPalicoEquipment palicoEquipment(int index) const;
    bool setPalicoEquipment(int index, const MhguPalicoEquipment &value);

    MhguPalico palico(int index) const;
    bool setPalico(int index, const MhguPalico &value);
    static quint32 experienceForLevel(int displayedLevel);
    static MhguPalicoStructure decodePalicoStructure(const MhguPalico &value);
    static QVector<PalicoValidationIssue> validatePalico(const MhguPalico &value);

    bool validate(QString *error = nullptr) const;
    QByteArray bytes() const { return m_raw; }

private:
    quint32 selectedBase() const;
    bool rangeOk(quint64 offset, quint64 length) const;
    quint16 read16(quint64 offset) const;
    quint32 read32(quint64 offset) const;
    void write16(quint64 offset, quint16 value);
    void write32(quint64 offset, quint32 value);
    QString readUtf8(quint64 offset, int length) const;
    void writeUtf8(quint64 offset, int length, const QString &value);
    void markIfChanged(quint64 offset, const QByteArray &value);
    QVector<MhguItem> decodeItems() const;
    void encodeItems(const QVector<MhguItem> &items);

    QByteArray m_raw;
    QByteArray m_header;
    QString m_path;
    QString m_error;
    int m_selectedSlot = -1;
    bool m_dirty = false;
};
