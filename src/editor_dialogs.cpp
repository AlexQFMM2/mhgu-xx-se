#include "editor_dialogs.hpp"

#include "game_data.hpp"
#include "mhgu_save.hpp"
#include "transfer_forms.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSet>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <functional>

namespace {
void localizeButtons(QDialogButtonBox *buttons, const QString &acceptText = QStringLiteral("确定"))
{
    if (buttons->button(QDialogButtonBox::Ok))
        buttons->button(QDialogButtonBox::Ok)->setText(acceptText);
    if (buttons->button(QDialogButtonBox::Save))
        buttons->button(QDialogButtonBox::Save)->setText(acceptText);
    if (buttons->button(QDialogButtonBox::Cancel))
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
}

bool confirmImport(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, parent);
    auto *confirm = box.addButton(QStringLiteral("导入"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    return box.clickedButton() == confirm;
}

void configureCombo(QComboBox *combo)
{
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    combo->completer()->setFilterMode(Qt::MatchContains);
    combo->setMaxVisibleItems(18);
}

void fillCombo(QComboBox *combo, const QVector<GameDataEntry> &entries, int selected, bool includeUnknown = true)
{
    combo->clear();
    for (const GameDataEntry &entry : entries)
        combo->addItem(QStringLiteral("%1  [ID %2]").arg(entry.name).arg(entry.id), entry.id);
    int index = combo->findData(selected);
    if (index < 0 && includeUnknown) {
        combo->addItem(QStringLiteral("未知 [ID %1]").arg(selected), selected);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(std::max(0, index));
}

quint32 readU32(QLineEdit *edit, bool *ok = nullptr)
{
    bool valid = false;
    const qulonglong value = edit->text().trimmed().toULongLong(&valid);
    valid = valid && value <= 0xFFFFFFFFull;
    if (ok) *ok = valid;
    return valid ? quint32(value) : 0;
}

QLineEdit *u32Edit(quint32 value, QWidget *parent)
{
    auto *edit = new QLineEdit(QString::number(value), parent);
    edit->setPlaceholderText(QStringLiteral("0 - 4294967295"));
    return edit;
}

QString rgbaText(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex().toUpper());
}

QByteArray parseRgba(const QString &text)
{
    return QByteArray::fromHex(text.trimmed().toLatin1());
}

class ItemEditDialog : public QDialog {
public:
    ItemEditDialog(GameData *data, const MhguItem &item, QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("编辑道具格"));
        m_item = new QComboBox(this);
        configureCombo(m_item);
        QVector<GameDataEntry> entries = data->entries(QStringLiteral("items"));
        if (entries.isEmpty() || entries.first().id != 0) {
            GameDataEntry none;
            none.name = QStringLiteral("无");
            none.english = QStringLiteral("None");
            entries.prepend(none);
        }
        fillCombo(m_item, entries, item.id);
        m_count = new QSpinBox(this);
        m_count->setRange(0, 99);
        m_count->setValue(item.count);
        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("道具"), m_item);
        form->addRow(QStringLiteral("数量"), m_count);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        localizeButtons(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        auto *layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(buttons);
        resize(520, 150);
    }
    MhguItem value() const
    {
        MhguItem result;
        result.id = quint16(m_item->currentData().toUInt());
        result.count = result.id == 0 ? 0 : quint8(std::max(1, m_count->value()));
        return result;
    }
private:
    QComboBox *m_item;
    QSpinBox *m_count;
};

static bool isArmorType(int type) { return type >= 1 && type <= 5; }
static bool isWeaponType(int type) { return type >= 7 && type <= 21 && type != 12; }

static bool validateHunterEquipment(GameData *data, const MhguEquipment &entry, QString *error)
{
    if (entry.type == 0) return true;
    const QString table = data->equipmentTable(entry.type);
    if (table.isEmpty() || !data->contains(table, entry.id)) {
        if (error) *error = QStringLiteral("装备类型 %1 与实际 ID %2 不匹配。").arg(entry.type).arg(entry.id);
        return false;
    }
    if (isArmorType(entry.type) && entry.appearanceId && !data->contains(table, entry.appearanceId)) {
        if (error) *error = QStringLiteral("防具幻化 ID %1 不属于相同部位。").arg(entry.appearanceId);
        return false;
    }
    if (isWeaponType(entry.type) && entry.appearanceId) {
        if (error) *error = QStringLiteral("MHGU 原版不支持武器幻化，请将武器幻化设为无。");
        return false;
    }

    bool slotsFound = false;
    int available = 0;
    QString slotSource;
    if (isArmorType(entry.type)) {
        available = data->armorSlots(entry.type, entry.id, &slotsFound);
        slotSource = QStringLiteral("该防具");
    } else if (isWeaponType(entry.type)) {
        available = data->weaponSlots(entry.type, entry.id, entry.level, &slotsFound);
        slotSource = QStringLiteral("该武器当前等级");
        if (!slotsFound) {
            if (error) *error = QStringLiteral(
                "游戏原生 weaponXXLevelData 中不存在此武器与等级组合。存档等级 %1 对应游戏显示等级 %2。"
            ).arg(entry.level).arg(entry.level + 1);
            return false;
        }
    } else if (entry.type == 6) {
        slotsFound = entry.talismanSlots <= 3;
        available = entry.talismanSlots;
        slotSource = QStringLiteral("该护石");
        if ((entry.skill1 && !data->contains(QStringLiteral("skills"), entry.skill1))
                || (entry.skill2 && !data->contains(QStringLiteral("skills"), entry.skill2))) {
            if (error) *error = QStringLiteral("护石包含不存在的技能 ID。");
            return false;
        }
        const auto validateSkill = [&](int position, int skillId, int points) {
            if (skillId == 0) {
                if (points == 0) return true;
                if (error) *error = QStringLiteral("护石技能 %1 为“无”时，技能点必须为 0。")
                    .arg(position);
                return false;
            }
            int minimum = 0;
            int maximum = 0;
            if (!data->talismanSkillRange(entry.id, skillId, position, &minimum, &maximum)) {
                if (error) *error = QStringLiteral("没有护石 ID %1、技能 ID %2 的合法范围记录。")
                    .arg(entry.id).arg(skillId);
                return false;
            }
            const QString skillName = data->name(QStringLiteral("skills"), skillId);
            if (minimum == 0 && maximum == 0) {
                if (error) *error = QStringLiteral("%1不能作为%2的第 %3 技能。")
                    .arg(skillName, data->name(QStringLiteral("talismans"), entry.id))
                    .arg(position);
                return false;
            }
            if (points < minimum || points > maximum) {
                if (error) *error = QStringLiteral(
                    "%1作为%2的第 %3 技能时，合法点数为 %4～%5，当前为 %6。"
                ).arg(skillName, data->name(QStringLiteral("talismans"), entry.id))
                 .arg(position).arg(minimum).arg(maximum).arg(points);
                return false;
            }
            return true;
        };
        if (!validateSkill(1, entry.skill1, entry.skill1Points)
                || !validateSkill(2, entry.skill2, entry.skill2Points))
            return false;
    }
    if (!slotsFound) {
        if (error) *error = QStringLiteral("游戏原生数据中不存在此装备的孔位记录。");
        return false;
    }

    int used = 0;
    for (quint16 decorationId : entry.decorations) {
        bool costFound = false;
        used += data->decorationSlotCost(decorationId, &costFound);
        if (!costFound) {
            if (error) *error = QStringLiteral(
                "装饰珠 ID %1 不在游戏原生 decoData 中。"
            ).arg(decorationId);
            return false;
        }
    }
    if (used > available) {
        if (error) *error = QStringLiteral(
            "装饰珠共占 %1 孔，但%2只有 %3 孔。"
        ).arg(used).arg(slotSource).arg(available);
        return false;
    }
    return true;
}

static bool validatePalicoEquipment(GameData *data, const MhguPalicoEquipment &entry, QString *error)
{
    if (entry.rawType == 0) return true;
    const QString table = data->palicoEquipmentTable(entry.rawType);
    if (table.isEmpty() || !data->contains(table, entry.id)) {
        if (error) *error = QStringLiteral("猫装备类型与实际 ID 不匹配。");
        return false;
    }
    if (entry.rawType == 22 && entry.appearanceId) {
        if (error) *error = QStringLiteral("MHGU 原版武器幻化不生效，请将猫武器幻化设为无。");
        return false;
    }
    if ((entry.rawType == 23 || entry.rawType == 24) && entry.appearanceId
            && !data->contains(table, entry.appearanceId)) {
        if (error) *error = QStringLiteral("猫防具幻化 ID 不属于相同部位。");
        return false;
    }
    return true;
}

class HunterEquipmentEditDialog : public QDialog {
public:
    HunterEquipmentEditDialog(GameData *data, const MhguEquipment &entry, QWidget *parent = nullptr)
        : QDialog(parent), m_data(data), m_original(entry)
    {
        setWindowTitle(QStringLiteral("编辑猎人装备"));
        m_type = new QComboBox(this);
        for (const GameDataEntry &type : data->entries(QStringLiteral("equipment_types")))
            m_type->addItem(QStringLiteral("%1 [类型 %2]").arg(type.name).arg(type.id), type.id);
        m_type->setCurrentIndex(m_type->findData(entry.type));
        m_id = new QComboBox(this);
        m_appearance = new QComboBox(this);
        configureCombo(m_id);
        configureCombo(m_appearance);
        m_level = new QSpinBox(this);
        m_level->setRange(0, 31);
        m_level->setValue(entry.level);
        for (int i = 0; i < 3; ++i) {
            m_decorations[i] = new QComboBox(this);
            configureCombo(m_decorations[i]);
            fillCombo(m_decorations[i], data->entries(QStringLiteral("decorations")), entry.decorations[i]);
        }
        m_skill1 = new QComboBox(this);
        m_skill2 = new QComboBox(this);
        configureCombo(m_skill1);
        configureCombo(m_skill2);
        fillCombo(m_skill1, data->entries(QStringLiteral("skills")), entry.skill1);
        fillCombo(m_skill2, data->entries(QStringLiteral("skills")), entry.skill2);
        m_skill1Points = new QSpinBox(this);
        m_skill2Points = new QSpinBox(this);
        m_skill1Points->setRange(-128, 127);
        m_skill2Points->setRange(-128, 127);
        m_skill1Points->setValue(entry.skill1Points);
        m_skill2Points->setValue(entry.skill2Points);
        m_slots = new QSpinBox(this);
        m_slots->setRange(0, 3);
        m_slots->setValue(entry.talismanSlots);

        m_warning = new QLabel(this);
        m_warning->setObjectName(QStringLiteral("warningLabel"));
        m_warning->setWordWrap(false);
        m_warning->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_warning->setMaximumHeight(46);
        m_slotStatus = new QLabel(this);
        m_slotStatus->setWordWrap(true);
        m_slotStatus->setToolTip(QStringLiteral("红色仅表示可能不符合游戏生成规则，不会阻止应用或保存。"));
        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("装备类型"), m_type);
        form->addRow(QStringLiteral("实际装备"), m_id);
        form->addRow(QStringLiteral("等级（存档值）"), m_level);
        form->addRow(QStringLiteral("幻化"), m_appearance);
        form->addRow(QStringLiteral("装饰珠 1"), m_decorations[0]);
        form->addRow(QStringLiteral("装饰珠 2"), m_decorations[1]);
        form->addRow(QStringLiteral("装饰珠 3"), m_decorations[2]);
        form->addRow(QStringLiteral("合法性提示"), m_slotStatus);
        m_talisman = new QGroupBox(QStringLiteral("护石属性"), this);
        auto *talismanForm = new QFormLayout(m_talisman);
        talismanForm->addRow(QStringLiteral("技能 1"), m_skill1);
        talismanForm->addRow(QStringLiteral("技能点 1"), m_skill1Points);
        talismanForm->addRow(QStringLiteral("技能 2"), m_skill2);
        talismanForm->addRow(QStringLiteral("技能点 2"), m_skill2Points);
        talismanForm->addRow(QStringLiteral("孔数"), m_slots);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        localizeButtons(buttons, QStringLiteral("应用修改"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateType(); });
        connect(m_id, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateEquipmentRules(); });
        connect(m_level, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateEquipmentRules(); });
        connect(m_slots, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateEquipmentRules(); });
        connect(m_skill1, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateEquipmentRules(); });
        connect(m_skill2, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateEquipmentRules(); });
        connect(m_skill1Points, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateEquipmentRules(); });
        connect(m_skill2Points, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateEquipmentRules(); });
        for (QComboBox *decoration : m_decorations)
            connect(decoration, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateEquipmentRules(); });
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_warning);
        layout->addLayout(form);
        layout->addWidget(m_talisman);
        layout->addWidget(buttons);
        updateType();
        resize(650, 630);
    }
    MhguEquipment value() const
    {
        MhguEquipment result = m_original;
        result.type = quint8(m_type->currentData().toUInt());
        result.id = quint16(m_id->currentData().toUInt());
        result.level = quint8(m_level->value());
        result.appearanceId = m_appearance->isEnabled() ? quint16(m_appearance->currentData().toUInt()) : 0;
        for (int i = 0; i < 3; ++i) result.decorations[i] = quint16(m_decorations[i]->currentData().toUInt());
        result.skill1 = quint8(m_skill1->currentData().toUInt());
        result.skill2 = quint8(m_skill2->currentData().toUInt());
        result.skill1Points = qint8(m_skill1Points->value());
        result.skill2Points = qint8(m_skill2Points->value());
        result.talismanSlots = quint8(m_slots->value());
        return result;
    }
