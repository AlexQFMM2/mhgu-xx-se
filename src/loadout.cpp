#include "loadout.hpp"

#include "game_data.hpp"
#include "auto_loadout_dialog.hpp"
#include "loadout_format.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
const char *SlotKeys[] = {"weapon", "head", "chest", "arms", "waist", "legs", "charm"};
const char *SlotNames[] = {"武器", "头", "胸", "腕", "腰", "腿", "护石"};
bool matchesSlot(int slot, int type)
{
    if (slot == 0) return type >= 7 && type <= 21 && type != 12;
    if (slot >= 1 && slot <= 5) return type == slot;
    return type == 6;
}
QJsonObject equipmentJson(const MhguEquipment &value)
{
    return {{QStringLiteral("save_type"), int(value.type)}, {QStringLiteral("save_id"), int(value.id)},
        {QStringLiteral("record_hex"), QString::fromLatin1(value.record.toHex())}};
}
class DecorationDialog final : public QDialog {
public:
    DecorationDialog(GameData *data, const MhguEquipment &value, QWidget *parent = nullptr)
        : QDialog(parent), m_data(data), m_value(value)
    {
        setWindowTitle(QStringLiteral("装饰珠 · 严格合法模式"));
        auto *root = new QHBoxLayout;
        auto *left = new QVBoxLayout;
        left->addWidget(new QLabel(QStringLiteral("可用装饰珠（名称）"), this));
        m_candidates = new QListWidget(this);
        for (const GameDataEntry &entry : data->entries(QStringLiteral("decorations"))) {
            bool found = false;
            const int cost = data->decorationSlotCost(entry.id, &found);
            if (!entry.id || !found || cost <= 0) continue;
            auto *item = new QListWidgetItem(QStringLiteral("%1  ·  %2 孔").arg(entry.name).arg(cost), m_candidates);
            item->setData(Qt::UserRole, entry.id);
        }
        left->addWidget(m_candidates, 1);
        auto *middle = new QVBoxLayout;
        middle->addStretch();
        m_circles = new QLabel(this);
        m_circles->setAlignment(Qt::AlignCenter);
        QFont circleFont = m_circles->font(); circleFont.setPointSize(circleFont.pointSize() + 5); m_circles->setFont(circleFont);
        middle->addWidget(m_circles);
        auto *add = new QPushButton(QStringLiteral("→ 加入"), this);
        auto *remove = new QPushButton(QStringLiteral("← 移除"), this);
        add->setMinimumHeight(32); remove->setMinimumHeight(32);
        middle->addWidget(add); middle->addWidget(remove); middle->addStretch();
        auto *right = new QVBoxLayout;
        right->addWidget(new QLabel(QStringLiteral("已安装珠"), this));
        m_installed = new QListWidget(this); right->addWidget(m_installed, 1);
        root->addLayout(left, 5); root->addLayout(middle, 2); root->addLayout(right, 4);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        auto *container = new QVBoxLayout;
        // Reparent the horizontal layout into a host so the standard action
        // row stays at the bottom without changing the requested 3-column UI.
        auto *host = new QWidget(this); host->setLayout(root);
        container->addWidget(host, 1); container->addWidget(buttons); setLayout(container);
        connect(add, &QPushButton::clicked, this, [this] { addSelected(); });
        connect(remove, &QPushButton::clicked, this, [this] { removeSelected(); });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        refresh(); resize(760, 460);
    }
    MhguEquipment value() const { return m_value; }
private:
    int capacity() const {
        bool found = false;
        if (m_value.type >= 1 && m_value.type <= 5) return m_data->armorSlots(m_value.type, m_value.id, &found);
        if (m_value.type == 6) return m_value.talismanSlots;
        return m_data->weaponSlots(m_value.type, m_value.id, m_value.level, &found);
    }
    int used() const {
        int result = 0;
        for (quint16 id : m_value.decorations) if (id) result += m_data->decorationSlotCost(id);
        return result;
    }
    void addSelected() {
        const QListWidgetItem *item = m_candidates->currentItem();
        if (!item) return;
        int empty = -1;
        for (int i = 0; i < 3; ++i) if (!m_value.decorations[size_t(i)]) { empty = i; break; }
        const int id = item->data(Qt::UserRole).toInt();
        const int cost = m_data->decorationSlotCost(id);
        if (empty < 0 || used() + cost > capacity()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("该珠会超过此装备自身的天然孔位，已拒绝加入。"));
            return;
        }
        MhguEquipment next = m_value; next.decorations[size_t(empty)] = quint16(id);
        QString error;
        if (!m_data->validateHunterEquipment(next, &error)) {
            QMessageBox::warning(this, windowTitle(), error); return;
        }
        m_value = next; refresh();
    }
    void removeSelected() {
        const int row = m_installed->currentRow();
        if (row < 0) return;
        std::array<quint16, 3> next{{0, 0, 0}};
        int target = 0, visible = 0;
        for (quint16 id : m_value.decorations) {
            if (!id) continue;
            if (visible++ == row) continue;
            next[size_t(target++)] = id;
        }
        m_value.decorations = next; refresh();
    }
    void refresh() {
        m_installed->clear();
        for (quint16 id : m_value.decorations) if (id)
            m_installed->addItem(m_data->name(QStringLiteral("decorations"), id));
        QStringList states;
        const int filled = used(), total = capacity();
        for (int i = 0; i < 3; ++i)
            states << (i < filled ? QStringLiteral("●") : i < total ? QStringLiteral("○") : QStringLiteral("⊘"));
        m_circles->setText(states.join(QLatin1Char(' ')));
        m_circles->setToolTip(QStringLiteral("○ 可装孔 / ● 已占孔 / ⊘ 不可装孔"));
    }
    GameData *m_data;
    MhguEquipment m_value;
    QListWidget *m_candidates = nullptr;
    QListWidget *m_installed = nullptr;
    QLabel *m_circles = nullptr;
};
}

