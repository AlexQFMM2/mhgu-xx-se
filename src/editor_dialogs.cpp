#include "editor_dialogs.hpp"

#include "game_data.hpp"
#include "mhgu_save.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
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
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace {
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

QString palicoLegality(GameData *data, const MhguPalico &cat)
{
    QStringList issues;
    QString coreIssue;
    if (!MhguSave::validatePalico(cat, &coreIssue)) issues << coreIssue;
    auto checkPattern = [data, &issues](const QString &kind, const QString &table,
                                        const auto &values, int patternId, const QString &label) {
        QString sequence;
        for (quint8 id : values) {
            if (!id) continue;
            const int tier = data->entry(table, id).generationTier;
            if (tier > 0) sequence += QString::number(tier);
        }
        if (sequence.isEmpty()) return;
        bool matches = false;
        for (const PalicoPattern &pattern : data->patterns(kind))
            if (pattern.id == patternId && pattern.sequence == sequence) matches = true;
        if (!matches) issues << QStringLiteral("%1 generation tier 序列与模式 %2 不一致。").arg(label).arg(patternId);
    };
    checkPattern(QStringLiteral("move"), QStringLiteral("palico_support_moves"), cat.learnedActions, cat.actionPattern, QStringLiteral("行动"));
    checkPattern(QStringLiteral("skill"), QStringLiteral("palico_skills"), cat.learnedSkills, cat.skillPattern, QStringLiteral("技能"));
    return issues.join(QStringLiteral("\n"));
}

class ItemEditDialog : public QDialog {
public:
    ItemEditDialog(GameData *data, const MhguItem &item, QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("编辑道具格"));
        m_item = new QComboBox(this);
        configureCombo(m_item);
        QVector<GameDataEntry> entries = data->entries(QStringLiteral("items"));
        if (entries.isEmpty() || entries.first().id != 0)
            entries.prepend(GameDataEntry{0, QStringLiteral("无"), QStringLiteral("None"), {}});
        fillCombo(m_item, entries, item.id);
        m_count = new QSpinBox(this);
        m_count->setRange(0, 99);
        m_count->setValue(item.count);
        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("道具"), m_item);
        form->addRow(QStringLiteral("数量"), m_count);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
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
        m_warning->setWordWrap(true);
        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("装备类型"), m_type);
        form->addRow(QStringLiteral("实际装备"), m_id);
        form->addRow(QStringLiteral("等级（存档值）"), m_level);
        form->addRow(QStringLiteral("幻化【测试】"), m_appearance);
        form->addRow(QStringLiteral("装饰珠 1"), m_decorations[0]);
        form->addRow(QStringLiteral("装饰珠 2"), m_decorations[1]);
        form->addRow(QStringLiteral("装饰珠 3"), m_decorations[2]);
        m_talisman = new QGroupBox(QStringLiteral("护石属性"), this);
        auto *talismanForm = new QFormLayout(m_talisman);
        talismanForm->addRow(QStringLiteral("技能 1"), m_skill1);
        talismanForm->addRow(QStringLiteral("技能点 1"), m_skill1Points);
        talismanForm->addRow(QStringLiteral("技能 2"), m_skill2);
        talismanForm->addRow(QStringLiteral("技能点 2"), m_skill2Points);
        talismanForm->addRow(QStringLiteral("孔数"), m_slots);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateType(); });
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_warning);
        layout->addLayout(form);
        layout->addWidget(m_talisman);
        layout->addWidget(buttons);
        updateType();
        resize(650, 700);
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
    void updateType()
    {
        const int type = m_type->currentData().toInt();
        const QString table = m_data->equipmentTable(type);
        fillCombo(m_id, m_data->entries(table), type == m_original.type ? m_original.id : 0);
        const bool testAppearance = (type >= 1 && type <= 5) || (type >= 7 && type <= 20);
        fillCombo(m_appearance, m_data->entries(table), type == m_original.type ? m_original.appearanceId : 0);
        m_appearance->setEnabled(testAppearance);
        m_talisman->setVisible(type == 6);
        if (type >= 7 && type <= 20)
            m_warning->setText(QStringLiteral("⚠ 武器幻化【测试】：存档中存在外观字段，但游戏可能忽略、还原或错误显示。仅可选择同一武器种类。"));
        else if (type >= 1 && type <= 5)
            m_warning->setText(QStringLiteral("防具幻化【测试】：外观选择被限制在相同防具部位。"));
        else m_warning->setText(QStringLiteral("此装备类型不使用幻化。"));
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
        m_warning->setWordWrap(true);
        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("类型"), m_type);
        form->addRow(QStringLiteral("实际装备"), m_id);
        form->addRow(QStringLiteral("幻化【测试】"), m_appearance);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
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
        result.appearanceId = result.rawType ? quint16(m_appearance->currentData().toUInt()) : 0;
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
        m_appearance->setEnabled(type != 0);
        if (type == 22)
            m_warning->setText(QStringLiteral("⚠ 猫武器幻化【测试】：游戏可能忽略、还原或错误显示该外观字段。"));
        else if (type == 23 || type == 24)
            m_warning->setText(QStringLiteral("猫防具幻化【测试】：外观选择被限制在相同部位。"));
        else m_warning->setText(QStringLiteral("空装备格。"));
    }
    GameData *m_data;
    MhguPalicoEquipment m_original;
    QComboBox *m_type;
    QComboBox *m_id;
    QComboBox *m_appearance;
    QLabel *m_warning;
};

