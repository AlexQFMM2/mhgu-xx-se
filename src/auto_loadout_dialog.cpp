#include "auto_loadout_dialog.hpp"

#include "game_data.hpp"
#include "mhgu_save.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

AutoLoadoutDialog::AutoLoadoutDialog(MhguSave *save, GameData *data,
                                     const MhguEquipment &preferredWeapon, QWidget *parent)
    : QDialog(parent), m_save(save), m_data(data), m_preferredWeapon(preferredWeapon)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("MHGU / MHXX 自动配装"));
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setModal(false);
    auto *root = new QHBoxLayout(this);
    auto *left = new QWidget(this); auto *leftLayout = new QVBoxLayout(left);
    auto *form = new QFormLayout;
    m_weapon = new QComboBox(left); m_weapon->addItem(QStringLiteral("请选择具体武器"), -1);
    for (int i = 0; i < MhguSave::EquipmentCount; ++i) {
        const MhguEquipment value = save->equipment(i);
        if (value.type < 7 || value.type > 21 || value.type == 12 || !data->validateHunterEquipment(value)) continue;
        m_weapon->addItem(QStringLiteral("%1 · 装备箱 #%2").arg(data->name(data->equipmentTable(value.type), value.id)).arg(i + 1), i);
    }
    bool preferredSelected = false;
    if (m_preferredWeapon.type) {
        for (int index = 0; index < m_weapon->count(); ++index) {
            const int boxIndex = m_weapon->itemData(index).toInt();
            if (boxIndex >= 0 && save->equipment(boxIndex).record == m_preferredWeapon.record) {
                m_weapon->setCurrentIndex(index);
                preferredSelected = true;
                break;
            }
        }
        if (!preferredSelected && data->validateHunterEquipment(m_preferredWeapon)) {
            m_weapon->addItem(QStringLiteral("当前配装器 · %1")
                .arg(data->name(data->equipmentTable(m_preferredWeapon.type), m_preferredWeapon.id)), -2);
            m_weapon->setCurrentIndex(m_weapon->count() - 1);
        }
    }
    auto *fixedBox = new QGroupBox(QStringLiteral("可选固定装备（严格合法）"), left);
    auto *fixedForm = new QFormLayout(fixedBox);
    const QString fixedLabels[] = {QStringLiteral("头"), QStringLiteral("胸"), QStringLiteral("腕"), QStringLiteral("腰"), QStringLiteral("腿"), QStringLiteral("护石")};
    for (int part = 0; part < 6; ++part) {
        m_fixed[part] = new QComboBox(fixedBox);
        m_fixed[part]->addItem(QStringLiteral("不固定"), -1);
        const int expected = part + 1;
        for (int i = 0; i < MhguSave::EquipmentCount; ++i) {
            const MhguEquipment value = save->equipment(i);
            if (value.type != expected || !data->validateHunterEquipment(value)) continue;
            m_fixed[part]->addItem(QStringLiteral("%1 · 装备箱 #%2")
                .arg(data->name(data->equipmentTable(value.type), value.id)).arg(i + 1), i);
        }
        fixedForm->addRow(fixedLabels[part], m_fixed[part]);
    }
    auto *clearFixed = new QPushButton(QStringLiteral("清空固定装备"), fixedBox);
    fixedForm->addRow(QString(), clearFixed);
    connect(clearFixed, &QPushButton::clicked, this, [this] {
        for (QComboBox *combo : m_fixed) if (combo) combo->setCurrentIndex(0);
    });
    m_gender = new QComboBox(left); m_gender->addItem(QStringLiteral("不限"), -1); m_gender->addItem(QStringLiteral("男性"), 0); m_gender->addItem(QStringLiteral("女性"), 1);
    m_minutes = new QSpinBox(left); m_minutes->setRange(1, 60); m_minutes->setValue(1);
    form->addRow(QStringLiteral("武器"), m_weapon); form->addRow(QStringLiteral("性别"), m_gender); form->addRow(QStringLiteral("最大时间（分）"), m_minutes);
    leftLayout->addLayout(form);
    leftLayout->addWidget(fixedBox);
    auto *skillLabel = new QLabel(QStringLiteral("目标技能"), left); leftLayout->addWidget(skillLabel);
    auto *skillHost = new QWidget(left); m_skillRows = new QVBoxLayout(skillHost); m_skillRows->setContentsMargins(0,0,0,0); leftLayout->addWidget(skillHost);
    auto *add = new QPushButton(QStringLiteral("＋ 添加技能"), left); leftLayout->addWidget(add); connect(add, &QPushButton::clicked, this, &AutoLoadoutDialog::addSkillRow); addSkillRow();
    QStringList missing; const bool ready = data->automaticLoadoutDataComplete(&missing);
    auto *gate = new QLabel(ready ? QStringLiteral("数据完整性：通过") : QStringLiteral("自动搜索已安全禁用，缺少已验证表：%1").arg(missing.join(QStringLiteral("、"))), left);
    gate->setObjectName(QStringLiteral("warningLabel")); gate->setWordWrap(true); leftLayout->addWidget(gate); leftLayout->addStretch();
    m_start = new QPushButton(QStringLiteral("开始搜索"), left); m_start->setObjectName(QStringLiteral("primaryButton")); m_start->setEnabled(ready);
    leftLayout->addWidget(m_start); connect(m_start, &QPushButton::clicked, this, &AutoLoadoutDialog::startSearch);

    auto *right = new QWidget(this); auto *rightLayout = new QVBoxLayout(right);
    auto *status = new QHBoxLayout;
    m_stage = new QLabel(QStringLiteral("阶段：等待开始"), right); m_time = new QLabel(QStringLiteral("已用 00:00 · 剩余 01:00"), right); m_checked = new QLabel(QStringLiteral("已检查 0"), right);
    m_pause = new QPushButton(QStringLiteral("暂停"), right); m_cancel = new QPushButton(QStringLiteral("取消任务"), right);
    auto *fullscreen = new QPushButton(QStringLiteral("全屏"), right);
    status->addWidget(m_stage); status->addWidget(m_time); status->addWidget(m_checked); status->addStretch(); status->addWidget(m_pause); status->addWidget(m_cancel); status->addWidget(fullscreen); rightLayout->addLayout(status);
    m_progress = new QProgressBar(right); m_progress->setRange(0, 60); m_progress->setValue(0); rightLayout->addWidget(m_progress);
    m_results = new QTableWidget(right); m_results->setColumnCount(15);
    m_results->setHorizontalHeaderLabels({QStringLiteral("武器"),QStringLiteral("头"),QStringLiteral("胸"),QStringLiteral("腕"),QStringLiteral("腰"),QStringLiteral("腿"),QStringLiteral("护石"),QStringLiteral("防御力"),QStringLiteral("空余孔数"),QStringLiteral("火"),QStringLiteral("水"),QStringLiteral("雷"),QStringLiteral("冰"),QStringLiteral("龙"),QStringLiteral("操作")});
    m_results->setSortingEnabled(false);
    m_results->setAlternatingRowColors(true);
    m_results->verticalHeader()->setVisible(false);
    m_results->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    for (int column = 0; column < 7; ++column) m_results->setColumnWidth(column, 132);
    for (int column = 7; column < 14; ++column) m_results->setColumnWidth(column, 68);
    m_results->setColumnWidth(14, 96);
    rightLayout->addWidget(m_results, 1);
    auto *leftScroll = new QScrollArea(this);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setWidget(left);
    root->addWidget(leftScroll, 2); root->addWidget(right, 8);
    connect(m_pause, &QPushButton::clicked, this, &AutoLoadoutDialog::togglePause); connect(m_cancel, &QPushButton::clicked, this, &AutoLoadoutDialog::cancelSearch);
    connect(fullscreen, &QPushButton::clicked, this, &AutoLoadoutDialog::toggleFullScreen);
    m_timer = new QTimer(this); m_timer->setInterval(1000); connect(m_timer, &QTimer::timeout, this, &AutoLoadoutDialog::tick);
    m_resultTimer = new QTimer(this); m_resultTimer->setInterval(350); connect(m_resultTimer, &QTimer::timeout, this, &AutoLoadoutDialog::flushResults);
    updateControls(); resize(1280, 720);
}