LoadoutPage::LoadoutPage(MhguSave *save, GameData *data, QWidget *parent)
    : QWidget(parent), m_save(save), m_data(data)
{
    setObjectName(QStringLiteral("pageSurface"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *hint = new QLabel(QStringLiteral("从当前装备箱选择七件已通过严格合法性校验的装备。不会生成非法护石或超孔珠。"), this);
    hint->setObjectName(QStringLiteral("warningLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *group = new QGroupBox(QStringLiteral("手动配装"), this);
    auto *form = new QFormLayout(group);
    m_name = new QLineEdit(QStringLiteral("我的 MHGU 配装"), group);
    m_loadoutGender = new QComboBox(group); m_loadoutGender->addItem(QStringLiteral("男性"), QStringLiteral("male")); m_loadoutGender->addItem(QStringLiteral("女性"), QStringLiteral("female"));
    form->addRow(QStringLiteral("配装名称"), m_name);
    form->addRow(QStringLiteral("性别"), m_loadoutGender);
    for (int slot = 0; slot < 7; ++slot) {
        auto *row = new QWidget(group);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        m_slots[slot] = new QComboBox(row);
        m_slots[slot]->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_circleLabels[slot] = new QLabel(QStringLiteral("⊘ ⊘ ⊘"), row);
        m_circleLabels[slot]->setMinimumWidth(86);
        rowLayout->addWidget(m_slots[slot], 1);
        rowLayout->addWidget(m_circleLabels[slot]);
        auto *jewels = new QPushButton(QStringLiteral("装珠"), row);
        jewels->setMinimumHeight(28); jewels->setMaximumHeight(30);
        rowLayout->addWidget(jewels);
        form->addRow(QString::fromUtf8(SlotNames[slot]), row);
        connect(m_slots[slot], qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, slot] { slotChanged(slot); });
        connect(jewels, &QPushButton::clicked, this, [this, slot] { editDecorations(slot); });
    }
    layout->addWidget(group);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    auto *actions = new QHBoxLayout;
    auto *automatic = new QPushButton(QStringLiteral("自动配装"), this);
    auto *clear = new QPushButton(QStringLiteral("清空"), this);
    auto *exportButton = new QPushButton(QStringLiteral("导出配装 JSON"), this);
    auto *importButton = new QPushButton(QStringLiteral("导入配装 JSON"), this);
    m_append = new QPushButton(QStringLiteral("添加到装备箱"), this);
    m_append->setObjectName(QStringLiteral("primaryButton"));
    actions->addWidget(automatic);
    actions->addWidget(clear);
    actions->addStretch();
    actions->addWidget(exportButton);
    actions->addWidget(importButton);
    actions->addWidget(m_append);
    layout->addLayout(actions);
    layout->addStretch();
    connect(automatic, &QPushButton::clicked, this, &LoadoutPage::openAutomatic);
    connect(clear, &QPushButton::clicked, this, &LoadoutPage::clearLoadout);
    connect(exportButton, &QPushButton::clicked, this, &LoadoutPage::exportJson);
    connect(importButton, &QPushButton::clicked, this, &LoadoutPage::importJson);
    connect(m_append, &QPushButton::clicked, this, &LoadoutPage::appendToBox);
}

void LoadoutPage::loadFromModel()
{
    for (int slot = 0; slot < 7; ++slot) {
        m_selected[slot] = MhguEquipment{};
        m_slots[slot]->blockSignals(true);
        m_slots[slot]->clear();
        m_slots[slot]->addItem(QStringLiteral("请选择装备箱记录"), -1);
        for (int index = 0; index < MhguSave::EquipmentCount; ++index) {
            const MhguEquipment value = m_save->equipment(index);
            if (!matchesSlot(slot, value.type) || !m_data->validateHunterEquipment(value)) continue;
            const QString name = m_data->name(m_data->equipmentTable(value.type), value.id);
            m_slots[slot]->addItem(QStringLiteral("%1 · 装备箱 #%2").arg(name).arg(index + 1), index);
        }
        m_slots[slot]->blockSignals(false);
    }
    refreshSummary();
}

bool LoadoutPage::commitToModel(QString *error)
{
    QVector<MhguEquipment> entries;
    for (const MhguEquipment &entry : m_selected) entries.push_back(entry);
    QStringList issues;
    const bool valid = m_data->validateLoadout(entries, &issues,
        m_loadoutGender->currentData().toString());
    if (error) *error = issues.join(QLatin1Char('\n'));
    return valid;
}

int LoadoutPage::selectedBoxIndex(int slot) const { return m_slots[slot]->currentData().toInt(); }
MhguEquipment LoadoutPage::selectedEquipment(int slot) const
{
    return slot >= 0 && slot < 7 ? m_selected[slot] : MhguEquipment{};
}
int LoadoutPage::availableSlots(const MhguEquipment &value) const
{
    bool found = false;
    if (value.type >= 1 && value.type <= 5) return m_data->armorSlots(value.type, value.id, &found);
    if (value.type == 6) return value.talismanSlots;
    return m_data->weaponSlots(value.type, value.id, value.level, &found);
}
QString LoadoutPage::circles(const MhguEquipment &value) const
{
    const int capacity = availableSlots(value);
    int used = 0;
    for (quint16 id : value.decorations) if (id) used += qMax(0, m_data->decorationSlotCost(id));
    QStringList result;
    for (int i = 0; i < 3; ++i) result << (i < used ? QStringLiteral("●") : i < capacity ? QStringLiteral("○") : QStringLiteral("⊘"));
    return result.join(QLatin1Char(' '));
}
void LoadoutPage::slotChanged(int slot)
{
    if (slot < 0 || slot >= 7) return;
    const int index = selectedBoxIndex(slot);
    m_selected[slot] = index >= 0 ? m_save->equipment(index) : MhguEquipment{};
    refreshSummary();
}

void LoadoutPage::editDecorations(int slot)
{
    if (slot < 0 || slot >= 7 || !m_selected[slot].type) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("请先选择该部位装备。")); return;
    }
    DecorationDialog dialog(m_data, m_selected[slot], this);
    if (dialog.exec() == QDialog::Accepted) {
        m_selected[slot] = dialog.value();
        refreshSummary();
    }
}

void LoadoutPage::clearLoadout()
{
    for (int slot = 0; slot < 7; ++slot) {
        m_slots[slot]->setCurrentIndex(0);
        m_selected[slot] = MhguEquipment{};
    }
    refreshSummary();
}
void LoadoutPage::refreshSummary()
{
    int selected = 0, free = 0;
    for (int slot = 0; slot < 7; ++slot) {
        const MhguEquipment value = selectedEquipment(slot);
        if (value.type) {
            ++selected;
            int used = 0;
            for (quint16 id : value.decorations) if (id) used += qMax(0, m_data->decorationSlotCost(id));
            free += qMax(0, availableSlots(value) - used);
            m_circleLabels[slot]->setText(circles(value));
        } else m_circleLabels[slot]->setText(QStringLiteral("⊘ ⊘ ⊘"));
    }
    m_summary->setText(QStringLiteral("已选 %1 / 7 件 · 空余孔数 %2 · ○ 可装 / ● 已装 / ⊘ 不可装").arg(selected).arg(free));
    m_append->setEnabled(selected == 7 && !m_save->isReadOnly());
}

void LoadoutPage::appendToBox()
{
    QString error;
    if (!commitToModel(&error)) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("配装未通过严格合法性校验：\n%1").arg(error)); return;
    }
    QVector<int> empty;
    for (int i = 0; i < MhguSave::EquipmentCount && empty.size() < 7; ++i)
        if (m_save->equipment(i).type == 0) empty.push_back(i);
    if (empty.size() < 7) { QMessageBox::warning(this, windowTitle(), QStringLiteral("装备箱没有 7 个空位。")); return; }
    QVector<MhguEquipmentUpdate> updates;
    for (int slot = 0; slot < 7; ++slot) updates.push_back({empty[slot], selectedEquipment(slot)});
    QStringList warnings;
    if (!m_save->setEquipmentBatch(updates, {}, &warnings)) {
        QMessageBox::critical(this, windowTitle(), QStringLiteral("配装未通过严格合法性校验，装备箱未发生变化。")); return;
    }
    emit modified();
    QMessageBox::information(this, windowTitle(), QStringLiteral("七件装备已添加到装备箱。"));
    loadFromModel();
}