class IdArrayEditor : public QWidget {
public:
    IdArrayEditor(const QString &title, const QVector<GameDataEntry> &entries, int count,
                  const quint8 *values, QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *group = new QGroupBox(title, this);
        auto *grid = new QGridLayout(group);
        for (int i = 0; i < count; ++i) {
            auto *combo = new QComboBox(group);
            configureCombo(combo);
            fillCombo(combo, entries, values[i]);
            grid->addWidget(new QLabel(QString::number(i + 1), group), i / 2, (i % 2) * 2);
            grid->addWidget(combo, i / 2, (i % 2) * 2 + 1);
            m_combos.push_back(combo);
        }
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(group);
    }
    QVector<quint8> values() const
    {
        QVector<quint8> result;
        for (QComboBox *combo : m_combos) result.push_back(quint8(combo->currentData().toUInt()));
        return result;
    }
private:
    QVector<QComboBox *> m_combos;
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

        auto *actions = new QWidget(tabs);
        auto *actionsLayout = new QVBoxLayout(actions);
        m_learnedActions = new IdArrayEditor(QStringLiteral("已学支援行动（最多 10 个）"), data->entries(QStringLiteral("palico_support_moves")), 16, m_value.learnedActions.data(), actions);
        m_equippedActions = new IdArrayEditor(QStringLiteral("已装备支援行动（随等级开放，最多 6 个）"), data->entries(QStringLiteral("palico_support_moves")), 8, m_value.equippedActions.data(), actions);
        actionsLayout->addWidget(m_learnedActions);
        actionsLayout->addWidget(m_equippedActions);
        actionsLayout->addStretch();
        auto *actionsScroll = new QScrollArea(tabs);
        actionsScroll->setWidgetResizable(true);
        actionsScroll->setWidget(actions);
        tabs->addTab(actionsScroll, QStringLiteral("支援行动"));

        auto *skills = new QWidget(tabs);
        auto *skillsLayout = new QVBoxLayout(skills);
        m_learnedSkills = new IdArrayEditor(QStringLiteral("已学被动技能（最多 8 个）"), data->entries(QStringLiteral("palico_skills")), 12, m_value.learnedSkills.data(), skills);
        m_equippedSkills = new IdArrayEditor(QStringLiteral("已装备被动技能（最多 4 个）"), data->entries(QStringLiteral("palico_skills")), 8, m_value.equippedSkills.data(), skills);
        skillsLayout->addWidget(m_learnedSkills);
        skillsLayout->addWidget(m_equippedSkills);
        skillsLayout->addStretch();
        auto *skillsScroll = new QScrollArea(tabs);
        skillsScroll->setWidgetResizable(true);
        skillsScroll->setWidget(skills);
        tabs->addTab(skillsScroll, QStringLiteral("被动技能"));

