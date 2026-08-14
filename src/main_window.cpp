#include "main_window.hpp"

#include "editor_dialogs.hpp"

#include <QCloseEvent>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("mainSurface"));
    setWindowTitle(QStringLiteral("MHGU 存档修改器"));

    if (!m_data.load(QStringLiteral("cn"))) {
        QMessageBox::critical(this, QStringLiteral("数据加载失败"), m_data.error());
    }

    auto navigation = [this](const QString &text) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(QStringLiteral("navigationButton"));
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    m_character = navigation(QStringLiteral("角色信息"));
    m_items = navigation(QStringLiteral("道具箱"));
    m_equipment = navigation(QStringLiteral("装备箱 · 幻化【测试】"));
    m_palicos = navigation(QStringLiteral("猫猫"));
    connect(m_character, &QPushButton::clicked, this, &MainWindow::openCharacter);
    connect(m_items, &QPushButton::clicked, this, &MainWindow::openItems);
    connect(m_equipment, &QPushButton::clicked, this, &MainWindow::openEquipment);
    connect(m_palicos, &QPushButton::clicked, this, &MainWindow::openPalicos);

    m_open = new QPushButton(QStringLiteral("打开 system"), this);
    m_open->setObjectName(QStringLiteral("primaryButton"));
    m_saveButton = new QPushButton(QStringLiteral("保存"), this);
    m_saveButton->setObjectName(QStringLiteral("saveButton"));
    m_switchSlot = new QPushButton(QStringLiteral("切换存档槽"), this);
    connect(m_open, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::saveFile);
    connect(m_switchSlot, &QPushButton::clicked, this, &MainWindow::switchSlot);

    auto *title = new QLabel(QStringLiteral("MHGU 存档修改器"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    auto *subtitle = new QLabel(QStringLiteral("Nintendo Switch · system 三槽存档编辑"), this);
    subtitle->setObjectName(QStringLiteral("appSubtitle"));
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *contentCard = new QFrame(this);
    contentCard->setObjectName(QStringLiteral("contentCard"));
    auto *contentLayout = new QVBoxLayout(contentCard);
    contentLayout->setContentsMargins(18, 16, 18, 18);
    contentLayout->addWidget(new QLabel(QStringLiteral("存档内容"), contentCard));
    auto *grid = new QGridLayout;
    grid->setSpacing(10);
    grid->addWidget(m_character, 0, 0);
    grid->addWidget(m_items, 0, 1);
    grid->addWidget(m_equipment, 1, 0);
    grid->addWidget(m_palicos, 1, 1);
    contentLayout->addLayout(grid);

    auto *fileCard = new QFrame(this);
    fileCard->setObjectName(QStringLiteral("contentCard"));
    auto *fileLayout = new QVBoxLayout(fileCard);
    fileLayout->setContentsMargins(18, 16, 18, 18);
    fileLayout->addWidget(new QLabel(QStringLiteral("存档文件"), fileCard));
    auto *actions = new QHBoxLayout;
    actions->addWidget(m_open);
    actions->addWidget(m_saveButton);
    fileLayout->addLayout(actions);
    fileLayout->addWidget(m_switchSlot);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 26);
    layout->setSpacing(15);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(m_status);
    layout->addWidget(contentCard);
    layout->addWidget(fileCard);
    setMinimumSize(500, 520);
    resize(540, 560);
    refresh();
}

