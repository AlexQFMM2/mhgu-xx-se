#pragma once

#include "mhgu_save.hpp"

#include <QWidget>
#include <QPointer>

class GameData;
class QComboBox;
class QLabel;
class QPushButton;
class QLineEdit;
class AutoLoadoutDialog;

class LoadoutPage : public QWidget {
    Q_OBJECT
public:
    explicit LoadoutPage(MhguSave *save, GameData *data, QWidget *parent = nullptr);
    void loadFromModel();
    bool commitToModel(QString *error = nullptr);
signals:
    void modified();
private slots:
    void slotChanged(int slot);
    void refreshSummary();
    void appendToBox();
    void exportJson();
    void importJson();
    void openAutomatic();
    void clearLoadout();
private:
    int selectedBoxIndex(int slot) const;
    MhguEquipment selectedEquipment(int slot) const;
    int availableSlots(const MhguEquipment &value) const;
    QString circles(const MhguEquipment &value) const;
    void editDecorations(int slot);

    MhguSave *m_save;
    GameData *m_data;
    QComboBox *m_slots[7] = {};
    MhguEquipment m_selected[7];
    QLabel *m_circleLabels[7] = {};
    QLabel *m_summary = nullptr;
    QPushButton *m_append = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_loadoutGender = nullptr;
    QPointer<AutoLoadoutDialog> m_autoDialog;
};
