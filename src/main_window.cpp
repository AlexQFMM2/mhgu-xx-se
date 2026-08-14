#include "main_window.hpp"

#include "editor_dialogs.hpp"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainSurface"));
    setWindowTitle(QStringLiteral("MHGU 存档修改器"));
    if (!m_data.load(QStringLiteral("cn")))
        QMessageBox::critical(this, QStringLiteral("数据加载失败"), m_data.error());

    auto *surface = new QWidget(this);
    setCentralWidget(surface);
    auto *shell = new QHBoxLayout(surface);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto *sidebar = new QFrame(surface);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(210);
    auto *navigation = new QVBoxLayout(sidebar);
    navigation->setContentsMargins(18, 24, 18, 20);
    navigation->setSpacing(8);
    auto *brand = new QLabel(QStringLiteral("MHGU"), sidebar);
    brand->setObjectName(QStringLiteral("sidebarTitle"));
    auto *caption = new QLabel(QStringLiteral("存档修改器"), sidebar);
    caption->setObjectName(QStringLiteral("sidebarCaption"));
    navigation->addWidget(brand);
    navigation->addWidget(caption);
    navigation->addSpacing(22);
    auto makeNavigation = [sidebar](const QString &text) {
        auto *button = new QPushButton(text, sidebar);
        button->setObjectName(QStringLiteral("navigationButton"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    m_switchButton = makeNavigation(QStringLiteral("切换存档槽"));
    m_characterButton = makeNavigation(QStringLiteral("角色"));
    m_itemsButton = makeNavigation(QStringLiteral("道具箱"));
    m_equipmentButton = makeNavigation(QStringLiteral("装备箱"));
    m_palicosButton = makeNavigation(QStringLiteral("猫猫"));
    navigation->addWidget(m_switchButton);
    navigation->addSpacing(8);
    navigation->addWidget(m_characterButton);
    navigation->addWidget(m_itemsButton);
    navigation->addWidget(m_equipmentButton);
    navigation->addWidget(m_palicosButton);
    navigation->addStretch();

    auto *workspace = new QWidget(surface);
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(24, 18, 24, 18);
    workspaceLayout->setSpacing(12);
    auto *header = new QHBoxLayout;
    auto *heading = new QVBoxLayout;
    m_pageTitle = new QLabel(QStringLiteral("存档管理"), workspace);
    m_pageTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *subtitle = new QLabel(QStringLiteral("Nintendo Switch · system 三槽存档"), workspace);
    subtitle->setObjectName(QStringLiteral("appSubtitle"));
    heading->addWidget(m_pageTitle);
    heading->addWidget(subtitle);
    header->addLayout(heading);
    header->addStretch();
    workspaceLayout->addLayout(header);
    auto *riskWarning = new QLabel(QStringLiteral("⚠ 修改有风险，请自主备份存档后再修改。"), workspace);
    riskWarning->setObjectName(QStringLiteral("riskWarning"));
    workspaceLayout->addWidget(riskWarning);
    m_status = new QLabel(workspace);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    workspaceLayout->addWidget(m_status);

    m_pages = new QStackedWidget(workspace);
    m_emptyPage = new QWidget(m_pages);
    m_emptyPage->setObjectName(QStringLiteral("pageSurface"));
    auto *emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto *emptyTitle = new QLabel(QStringLiteral("尚未读取存档"), m_emptyPage);
    emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyHint = new QLabel(QStringLiteral("点击右下角“读取存档”，选择无文件头的 system。"), m_emptyPage);
    emptyHint->setObjectName(QStringLiteral("appSubtitle"));
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyHint);
    m_pages->addWidget(m_emptyPage);

    m_slotPage = new QWidget(m_pages);
    m_slotPage->setObjectName(QStringLiteral("pageSurface"));
    auto *slotLayout = new QVBoxLayout(m_slotPage);
    slotLayout->setContentsMargins(18, 18, 18, 18);
    slotLayout->setSpacing(12);
    auto *slotHint = new QLabel(QStringLiteral("一个 system 中包含三个存档槽，请选择要编辑的角色。"), m_slotPage);
    slotHint->setObjectName(QStringLiteral("appSubtitle"));
    slotLayout->addWidget(slotHint);
    for (int i = 0; i < 3; ++i) {
        auto *card = new QFrame(m_slotPage);
        card->setObjectName(QStringLiteral("slotCard"));
        auto *row = new QHBoxLayout(card);
        row->setContentsMargins(16, 13, 16, 13);
        m_slotDescriptions[i] = new QLabel(card);
        m_slotDescriptions[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_slotEnterButtons[i] = new QPushButton(QStringLiteral("进入"), card);
        m_slotEnterButtons[i]->setObjectName(QStringLiteral("primaryButton"));
        row->addWidget(m_slotDescriptions[i], 1);
        row->addWidget(m_slotEnterButtons[i]);
        connect(m_slotEnterButtons[i], &QPushButton::clicked, this, [this, i] { selectSlot(i); });
        slotLayout->addWidget(card);
    }
    slotLayout->addStretch();
    m_pages->addWidget(m_slotPage);

    auto *content = new QScrollArea(workspace);
    content->setObjectName(QStringLiteral("contentArea"));
    content->setWidgetResizable(true);
    content->setFrameShape(QFrame::NoFrame);
    content->setWidget(m_pages);
    workspaceLayout->addWidget(content, 1);

    auto *footer = new QFrame(workspace);
    footer->setObjectName(QStringLiteral("footerBar"));
    auto *actions = new QHBoxLayout(footer);
    actions->setContentsMargins(14, 10, 14, 10);
    actions->addStretch();
    m_openButton = new QPushButton(QStringLiteral("读取存档"), footer);
    m_openButton->setObjectName(QStringLiteral("primaryButton"));
    m_saveButton = new QPushButton(QStringLiteral("保存修改"), footer);
    m_saveButton->setObjectName(QStringLiteral("saveButton"));
    actions->addWidget(m_openButton);
    actions->addWidget(m_saveButton);
    workspaceLayout->addWidget(footer);
    shell->addWidget(sidebar);
    shell->addWidget(workspace, 1);

    connect(m_switchButton, &QPushButton::clicked, this, &MainWindow::switchSlot);
    connect(m_characterButton, &QPushButton::clicked, this, &MainWindow::showCharacter);
    connect(m_itemsButton, &QPushButton::clicked, this, &MainWindow::showItems);
    connect(m_equipmentButton, &QPushButton::clicked, this, &MainWindow::showEquipment);
    connect(m_palicosButton, &QPushButton::clicked, this, &MainWindow::showPalicos);
    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::saveFile);
    resize(1100, 700);
    setMinimumSize(900, 620);
    refresh();
}

void MainWindow::createPages()
{
    if (m_characterPage) return;
    m_characterPage = new CharacterDialog(&m_save, m_pages);
    m_itemsPage = new ItemBoxDialog(&m_save, &m_data, m_pages);
    m_equipmentPage = new EquipmentBoxDialog(&m_save, &m_data, m_pages);
    m_palicosPage = new PalicoListDialog(&m_save, &m_data, m_pages);
    m_pages->addWidget(m_characterPage);
    m_pages->addWidget(m_itemsPage);
    m_pages->addWidget(m_equipmentPage);
    m_pages->addWidget(m_palicosPage);
    connect(m_characterPage, &CharacterDialog::modified, this, &MainWindow::markModified);
    connect(m_itemsPage, &ItemBoxDialog::modified, this, &MainWindow::markModified);
    connect(m_equipmentPage, &EquipmentBoxDialog::modified, this, &MainWindow::markModified);
    connect(m_palicosPage, &PalicoListDialog::modified, this, &MainWindow::markModified);
}

void MainWindow::loadPages()
{
    if (m_save.selectedSlot() < 0) return;
    createPages();
    m_characterPage->loadFromModel();
    m_itemsPage->loadFromModel();
    m_equipmentPage->loadFromModel();
    m_palicosPage->loadFromModel();
}

bool MainWindow::commitPages(QString *error)
{
    if (!m_characterPage) return true;
    if (!m_characterPage->commitToModel(error)) { showCharacter(); return false; }
    if (!m_itemsPage->commitToModel(error)) { showItems(); return false; }
    if (!m_equipmentPage->commitToModel(error)) { showEquipment(); return false; }
    if (!m_palicosPage->commitToModel(error)) { showPalicos(); return false; }
    return true;
}

bool MainWindow::hasUnsavedChanges() const { return m_uiDirty || m_save.isDirty(); }

void MainWindow::markModified()
{
    if (m_save.selectedSlot() < 0) return;
    m_uiDirty = true;
    refresh();
}

bool MainWindow::discardChanges()
{
    const QString path = m_save.path();
    const int slot = m_save.selectedSlot();
    if (path.isEmpty() || !m_save.open(path) || (slot >= 0 && !m_save.selectSlot(slot))) {
        QMessageBox::critical(this, QStringLiteral("重新读取失败"), m_save.error());
        return false;
    }
    m_uiDirty = false;
    loadPages();
    refresh();
    return true;
}

bool MainWindow::maybeLeaveDirty()
{
    if (!hasUnsavedChanges()) return true;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("尚未保存"),
                    QStringLiteral("当前存档槽有尚未保存的修改。"), QMessageBox::NoButton, this);
    auto *save = box.addButton(QStringLiteral("保存"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(QStringLiteral("放弃"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == save) return saveFile();
    if (box.clickedButton() == discard) return discardChanges();
    return false;
}

void MainWindow::openFile()
{
    if (!maybeLeaveDirty()) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("读取 MHGU system"), {},
        QStringLiteral("MHGU system (system);;所有文件 (*)"));
    if (path.isEmpty()) return;
    if (!m_save.open(path)) {
        QMessageBox::critical(this, QStringLiteral("读取失败"), m_save.error());
        refresh();
        return;
    }
    m_uiDirty = false;
    showSlotSelection();
    refresh();
}

bool MainWindow::saveFile()
{
    if (m_save.selectedSlot() < 0) return false;
    QString error;
    if (!commitPages(&error)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), error.isEmpty() ? QStringLiteral("页面数据校验失败。") : error);
        return false;
    }
    if (!m_save.save()) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), m_save.error());
        m_uiDirty = true;
        refresh();
        return false;
    }
    m_uiDirty = false;
    refresh();
    QMessageBox::information(this, QStringLiteral("保存成功"),
        QStringLiteral("已覆盖当前 system：\n%1\n\n未创建备份，也未修改 system_backup。").arg(m_save.path()));
    return true;
}