void LoadoutPage::exportJson()
{
    for (int slot = 0; slot < 7; ++slot) if (!selectedEquipment(slot).type) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("请先选择完整的七件装备。")); return;
    }
    QString legalityError;
    if (!commitToModel(&legalityError)) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("严格合法性校验失败，已禁止导出：\n%1").arg(legalityError)); return;
    }
    QJsonObject armor;
    for (int slot = 1; slot <= 5; ++slot) armor.insert(QString::fromLatin1(SlotKeys[slot]), equipmentJson(selectedEquipment(slot)));
    QJsonObject root{{QStringLiteral("schema"), QStringLiteral("MH_LOADOUT")}, {QStringLiteral("schema_version"), 1},
        {QStringLiteral("game"), QStringLiteral("mhgu")}, {QStringLiteral("data_version"), m_data->dataVersion()},
        {QStringLiteral("name"), m_name->text().trimmed()}, {QStringLiteral("gender"), m_loadoutGender->currentData().toString()},
        {QStringLiteral("weapon"), equipmentJson(selectedEquipment(0))}, {QStringLiteral("armor"), armor},
        {QStringLiteral("charm"), equipmentJson(selectedEquipment(6))}};
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 MHGU 配装"), QStringLiteral("mhgu-loadout.json"), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
        QMessageBox::critical(this, windowTitle(), QStringLiteral("无法写入配装文件。"));
}