AutoLoadoutDialog::~AutoLoadoutDialog() { stopWorker(); }
void AutoLoadoutDialog::addSkillRow()
{
    auto *row = new QWidget(this); auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0);
    auto *combo = new QComboBox(row); combo->addItem(QStringLiteral("请选择发动技能"), 0);
    for (const ActiveSkillData &entry : m_data->activeSkills())
        combo->addItem(entry.name, entry.id);
    auto *remove = new QPushButton(QStringLiteral("－"), row); layout->addWidget(combo, 1); layout->addWidget(remove); m_skillRows->addWidget(row); m_skillCombos.push_back(combo);
    connect(remove, &QPushButton::clicked, this, [this,row,combo]{ m_skillCombos.removeOne(combo); row->deleteLater(); });
}
void AutoLoadoutDialog::startSearch()
{
    const int weaponIndex = m_weapon->currentData().toInt();
    if (weaponIndex == -1) { QMessageBox::warning(this, windowTitle(), QStringLiteral("请先选择具体武器。")); return; }
    LoadoutSearchRequest request; request.weapon = weaponIndex == -2 ? m_preferredWeapon : m_save->equipment(weaponIndex); request.gender = m_gender->currentData().toInt(); request.maxSeconds = m_minutes->value()*60;
    m_searchTotalSeconds = request.maxSeconds;
    for (int part = 0; part < 6; ++part) {
        const int index = m_fixed[part]->currentData().toInt();
        if (index < 0) continue;
        const MhguEquipment value = m_save->equipment(index);
        if (part < 5) request.fixedArmor[part] = value; else request.fixedCharm = value;
    }
    const QMap<int, ActiveSkillData> active = [&] { QMap<int, ActiveSkillData> value; for (const ActiveSkillData &skill : m_data->activeSkills()) value.insert(skill.id, skill); return value; }();
    for (QComboBox *combo : m_skillCombos) if (combo->currentData().toInt()) {
        const ActiveSkillData skill = active.value(combo->currentData().toInt());
        request.skills.push_back({skill.id, skill.skillId, skill.points, skill.name});
    }
    LoadoutSearchSnapshot snapshot;
    QString snapshotError;
    if (!buildLoadoutSearchSnapshot(request, *m_data, &snapshot, &snapshotError)) {
        QMessageBox::critical(this, windowTitle(), snapshotError.isEmpty() ? QStringLiteral("严格数据门控未通过，拒绝启动搜索。") : snapshotError); return;
    }
    m_thread = new QThread(this); m_worker = new LoadoutSearchWorker(snapshot); m_worker->moveToThread(m_thread);
    connect(m_thread,&QThread::started,m_worker,&LoadoutSearchWorker::run); connect(m_worker,&LoadoutSearchWorker::finished,this,&AutoLoadoutDialog::workerFinished); connect(m_worker,&LoadoutSearchWorker::progress,this,&AutoLoadoutDialog::updateProgress); connect(m_worker,&LoadoutSearchWorker::result,this,&AutoLoadoutDialog::queueResult); connect(m_worker,&LoadoutSearchWorker::finished,m_thread,&QThread::quit,Qt::DirectConnection); connect(m_thread,&QThread::finished,m_worker,&QObject::deleteLater);
    m_pendingResults.clear(); m_resultByFingerprint.clear(); m_results->setRowCount(0); m_startedMs=QDateTime::currentMSecsSinceEpoch(); m_pausedMs=0; m_paused=false; m_timer->start(); m_resultTimer->start(); m_thread->start(); updateControls();
}
void AutoLoadoutDialog::togglePause() { if (!m_worker) return; m_paused=!m_paused; if (m_paused) {m_pauseStartedMs=QDateTime::currentMSecsSinceEpoch(); m_worker->pause();} else {m_pausedMs+=QDateTime::currentMSecsSinceEpoch()-m_pauseStartedMs; m_worker->resume();} updateControls(); }
void AutoLoadoutDialog::cancelSearch() { if (m_worker) m_worker->cancel(); }
void AutoLoadoutDialog::tick() { if (!m_thread || m_paused) return; const qint64 elapsed=(QDateTime::currentMSecsSinceEpoch()-m_startedMs-m_pausedMs)/1000; const int total=m_searchTotalSeconds; m_progress->setRange(0,total); m_progress->setValue(qMin<qint64>(elapsed,total)); m_time->setText(QStringLiteral("已用 %1:%2 · 剩余 %3:%4").arg(elapsed/60,2,10,QLatin1Char('0')).arg(elapsed%60,2,10,QLatin1Char('0')).arg(qMax<qint64>(0,total-elapsed)/60,2,10,QLatin1Char('0')).arg(qMax<qint64>(0,total-elapsed)%60,2,10,QLatin1Char('0'))); }
void AutoLoadoutDialog::workerFinished(bool cancelled,const QString &message) { m_timer->stop(); m_resultTimer->stop(); flushResults(); m_finishMessage = message; m_stage->setText(cancelled?QStringLiteral("阶段：已取消"):QStringLiteral("阶段：已结束")); if (!message.isEmpty()) m_stage->setToolTip(message); if(m_thread){m_thread->wait();m_thread->deleteLater();} m_thread=nullptr;m_worker=nullptr;updateControls(); }
void AutoLoadoutDialog::stopWorker() { if(!m_thread)return; if(m_worker)m_worker->cancel(); m_thread->quit();m_thread->wait();delete m_thread;m_thread=nullptr;m_worker=nullptr; }
void AutoLoadoutDialog::updateControls() { const bool running=m_thread; m_start->setEnabled(!running && m_data->automaticLoadoutDataComplete()); m_pause->setEnabled(running);m_pause->setText(m_paused?QStringLiteral("继续"):QStringLiteral("暂停"));m_cancel->setEnabled(running); }
void AutoLoadoutDialog::closeEvent(QCloseEvent *event) { if(m_thread && QMessageBox::question(this,windowTitle(),QStringLiteral("关闭将结束本次搜索，确定关闭吗？"))!=QMessageBox::Yes){event->ignore();return;} stopWorker();event->accept(); }