void MainWindow::showSlotSelection()
{
    if (!m_save.isOpen()) return;
    const QVector<MhguSlotInfo> slotInfos = m_save.slotInfos();
    for (int i = 0; i < 3; ++i) {
        const MhguSlotInfo info = slotInfos.value(i);
        const QString time = QStringLiteral("%1:%2").arg(info.playTime / 3600).arg((info.playTime / 60) % 60, 2, 10, QLatin1Char('0'));
        m_slotDescriptions[i]->setText(info.used
            ? QStringLiteral("存档 %1\n%2 · HR %3 · %4").arg(i + 1).arg(info.name).arg(info.hunterRank).arg(time)
            : QStringLiteral("存档 %1\n未使用").arg(i + 1));
        m_slotEnterButtons[i]->setEnabled(info.used);
    }
    m_pages->setCurrentWidget(m_slotPage);
    m_switchButton->setChecked(true);
    m_pageTitle->setText(QStringLiteral("选择存档槽"));
}

void MainWindow::selectSlot(int index)
{
    if (!m_save.selectSlot(index)) {
        QMessageBox::critical(this, QStringLiteral("无法选择存档"), m_save.error());
        return;
    }
    m_uiDirty = false;
    loadPages();
    showCharacter();
    refresh();
}

void MainWindow::switchSlot()
{
    if (!maybeLeaveDirty()) return;
    showSlotSelection();
    refresh();
}