private:
    void updateEquipmentRules()
    {
        const int type = m_type->currentData().toInt();
        if (type == 0) {
            m_slotStatus->setStyleSheet(QString());
            m_slotStatus->setText(QStringLiteral("空装备格。"));
            return;
        }
        const MhguEquipment current = value();
        QString error;
        const bool valid = validateHunterEquipment(m_data, current, &error);
        bool found = false;
        int available = 0;
        if (isArmorType(type))
            available = m_data->armorSlots(type, current.id, &found);
        else if (isWeaponType(type))
            available = m_data->weaponSlots(type, current.id, current.level, &found);
        else if (type == 6) {
            available = current.talismanSlots;
            found = true;
        }
        int used = 0;
        for (quint16 decorationId : current.decorations) {
            bool costFound = false;
            used += m_data->decorationSlotCost(decorationId, &costFound);
        }
        if (valid && found) {
            m_slotStatus->setStyleSheet(QString());
            m_slotStatus->setText(current.type == 6
                ? QStringLiteral("护石技能点与孔位：合法（%1 孔，装饰珠占用 %2）").arg(available).arg(used)
                : QStringLiteral("原生孔数 %1，装饰珠占用 %2：%3")
                    .arg(available).arg(used).arg(used <= available ? QStringLiteral("合法") : QStringLiteral("超出孔位")));
        } else {
            m_slotStatus->setStyleSheet(QStringLiteral("color: #c62828; font-weight: 600;"));
            m_slotStatus->setText(QStringLiteral("不合法：%1").arg(error));
        }
    }

    void updateType()
    {
        const int type = m_type->currentData().toInt();
        const QString table = m_data->equipmentTable(type);
        fillCombo(m_id, m_data->entries(table), type == m_original.type ? m_original.id : 0);
        fillCombo(m_appearance, m_data->entries(table), type == m_original.type ? m_original.appearanceId : 0);
        m_appearance->setEnabled(isArmorType(type));
        m_talisman->setVisible(type == 6);
        if (isWeaponType(type))
            m_warning->setText(QStringLiteral("MHGU 原版武器幻化不生效，已停用武器外观编辑。"));
        else if (isArmorType(type))
            m_warning->setText(QStringLiteral("防具幻化已实机验证可用，请选择相同部位的外观。"));
        else m_warning->setText(QStringLiteral("此装备类型不使用幻化。"));
        m_warning->setToolTip(QStringLiteral("游戏原版只会实际显示防具幻化；武器外观字段不会改变武器模型。"));
        updateEquipmentRules();
    }
    GameData *m_data;
    MhguEquipment m_original;
    QComboBox *m_type;
    QComboBox *m_id;
    QComboBox *m_appearance;
    QSpinBox *m_level;
    QComboBox *m_decorations[3];
    QComboBox *m_skill1;
    QComboBox *m_skill2;
    QSpinBox *m_skill1Points;
    QSpinBox *m_skill2Points;
    QSpinBox *m_slots;
    QGroupBox *m_talisman;
    QLabel *m_warning;
    QLabel *m_slotStatus;
};

class PalicoEquipmentEditDialog : public QDialog {
public:
    PalicoEquipmentEditDialog(GameData *data, const MhguPalicoEquipment &entry, QWidget *parent = nullptr)
        : QDialog(parent), m_data(data), m_original(entry)
    {
        setWindowTitle(QStringLiteral("编辑猫装备"));
        m_type = new QComboBox(this);
        m_type->addItem(QStringLiteral("空"), 0);
        m_type->addItem(QStringLiteral("猫武器"), 22);
        m_type->addItem(QStringLiteral("猫头部"), 23);
        m_type->addItem(QStringLiteral("猫身体"), 24);
        m_type->setCurrentIndex(m_type->findData(entry.rawType));
        m_id = new QComboBox(this);
        m_appearance = new QComboBox(this);
        configureCombo(m_id);
        configureCombo(m_appearance);
        m_warning = new QLabel(this);
        m_warning->setObjectName(QStringLiteral("warningLabel"));
        m_warning->setWordWrap(false);
        m_warning->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_warning->setMaximumHeight(46);
        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("类型"), m_type);
        form->addRow(QStringLiteral("实际装备"), m_id);
        form->addRow(QStringLiteral("幻化"), m_appearance);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        localizeButtons(buttons, QStringLiteral("应用修改"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateType(); });
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_warning);
        layout->addLayout(form);
        layout->addWidget(buttons);
        updateType();
        resize(620, 250);
    }
    MhguPalicoEquipment value() const
    {
        MhguPalicoEquipment result;
        result.rawType = quint8(m_type->currentData().toUInt());
        result.id = result.rawType ? quint16(m_id->currentData().toUInt()) : 0;
        result.appearanceId = m_appearance->isEnabled() ? quint16(m_appearance->currentData().toUInt()) : 0;
        return result;
    }
private:
    void updateType()
    {
        const int type = m_type->currentData().toInt();
        const QString table = m_data->palicoEquipmentTable(type);
        fillCombo(m_id, m_data->entries(table), type == m_original.rawType ? m_original.id : 0);
        fillCombo(m_appearance, m_data->entries(table), type == m_original.rawType ? m_original.appearanceId : 0);
        m_id->setEnabled(type != 0);
        m_appearance->setEnabled(type == 23 || type == 24);
        if (type == 22)
            m_warning->setText(QStringLiteral("原版武器幻化不生效，已停用猫武器外观编辑。"));
        else if (type == 23 || type == 24)
            m_warning->setText(QStringLiteral("猫防具幻化可用，请选择相同部位的外观。"));
        else m_warning->setText(QStringLiteral("空装备格。"));
        m_warning->setToolTip(QStringLiteral("武器外观字段不会改变游戏中的武器模型。"));
    }
    GameData *m_data;
    MhguPalicoEquipment m_original;
    QComboBox *m_type;
    QComboBox *m_id;
    QComboBox *m_appearance;
    QLabel *m_warning;
};

class RawByteArrayEditor : public QWidget {
public:
    RawByteArrayEditor(const QString &title, int count, const quint8 *values,
                       QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *group = new QGroupBox(title, this);
        auto *grid = new QGridLayout;
        for (int i = 0; i < count; ++i) {
            auto *spin = new QSpinBox(group);
            spin->setRange(0, 255);
            spin->setValue(values[i]);
            spin->setToolTip(QStringLiteral("物理槽 %1；0 是有效区域空槽，57/96 可能是尾部哨兵。")
                                 .arg(i + 1));
            grid->addWidget(new QLabel(QString::number(i + 1), group), i / 8 * 2, i % 8);
            grid->addWidget(spin, i / 8 * 2 + 1, i % 8);
            m_spins.push_back(spin);
        }
        group->setLayout(grid);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(group);
    }
    QVector<quint8> values() const
    {
        QVector<quint8> result;
        for (QSpinBox *spin : m_spins) result.push_back(quint8(spin->value()));
        return result;
    }
    void onChanged(const std::function<void()> &callback)
    {
        for (QSpinBox *spin : m_spins)
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), spin, [callback] { callback(); });
    }
private:
    QVector<QSpinBox *> m_spins;
};

QVector<GameDataEntry> selectableEntries(const QVector<GameDataEntry> &all, const QString &group = {},
                                         bool teachableOnly = false, int preserve = -1)
{
    QVector<GameDataEntry> result;
    for (const GameDataEntry &entry : all) {
        if (entry.role == QStringLiteral("sentinel")) continue;
        if (!group.isEmpty() && entry.generationGroup != group && entry.id != preserve) continue;
        if (teachableOnly && entry.id != 0 && entry.teachable != 1 && entry.id != preserve) continue;
        result.push_back(entry);
    }
    return result;
}

