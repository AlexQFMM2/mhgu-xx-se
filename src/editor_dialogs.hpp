#pragma once

#include <QDialog>

class GameData;
class MhguSave;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;

class CharacterDialog : public QDialog {
    Q_OBJECT
public:
    explicit CharacterDialog(MhguSave *save, QWidget *parent = nullptr);
private slots:
    void apply();
private:
    MhguSave *m_save;
    QLineEdit *m_name;
    QLineEdit *m_fields[9];
};

class ItemBoxDialog : public QDialog {
    Q_OBJECT
public:
    ItemBoxDialog(MhguSave *save, GameData *data, QWidget *parent = nullptr);
private slots:
    void populate();
    void editSelected();
    void addFirstEmpty();
private:
    MhguSave *m_save;
    GameData *m_data;
    QTableWidget *m_table;
    QLineEdit *m_search;
    QCheckBox *m_nonEmpty;
};

class EquipmentBoxDialog : public QDialog {
    Q_OBJECT
public:
    EquipmentBoxDialog(MhguSave *save, GameData *data, QWidget *parent = nullptr);
private slots:
    void populateHunter();
    void populatePalico();
    void editHunter();
    void editPalico();
private:
    MhguSave *m_save;
    GameData *m_data;
    QTableWidget *m_hunterTable;
    QTableWidget *m_palicoTable;
    QLineEdit *m_hunterSearch;
    QLineEdit *m_palicoSearch;
    QComboBox *m_hunterType;
    QComboBox *m_palicoType;
    QCheckBox *m_hunterNonEmpty;
    QCheckBox *m_palicoNonEmpty;
};

class PalicoListDialog : public QDialog {
    Q_OBJECT
public:
    PalicoListDialog(MhguSave *save, GameData *data, QWidget *parent = nullptr);
private slots:
    void populate();
    void editSelected();
private:
    MhguSave *m_save;
    GameData *m_data;
    QTableWidget *m_table;
    QCheckBox *m_showEmpty;
};