bool MainWindow::maybeLeaveDirty()
{
    if (!m_save.isDirty()) return true;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("尚未保存"),
                    QStringLiteral("当前存档槽有尚未保存的修改。"), QMessageBox::NoButton, this);
    auto *save = box.addButton(QStringLiteral("保存"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(QStringLiteral("放弃"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == save) {
        saveFile();
        return !m_save.isDirty();
    }
    if (box.clickedButton() == discard) {
        const QString path = m_save.path();
        const int slot = m_save.selectedSlot();
        if (!m_save.open(path)) {
            QMessageBox::critical(this, QStringLiteral("重新读取失败"), m_save.error());
            return false;
        }
        if (slot >= 0) m_save.selectSlot(slot);
        return true;
    }
    return false;
}

bool MainWindow::chooseSlot()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("选择要编辑的存档"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(QStringLiteral("选择存档槽 1 / 2 / 3"), &dialog);
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);
    layout->addWidget(new QLabel(QStringLiteral("三个角色都保存在同一个 system 中，只会编辑你选择的槽位。"), &dialog));
    int chosen = -1;
    for (const MhguSlotInfo &slot : m_save.slotInfos()) {
        auto *card = new QFrame(&dialog);
        card->setObjectName(QStringLiteral("contentCard"));
        auto *row = new QHBoxLayout(card);
        const QString time = QStringLiteral("%1:%2").arg(slot.playTime / 3600).arg((slot.playTime / 60) % 60, 2, 10, QLatin1Char('0'));
        auto *description = new QLabel(slot.used
            ? QStringLiteral("存档 %1\n%2 · HR %3 · %4").arg(slot.index + 1).arg(slot.name).arg(slot.hunterRank).arg(time)
            : QStringLiteral("存档 %1\n未使用").arg(slot.index + 1), card);
        auto *button = new QPushButton(QStringLiteral("进入"), card);
        button->setEnabled(slot.used);
        row->addWidget(description, 1);
        row->addWidget(button);
        connect(button, &QPushButton::clicked, &dialog, [&, index = slot.index] { chosen = index; dialog.accept(); });
        layout->addWidget(card);
    }
    dialog.resize(510, 360);
    if (dialog.exec() != QDialog::Accepted || chosen < 0) return false;
    if (!m_save.selectSlot(chosen)) {
        QMessageBox::critical(this, QStringLiteral("无法选择存档"), m_save.error());
        return false;
    }
    return true;
}

void MainWindow::openFile()
{
    if (!maybeLeaveDirty()) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 MHGU system"), {},
        QStringLiteral("MHGU system (system);;所有文件 (*)"));
    if (path.isEmpty()) return;
    if (!m_save.open(path)) {
        QMessageBox::critical(this, QStringLiteral("读取失败"), m_save.error());
        refresh();
        return;
    }
    if (!chooseSlot()) m_save.close();
    refresh();
}

void MainWindow::saveFile()
{
    if (!m_save.save()) QMessageBox::critical(this, QStringLiteral("保存失败"), m_save.error());
    else QMessageBox::information(this, QStringLiteral("保存完成"), QStringLiteral("已覆盖当前 system。未创建备份，也未修改 system_backup。"));
    refresh();
}

void MainWindow::switchSlot()
{
    if (!maybeLeaveDirty()) return;
    chooseSlot();
    refresh();
}

void MainWindow::openCharacter() { CharacterDialog(&m_save, this).exec(); refresh(); }
void MainWindow::openItems() { ItemBoxDialog(&m_save, &m_data, this).exec(); refresh(); }
void MainWindow::openEquipment() { EquipmentBoxDialog(&m_save, &m_data, this).exec(); refresh(); }
void MainWindow::openPalicos() { PalicoListDialog(&m_save, &m_data, this).exec(); refresh(); }

void MainWindow::refresh()
{
    const bool ready = m_save.isOpen() && m_save.selectedSlot() >= 0;
    m_character->setEnabled(ready);
    m_items->setEnabled(ready);
    m_equipment->setEnabled(ready);
    m_palicos->setEnabled(ready);
    m_saveButton->setEnabled(ready);
    m_switchSlot->setEnabled(m_save.isOpen());
    if (ready) {
        const MhguCharacter character = m_save.character();
        m_status->setText(QStringLiteral("%1 · 存档 %2 · %3 · %4")
            .arg(QFileInfo(m_save.path()).fileName()).arg(m_save.selectedSlot() + 1).arg(character.name,
                 m_save.isDirty() ? QStringLiteral("未保存") : QStringLiteral("已保存")));
    } else m_status->setText(QStringLiteral("尚未读取存档，请先打开无头的 system"));
    m_status->setProperty("loaded", ready);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeLeaveDirty()) event->accept();
    else event->ignore();
}