class CompactIdListEditor : public QWidget {
public:
    CompactIdListEditor(const QString &title, const QVector<GameDataEntry> &entries,
                        const quint8 *values, int capacity, QWidget *parent = nullptr)
        : QWidget(parent), m_entries(entries), m_capacity(capacity)
    {
        auto *group = new QGroupBox(title, this);
        m_rows = new QVBoxLayout;
        int used = 0;
        while (used < capacity && values[used] != 0) ++used;
        for (int i = 0; i < used; ++i) addRow(values[i]);
        auto *buttons = new QHBoxLayout;
        auto *add = new QPushButton(QStringLiteral("＋ 添加"), group);
        auto *remove = new QPushButton(QStringLiteral("－ 移除末项"), group);
        buttons->addWidget(add);
        buttons->addWidget(remove);
        buttons->addStretch();
        connect(add, &QPushButton::clicked, this, [this] { if (m_combos.size() < m_capacity) { addRow(0); changed(); } });
        connect(remove, &QPushButton::clicked, this, [this] {
            if (m_combos.isEmpty()) return;
            QComboBox *combo = m_combos.takeLast();
            QWidget *row = combo->parentWidget();
            delete row;
            changed();
        });
        auto *box = new QVBoxLayout(group);
        box->addLayout(m_rows);
        box->addLayout(buttons);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(group);
    }
    QVector<quint8> values() const
    {
        QVector<quint8> result(m_capacity, 0);
        for (int i = 0; i < m_combos.size(); ++i) result[i] = quint8(m_combos[i]->currentData().toUInt());
        return result;
    }
    void setValues(const QVector<quint8> &values)
    {
        while (QLayoutItem *item = m_rows->takeAt(0)) {
            if (item->widget()) delete item->widget();
            delete item;
        }
        m_combos.clear();
        int used = 0;
        while (used < values.size() && used < m_capacity && values[used] != 0) ++used;
        for (int i = 0; i < used; ++i) addRow(values[i]);
    }
    void setEntries(const QVector<GameDataEntry> &entries)
    {
        m_entries = entries;
        for (QComboBox *combo : m_combos) {
            const int current = combo->currentData().toInt();
            fillCombo(combo, selectableEntries(m_entries, {}, false, current), current);
        }
    }
    void onChanged(const std::function<void()> &callback) { m_changed = callback; }
private:
    void addRow(int value)
    {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *combo = new QComboBox(row);
        configureCombo(combo);
        fillCombo(combo, selectableEntries(m_entries, {}, false, value), value);
        layout->addWidget(new QLabel(QString::number(m_combos.size() + 1), row));
        layout->addWidget(combo, 1);
        m_rows->addWidget(row);
        m_combos.push_back(combo);
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { changed(); });
    }
    void changed() { if (m_changed) m_changed(); }
    QVector<GameDataEntry> m_entries;
    int m_capacity;
    QVBoxLayout *m_rows;
    QVector<QComboBox *> m_combos;
    std::function<void()> m_changed;
};

class GroupedSlotEditor : public QWidget {
public:
    GroupedSlotEditor(const QString &title, const QVector<GameDataEntry> &entries,
                      QWidget *parent = nullptr) : QWidget(parent), m_entries(entries)
    {
        m_group = new QGroupBox(title, this);
        m_grid = new QGridLayout(m_group);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_group);
    }
    void rebuild(const QString &sequence, const QVector<quint8> &values)
    {
        while (QLayoutItem *item = m_grid->takeAt(0)) {
            if (item->widget()) delete item->widget();
            delete item;
        }
        m_combos.clear();
        m_free.clear();
        m_sequence = sequence;
        for (int i = 0; i < sequence.size(); ++i) {
            const QString group = sequence.mid(i, 1);
            const int current = i < values.size() ? values[i] : 0;
            auto *label = new QLabel(QStringLiteral("%1槽 %2").arg(group).arg(i + 1), m_group);
            auto *combo = new QComboBox(m_group);
            configureCombo(combo);
            fillCombo(combo, selectableEntries(m_entries, group, false, current), current);
            auto *free = new QCheckBox(QStringLiteral("自由选择"), m_group);
            const bool mismatch = current != 0 && m_entryGroup(current) != group;
            free->setChecked(mismatch);
            if (mismatch) refill(combo, current, true, group);
            m_grid->addWidget(label, i, 0);
            m_grid->addWidget(combo, i, 1);
            m_grid->addWidget(free, i, 2);
            m_combos.push_back(combo);
            m_free.push_back(free);
            connect(free, &QCheckBox::toggled, this, [this, combo, group](bool enabled) {
                refill(combo, combo->currentData().toInt(), enabled, group);
                changed();
            });
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { changed(); });
        }
        changed();
    }
    QVector<quint8> values() const
    {
        QVector<quint8> result;
        for (QComboBox *combo : m_combos) result.push_back(quint8(combo->currentData().toUInt()));
        return result;
    }
    void onChanged(const std::function<void()> &callback) { m_changed = callback; }
private:
    QString m_entryGroup(int id) const
    {
        for (const GameDataEntry &entry : m_entries) if (entry.id == id) return entry.generationGroup;
        return {};
    }
    void refill(QComboBox *combo, int current, bool free, const QString &group)
    {
        fillCombo(combo, selectableEntries(m_entries, free ? QString() : group, false, current), current);
    }
    void changed()
    {
        for (int i = 0; i < m_combos.size(); ++i) {
            const bool invalid = m_entryGroup(m_combos[i]->currentData().toInt()) != m_sequence.mid(i, 1);
            m_combos[i]->setStyleSheet(invalid
                ? QStringLiteral("QComboBox{border:2px solid #C62828;color:#A61B1B;}") : QString());
        }
        if (m_changed) m_changed();
    }
    QVector<GameDataEntry> m_entries;
    QString m_sequence;
    QGroupBox *m_group;
    QGridLayout *m_grid;
    QVector<QComboBox *> m_combos;
    QVector<QCheckBox *> m_free;
    std::function<void()> m_changed;
};

