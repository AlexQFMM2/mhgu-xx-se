#pragma once

#include "game_data.hpp"
#include "mhgu_save.hpp"

#include <QMainWindow>

class CharacterDialog;
class ItemBoxDialog;
class EquipmentBoxDialog;
class PalicoListDialog;
class QLabel;
class QPushButton;
class QStackedWidget;
class QCloseEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private slots:
    void openFile();
    bool saveFile();
    void switchSlot();
    void showCharacter();
    void showItems();
    void showEquipment();
    void showPalicos();
    void markModified();
private:
    void createPages();
    void loadPages();
    bool commitPages(QString *error = nullptr);
    bool maybeLeaveDirty();
    bool discardChanges();
    bool hasUnsavedChanges() const;
    void showSlotSelection();
    void selectSlot(int index);
    void setCurrentPage(QWidget *page, QPushButton *button, const QString &title);
    void refresh();

    MhguSave m_save;
    GameData m_data;
    bool m_uiDirty = false;
    QLabel *m_status = nullptr;
    QLabel *m_pageTitle = nullptr;
    QPushButton *m_characterButton = nullptr;
    QPushButton *m_itemsButton = nullptr;
    QPushButton *m_equipmentButton = nullptr;
    QPushButton *m_palicosButton = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_switchButton = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_emptyPage = nullptr;
    QWidget *m_slotPage = nullptr;
    QLabel *m_slotDescriptions[3] = {};
    QPushButton *m_slotEnterButtons[3] = {};
    CharacterDialog *m_characterPage = nullptr;
    ItemBoxDialog *m_itemsPage = nullptr;
    EquipmentBoxDialog *m_equipmentPage = nullptr;
    PalicoListDialog *m_palicosPage = nullptr;
};