void LoadoutPage::importJson()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入配装 JSON"), {}, QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, windowTitle(), QStringLiteral("无法读取配装文件。")); return; }
    MhguLoadoutDocument document;
    QStringList errors;
    if (!parseMhguLoadoutJson(file.readAll(), m_data->dataVersion(), *m_data, &document, &errors)) {
        QMessageBox::critical(this, QStringLiteral("严格合法性校验失败"), errors.join(QLatin1Char('\n')));
        return;
    }
    QVector<MhguEquipment> knownEquipment;
    knownEquipment.reserve(MhguSave::EquipmentCount);
    for (int index = 0; index < MhguSave::EquipmentCount; ++index)
        knownEquipment.push_back(m_save->equipment(index));
    validateMhguLoadoutProvenance(document, knownEquipment, &errors);
    if (!errors.isEmpty()) { QMessageBox::critical(this, QStringLiteral("严格合法性校验失败"), errors.join(QLatin1Char('\n'))); return; }
    for (int i = 0; i < 7; ++i) {
        m_selected[i] = document.records[i];
        m_slots[i]->blockSignals(true);
        m_slots[i]->insertItem(0, QStringLiteral("导入结果 · %1").arg(m_data->name(m_data->equipmentTable(document.records[i].type), document.records[i].id)), -2);
        m_slots[i]->setCurrentIndex(0);
        m_slots[i]->blockSignals(false);
    }
    const int genderIndex = m_loadoutGender->findData(document.gender); if (genderIndex >= 0) m_loadoutGender->setCurrentIndex(genderIndex);
    if (!document.name.isEmpty()) m_name->setText(document.name);
    refreshSummary();
    QMessageBox::information(this, windowTitle(), QStringLiteral("配装已导入到编辑器；尚未写入存档，确认后再保存。"));
}