class PalicoEditDialog : public QDialog {
public:
    PalicoEditDialog(MhguSave *save, GameData *data, int index, QWidget *parent = nullptr)
        : QDialog(parent), m_save(save), m_data(data), m_index(index), m_value(save->palico(index))
    {
        setWindowTitle(QStringLiteral("编辑猫猫 #%1").arg(index + 1));
        auto *tabs = new QTabWidget(this);

        auto *basic = new QWidget(tabs);
        auto *basicForm = new QFormLayout(basic);
        m_name = new QLineEdit(m_value.name, basic);
        m_level = new QSpinBox(basic);
        m_level->setRange(1, 60);
        m_level->setValue(m_value.level);
        m_exp = new QLabel(basic);
        m_forte = new QComboBox(basic);
        fillCombo(m_forte, data->entries(QStringLiteral("palico_fortes")), m_value.forte);
        m_enthusiasm = new QSpinBox(basic);
        m_enthusiasm->setRange(0, 255);
        m_enthusiasm->setValue(m_value.enthusiasm);
        m_target = new QComboBox(basic);
        fillCombo(m_target, data->entries(QStringLiteral("palico_targets")), m_value.target);
        m_greeting = new QTextEdit(m_value.greeting, basic);
        m_greeting->setMaximumHeight(90);
        m_nameGiver = new QLineEdit(m_value.nameGiver, basic);
        m_previousOwner = new QLineEdit(m_value.previousOwner, basic);
        basicForm->addRow(QStringLiteral("名字"), m_name);
        basicForm->addRow(QStringLiteral("等级"), m_level);
        basicForm->addRow(QStringLiteral("同步经验值"), m_exp);
        basicForm->addRow(QStringLiteral("倾向 / 种类"), m_forte);
        basicForm->addRow(QStringLiteral("热情"), m_enthusiasm);
        basicForm->addRow(QStringLiteral("目标偏好"), m_target);
        basicForm->addRow(QStringLiteral("问候"), m_greeting);
        basicForm->addRow(QStringLiteral("命名者"), m_nameGiver);
        basicForm->addRow(QStringLiteral("上一任主人"), m_previousOwner);
        if (m_value.received) {
            auto *warning = new QLabel(QStringLiteral("⚠ 这是外来/联动猫。游戏可能在进入任务时重置自定义行动或技能。"), basic);
            warning->setObjectName(QStringLiteral("warningLabel"));
            warning->setWordWrap(true);
            basicForm->addRow(warning);
        }
        auto *status = new QLabel(QStringLiteral("只读状态：状态 %1 · 训练 %2 · 设施 %3 · 猎猫 %4 · 引用 %5")
            .arg(m_value.status).arg(m_value.trainingState).arg(m_value.assignment).arg(m_value.prowlerSelected)
            .arg(QString::fromLatin1(m_value.assignmentReferences.toHex().toUpper())), basic);
        status->setWordWrap(true);
        basicForm->addRow(status);
        tabs->addTab(basic, QStringLiteral("基本信息"));

        auto *appearance = new QWidget(tabs);
        auto *appearanceForm = new QFormLayout(appearance);
        const QStringList appearanceNames = {QStringLiteral("声音"), QStringLiteral("眼睛"), QStringLiteral("服装"),
            QStringLiteral("毛色样式"), QStringLiteral("耳朵"), QStringLiteral("尾巴")};
        const quint8 appearanceValues[] = {m_value.voice, m_value.eyes, m_value.clothing, m_value.coat, m_value.ears, m_value.tail};
        for (int i = 0; i < 6; ++i) {
            m_appearance[i] = new QSpinBox(appearance);
            m_appearance[i]->setRange(0, 255);
            m_appearance[i]->setValue(appearanceValues[i]);
            appearanceForm->addRow(appearanceNames[i], m_appearance[i]);
        }
        const QByteArray colors[] = {m_value.coatColor, m_value.rightEyeColor, m_value.leftEyeColor, m_value.vestColor};
        const QStringList colorNames = {QStringLiteral("毛色 RGBA"), QStringLiteral("右眼 RGBA"), QStringLiteral("左眼 RGBA"), QStringLiteral("服装 RGBA")};
        for (int i = 0; i < 4; ++i) {
            m_colors[i] = new QLineEdit(rgbaText(colors[i]), appearance);
            m_colors[i]->setMaxLength(8);
            m_colors[i]->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{8}")), m_colors[i]));
            appearanceForm->addRow(colorNames[i], m_colors[i]);
        }
        tabs->addTab(appearance, QStringLiteral("外观"));

        m_moveEntries = data->entries(QStringLiteral("palico_support_moves"));
        m_skillEntries = data->entries(QStringLiteral("palico_skills"));
        const MhguPalicoStructure originalStructure = MhguSave::decodePalicoStructure(m_value);
        // Origin/distribution flags are advisory. A structurally recognizable
        // cat must still be decoded and shown; otherwise applying a valid edit
        // succeeds in the model but appears empty when the dialog is reopened.
        m_ruleActive = originalStructure.recognized;

        auto *actions = new QWidget(tabs);
        auto *actionsLayout = new QVBoxLayout(actions);
        m_actionState = new QLabel(actions);
        m_actionState->setWordWrap(true);
        m_actionState->setObjectName(QStringLiteral("warningLabel"));
        auto *rebuild = new QPushButton(QStringLiteral("按当前倾向重建普通结构"), actions);
        auto *actionPatternRow = new QHBoxLayout;
        m_actionPattern = new QComboBox(actions);
        actionPatternRow->addWidget(new QLabel(QStringLiteral("A/B/C 构成"), actions));
        actionPatternRow->addWidget(m_actionPattern, 1);
        actionPatternRow->addWidget(rebuild);
        m_actionFixed = new QLabel(actions);
        m_actionFixed->setWordWrap(true);
        m_secondary = new QComboBox(actions);
        configureCombo(m_secondary);
        auto *secondaryRow = new QHBoxLayout;
        secondaryRow->addWidget(new QLabel(QStringLiteral("准固有行动"), actions));
        secondaryRow->addWidget(m_secondary, 1);
        m_actionGenerated = new GroupedSlotEditor(QStringLiteral("按组生成的行动槽"), m_moveEntries, actions);
        quint8 emptyMoves[3]{};
        m_actionTransfers = new CompactIdListEditor(QStringLiteral("传授行动槽"),
            selectableEntries(m_moveEntries, {}, true), emptyMoves, 3, actions);
        m_equippedActions = new CompactIdListEditor(QStringLiteral("已装备行动（从已学池选择）"),
            m_moveEntries, m_value.equippedActions.data(), 8, actions);
        m_actionDiagnostics = new QLabel(actions);
        m_actionDiagnostics->setWordWrap(true);
        actionsLayout->addWidget(m_actionState);
        actionsLayout->addLayout(actionPatternRow);
        actionsLayout->addWidget(m_actionFixed);
        actionsLayout->addLayout(secondaryRow);
        actionsLayout->addWidget(m_actionGenerated);
        actionsLayout->addWidget(m_actionTransfers);
        actionsLayout->addWidget(m_equippedActions);
        actionsLayout->addWidget(m_actionDiagnostics);
        actionsLayout->addStretch();
        auto *actionsScroll = new QScrollArea(tabs);
        actionsScroll->setWidgetResizable(true);
        actionsScroll->setWidget(actions);
        tabs->addTab(actionsScroll, QStringLiteral("支援行动"));

        auto *skills = new QWidget(tabs);
        auto *skillsLayout = new QVBoxLayout(skills);
        auto *skillPatternRow = new QHBoxLayout;
        m_skillPattern = new QComboBox(skills);
        skillPatternRow->addWidget(new QLabel(QStringLiteral("A/B/C 构成"), skills));
        skillPatternRow->addWidget(m_skillPattern, 1);
        m_skillFixed = new QLabel(skills);
        m_skillFixed->setWordWrap(true);
        m_skillGenerated = new GroupedSlotEditor(QStringLiteral("按组生成的技能槽"), m_skillEntries, skills);
        quint8 emptySkills[2]{};
        m_skillTransfers = new CompactIdListEditor(QStringLiteral("传授技能槽"),
            selectableEntries(m_skillEntries, {}, true), emptySkills, 2, skills);
        m_equippedSkills = new CompactIdListEditor(QStringLiteral("已装备技能（从已学池选择）"),
            m_skillEntries, m_value.equippedSkills.data(), 8, skills);
        m_memoryUsage = new QLabel(QStringLiteral("记忆槽：成本表尚未确认；未知成本不判定为非法。"), skills);
        m_skillDiagnostics = new QLabel(skills);
        m_skillDiagnostics->setWordWrap(true);
        skillsLayout->addLayout(skillPatternRow);
        skillsLayout->addWidget(m_skillFixed);
        skillsLayout->addWidget(m_skillGenerated);
        skillsLayout->addWidget(m_skillTransfers);
        skillsLayout->addWidget(m_equippedSkills);
        skillsLayout->addWidget(m_memoryUsage);
        skillsLayout->addWidget(m_skillDiagnostics);
        skillsLayout->addStretch();
        auto *skillsScroll = new QScrollArea(tabs);
        skillsScroll->setWidgetResizable(true);
        skillsScroll->setWidget(skills);
        tabs->addTab(skillsScroll, QStringLiteral("被动技能"));

        auto *advanced = new QWidget(tabs);
        auto *advancedLayout = new QVBoxLayout(advanced);
        m_advanced = new QGroupBox(QStringLiteral("启用高级原始编辑（0..255）"), advanced);
        m_advanced->setCheckable(true);
        m_advanced->setChecked(false);
        auto *advancedBox = new QVBoxLayout(m_advanced);
        m_rawEquippedActions = new RawByteArrayEditor(QStringLiteral("已装备行动原始数组"), 8, m_value.equippedActions.data(), m_advanced);
        m_rawEquippedSkills = new RawByteArrayEditor(QStringLiteral("已装备技能原始数组"), 8, m_value.equippedSkills.data(), m_advanced);
        m_rawLearnedActions = new RawByteArrayEditor(QStringLiteral("已学行动原始数组（尾部哨兵 57）"), 16, m_value.learnedActions.data(), m_advanced);
        m_rawLearnedSkills = new RawByteArrayEditor(QStringLiteral("已学技能原始数组（尾部哨兵 96）"), 12, m_value.learnedSkills.data(), m_advanced);
        auto *rawCodes = new QFormLayout;
        m_rawActionPattern = byteSpin(m_value.actionPattern, m_advanced);
        m_rawActionLength = byteSpin(m_value.actionValidLength, m_advanced);
        m_rawSkillPattern = byteSpin(m_value.skillPattern, m_advanced);
        m_rawSkillLength = byteSpin(m_value.skillValidLength, m_advanced);
        rawCodes->addRow(QStringLiteral("0x54 行动模式编号"), m_rawActionPattern);
        rawCodes->addRow(QStringLiteral("0x55 行动有效区域长度"), m_rawActionLength);
        rawCodes->addRow(QStringLiteral("0x56 技能模式编号"), m_rawSkillPattern);
        rawCodes->addRow(QStringLiteral("0x57 技能有效区域长度"), m_rawSkillLength);
        auto *advancedWarning = new QLabel(QStringLiteral("高级值会逐字节写入。非法只会标红提示，不会被自动修正、清除或阻止保存。"), m_advanced);
        advancedWarning->setObjectName(QStringLiteral("warningLabel"));
        advancedWarning->setWordWrap(true);
        advancedBox->addWidget(advancedWarning);
        advancedBox->addWidget(m_rawEquippedActions);
        advancedBox->addWidget(m_rawEquippedSkills);
        advancedBox->addWidget(m_rawLearnedActions);
        advancedBox->addWidget(m_rawLearnedSkills);
        advancedBox->addLayout(rawCodes);
        m_advancedDiagnostics = new QLabel(m_advanced);
        m_advancedDiagnostics->setWordWrap(true);
        advancedBox->addWidget(m_advancedDiagnostics);
        advancedLayout->addWidget(m_advanced);
        advancedLayout->addStretch();
        tabs->addTab(advanced, QStringLiteral("高级设置"));

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        localizeButtons(buttons, QStringLiteral("应用修改"));
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { apply(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_level, qOverload<int>(&QSpinBox::valueChanged), this, [this](int level) {
            m_exp->setText(QString::number(MhguSave::experienceForLevel(level)));
        });
        connect(m_forte, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
            populatePatterns(); updateFixedItems(); updateDiagnostics();
        });
        connect(rebuild, &QPushButton::clicked, this, [this] { activateRules(false); });
        connect(m_actionPattern, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { rebuildGenerated(false); });
        connect(m_skillPattern, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { rebuildGenerated(false); });
        connect(m_secondary, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateDiagnostics(); });
        m_actionGenerated->onChanged([this] { refreshEquippedPools(); updateDiagnostics(); });
        m_skillGenerated->onChanged([this] { refreshEquippedPools(); updateDiagnostics(); });
        m_actionTransfers->onChanged([this] { refreshEquippedPools(); updateDiagnostics(); });
        m_skillTransfers->onChanged([this] { refreshEquippedPools(); updateDiagnostics(); });
        m_equippedActions->onChanged([this] { updateDiagnostics(); });
        m_equippedSkills->onChanged([this] { updateDiagnostics(); });
        connect(m_advanced, &QGroupBox::toggled, this, [this] { updateDiagnostics(); });
        auto rawChanged = [this] { updateDiagnostics(); };
        m_rawEquippedActions->onChanged(rawChanged); m_rawEquippedSkills->onChanged(rawChanged);
        m_rawLearnedActions->onChanged(rawChanged); m_rawLearnedSkills->onChanged(rawChanged);
        for (QSpinBox *spin : {m_rawActionPattern, m_rawActionLength, m_rawSkillPattern, m_rawSkillLength})
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateDiagnostics(); });
        m_exp->setText(QString::number(MhguSave::experienceForLevel(m_value.level)));
        populatePatterns();
        if (m_ruleActive) activateRules(true); else setRuleWidgetsEnabled(false);
        updateFixedItems();
        updateDiagnostics();

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(tabs);
        layout->addWidget(buttons);
        resize(880, 700);
    }