void MainWindow::setCurrentPage(QWidget *page, QPushButton *button, const QString &title)
{
    if (!page) return;
    m_pages->setCurrentWidget(page);
    button->setChecked(true);
    m_pageTitle->setText(title);
}
void MainWindow::showCharacter() { setCurrentPage(m_characterPage, m_characterButton, QStringLiteral("角色")); }
void MainWindow::showItems() { setCurrentPage(m_itemsPage, m_itemsButton, QStringLiteral("道具箱")); }
void MainWindow::showEquipment() { setCurrentPage(m_equipmentPage, m_equipmentButton, QStringLiteral("装备箱 · 幻化【测试】")); }
void MainWindow::showPalicos() { setCurrentPage(m_palicosPage, m_palicosButton, QStringLiteral("猫猫")); }

void MainWindow::refresh()
{
    const bool open = m_save.isOpen();
    const bool ready = open && m_save.selectedSlot() >= 0;
    m_switchButton->setEnabled(open);
    m_characterButton->setEnabled(ready);
    m_itemsButton->setEnabled(ready);
    m_equipmentButton->setEnabled(ready);
    m_palicosButton->setEnabled(ready);
    m_saveButton->setEnabled(ready);
    m_openButton->setText(open ? QStringLiteral("读取其他存档") : QStringLiteral("读取存档"));
    if (ready) {
        const MhguCharacter character = m_save.character();
        m_status->setText(QStringLiteral("%1 · 存档 %2 · %3 · %4")
            .arg(QFileInfo(m_save.path()).fileName()).arg(m_save.selectedSlot() + 1).arg(character.name,
                 hasUnsavedChanges() ? QStringLiteral("未保存") : QStringLiteral("已保存")));
    } else if (open) {
        m_status->setText(QStringLiteral("%1 · 请选择存档槽 1 / 2 / 3").arg(QFileInfo(m_save.path()).fileName()));
    } else {
        m_status->setText(QStringLiteral("尚未读取存档"));
        m_pages->setCurrentWidget(m_emptyPage);
        m_pageTitle->setText(QStringLiteral("存档管理"));
    }
    m_status->setProperty("loaded", ready);
    m_status->setProperty("dirty", hasUnsavedChanges());
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeLeaveDirty()) event->accept();
    else event->ignore();
}
