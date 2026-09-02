#pragma once

#include "loadout_search.hpp"

#include <QDialog>
#include <QMap>

class GameData;
class MhguSave;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;
class QTimer;
class QVBoxLayout;

class AutoLoadoutDialog : public QDialog {
    Q_OBJECT
public:
    AutoLoadoutDialog(MhguSave *save, GameData *data,
                      const MhguEquipment &preferredWeapon = {}, QWidget *parent = nullptr);
    ~AutoLoadoutDialog() override;
signals:
    void loadoutChosen(const LoadoutSearchResult &result);
protected:
    void closeEvent(QCloseEvent *event) override;
private slots:
    void addSkillRow();
    void startSearch();
    void togglePause();
    void cancelSearch();
    void tick();
    void workerFinished(bool cancelled, const QString &message);
    void updateProgress(const LoadoutSearchProgress &value);
    void queueResult(const LoadoutSearchResult &value);
    void flushResults();
    void toggleFullScreen();
private:
    void stopWorker();
    void updateControls();
    void updateResultRow(int row, const LoadoutSearchResult &value);
    MhguSave *m_save;
    GameData *m_data;
    QComboBox *m_weapon;
    QComboBox *m_fixed[6] = {};
    QComboBox *m_gender;
    QVBoxLayout *m_skillRows;
    QVector<QComboBox *> m_skillCombos;
    QSpinBox *m_minutes;
    QLabel *m_stage;
    QLabel *m_time;
    QLabel *m_checked;
    QProgressBar *m_progress;
    QTableWidget *m_results;
    QPushButton *m_start;
    QPushButton *m_pause;
    QPushButton *m_cancel;
    QThread *m_thread = nullptr;
    LoadoutSearchWorker *m_worker = nullptr;
    QTimer *m_timer;
    QTimer *m_resultTimer;
    QVector<LoadoutSearchResult> m_pendingResults;
    QMap<QString, LoadoutSearchResult> m_resultByFingerprint;
    MhguEquipment m_preferredWeapon;
    int m_searchTotalSeconds = 60;
    qint64 m_startedMs = 0;
    qint64 m_pauseStartedMs = 0;
    qint64 m_pausedMs = 0;
    bool m_paused = false;
    QString m_finishMessage;
};