private:
    static QSpinBox *byteSpin(int value, QWidget *parent)
    {
        auto *spin = new QSpinBox(parent); spin->setRange(0, 255); spin->setValue(value); return spin;
    }
    QVector<int> grantIds(const QString &kind, const QString &role) const
    {
        QVector<int> result;
        for (const PalicoForteGrant &grant : m_data->forteGrants(m_forte->currentData().toInt(), kind, role))
            result.push_back(grant.entryId);
        return result;
    }
    void populatePatterns()
    {
        const int oldAction = m_actionPattern->currentIndex() >= 0 ? m_actionPattern->currentData().toInt() : m_value.actionPattern;
        const int oldSkill = m_skillPattern->currentIndex() >= 0 ? m_skillPattern->currentData().toInt() : m_value.skillPattern;
        m_updating = true;
        m_actionPattern->clear();
        const QString scope = m_forte->currentData().toInt() == 0 ? QStringLiteral("charisma_move") : QStringLiteral("normal_move");
        for (const PalicoPattern &pattern : m_data->patterns(scope))
            m_actionPattern->addItem(QStringLiteral("%1 · 模式 %2 · 权重 %3").arg(pattern.sequence).arg(pattern.id).arg(pattern.weight), pattern.id);
        m_skillPattern->clear();
        for (const PalicoPattern &pattern : m_data->patterns(QStringLiteral("skill")))
            m_skillPattern->addItem(QStringLiteral("%1 · 模式 %2 · 权重 %3").arg(pattern.sequence).arg(pattern.id).arg(pattern.weight), pattern.id);
        m_actionPattern->setCurrentIndex(std::max(0, m_actionPattern->findData(oldAction)));
        m_skillPattern->setCurrentIndex(std::max(0, m_skillPattern->findData(oldSkill)));
        m_updating = false;
    }
    void setRuleWidgetsEnabled(bool enabled)
    {
        m_actionPattern->setEnabled(enabled); m_skillPattern->setEnabled(enabled);
        m_secondary->setEnabled(enabled && m_forte->currentData().toInt() != 0);
        m_actionGenerated->setEnabled(enabled); m_skillGenerated->setEnabled(enabled);
        m_actionTransfers->setEnabled(enabled); m_skillTransfers->setEnabled(enabled);
        m_equippedActions->setEnabled(enabled); m_equippedSkills->setEnabled(enabled);
    }
    void updateFixedItems()
    {
        QStringList moveText;
        for (int id : grantIds(QStringLiteral("move"), QStringLiteral("primary")))
            moveText << QStringLiteral("固有：%1").arg(m_data->name(QStringLiteral("palico_support_moves"), id));
        for (int id : grantIds(QStringLiteral("move"), QStringLiteral("common")))
            moveText << QStringLiteral("共通：%1").arg(m_data->name(QStringLiteral("palico_support_moves"), id));
        m_actionFixed->setText(moveText.join(QStringLiteral("　")));
        const QString secondaryRole = m_forte->currentData().toInt() == 7 ? QStringLiteral("secondary_fixed") : QStringLiteral("secondary");
        QVector<GameDataEntry> choices;
        for (int id : grantIds(QStringLiteral("move"), secondaryRole))
            choices.push_back(m_data->entry(QStringLiteral("palico_support_moves"), id));
        const int current = m_secondary->currentIndex() >= 0 ? m_secondary->currentData().toInt() : m_value.learnedActions[1];
        fillCombo(m_secondary, choices, current);
        m_secondary->setVisible(m_forte->currentData().toInt() != 0);
        QStringList skills;
        for (int id : grantIds(QStringLiteral("skill"), QStringLiteral("innate")))
            skills << m_data->name(QStringLiteral("palico_skills"), id);
        m_skillFixed->setText(QStringLiteral("倾向固有技能（可传授）：%1").arg(skills.join(QStringLiteral("、"))));
    }
    void activateRules(bool preserveStored)
    {
        m_ruleActive = true; setRuleWidgetsEnabled(true); updateFixedItems();
        if (!preserveStored && m_secondary->count() > 0) m_secondary->setCurrentIndex(0);
        const QString scope = m_forte->currentData().toInt() == 0 ? QStringLiteral("charisma_move") : QStringLiteral("normal_move");
        const PalicoPattern action = m_data->pattern(scope, m_actionPattern->currentData().toInt());
        const PalicoPattern skill = m_data->pattern(QStringLiteral("skill"), m_skillPattern->currentData().toInt());
        if (preserveStored) rebuildGenerated(true);
        else {
            m_actionGenerated->rebuild(action.sequence, {});
            m_skillGenerated->rebuild(skill.sequence, {});
        }
        const int actionStart = (m_forte->currentData().toInt() == 0 ? 3 : 4) + action.sequence.size();
        QVector<quint8> actionTransfers, skillTransfers;
        const int transferCount = m_forte->currentData().toInt() == 0 ? 3 : 2;
        if (preserveStored) {
            for (int i = 0; i < transferCount; ++i) actionTransfers.push_back(m_value.learnedActions[size_t(actionStart + i)]);
            for (int i = 0; i < 2; ++i) skillTransfers.push_back(m_value.learnedSkills[size_t(2 + skill.sequence.size() + i)]);
        }
        m_actionTransfers->setValues(actionTransfers); m_skillTransfers->setValues(skillTransfers);
        if (!preserveStored) {
            m_equippedActions->setValues({});
            m_equippedSkills->setValues({});
        }
        refreshEquippedPools();
        updateDiagnostics();
    }
    void rebuildGenerated(bool fromStored)
    {
        if (m_updating || !m_ruleActive) return;
        const QString scope = m_forte->currentData().toInt() == 0 ? QStringLiteral("charisma_move") : QStringLiteral("normal_move");
        const PalicoPattern action = m_data->pattern(scope, m_actionPattern->currentData().toInt());
        const PalicoPattern skill = m_data->pattern(QStringLiteral("skill"), m_skillPattern->currentData().toInt());
        QVector<quint8> actionValues = fromStored ? QVector<quint8>() : m_actionGenerated->values();
        QVector<quint8> skillValues = fromStored ? QVector<quint8>() : m_skillGenerated->values();
        if (fromStored) {
            const int fixed = m_forte->currentData().toInt() == 0 ? 3 : 4;
            for (int i = 0; i < action.sequence.size(); ++i) actionValues.push_back(m_value.learnedActions[size_t(fixed + i)]);
            for (int i = 0; i < skill.sequence.size(); ++i) skillValues.push_back(m_value.learnedSkills[size_t(2 + i)]);
        }
        m_actionGenerated->rebuild(action.sequence, actionValues);
        m_skillGenerated->rebuild(skill.sequence, skillValues);
        updateDiagnostics();
    }
    template <size_t N> static void copyBytes(const QVector<quint8> &source, std::array<quint8, N> &target)
    {
        for (int i = 0; i < int(N) && i < source.size(); ++i) target[size_t(i)] = source[i];
    }
    MhguPalico candidate() const
    {
        MhguPalico out = m_value;
        out.forte = quint8(m_forte->currentData().toUInt()); out.level = quint8(m_level->value());
        out.target = quint8(m_target->currentData().toUInt());
        if (m_advanced->isChecked()) {
            copyBytes(m_rawEquippedActions->values(), out.equippedActions); copyBytes(m_rawEquippedSkills->values(), out.equippedSkills);
            copyBytes(m_rawLearnedActions->values(), out.learnedActions); copyBytes(m_rawLearnedSkills->values(), out.learnedSkills);
            out.actionPattern = quint8(m_rawActionPattern->value()); out.actionValidLength = quint8(m_rawActionLength->value());
            out.skillPattern = quint8(m_rawSkillPattern->value()); out.skillValidLength = quint8(m_rawSkillLength->value());
        } else if (m_ruleActive) {
            out.learnedActions.fill(57); out.learnedSkills.fill(96); out.equippedActions.fill(0); out.equippedSkills.fill(0);
            int pos = 0;
            const QVector<int> primary = grantIds(QStringLiteral("move"), QStringLiteral("primary"));
            if (!primary.isEmpty()) out.learnedActions[size_t(pos++)] = quint8(primary[0]);
            if (out.forte != 0) out.learnedActions[size_t(pos++)] = quint8(m_secondary->currentData().toUInt());
            for (int id : grantIds(QStringLiteral("move"), QStringLiteral("common"))) out.learnedActions[size_t(pos++)] = quint8(id);
            for (quint8 id : m_actionGenerated->values()) if (pos < 16) out.learnedActions[size_t(pos++)] = id;
            const int actionTransferCount = out.forte == 0 ? 3 : 2;
            const QVector<quint8> actionTransfers = m_actionTransfers->values();
            for (int i = 0; i < actionTransferCount && pos < 16; ++i) out.learnedActions[size_t(pos++)] = actionTransfers[i];
            out.actionPattern = quint8(m_actionPattern->currentData().toUInt());
            const QString scope = out.forte == 0 ? QStringLiteral("charisma_move") : QStringLiteral("normal_move");
            out.actionValidLength = quint8(m_data->pattern(scope, out.actionPattern).validLength);
            pos = 0;
            for (int id : grantIds(QStringLiteral("skill"), QStringLiteral("innate"))) out.learnedSkills[size_t(pos++)] = quint8(id);
            for (quint8 id : m_skillGenerated->values()) if (pos < 12) out.learnedSkills[size_t(pos++)] = id;
            for (quint8 id : m_skillTransfers->values()) if (pos < 12) out.learnedSkills[size_t(pos++)] = id;
            out.skillPattern = quint8(m_skillPattern->currentData().toUInt());
            out.skillValidLength = quint8(m_data->pattern(QStringLiteral("skill"), out.skillPattern).validLength);
            copyBytes(m_equippedActions->values(), out.equippedActions); copyBytes(m_equippedSkills->values(), out.equippedSkills);
        }
        return out;
    }
    void refreshEquippedPools()
    {
        if (!m_ruleActive || m_updating || m_advanced->isChecked()) return;
        const MhguPalico current = candidate();
        QSet<int> actionIds, skillIds;
        for (quint8 id : current.learnedActions) if (id != 0 && id != 57) actionIds.insert(id);
        for (quint8 id : current.learnedSkills) if (id != 0 && id != 96) skillIds.insert(id);
        for (quint8 id : current.equippedActions) if (id != 0) actionIds.insert(id);
        for (quint8 id : current.equippedSkills) if (id != 0) skillIds.insert(id);
        QVector<GameDataEntry> actions, skills;
        for (const GameDataEntry &entry : m_moveEntries) if (entry.id == 0 || actionIds.contains(entry.id)) actions.push_back(entry);
        for (const GameDataEntry &entry : m_skillEntries) if (entry.id == 0 || skillIds.contains(entry.id)) skills.push_back(entry);
        m_updating = true;
        m_equippedActions->setEntries(actions); m_equippedSkills->setEntries(skills);
        m_updating = false;
    }
    void updateDiagnostics()
    {
        if (!m_actionDiagnostics || m_updating) return;
        m_actionState->setText(!m_ruleActive && !m_advanced->isChecked()
            ? QStringLiteral("当前猫是外来/配信猫或结构无法识别。数组保持原样；点击“按当前倾向重建”才套用普通布局。")
            : QStringLiteral("规则只用于提示。红色配置仍可应用并保存，程序不会替换或清除。"));
        const MhguPalico current = candidate();
        int equippedSkillCount = 0;
        for (quint8 id : current.equippedSkills) if (id != 0) ++equippedSkillCount;
        m_memoryUsage->setText(QStringLiteral("已装备 %1 项；记忆槽成本尚未从原生表确认，未知成本不判定为非法。")
                                   .arg(equippedSkillCount));
        QStringList action, skill, all;
        bool actionError = false, skillError = false, anyError = false;
        for (const PalicoValidationIssue &issue : MhguSave::validatePalico(current)) {
            const bool error = issue.severity == PalicoIssueSeverity::Error;
            const QString line = QStringLiteral("%1 [%2] %3")
                .arg(error ? QStringLiteral("●") : QStringLiteral("⚠"), issue.code, issue.message);
            all << line; anyError = anyError || error;
            if (issue.field.contains(QStringLiteral("skill"), Qt::CaseInsensitive)) {
                skill << line; skillError = skillError || error;
            } else {
                action << line; actionError = actionError || error;
            }
        }
        auto show = [](QLabel *label, const QStringList &messages, bool hasError) {
            label->setText(messages.isEmpty() ? QStringLiteral("✓ 当前未发现规则问题") : messages.join(QLatin1Char('\n')));
            label->setStyleSheet(messages.isEmpty() ? QStringLiteral("color:#287A3D;") : hasError
                ? QStringLiteral("color:#C62828;font-weight:600;")
                : QStringLiteral("color:#9A5A00;font-weight:600;"));
        };
        show(m_actionDiagnostics, action, actionError); show(m_skillDiagnostics, skill, skillError);
        show(m_advancedDiagnostics, all, anyError);
    }
    void apply()
    {
        MhguPalico edited = candidate(); edited.name = m_name->text();
        edited.enthusiasm = quint8(m_enthusiasm->value()); edited.greeting = m_greeting->toPlainText();
        edited.nameGiver = m_nameGiver->text(); edited.previousOwner = m_previousOwner->text();
        edited.voice = quint8(m_appearance[0]->value()); edited.eyes = quint8(m_appearance[1]->value());
        edited.clothing = quint8(m_appearance[2]->value()); edited.coat = quint8(m_appearance[3]->value());
        edited.ears = quint8(m_appearance[4]->value()); edited.tail = quint8(m_appearance[5]->value());
        for (QLineEdit *color : m_colors) {
            if (!color->hasAcceptableInput()) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("RGBA 必须是 8 个十六进制字符。"));
                return;
            }
        }
        edited.coatColor = parseRgba(m_colors[0]->text()); edited.rightEyeColor = parseRgba(m_colors[1]->text());
        edited.leftEyeColor = parseRgba(m_colors[2]->text()); edited.vestColor = parseRgba(m_colors[3]->text());
        if (!m_save->setPalico(m_index, edited)) {
            QMessageBox::critical(this, windowTitle(), QStringLiteral("猫猫记录写入越界或索引无效。"));
            return;
        }
        accept();
    }
    MhguSave *m_save;
    GameData *m_data;
    int m_index;
    MhguPalico m_value;
    QLineEdit *m_name;
    QSpinBox *m_level;
    QLabel *m_exp;
    QComboBox *m_forte;
    QSpinBox *m_enthusiasm;
    QComboBox *m_target;
    QTextEdit *m_greeting;
    QLineEdit *m_nameGiver;
    QLineEdit *m_previousOwner;
    QSpinBox *m_appearance[6];
    QLineEdit *m_colors[4];
    QVector<GameDataEntry> m_moveEntries, m_skillEntries;
    CompactIdListEditor *m_equippedActions; CompactIdListEditor *m_equippedSkills;
    CompactIdListEditor *m_actionTransfers; CompactIdListEditor *m_skillTransfers;
    GroupedSlotEditor *m_actionGenerated; GroupedSlotEditor *m_skillGenerated;
    QComboBox *m_actionPattern;
    QComboBox *m_skillPattern;
    QComboBox *m_secondary;
    QLabel *m_actionFixed; QLabel *m_skillFixed; QLabel *m_actionState;
    QLabel *m_actionDiagnostics; QLabel *m_skillDiagnostics; QLabel *m_memoryUsage;
    QGroupBox *m_advanced;
    RawByteArrayEditor *m_rawEquippedActions; RawByteArrayEditor *m_rawEquippedSkills;
    RawByteArrayEditor *m_rawLearnedActions; RawByteArrayEditor *m_rawLearnedSkills;
    QSpinBox *m_rawActionPattern; QSpinBox *m_rawActionLength; QSpinBox *m_rawSkillPattern; QSpinBox *m_rawSkillLength;
    QLabel *m_advancedDiagnostics;
    bool m_ruleActive = false; bool m_updating = false;
};
}

