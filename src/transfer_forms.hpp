#pragma once

#include "mhgu_save.hpp"

#include <QByteArray>

namespace MhxxGuTransfer {

struct ItemUpdate {
    int index = -1;
    MhguItem value;
};

QByteArray exportItems(const QVector<MhguItem> &items);
bool parseItems(const QByteArray &form, QVector<ItemUpdate> *updates, QString *error = nullptr);

QByteArray exportEquipment(const QVector<MhguEquipmentUpdate> &hunter,
                           const QVector<MhguPalicoEquipmentUpdate> &palico);
bool parseEquipment(const QByteArray &form,
                    QVector<MhguEquipmentUpdate> *hunter,
                    QVector<MhguPalicoEquipmentUpdate> *palico,
                    QString *error = nullptr);

}