void LoadoutPage::openAutomatic()
{
    // Keep one non-modal search window per loadout page.  Re-opening the
    // entry must not create a second worker or a second independent timer.
    if (m_autoDialog) {
        m_autoDialog->showNormal();
        m_autoDialog->raise();
        m_autoDialog->activateWindow();
        return;
    }
    m_autoDialog = new AutoLoadoutDialog(m_save, m_data, selectedEquipment(0), this);
    connect(m_autoDialog, &QObject::destroyed, this, [this] { m_autoDialog = nullptr; });
    connect(m_autoDialog, &AutoLoadoutDialog::loadoutChosen, this, [this](const LoadoutSearchResult &result) {
        QStringList errors;
        QVector<MhguEquipment> records;
        for (const MhguEquipment &record : result.records) records.push_back(record);
        const QString gender = result.gender == 1 ? QStringLiteral("female") : QStringLiteral("male");
        if (!m_data->validateLoadout(records, &errors, gender)) {
            QMessageBox::critical(this, windowTitle(), QStringLiteral("搜索结果未通过回填前严格复核，已拒绝：\n%1").arg(errors.join(QLatin1Char('\n'))));
            return;
        }
        const int genderIndex = m_loadoutGender->findData(gender);
        if (genderIndex >= 0) m_loadoutGender->setCurrentIndex(genderIndex);
        for (int slot = 0; slot < 7; ++slot) {
            m_selected[slot] = result.records[slot];
            m_slots[slot]->blockSignals(true);
            m_slots[slot]->insertItem(0, QStringLiteral("自动结果 · %1").arg(result.names[slot]), -2);
            m_slots[slot]->setCurrentIndex(0);
            m_slots[slot]->blockSignals(false);
        }
        refreshSummary();
    });
    m_autoDialog->show();
}