CharacterDialog::CharacterDialog(MhguSave *save, QWidget *parent) : QWidget(parent), m_save(save)
{
    setObjectName(QStringLiteral("pageSurface"));
    m_name = new QLineEdit(this);
    for (int i = 0; i < 9; ++i) m_fields[i] = u32Edit(0, this);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("角色名（UTF-8，最多 31 字节）"), m_name);
    form->addRow(QStringLiteral("游玩时间（秒）"), m_fields[0]);
    form->addRow(QStringLiteral("金钱"), m_fields[1]);
    form->addRow(QStringLiteral("HR"), m_fields[8]);
    form->addRow(QStringLiteral("猎人等级点数"), m_fields[2]);
    form->addRow(QStringLiteral("龙识院点数"), m_fields[3]);
    form->addRow(QStringLiteral("贝尔纳村点数"), m_fields[4]);
    form->addRow(QStringLiteral("科科特村点数"), m_fields[5]);
    form->addRow(QStringLiteral("波凯村点数"), m_fields[6]);
    form->addRow(QStringLiteral("结云村点数"), m_fields[7]);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->addLayout(form);
    layout->addStretch();
    connect(m_name, &QLineEdit::textEdited, this, [this] { if (!m_loading) emit modified(); });
    for (QLineEdit *field : m_fields)
        connect(field, &QLineEdit::textEdited, this, [this] { if (!m_loading) emit modified(); });
    loadFromModel();
}

void CharacterDialog::loadFromModel()
{
    m_loading = true;
    const MhguCharacter c = m_save->character();
    m_name->setText(c.name);
    const quint32 values[] = {c.playTime, c.money, c.hunterRankPoints, c.academyPoints, c.bhernaPoints,
                              c.kokotoPoints, c.pokkePoints, c.yukumoPoints, c.hunterRank};
    for (int i = 0; i < 9; ++i) m_fields[i]->setText(QString::number(values[i]));
    m_loading = false;
}

bool CharacterDialog::commitToModel(QString *error)
{
    bool ok = true;
    quint32 values[9];
    for (int i = 0; i < 9; ++i) {
        bool fieldOk;
        values[i] = readU32(m_fields[i], &fieldOk);
        ok &= fieldOk;
    }
    if (!ok || values[8] > 0xFFFF) {
        if (error) *error = QStringLiteral("请输入有效的无符号数值；HR 最大为 65535。");
        return false;
    }
    MhguCharacter c;
    c.name = m_name->text();
    c.playTime = values[0]; c.money = values[1]; c.hunterRank = quint16(values[8]);
    c.hunterRankPoints = values[2]; c.academyPoints = values[3]; c.bhernaPoints = values[4];
    c.kokotoPoints = values[5]; c.pokkePoints = values[6]; c.yukumoPoints = values[7];
    if (!m_save->setCharacter(c)) {
        if (error) *error = QStringLiteral("角色字段写入失败。");
        return false;
    }
    return true;
}

ItemBoxDialog::ItemBoxDialog(MhguSave *save, GameData *data, QWidget *parent)
    : QWidget(parent), m_save(save), m_data(data)
{
    setObjectName(QStringLiteral("pageSurface"));
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({QStringLiteral("页"), QStringLiteral("格"), QStringLiteral("道具"), QStringLiteral("数量"), QStringLiteral("ID")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("搜索道具名称或 ID"));
    m_nonEmpty = new QCheckBox(QStringLiteral("只显示非空"), this);
    auto *edit = new QPushButton(QStringLiteral("编辑选中"), this);
    auto *add = new QPushButton(QStringLiteral("新增到第一个空位"), this);
    auto *exportButton = new QPushButton(QStringLiteral("导出表单"), this);
    auto *importButton = new QPushButton(QStringLiteral("导入表单"), this);
    connect(m_search, &QLineEdit::textChanged, this, &ItemBoxDialog::populate);
    connect(m_nonEmpty, &QCheckBox::toggled, this, &ItemBoxDialog::populate);
    connect(edit, &QPushButton::clicked, this, &ItemBoxDialog::editSelected);
    connect(add, &QPushButton::clicked, this, &ItemBoxDialog::addFirstEmpty);
    connect(exportButton, &QPushButton::clicked, this, &ItemBoxDialog::exportForm);
    connect(importButton, &QPushButton::clicked, this, &ItemBoxDialog::importForm);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelected(); });
    auto *filters = new QHBoxLayout;
    filters->addWidget(m_search, 1);
    filters->addWidget(m_nonEmpty);
    filters->addWidget(exportButton);
    filters->addWidget(importButton);
    filters->addWidget(add);
    filters->addWidget(edit);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addLayout(filters);
    layout->addWidget(m_table, 1);
    populate();
}

void ItemBoxDialog::loadFromModel() { populate(); }
bool ItemBoxDialog::commitToModel(QString *) { return m_save && m_save->selectedSlot() >= 0; }

void ItemBoxDialog::populate()
{
    const QVector<MhguItem> items = m_save->items();
    const QString search = m_search->text().trimmed();
    m_table->setRowCount(0);
    for (int i = 0; i < items.size(); ++i) {
        const MhguItem item = items[i];
        const QString name = m_data->name(QStringLiteral("items"), item.id);
        if (m_nonEmpty->isChecked() && item.id == 0) continue;
        if (!search.isEmpty() && !name.contains(search, Qt::CaseInsensitive) && QString::number(item.id) != search) continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *page = new QTableWidgetItem(QString::number(i / 100 + 1));
        page->setData(Qt::UserRole, i);
        m_table->setItem(row, 0, page);
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(i % 100 + 1)));
        m_table->setItem(row, 2, new QTableWidgetItem(name));
        m_table->setItem(row, 3, new QTableWidgetItem(QString::number(item.count)));
        m_table->setItem(row, 4, new QTableWidgetItem(QString::number(item.id)));
    }
}

