#pragma once

#include "game_data.hpp"
#include "mhgu_save.hpp"

#include <QWidget>

class QLabel;
class QPushButton;
class QCloseEvent;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openFile();
    void saveFile();
    void switchSlot();
    void openCharacter();
    void openItems();
    void openEquipment();
    void openPalicos();
    void refresh();

private:
    bool maybeLeaveDirty();
    bool chooseSlot();

    MhguSave m_save;
    GameData m_data;
    QLabel *m_status = nullptr;
    QPushButton *m_character = nullptr;
    QPushButton *m_items = nullptr;
    QPushButton *m_equipment = nullptr;
    QPushButton *m_palicos = nullptr;
    QPushButton *m_open = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_switchSlot = nullptr;
};

