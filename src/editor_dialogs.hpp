#pragma once

#include <QWidget>

class GameData;
class MhguSave;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;

class CharacterDialog : public QWidget {
    Q_OBJECT
public:
    explicit CharacterDialog(MhguSave *save, QWidget *parent = nullptr);
    void loadFromModel();
    bool commitToModel(QString *error = nullptr);
signals:
    void modified();
private:
    MhguSave *m_save;
    QLineEdit *m_name;
    QLineEdit *m_fields[9];
    bool m_loading = false;
};

class ItemBoxDialog : public QWidget {
    Q_OBJECT
public:
    ItemBoxDialog(MhguSave *save, GameData *data, QWidget *parent = nullptr);
    void loadFromModel();
    bool commitToModel(QString *error = nullptr);
signals:
    void modified();
private slots:
    void populate();
    void editSelected();
    void addFirstEmpty();
    void exportForm();
    void importForm();
private:
    MhguSave *m_save;
    GameData *m_data;
    QTableWidget *m_table;
    QLineEdit *m_search;
    QCheckBox *m_nonEmpty;
};

class EquipmentBoxDialog : public QWidget {
    Q_OBJECT
public:
    EquipmentBoxDialog(MhguSave *save, GameData *data, QWidget *parent = nullptr);
    void loadFromModel();
    bool commitToModel(QString *error = nullptr);
signals:
    void modified();
private slots:
    void populateHunter();
    void populatePalico();
    void editHunter();
    void editPalico();
    void exportForm();
    void importForm();
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

class PalicoListDialog : public QWidget {
    Q_OBJECT
public:
    PalicoListDialog(MhguSave *save, GameData *data, QWidget *parent = nullptr);
    void loadFromModel();
    bool commitToModel(QString *error = nullptr);
signals:
    void modified();
private slots:
    void populate();
    void editSelected();
private:
    MhguSave *m_save;
    GameData *m_data;
    QTableWidget *m_table;
    QCheckBox *m_showEmpty;
};