void ItemBoxDialog::editSelected()
{
    const int row = m_table->currentRow();
    if (row < 0) return;
    const int index = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const MhguItem item = m_save->items().value(index);
    ItemEditDialog dialog(m_data, item, this);
    if (dialog.exec() == QDialog::Accepted) {
        if (!m_save->setItem(index, dialog.value())) QMessageBox::warning(this, QStringLiteral("道具箱"), QStringLiteral("道具值无效。"));
        else emit modified();
        populate();
    }
}

void ItemBoxDialog::addFirstEmpty()
{
    const QVector<MhguItem> items = m_save->items();
    for (int i = 0; i < items.size(); ++i) if (items[i].id == 0) {
        ItemEditDialog dialog(m_data, MhguItem{0, 1}, this);
        if (dialog.exec() == QDialog::Accepted && m_save->setItem(i, dialog.value())) emit modified();
        populate();
        return;
    }
    QMessageBox::information(this, windowTitle(), QStringLiteral("没有空道具格。"));
}

void ItemBoxDialog::exportForm()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出道具箱表单"),
        QStringLiteral("mhxx-gu-item-box.csv"), QStringLiteral("CSV 表单 (*.csv);;所有文件 (*)"));
    if (path.isEmpty()) return;
    const QByteArray csv = MhxxGuTransfer::exportItems(m_save->items());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(csv) != csv.size()) {
        QMessageBox::critical(this, windowTitle(), QStringLiteral("无法导出表单：%1").arg(file.errorString()));
        return;
    }
    QMessageBox::information(this, windowTitle(), QStringLiteral("已导出全部 2300 个道具格，可在 MHXX 与 MHGU 间导入。"));
}

void ItemBoxDialog::importForm()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入道具箱表单"), {},
        QStringLiteral("CSV 表单 (*.csv);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, windowTitle(), QStringLiteral("无法读取表单：%1").arg(file.errorString()));
        return;
    }
    QVector<MhxxGuTransfer::ItemUpdate> parsed;
    QString parseError;
    if (!MhxxGuTransfer::parseItems(file.readAll(), &parsed, &parseError)) {
        QMessageBox::warning(this, windowTitle(), parseError);
        return;
    }
    QVector<MhguItem> updated = m_save->items();
    for (const MhxxGuTransfer::ItemUpdate &update : parsed) updated[update.index] = update.value;
    if (!confirmImport(this, QStringLiteral("确认导入道具箱"),
            QStringLiteral("表单包含 %1 个道具格。\n\n导入会覆盖表单中列出的格子，未列出的格子保持不变。").arg(parsed.size()))) return;
    if (!m_save->setItems(updated)) {
        QMessageBox::critical(this, windowTitle(), QStringLiteral("批量写入道具箱失败。"));
        return;
    }
    emit modified();
    populate();
    QMessageBox::information(this, windowTitle(), QStringLiteral("道具箱已导入。请回到主窗口保存 system。"));
}

EquipmentBoxDialog::EquipmentBoxDialog(MhguSave *save, GameData *data, QWidget *parent)
    : QWidget(parent), m_save(save), m_data(data)
{
    setObjectName(QStringLiteral("pageSurface"));
    auto createTable = [](QWidget *parent) {
        auto *table = new QTableWidget(parent);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->hide();
        return table;
    };
    auto *tabs = new QTabWidget(this);
    auto *hunter = new QWidget(tabs);
    m_hunterTable = createTable(hunter);
    m_hunterTable->setColumnCount(7);
    m_hunterTable->setHorizontalHeaderLabels({QStringLiteral("格"), QStringLiteral("类型"), QStringLiteral("装备"), QStringLiteral("等级"), QStringLiteral("幻化"), QStringLiteral("装饰珠"), QStringLiteral("ID")});
    m_hunterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_hunterTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_hunterSearch = new QLineEdit(hunter);
    m_hunterSearch->setPlaceholderText(QStringLiteral("搜索名称或 ID"));
    m_hunterType = new QComboBox(hunter);
    m_hunterType->addItem(QStringLiteral("全部类型"), -1);
    for (const GameDataEntry &type : data->entries(QStringLiteral("equipment_types")))
        if (type.id > 0) m_hunterType->addItem(type.name, type.id);
    m_hunterNonEmpty = new QCheckBox(QStringLiteral("只显示非空"), hunter);
    auto *editHunterButton = new QPushButton(QStringLiteral("编辑选中"), hunter);
    auto *hunterFilters = new QHBoxLayout;
    hunterFilters->addWidget(m_hunterSearch, 1); hunterFilters->addWidget(m_hunterType); hunterFilters->addWidget(m_hunterNonEmpty); hunterFilters->addWidget(editHunterButton);
    auto *hunterLayout = new QVBoxLayout(hunter);
    hunterLayout->addLayout(hunterFilters); hunterLayout->addWidget(m_hunterTable);
    tabs->addTab(hunter, QStringLiteral("猎人装备"));

    auto *palico = new QWidget(tabs);
    m_palicoTable = createTable(palico);
    m_palicoTable->setColumnCount(5);
    m_palicoTable->setHorizontalHeaderLabels({QStringLiteral("格"), QStringLiteral("类型"), QStringLiteral("装备"), QStringLiteral("幻化"), QStringLiteral("ID")});
    m_palicoTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_palicoTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_palicoSearch = new QLineEdit(palico);
    m_palicoSearch->setPlaceholderText(QStringLiteral("搜索名称或 ID"));
    m_palicoType = new QComboBox(palico);
    m_palicoType->addItem(QStringLiteral("全部类型"), -1);
    m_palicoType->addItem(QStringLiteral("猫武器"), 22);
    m_palicoType->addItem(QStringLiteral("猫头部"), 23);
    m_palicoType->addItem(QStringLiteral("猫身体"), 24);
    m_palicoNonEmpty = new QCheckBox(QStringLiteral("只显示非空"), palico);
    auto *editPalicoButton = new QPushButton(QStringLiteral("编辑选中"), palico);
    auto *palicoFilters = new QHBoxLayout;
    palicoFilters->addWidget(m_palicoSearch, 1); palicoFilters->addWidget(m_palicoType); palicoFilters->addWidget(m_palicoNonEmpty); palicoFilters->addWidget(editPalicoButton);
    auto *palicoLayout = new QVBoxLayout(palico);
    palicoLayout->addLayout(palicoFilters); palicoLayout->addWidget(m_palicoTable);
    tabs->addTab(palico, QStringLiteral("猫装备"));

    auto *warning = new QLabel(QStringLiteral("防具幻化已验证可用 · MHGU 原版武器幻化不生效"), this);
    warning->setObjectName(QStringLiteral("warningLabel"));
    warning->setWordWrap(false);
    warning->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    warning->setMaximumHeight(34);
    warning->setToolTip(QStringLiteral("防具仅允许同部位幻化；武器外观编辑已停用。"));
    auto *exportButton = new QPushButton(QStringLiteral("导出装备箱表单"), this);
    auto *importButton = new QPushButton(QStringLiteral("导入装备箱表单"), this);
    connect(exportButton, &QPushButton::clicked, this, &EquipmentBoxDialog::exportForm);
    connect(importButton, &QPushButton::clicked, this, &EquipmentBoxDialog::importForm);
    connect(m_hunterSearch, &QLineEdit::textChanged, this, &EquipmentBoxDialog::populateHunter);
    connect(m_hunterType, qOverload<int>(&QComboBox::currentIndexChanged), this, &EquipmentBoxDialog::populateHunter);
    connect(m_hunterNonEmpty, &QCheckBox::toggled, this, &EquipmentBoxDialog::populateHunter);
    connect(m_palicoSearch, &QLineEdit::textChanged, this, &EquipmentBoxDialog::populatePalico);
    connect(m_palicoType, qOverload<int>(&QComboBox::currentIndexChanged), this, &EquipmentBoxDialog::populatePalico);
    connect(m_palicoNonEmpty, &QCheckBox::toggled, this, &EquipmentBoxDialog::populatePalico);
    connect(editHunterButton, &QPushButton::clicked, this, &EquipmentBoxDialog::editHunter);
    connect(editPalicoButton, &QPushButton::clicked, this, &EquipmentBoxDialog::editPalico);
    connect(m_hunterTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editHunter(); });
    connect(m_palicoTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editPalico(); });
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *actions = new QHBoxLayout;
    actions->addWidget(exportButton);
    actions->addWidget(importButton);
    actions->addStretch();
    layout->addWidget(warning); layout->addLayout(actions); layout->addWidget(tabs, 1);
    populateHunter(); populatePalico();
}

void EquipmentBoxDialog::loadFromModel() { populateHunter(); populatePalico(); }
bool EquipmentBoxDialog::commitToModel(QString *error)
{
    Q_UNUSED(error);
    // Game-side legality is deliberately advisory. Saving still relies on
    // MhguSave for file size, slot base, write bounds and field-width safety.
    return m_save && m_save->selectedSlot() >= 0;
}

void EquipmentBoxDialog::populateHunter()
{
    const int filterType = m_hunterType->currentData().toInt();
    const QString search = m_hunterSearch->text().trimmed();
    m_hunterTable->setRowCount(0);
    for (int i = 0; i < MhguSave::EquipmentCount; ++i) {
        const MhguEquipment entry = m_save->equipment(i);
        if (m_hunterNonEmpty->isChecked() && entry.type == 0) continue;
        if (filterType >= 0 && entry.type != filterType) continue;
        const QString table = m_data->equipmentTable(entry.type);
        const QString name = m_data->name(table, entry.id);
        if (!search.isEmpty() && !name.contains(search, Qt::CaseInsensitive) && QString::number(entry.id) != search) continue;
        const int row = m_hunterTable->rowCount(); m_hunterTable->insertRow(row);
        auto *slot = new QTableWidgetItem(QString::number(i + 1)); slot->setData(Qt::UserRole, i);
        m_hunterTable->setItem(row, 0, slot);
        m_hunterTable->setItem(row, 1, new QTableWidgetItem(m_data->name(QStringLiteral("equipment_types"), entry.type)));
        m_hunterTable->setItem(row, 2, new QTableWidgetItem(name));
        m_hunterTable->setItem(row, 3, new QTableWidgetItem(QString::number(entry.level)));
        const QString appearance = isWeaponType(entry.type) && entry.appearanceId
            ? QStringLiteral("不支持（ID %1）").arg(entry.appearanceId)
            : entry.appearanceId ? m_data->name(table, entry.appearanceId) : QStringLiteral("无");
        m_hunterTable->setItem(row, 4, new QTableWidgetItem(appearance));
        QStringList decorations; for (quint16 id : entry.decorations) if (id) decorations << m_data->name(QStringLiteral("decorations"), id);
        m_hunterTable->setItem(row, 5, new QTableWidgetItem(decorations.join(QStringLiteral(" / "))));
        m_hunterTable->setItem(row, 6, new QTableWidgetItem(QString::number(entry.id)));
    }
}