        auto *advanced = new QWidget(tabs);
        auto *advancedForm = new QFormLayout(advanced);
        m_actionPattern = new QComboBox(advanced);
        for (const PalicoPattern &pattern : data->patterns(QStringLiteral("move")))
            m_actionPattern->addItem(QStringLiteral("模式 %1 · %2").arg(pattern.id).arg(pattern.sequence), pattern.id);
        m_actionPattern->setCurrentIndex(m_actionPattern->findData(m_value.actionPattern));
        if (m_actionPattern->currentIndex() < 0) {
            m_actionPattern->addItem(QStringLiteral("未知模式 %1").arg(m_value.actionPattern), m_value.actionPattern);
            m_actionPattern->setCurrentIndex(m_actionPattern->count() - 1);
        }
        m_skillPattern = new QComboBox(advanced);
        for (const PalicoPattern &pattern : data->patterns(QStringLiteral("skill")))
            m_skillPattern->addItem(QStringLiteral("模式 %1 · %2").arg(pattern.id).arg(pattern.sequence), pattern.id);
        m_skillPattern->setCurrentIndex(m_skillPattern->findData(m_value.skillPattern));
        if (m_skillPattern->currentIndex() < 0) {
            m_skillPattern->addItem(QStringLiteral("未知模式 %1").arg(m_value.skillPattern), m_value.skillPattern);
            m_skillPattern->setCurrentIndex(m_skillPattern->count() - 1);
        }
        m_grants = new QLabel(advanced);
        m_grants->setWordWrap(true);
        advancedForm->addRow(QStringLiteral("倾向固有项"), m_grants);
        advancedForm->addRow(QStringLiteral("行动生成规律"), m_actionPattern);
        advancedForm->addRow(QStringLiteral("行动种子（保留）"), new QLabel(QStringLiteral("0x%1").arg(m_value.actionSeed, 2, 16, QLatin1Char('0')).toUpper(), advanced));
        advancedForm->addRow(QStringLiteral("技能生成规律"), m_skillPattern);
        advancedForm->addRow(QStringLiteral("技能种子（保留）"), new QLabel(QStringLiteral("0x%1").arg(m_value.skillSeed, 2, 16, QLatin1Char('0')).toUpper(), advanced));
        auto *advancedWarning = new QLabel(QStringLiteral("高级设置会改变猫猫的生成结构。固有行动/技能可在对应的已学池前置槽位中编辑；保存前会检查数量、连续排列和装备子集。"), advanced);
        advancedWarning->setObjectName(QStringLiteral("warningLabel"));
        advancedWarning->setWordWrap(true);
        advancedForm->addRow(advancedWarning);
        tabs->addTab(advanced, QStringLiteral("高级设置"));

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("应用修改"));
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { apply(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_level, qOverload<int>(&QSpinBox::valueChanged), this, [this](int level) {
            m_exp->setText(QString::number(MhguSave::experienceForLevel(level)));
        });
        connect(m_forte, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateGrants(); });
        m_exp->setText(QString::number(MhguSave::experienceForLevel(m_value.level)));
        updateGrants();

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(tabs);
        layout->addWidget(buttons);
        resize(880, 760);
    }
private:
    void updateGrants()
    {
        const int forte = m_forte->currentData().toInt();
        QStringList moves, skills;
        for (const PalicoForteGrant &grant : m_data->forteGrants(forte, QStringLiteral("move")))
            moves << m_data->name(QStringLiteral("palico_support_moves"), grant.entryId);
        for (const PalicoForteGrant &grant : m_data->forteGrants(forte, QStringLiteral("skill")))
            skills << m_data->name(QStringLiteral("palico_skills"), grant.entryId);
        m_grants->setText(QStringLiteral("固有行动：%1\n固有技能：%2").arg(moves.join(QStringLiteral("、")), skills.join(QStringLiteral("、"))));
    }
    void apply()
    {
        m_value.name = m_name->text();
        m_value.level = quint8(m_level->value());
        m_value.forte = quint8(m_forte->currentData().toUInt());
        m_value.enthusiasm = quint8(m_enthusiasm->value());
        m_value.target = quint8(m_target->currentData().toUInt());
        m_value.greeting = m_greeting->toPlainText();
        m_value.nameGiver = m_nameGiver->text();
        m_value.previousOwner = m_previousOwner->text();
        const QVector<quint8> ea = m_equippedActions->values(), es = m_equippedSkills->values();
        const QVector<quint8> la = m_learnedActions->values(), ls = m_learnedSkills->values();
        std::copy(ea.begin(), ea.end(), m_value.equippedActions.begin());
        std::copy(es.begin(), es.end(), m_value.equippedSkills.begin());
        std::copy(la.begin(), la.end(), m_value.learnedActions.begin());
        std::copy(ls.begin(), ls.end(), m_value.learnedSkills.begin());
        m_value.actionPattern = quint8(m_actionPattern->currentData().toUInt());
        m_value.skillPattern = quint8(m_skillPattern->currentData().toUInt());
        m_value.voice = quint8(m_appearance[0]->value());
        m_value.eyes = quint8(m_appearance[1]->value());
        m_value.clothing = quint8(m_appearance[2]->value());
        m_value.coat = quint8(m_appearance[3]->value());
        m_value.ears = quint8(m_appearance[4]->value());
        m_value.tail = quint8(m_appearance[5]->value());
        for (QLineEdit *color : m_colors) {
            if (!color->hasAcceptableInput()) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("RGBA 必须是 8 个十六进制字符。"));
                return;
            }
        }
        m_value.coatColor = parseRgba(m_colors[0]->text());
        m_value.rightEyeColor = parseRgba(m_colors[1]->text());
        m_value.leftEyeColor = parseRgba(m_colors[2]->text());
        m_value.vestColor = parseRgba(m_colors[3]->text());
        QString advisory;
        if (!m_save->setPalico(m_index, m_value, &advisory)) {
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
    IdArrayEditor *m_equippedActions;
    IdArrayEditor *m_equippedSkills;
    IdArrayEditor *m_learnedActions;
    IdArrayEditor *m_learnedSkills;
    QComboBox *m_actionPattern;
    QComboBox *m_skillPattern;
    QLabel *m_grants;
};
}

