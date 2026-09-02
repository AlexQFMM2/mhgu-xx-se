#pragma once

#include "mhgu_save.hpp"

#include <QByteArray>
#include <QStringList>
#include <QVector>

class GameData;

struct MhguLoadoutDocument {
    MhguEquipment records[7];
    QString name;
    QString gender;
};

// UI-free strict parser used by both the loadout page and core tests.
bool parseMhguLoadoutJson(const QByteArray &json, const QString &dataVersion,
                          const GameData &data, MhguLoadoutDocument *document,
                          QStringList *errors = nullptr);

// Non-zero armour upgrade levels are not derivable from the audited data yet.
// They may only be carried across when the exact 36-byte record already exists
// in the currently opened save.
bool validateMhguLoadoutProvenance(const MhguLoadoutDocument &document,
                                   const QVector<MhguEquipment> &knownEquipment,
                                   QStringList *errors = nullptr);