void AutoLoadoutDialog::updateProgress(const LoadoutSearchProgress &value)
{
    m_stage->setText(QStringLiteral("阶段：%1").arg(value.stage));
    m_checked->setText(QStringLiteral("已检查 %1").arg(value.checked));
    if (!m_paused) {
        const qint64 elapsed = value.elapsedMs / 1000, remaining = value.remainingMs / 1000;
        m_time->setText(QStringLiteral("已用 %1:%2 · 剩余 %3:%4").arg(elapsed / 60, 2, 10, QLatin1Char('0')).arg(elapsed % 60, 2, 10, QLatin1Char('0')).arg(remaining / 60, 2, 10, QLatin1Char('0')).arg(remaining % 60, 2, 10, QLatin1Char('0')));
    }
}

void AutoLoadoutDialog::queueResult(const LoadoutSearchResult &value)
{
    m_resultByFingerprint.insert(value.combinationKey, value);
    m_pendingResults.push_back(value);
}

void AutoLoadoutDialog::flushResults()
{
    if (m_pendingResults.isEmpty()) return;
    m_pendingResults.clear();
    QVector<LoadoutSearchResult> values = m_resultByFingerprint.values().toVector();
    std::sort(values.begin(), values.end(), [](const LoadoutSearchResult &a, const LoadoutSearchResult &b) {
        if (a.defense != b.defense) return a.defense > b.defense;
        if (a.freeSlots != b.freeSlots) return a.freeSlots > b.freeSlots;
        const int ar[] = {a.fireRes,a.waterRes,a.thunderRes,a.iceRes,a.dragonRes};
        const int br[] = {b.fireRes,b.waterRes,b.thunderRes,b.iceRes,b.dragonRes};
        for (int i=0;i<5;++i) if (ar[i]!=br[i]) return ar[i]>br[i];
        return a.fingerprint < b.fingerprint;
    });
    if (values.size() > 100) values.resize(100);
    m_resultByFingerprint.clear();
    for (const LoadoutSearchResult &value : values)
        m_resultByFingerprint.insert(value.combinationKey, value);
    QMap<QString, int> oldRows;
    for (int row = 0; row < m_results->rowCount(); ++row) {
        if (auto *item = m_results->item(row, 0)) oldRows.insert(item->data(Qt::UserRole).toString(), row);
    }
    m_results->setUpdatesEnabled(false);
    m_results->setRowCount(values.size());
    for (int row = 0; row < values.size(); ++row) {
        const LoadoutSearchResult &value = values.at(row);
        const int previousRow = oldRows.value(value.combinationKey, -1);
        if (previousRow == row && m_results->item(row, 0) &&
                m_results->item(row, 0)->data(Qt::UserRole).toString() == value.combinationKey &&
                m_results->item(row, 0)->data(Qt::UserRole + 1).toString() == value.fingerprint)
            continue;
        updateResultRow(row, value);
    }
    m_results->setUpdatesEnabled(true);
}

void AutoLoadoutDialog::updateResultRow(int row, const LoadoutSearchResult &value)
{
    for (int col = 0; col < 7; ++col) {
        auto *item = new QTableWidgetItem(value.names[col]);
        item->setData(Qt::UserRole, value.combinationKey);
        item->setData(Qt::UserRole + 1, value.fingerprint);
        m_results->setItem(row, col, item);
    }
    const int numbers[] = {value.defense, value.freeSlots, value.fireRes, value.waterRes,
                           value.thunderRes, value.iceRes, value.dragonRes};
    for (int i = 0; i < 7; ++i) m_results->setItem(row, 7 + i, new QTableWidgetItem(QString::number(numbers[i])));
    auto *add = new QPushButton(QStringLiteral("加入配装器"), m_results);
    add->setMinimumHeight(24); add->setMaximumHeight(28);
    connect(add, &QPushButton::clicked, this, [this, value] { emit loadoutChosen(value); });
    m_results->setCellWidget(row, 14, add);
}

void AutoLoadoutDialog::toggleFullScreen()
{
    if (isFullScreen()) showNormal(); else showFullScreen();
}