CharacterDialog::CharacterDialog(MhguSave *save, QWidget *parent) : QDialog(parent), m_save(save)
{
    setWindowTitle(QStringLiteral("角色信息"));
    const MhguCharacter c = save->character();
    m_name = new QLineEdit(c.name, this);
    const quint32 values[] = {c.playTime, c.money, c.hunterRankPoints, c.academyPoints, c.bhernaPoints,
                              c.kokotoPoints, c.pokkePoints, c.yukumoPoints};
    for (int i = 0; i < 8; ++i) m_fields[i] = u32Edit(values[i], this);
    m_fields[8] = u32Edit(c.hunterRank, this);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("角色名（UTF-8，最多 31 字节）"), m_name);
    form->addRow(QStringLiteral("游玩时间（秒）"), m_fields[0]);
    form->addRow(QStringLiteral("金钱"), m_fields[1]);
    form->addRow(QStringLiteral("HR"), m_fields[8]);
    form->addRow(QStringLiteral("HR Points"), m_fields[2]);
    form->addRow(QStringLiteral("Academy Points"), m_fields[3]);
    form->addRow(QStringLiteral("Bherna Points"), m_fields[4]);
    form->addRow(QStringLiteral("Kokoto Points"), m_fields[5]);
    form->addRow(QStringLiteral("Pokke Points"), m_fields[6]);
    form->addRow(QStringLiteral("Yukumo Points"), m_fields[7]);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("应用修改"));
    connect(buttons, &QDialogButtonBox::accepted, this, &CharacterDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    resize(620, 520);
}

void CharacterDialog::apply()
{
    bool ok = true;
    quint32 values[9];
    for (int i = 0; i < 9; ++i) {
        bool fieldOk;
        values[i] = readU32(m_fields[i], &fieldOk);
        ok &= fieldOk;
    }
    if (!ok || values[8] > 0xFFFF) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("请输入有效的无符号数值；HR 最大为 65535。"));
        return;
    }
    MhguCharacter c;
    c.name = m_name->text();
    c.playTime = values[0]; c.money = values[1]; c.hunterRank = quint16(values[8]);
    c.hunterRankPoints = values[2]; c.academyPoints = values[3]; c.bhernaPoints = values[4];
    c.kokotoPoints = values[5]; c.pokkePoints = values[6]; c.yukumoPoints = values[7];
    if (!m_save->setCharacter(c)) QMessageBox::critical(this, windowTitle(), QStringLiteral("角色字段写入失败。"));
    else accept();
}

ItemBoxDialog::ItemBoxDialog(MhguSave *save, GameData *data, QWidget *parent)
    : QDialog(parent), m_save(save), m_data(data)
{
    setWindowTitle(QStringLiteral("道具箱"));
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
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    connect(m_search, &QLineEdit::textChanged, this, &ItemBoxDialog::populate);
    connect(m_nonEmpty, &QCheckBox::toggled, this, &ItemBoxDialog::populate);
    connect(edit, &QPushButton::clicked, this, &ItemBoxDialog::editSelected);
    connect(add, &QPushButton::clicked, this, &ItemBoxDialog::addFirstEmpty);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelected(); });
    auto *filters = new QHBoxLayout;
    filters->addWidget(m_search, 1);
    filters->addWidget(m_nonEmpty);
    filters->addWidget(add);
    filters->addWidget(edit);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(filters);
    layout->addWidget(m_table, 1);
    layout->addWidget(close, 0, Qt::AlignRight);
    resize(1050, 720);
    populate();
}

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
        if (!m_save->setItem(index, dialog.value())) QMessageBox::warning(this, windowTitle(), QStringLiteral("道具值无效。"));
        populate();
    }
}

void ItemBoxDialog::addFirstEmpty()
{
    const QVector<MhguItem> items = m_save->items();
    for (int i = 0; i < items.size(); ++i) if (items[i].id == 0) {
        ItemEditDialog dialog(m_data, MhguItem{0, 1}, this);
        if (dialog.exec() == QDialog::Accepted) m_save->setItem(i, dialog.value());
        populate();
        return;
    }
    QMessageBox::information(this, windowTitle(), QStringLiteral("没有空道具格。"));
}

