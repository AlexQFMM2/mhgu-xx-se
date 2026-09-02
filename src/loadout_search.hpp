#pragma once

#include "mhgu_save.hpp"

#include <QMutex>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QWaitCondition>
#include <QVector>

class GameData;

struct LoadoutSearchSkill {
    int activeSkillId = 0;
    int skillId = 0;
    int threshold = 0;
    QString name;
};

struct LoadoutSearchRequest {
    MhguEquipment weapon;
    // Optional fixed natural equipment.  Empty records leave that part to the
    // searcher; non-empty records are validated before entering the snapshot.
    MhguEquipment fixedArmor[5];
    MhguEquipment fixedCharm;
    int gender = -1;
    QVector<LoadoutSearchSkill> skills;
    int maxSeconds = 60;
    QString dataVersion;
};

struct LoadoutSearchArmor {
    MhguEquipment record;
    QString name;
    int gender = 2;
    int combat = 2;
    int slotCount = 0;
    int baseDefense = 0;
    int maxDefense = 0;
    int fireRes = 0, waterRes = 0, thunderRes = 0, iceRes = 0, dragonRes = 0;
    bool torsoUp = false;
    QVector<int> points;
};

struct LoadoutSearchDecoration {
    int id = 0;
    QString name;
    int slotCount = 0;
    QVector<int> points;
};

struct LoadoutSearchCharm {
    MhguEquipment record;
    QString name;
    int slotCount = 0;
    QVector<int> points;
};

struct LoadoutSearchSnapshot {
    LoadoutSearchRequest request;
    QVector<LoadoutSearchArmor> candidates[5];
    QVector<LoadoutSearchDecoration> decorations;
    QVector<LoadoutSearchCharm> charms;
    QMap<int, LoadoutSearchDecoration> decorationsById;
    QSet<QString> legalCharmKeys;
    QString weaponName;
    int weaponSlots = 0;
    bool complete = false;
    QStringList missingData;
};

struct LoadoutSearchProgress {
    QString stage;
    qint64 checked = 0;
    qint64 elapsedMs = 0;
    qint64 remainingMs = 0;
    bool paused = false;
};

struct LoadoutSearchResult {
    MhguEquipment records[7];
    QString names[7];
    int defense = 0;
    int freeSlots = 0;
    int fireRes = 0;
    int waterRes = 0;
    int thunderRes = 0;
    int iceRes = 0;
    int dragonRes = 0;
    QString fingerprint;
    QString combinationKey;
    int gender = 0;
};

Q_DECLARE_METATYPE(LoadoutSearchProgress)
Q_DECLARE_METATYPE(LoadoutSearchResult)

bool buildLoadoutSearchSnapshot(const LoadoutSearchRequest &request, const GameData &data,
                                LoadoutSearchSnapshot *snapshot, QString *error = nullptr);

class LoadoutSearchWorker : public QObject {
    Q_OBJECT
public:
    explicit LoadoutSearchWorker(const LoadoutSearchSnapshot &snapshot, QObject *parent = nullptr);
public slots:
    void run();
    void pause();
    void resume();
    void cancel();
signals:
    void progress(const LoadoutSearchProgress &value);
    void result(const LoadoutSearchResult &value);
    void finished(bool cancelled, const QString &message);
private:
    bool stopRequested();
    bool waitIfPaused();
    qint64 activeElapsed() const;
    void report(const QString &stage, qint64 checked);

    LoadoutSearchSnapshot m_snapshot;
    QMutex m_mutex;
    QWaitCondition m_condition;
    bool m_paused = false;
    bool m_cancelled = false;
    qint64 m_startedMs = 0;
    qint64 m_pauseStartedMs = 0;
    qint64 m_pausedMs = 0;
    qint64 m_lastProgressMs = -1000;
};