void EquipmentBoxDialog::populatePalico()
{
    const int filterType = m_palicoType->currentData().toInt();
    const QString search = m_palicoSearch->text().trimmed();
    m_palicoTable->setRowCount(0);
    for (int i = 0; i < MhguSave::PalicoEquipmentCount; ++i) {
        const MhguPalicoEquipment entry = m_save->palicoEquipment(i);
        if (m_palicoNonEmpty->isChecked() && entry.rawType == 0) continue;
        if (filterType >= 0 && entry.rawType != filterType) continue;
        const QString table = m_data->palicoEquipmentTable(entry.rawType);
        const QString name = m_data->name(table, entry.id);
        if (!search.isEmpty() && !name.contains(search, Qt::CaseInsensitive) && QString::number(entry.id) != search) continue;
        const int row = m_palicoTable->rowCount(); m_palicoTable->insertRow(row);
        auto *slot = new QTableWidgetItem(QString::number(i + 1)); slot->setData(Qt::UserRole, i);
        m_palicoTable->setItem(row, 0, slot);
        const QString type = entry.rawType == 22 ? QStringLiteral("猫武器") : entry.rawType == 23 ? QStringLiteral("猫头部") : entry.rawType == 24 ? QStringLiteral("猫身体") : QStringLiteral("空");
        m_palicoTable->setItem(row, 1, new QTableWidgetItem(type));
        m_palicoTable->setItem(row, 2, new QTableWidgetItem(name));
        const QString appearance = entry.rawType == 22 && entry.appearanceId
            ? QStringLiteral("不支持（ID %1）").arg(entry.appearanceId)
            : entry.appearanceId ? m_data->name(table, entry.appearanceId) : QStringLiteral("无");
        m_palicoTable->setItem(row, 3, new QTableWidgetItem(appearance));
        m_palicoTable->setItem(row, 4, new QTableWidgetItem(QString::number(entry.id)));
    }
}

void EquipmentBoxDialog::editHunter()
{
    const int row = m_hunterTable->currentRow(); if (row < 0) return;
    const int index = m_hunterTable->item(row, 0)->data(Qt::UserRole).toInt();
    HunterEquipmentEditDialog dialog(m_data, m_save->equipment(index), this);
    if (dialog.exec() == QDialog::Accepted) {
        QString warning;
        if (!m_save->setEquipment(index, dialog.value(), &warning)) QMessageBox::warning(this, QStringLiteral("装备箱"), QStringLiteral("装备值无效。"));
        else {
            emit modified();
            if (!warning.isEmpty()) QMessageBox::warning(this, QStringLiteral("穿戴缓存未同步"), warning);
        }
        populateHunter();
    }
}

void EquipmentBoxDialog::editPalico()
{
    const int row = m_palicoTable->currentRow(); if (row < 0) return;
    const int index = m_palicoTable->item(row, 0)->data(Qt::UserRole).toInt();
    PalicoEquipmentEditDialog dialog(m_data, m_save->palicoEquipment(index), this);
    if (dialog.exec() == QDialog::Accepted) {
        if (!m_save->setPalicoEquipment(index, dialog.value())) QMessageBox::warning(this, QStringLiteral("装备箱"), QStringLiteral("猫装备值无效。"));
        else emit modified();
        populatePalico();
    }
}

void EquipmentBoxDialog::exportForm()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出装备箱表单"),
        QStringLiteral("mhxx-gu-equipment-box.csv"), QStringLiteral("CSV 表单 (*.csv);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QVector<MhguEquipmentUpdate> hunter;
    QVector<MhguPalicoEquipmentUpdate> palico;
    hunter.reserve(MhguSave::EquipmentCount);
    palico.reserve(MhguSave::PalicoEquipmentCount);
    for (int i = 0; i < MhguSave::EquipmentCount; ++i) {
        MhguEquipmentUpdate row; row.index = i; row.value = m_save->equipment(i); hunter.push_back(row);
    }
    for (int i = 0; i < MhguSave::PalicoEquipmentCount; ++i) {
        MhguPalicoEquipmentUpdate row; row.index = i; row.value = m_save->palicoEquipment(i); palico.push_back(row);
    }
    const QByteArray csv = MhxxGuTransfer::exportEquipment(hunter, palico);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(csv) != csv.size()) {
        QMessageBox::critical(this, windowTitle(), QStringLiteral("无法导出表单：%1").arg(file.errorString()));
        return;
    }
    QMessageBox::information(this, windowTitle(), QStringLiteral("已导出 2000 个猎人装备格和 1000 个猫装备格，可在 MHXX 与 MHGU 间导入。"));
}

void EquipmentBoxDialog::importForm()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入装备箱表单"), {},
        QStringLiteral("CSV 表单 (*.csv);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, windowTitle(), QStringLiteral("无法读取表单：%1").arg(file.errorString()));
        return;
    }
    QVector<MhguEquipmentUpdate> hunter;
    QVector<MhguPalicoEquipmentUpdate> palico;
    QString parseError;
    if (!MhxxGuTransfer::parseEquipment(file.readAll(), &hunter, &palico, &parseError)) {
        QMessageBox::warning(this, windowTitle(), parseError);
        return;
    }
    QStringList legalityWarnings;
    for (const MhguEquipmentUpdate &entry : hunter) {
        QString validationError;
        if (!validateHunterEquipment(m_data, entry.value, &validationError))
            legalityWarnings.push_back(QStringLiteral("猎人装备格 %1：%2").arg(entry.index + 1).arg(validationError));
    }
    for (const MhguPalicoEquipmentUpdate &entry : palico) {
        QString validationError;
        if (!validatePalicoEquipment(m_data, entry.value, &validationError))
            legalityWarnings.push_back(QStringLiteral("猫装备格 %1：%2").arg(entry.index + 1).arg(validationError));
    }
    QString confirmation = QStringLiteral(
        "表单包含 %1 个装备格。\n\n导入会覆盖表单中列出的格子，未列出的格子保持不变。"
    ).arg(hunter.size() + palico.size());
    if (!legalityWarnings.isEmpty()) {
        confirmation += QStringLiteral(
            "\n\n检测到 %1 个可能不合法的组合，但程序不会阻止导入。请自行承担游戏重置、显示异常或崩溃风险。"
        ).arg(legalityWarnings.size());
        const int previewCount = std::min(3, legalityWarnings.size());
        for (int i = 0; i < previewCount; ++i) confirmation += QStringLiteral("\n• %1").arg(legalityWarnings[i]);
        if (legalityWarnings.size() > previewCount) confirmation += QStringLiteral("\n• ……");
    }
    if (!confirmImport(this, QStringLiteral("确认导入装备箱"), confirmation)) return;
    QStringList cacheWarnings;
    if (!m_save->setEquipmentBatch(hunter, palico, &cacheWarnings)) {
        QMessageBox::critical(this, windowTitle(), QStringLiteral("装备箱批量写入失败，存档内存未发生变化。"));
        return;
    }
    emit modified();
    populateHunter(); populatePalico();
    QString result = QStringLiteral("装备箱已导入。请回到主窗口保存 system。");
    if (!cacheWarnings.isEmpty()) {
        result += QStringLiteral("\n\n%1 条已穿戴装备未能安全同步：").arg(cacheWarnings.size());
        const int previewCount = std::min(3, cacheWarnings.size());
        for (int i = 0; i < previewCount; ++i) result += QStringLiteral("\n• %1").arg(cacheWarnings[i]);
        if (cacheWarnings.size() > previewCount) result += QStringLiteral("\n• ……");
    }
    QMessageBox::information(this, windowTitle(), result);
}

PalicoListDialog::PalicoListDialog(MhguSave *save, GameData *data, QWidget *parent)
    : QWidget(parent), m_save(save), m_data(data)
{
    setObjectName(QStringLiteral("pageSurface"));
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({QStringLiteral("槽"), QStringLiteral("名字"), QStringLiteral("等级"), QStringLiteral("倾向"), QStringLiteral("目标"), QStringLiteral("来源")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_showEmpty = new QCheckBox(QStringLiteral("显示空记录"), this);
    auto *edit = new QPushButton(QStringLiteral("编辑选中"), this);
    connect(m_showEmpty, &QCheckBox::toggled, this, &PalicoListDialog::populate);
    connect(edit, &QPushButton::clicked, this, &PalicoListDialog::editSelected);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelected(); });
    auto *top = new QHBoxLayout; top->addWidget(m_showEmpty); top->addStretch(); top->addWidget(edit);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(12, 12, 12, 12); layout->addLayout(top); layout->addWidget(m_table);
    populate();
}

void PalicoListDialog::loadFromModel() { populate(); }
bool PalicoListDialog::commitToModel(QString *) { return m_save && m_save->selectedSlot() >= 0; }

void PalicoListDialog::populate()
{
    m_table->setRowCount(0);
    for (int i = 0; i < MhguSave::PalicoCount; ++i) {
        const MhguPalico cat = m_save->palico(i);
        if (!m_showEmpty->isChecked() && cat.name.isEmpty()) continue;
        const int row = m_table->rowCount(); m_table->insertRow(row);
        auto *slot = new QTableWidgetItem(QString::number(i + 1)); slot->setData(Qt::UserRole, i);
        m_table->setItem(row, 0, slot);
        m_table->setItem(row, 1, new QTableWidgetItem(cat.name.isEmpty() ? QStringLiteral("(空)") : cat.name));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(cat.level)));
        m_table->setItem(row, 3, new QTableWidgetItem(m_data->name(QStringLiteral("palico_fortes"), cat.forte)));
        m_table->setItem(row, 4, new QTableWidgetItem(m_data->name(QStringLiteral("palico_targets"), cat.target)));
        m_table->setItem(row, 5, new QTableWidgetItem(cat.received ? QStringLiteral("外来/联动") : QStringLiteral("本地")));
    }
}

void PalicoListDialog::editSelected()
{
    const int row = m_table->currentRow(); if (row < 0) return;
    const int index = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    PalicoEditDialog dialog(m_save, m_data, index, this);
    if (dialog.exec() == QDialog::Accepted) { populate(); emit modified(); }
}