EquipmentBoxDialog::EquipmentBoxDialog(MhguSave *save, GameData *data, QWidget *parent)
    : QDialog(parent), m_save(save), m_data(data)
{
    setWindowTitle(QStringLiteral("装备箱 · 幻化【测试】"));
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
    m_hunterTable->setHorizontalHeaderLabels({QStringLiteral("格"), QStringLiteral("类型"), QStringLiteral("装备"), QStringLiteral("等级"), QStringLiteral("幻化【测试】"), QStringLiteral("装饰珠"), QStringLiteral("ID")});
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
    m_palicoTable->setHorizontalHeaderLabels({QStringLiteral("格"), QStringLiteral("类型"), QStringLiteral("装备"), QStringLiteral("幻化【测试】"), QStringLiteral("ID")});
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

    auto *warning = new QLabel(QStringLiteral("⚠ 所有幻化功能均为【测试】。武器幻化尤其可能被游戏忽略或显示异常；请自行保留原始存档。"), this);
    warning->setObjectName(QStringLiteral("warningLabel"));
    warning->setWordWrap(true);
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
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
    layout->addWidget(warning); layout->addWidget(tabs, 1); layout->addWidget(close, 0, Qt::AlignRight);
    resize(1200, 780);
    populateHunter(); populatePalico();
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
        m_hunterTable->setItem(row, 4, new QTableWidgetItem(entry.appearanceId ? m_data->name(table, entry.appearanceId) : QStringLiteral("无")));
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
        m_palicoTable->setItem(row, 3, new QTableWidgetItem(entry.appearanceId ? m_data->name(table, entry.appearanceId) : QStringLiteral("无")));
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
        if (!m_save->setEquipment(index, dialog.value(), &warning)) QMessageBox::warning(this, windowTitle(), QStringLiteral("装备值无效。"));
        else if (!warning.isEmpty()) QMessageBox::warning(this, QStringLiteral("穿戴缓存未同步"), warning);
        populateHunter();
    }
}

void EquipmentBoxDialog::editPalico()
{
    const int row = m_palicoTable->currentRow(); if (row < 0) return;
    const int index = m_palicoTable->item(row, 0)->data(Qt::UserRole).toInt();
    PalicoEquipmentEditDialog dialog(m_data, m_save->palicoEquipment(index), this);
    if (dialog.exec() == QDialog::Accepted) {
        if (!m_save->setPalicoEquipment(index, dialog.value())) QMessageBox::warning(this, windowTitle(), QStringLiteral("猫装备值无效。"));
        populatePalico();
    }
}

PalicoListDialog::PalicoListDialog(MhguSave *save, GameData *data, QWidget *parent)
    : QDialog(parent), m_save(save), m_data(data)
{
    setWindowTitle(QStringLiteral("猫猫"));
    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({QStringLiteral("槽"), QStringLiteral("名字"), QStringLiteral("等级"), QStringLiteral("倾向"), QStringLiteral("目标"), QStringLiteral("来源"), QStringLiteral("规则检测")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_showEmpty = new QCheckBox(QStringLiteral("显示空记录"), this);
    auto *edit = new QPushButton(QStringLiteral("编辑选中"), this);
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    connect(m_showEmpty, &QCheckBox::toggled, this, &PalicoListDialog::populate);
    connect(edit, &QPushButton::clicked, this, &PalicoListDialog::editSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelected(); });
    auto *top = new QHBoxLayout; top->addWidget(m_showEmpty); top->addStretch(); top->addWidget(edit);
    auto *layout = new QVBoxLayout(this); layout->addLayout(top); layout->addWidget(m_table); layout->addWidget(close, 0, Qt::AlignRight);
    resize(1000, 700);
    populate();
}

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
        const QString legality = palicoLegality(m_data, cat);
        auto *status = new QTableWidgetItem(legality.isEmpty() ? QStringLiteral("合法") : QStringLiteral("不合法"));
        status->setForeground(legality.isEmpty() ? QColor(QStringLiteral("#17643a")) : QColor(QStringLiteral("#b45309")));
        status->setToolTip(legality.isEmpty() ? QStringLiteral("符合当前已知规则") : legality);
        m_table->setItem(row, 6, status);
    }
}

void PalicoListDialog::editSelected()
{
    const int row = m_table->currentRow(); if (row < 0) return;
    const int index = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    PalicoEditDialog dialog(m_save, m_data, index, this);
    if (dialog.exec() == QDialog::Accepted) populate();
}
