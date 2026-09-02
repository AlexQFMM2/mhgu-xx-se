#include "loadout_search.hpp"

#include "game_data.hpp"

#include <QDateTime>
#include <QMap>
#include <QMutexLocker>
#include <QSet>

#include <algorithm>
#include <functional>

namespace {
struct ArmorState {
    LoadoutSearchArmor armor[5];
    QVector<int> points;
    QVector<int> chestPoints;
    int slotCount[5]{0, 0, 0, 0, 0};
    int torsoUpCount = 0;
    int defense = 0;
    int fire = 0, water = 0, thunder = 0, ice = 0, dragon = 0;
};
struct JewelPattern {
    QVector<int> ids;
    QVector<int> points;
    int used = 0;
};
struct JewelState {
    QVector<int> points;
    QVector<int> ids[7];
    int used[7]{0, 0, 0, 0, 0, 0, 0};
};

QString vectorKey(const QVector<int> &values)
{
    QStringList result;
    for (int value : values) result << QString::number(value);
    return result.join(QLatin1Char(','));
}

void addCapped(QVector<int> &target, const QVector<int> &source,
               const LoadoutSearchRequest &request, int multiplier = 1)
{
    for (int i = 0; i < target.size(); ++i)
        target[i] = qBound(-128, target.at(i) + source.value(i) * multiplier,
                           request.skills.at(i).threshold);
}

int pointScore(const QVector<int> &points, const LoadoutSearchRequest &request)
{
    int score = 0;
    for (int i = 0; i < points.size(); ++i)
        score += qMin(points.at(i), request.skills.at(i).threshold);
    return score;
}

bool meets(const QVector<int> &points, const LoadoutSearchRequest &request)
{
    for (int i = 0; i < points.size(); ++i)
        if (points.at(i) < request.skills.at(i).threshold) return false;
    return true;
}

bool resultBetter(const LoadoutSearchResult &a, const LoadoutSearchResult &b)
{
    if (a.defense != b.defense) return a.defense > b.defense;
    if (a.freeSlots != b.freeSlots) return a.freeSlots > b.freeSlots;
    const int ar[] = {a.fireRes, a.waterRes, a.thunderRes, a.iceRes, a.dragonRes};
    const int br[] = {b.fireRes, b.waterRes, b.thunderRes, b.iceRes, b.dragonRes};
    for (int i = 0; i < 5; ++i) if (ar[i] != br[i]) return ar[i] > br[i];
    return a.fingerprint < b.fingerprint;
}

QByteArray naturalRecord(int type, int id)
{
    QByteArray record(36, char(0));
    record[0] = char(type & 0x1f);
    record[2] = char(id & 0xff);
    record[3] = char((id >> 8) & 0xff);
    return record;
}

void patchJewels(MhguEquipment &record, const QVector<int> &ids)
{
    record.decorations = {{0, 0, 0}};
    for (int i = 0; i < ids.size() && i < 3; ++i)
        record.decorations[size_t(i)] = quint16(ids.at(i));
    if (record.record.size() != 36) record.record = naturalRecord(record.type, record.id);
    for (int i = 0; i < 3; ++i) {
        record.record[6 + i * 2] = char(record.decorations[size_t(i)] & 0xff);
        record.record[7 + i * 2] = char(record.decorations[size_t(i)] >> 8);
    }
}

QString equipmentFingerprint(const LoadoutSearchResult &result)
{
    QStringList values;
    for (const MhguEquipment &record : result.records) {
        QStringList jewels;
        for (quint16 id : record.decorations) jewels << QString::number(id);
        values << QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
                  .arg(record.type).arg(record.id).arg(record.level)
                  .arg(record.skill1).arg(record.skill1Points)
                  .arg(record.skill2).arg(record.skill2Points)
                  + QLatin1Char(':') + jewels.join(QLatin1Char('.'));
    }
    return values.join(QLatin1Char('|'));
}

QString equipmentCombinationKey(const LoadoutSearchResult &result)
{
    QStringList values;
    for (const MhguEquipment &record : result.records) {
        values << QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8")
            .arg(record.type).arg(record.id).arg(record.level)
            .arg(record.skill1).arg(record.skill1Points)
            .arg(record.skill2).arg(record.skill2Points).arg(record.talismanSlots);
    }
    return values.join(QLatin1Char('|'));
}

QString charmKey(const MhguEquipment &record)
{
    return QStringLiteral("%1/%2/%3/%4/%5/%6")
        .arg(record.id).arg(record.talismanSlots).arg(record.skill1)
        .arg(record.skill1Points).arg(record.skill2).arg(record.skill2Points);
}

bool validateSearchResult(const LoadoutSearchSnapshot &snapshot,
                          const ArmorState &armor,
                          const LoadoutSearchCharm &charm,
                          const LoadoutSearchResult &result)
{
    const MhguEquipment &weapon = result.records[0];
    if (weapon.type != snapshot.request.weapon.type || weapon.id != snapshot.request.weapon.id ||
            weapon.level != snapshot.request.weapon.level || weapon.appearanceId != snapshot.request.weapon.appearanceId)
        return false;
    const int capacities[] = {snapshot.weaponSlots, armor.slotCount[0], armor.slotCount[1],
        armor.slotCount[2], armor.slotCount[3], armor.slotCount[4], charm.slotCount};
    QVector<int> points(snapshot.request.skills.size(), 0);
    QVector<int> chestPoints(snapshot.request.skills.size(), 0);
    for (int part = 0; part < 5; ++part) {
        const MhguEquipment &record = result.records[part + 1];
        if (record.type != part + 1 || record.id != armor.armor[part].record.id ||
                record.level != armor.armor[part].record.level ||
                record.appearanceId != armor.armor[part].record.appearanceId)
            return false;
        addCapped(points, armor.armor[part].points, snapshot.request);
        if (part == 1) chestPoints = armor.armor[part].points;
    }
    addCapped(points, chestPoints, snapshot.request, armor.torsoUpCount);
    const MhguEquipment &resultCharm = result.records[6];
    if (!snapshot.legalCharmKeys.contains(charmKey(resultCharm)) ||
            charmKey(resultCharm) != charmKey(charm.record))
        return false;
    addCapped(points, charm.points, snapshot.request);
    for (int piece = 0; piece < 7; ++piece) {
        const MhguEquipment &record = result.records[piece];
        if (record.record.size() != 36) return false;
        int used = 0;
        QVector<int> jewelPoints(snapshot.request.skills.size(), 0);
        for (quint16 id : record.decorations) {
            if (!id) continue;
            const auto decoration = snapshot.decorationsById.constFind(id);
            if (decoration == snapshot.decorationsById.constEnd() || decoration->slotCount <= 0)
                return false;
            used += decoration->slotCount;
            addCapped(jewelPoints, decoration->points, snapshot.request);
        }
        if (used > capacities[piece]) return false;
        addCapped(points, jewelPoints, snapshot.request,
                  piece == 2 ? 1 + armor.torsoUpCount : 1);
    }
    return meets(points, snapshot.request);
}

QVector<JewelPattern> patterns(const LoadoutSearchSnapshot &snapshot, int capacity)
{
    QMap<QString, JewelPattern> unique;
    JewelPattern empty; empty.points = QVector<int>(snapshot.request.skills.size(), 0);
    unique.insert(QStringLiteral("0"), empty);
    std::function<void(int, JewelPattern)> visit = [&](int first, JewelPattern current) {
        if (current.ids.size() >= 3) return;
        for (int i = first; i < snapshot.decorations.size(); ++i) {
            const LoadoutSearchDecoration &decoration = snapshot.decorations.at(i);
            if (current.used + decoration.slotCount > capacity) continue;
            JewelPattern next = current;
            next.ids.push_back(decoration.id); next.used += decoration.slotCount;
            for (int skill = 0; skill < next.points.size(); ++skill)
                next.points[skill] += decoration.points.value(skill);
            const QString key = QString::number(next.used) + QLatin1Char('/') + vectorKey(next.points);
            if (!unique.contains(key) || next.ids < unique.value(key).ids) unique.insert(key, next);
            visit(i, next);
        }
    };
    visit(0, empty);
    QVector<JewelPattern> result = QVector<JewelPattern>::fromList(unique.values());
    std::sort(result.begin(), result.end(), [&snapshot](const JewelPattern &a, const JewelPattern &b) {
        const int as = pointScore(a.points, snapshot.request), bs = pointScore(b.points, snapshot.request);
        if (as != bs) return as > bs;
        if (a.used != b.used) return a.used < b.used;
        return a.ids < b.ids;
    });
    return result;
}
}

bool buildLoadoutSearchSnapshot(const LoadoutSearchRequest &request, const GameData &data,
                                LoadoutSearchSnapshot *snapshot, QString *error)
{
    if (!snapshot) return false;
    LoadoutSearchSnapshot value;
    value.request = request;
    value.request.dataVersion = data.dataVersion();
    if (!data.automaticLoadoutDataComplete(&value.missingData)) {
        if (error) *error = QStringLiteral("自动配装数据未通过完整性门控：%1").arg(value.missingData.join(QStringLiteral("、")));
        return false;
    }
    QString legality;
    if (request.weapon.type < 7 || request.weapon.type > 21 || request.weapon.type == 12 ||
            !data.validateHunterEquipment(request.weapon, &legality)) {
        if (error) *error = QStringLiteral("所选武器不是已确认的自然合法记录：%1").arg(legality);
        return false;
    }
    if (request.skills.isEmpty()) {
        if (error) *error = QStringLiteral("请至少选择一个需要发动的技能。");
        return false;
    }
    if (request.maxSeconds < 1 || request.maxSeconds > 3600) {
        if (error) *error = QStringLiteral("计算时间必须在 1～60 分钟之间。");
        return false;
    }
    QMap<int, ActiveSkillData> active;
    for (const ActiveSkillData &skill : data.activeSkills()) active.insert(skill.id, skill);
    QSet<int> trees;
    for (const LoadoutSearchSkill &skill : request.skills) {
        if (!active.contains(skill.activeSkillId) || active.value(skill.activeSkillId).skillId != skill.skillId ||
                active.value(skill.activeSkillId).points != skill.threshold || skill.threshold <= 0 ||
                trees.contains(skill.skillId)) {
            if (error) *error = QStringLiteral("技能目标无效、重复或不是正向发动技能。");
            return false;
        }
        trees.insert(skill.skillId);
    }
    bool found = false;
    value.weaponSlots = data.weaponSlots(request.weapon.type, request.weapon.id, request.weapon.level, &found);
    if (!found) { if (error) *error = QStringLiteral("武器等级孔位无法确认。"); return false; }
    value.weaponName = data.name(data.equipmentTable(request.weapon.type), request.weapon.id);
    const int weaponCombat = request.weapon.type == 11 || request.weapon.type == 13 || request.weapon.type == 17 ? 1 : 0;

    for (int part = 0; part < 5; ++part) {
        QMap<QString, LoadoutSearchArmor> representatives;
        const MhguEquipment &fixed = request.fixedArmor[part];
        if (fixed.type != 0 && (fixed.type != part + 1 || !data.validateHunterEquipment(fixed, &legality))) {
            if (error) *error = QStringLiteral("固定的%1装备不是严格合法的自然记录：%2").arg(part + 1).arg(legality);
            return false;
        }
        for (const LoadoutArmorData &source : data.loadoutArmor(part + 1)) {
            if (fixed.type != 0 && source.id != fixed.id) continue;
            if (fixed.type != 0 && request.gender >= 0 && source.gender != 2 &&
                    source.gender != request.gender) continue;
            if (source.combat != 2 && source.combat != weaponCombat) continue;
            LoadoutSearchArmor candidate;
            candidate.record.type = quint8(part + 1); candidate.record.id = quint16(source.id);
            candidate.record.record = naturalRecord(part + 1, source.id);
            if (fixed.type != 0) candidate.record = fixed;
            candidate.name = source.name; candidate.gender = source.gender; candidate.combat = source.combat;
            candidate.slotCount = source.slotCount; candidate.baseDefense = source.baseDefense; candidate.maxDefense = source.maxDefense;
            candidate.fireRes = source.fireRes; candidate.waterRes = source.waterRes;
            candidate.thunderRes = source.thunderRes; candidate.iceRes = source.iceRes; candidate.dragonRes = source.dragonRes;
            candidate.torsoUp = source.skills.value(203) > 0;
            for (const LoadoutSearchSkill &skill : request.skills)
                candidate.points.push_back(source.skills.value(skill.skillId));
            QString key = QStringLiteral("%1/%2/%3/%4").arg(candidate.gender).arg(candidate.combat)
                .arg(candidate.slotCount).arg(vectorKey(candidate.points));
            const int resistance = candidate.fireRes + candidate.waterRes + candidate.thunderRes + candidate.iceRes + candidate.dragonRes;
            const LoadoutSearchArmor current = representatives.value(key);
            const int currentResistance = current.fireRes + current.waterRes + current.thunderRes + current.iceRes + current.dragonRes;
            if (!representatives.contains(key) || candidate.maxDefense > current.maxDefense ||
                    (candidate.maxDefense == current.maxDefense && resistance > currentResistance) ||
                    (candidate.maxDefense == current.maxDefense && resistance == currentResistance && candidate.record.id < current.record.id))
                representatives.insert(key, candidate);
        }
        value.candidates[part] = QVector<LoadoutSearchArmor>::fromList(representatives.values());
        if (value.candidates[part].isEmpty()) {
            if (error) *error = QStringLiteral("没有已验证的%1防具候选。").arg(part + 1);
            return false;
        }
    }
    for (const LoadoutDecorationData &source : data.loadoutDecorations()) {
        LoadoutSearchDecoration candidate; candidate.id = source.id; candidate.name = source.name; candidate.slotCount = source.slotCount;
        bool useful = false;
        for (const LoadoutSearchSkill &skill : request.skills) {
            const int points = source.skills.value(skill.skillId);
            candidate.points.push_back(points); useful |= points > 0;
        }
        if (useful) {
            value.decorations.push_back(candidate);
            value.decorationsById.insert(candidate.id, candidate);
        }
    }

    // Enumerate only target-relevant natural charm maxima.  Every generated
    // record is checked by the same strict validator used for save writes.
    for (int talisman = 1; talisman <= 10; ++talisman) {
        QVector<LoadoutSearchCharm> generated;
        LoadoutSearchCharm empty; empty.record.type = 6; empty.record.id = quint16(talisman);
        bool slotsFound = false;
        const int maximumSlots = data.talismanMaxSlots(talisman, &slotsFound);
        if (!slotsFound) continue;
        empty.record.talismanSlots = quint8(maximumSlots); empty.record.record = naturalRecord(6, talisman);
        empty.slotCount = maximumSlots; empty.points = QVector<int>(request.skills.size(), 0);
        generated.push_back(empty);
        for (int first = 0; first < request.skills.size(); ++first) {
            int minimum = 0, maximum = 0;
            if (!data.talismanSkillRange(talisman, request.skills.at(first).skillId, 1, &minimum, &maximum) || maximum <= 0) continue;
            LoadoutSearchCharm charm = empty;
            charm.record.skill1 = quint8(request.skills.at(first).skillId); charm.record.skill1Points = qint8(maximum);
            while (charm.record.talismanSlots > 0 &&
                   !data.talismanSlotsLegal(talisman, charm.record.skill1, charm.record.skill1Points,
                                            0, 0, charm.record.talismanSlots))
                --charm.record.talismanSlots;
            charm.slotCount = charm.record.talismanSlots;
            charm.points[first] = maximum; generated.push_back(charm);
            for (int second = 0; second < request.skills.size(); ++second) {
                if (second == first) continue;
                int secondMin = 0, secondMax = 0;
                if (!data.talismanSkillRange(talisman, request.skills.at(second).skillId, 2, &secondMin, &secondMax) || secondMax <= 0) continue;
                LoadoutSearchCharm pair = charm;
                pair.record.skill2 = quint8(request.skills.at(second).skillId); pair.record.skill2Points = qint8(secondMax);
                while (pair.record.talismanSlots > 0 &&
                       !data.talismanSlotsLegal(talisman, pair.record.skill1, pair.record.skill1Points,
                                                pair.record.skill2, pair.record.skill2Points,
                                                pair.record.talismanSlots))
                    --pair.record.talismanSlots;
                pair.slotCount = pair.record.talismanSlots;
                pair.points[second] = secondMax; generated.push_back(pair);
            }
        }
        for (LoadoutSearchCharm charm : generated) {
            charm.name = data.name(QStringLiteral("talismans"), talisman);
            charm.record.record[12] = char(charm.record.skill1); charm.record.record[13] = char(charm.record.skill2);
            charm.record.record[14] = char(charm.record.skill1Points); charm.record.record[15] = char(charm.record.skill2Points);
            charm.record.record[16] = char(charm.record.talismanSlots);
            QString issue;
            if (data.validateHunterEquipment(charm.record, &issue)) {
                value.charms.push_back(charm);
                value.legalCharmKeys.insert(charmKey(charm.record));
            }
        }
    }
    if (value.charms.isEmpty()) { if (error) *error = QStringLiteral("护石规则没有生成任何合法候选。"); return false; }
    if (request.fixedCharm.type != 0) {
        if (request.fixedCharm.type != 6 || !data.validateHunterEquipment(request.fixedCharm, &legality)) {
            if (error) *error = QStringLiteral("固定护石不是严格合法的自然记录：%1").arg(legality);
            return false;
        }
        const QString wanted = charmKey(request.fixedCharm);
        QVector<LoadoutSearchCharm> filtered;
        for (const LoadoutSearchCharm &candidate : value.charms)
            if (charmKey(candidate.record) == wanted) filtered.push_back(candidate);
        if (filtered.isEmpty()) {
            if (error) *error = QStringLiteral("固定护石组合不在已确认的自然生成候选中。");
            return false;
        }
        value.charms = filtered;
    }
    value.complete = true;
    *snapshot = value;
    if (error) error->clear();
    return true;
}

LoadoutSearchWorker::LoadoutSearchWorker(const LoadoutSearchSnapshot &snapshot, QObject *parent)
    : QObject(parent), m_snapshot(snapshot) {}

void LoadoutSearchWorker::pause()
{
    QMutexLocker lock(&m_mutex);
    if (!m_paused) { m_paused = true; m_pauseStartedMs = QDateTime::currentMSecsSinceEpoch(); }
}
void LoadoutSearchWorker::resume()
{
    QMutexLocker lock(&m_mutex);
    if (m_paused) { m_pausedMs += QDateTime::currentMSecsSinceEpoch() - m_pauseStartedMs; m_paused = false; m_condition.wakeAll(); }
}
void LoadoutSearchWorker::cancel()
{
    QMutexLocker lock(&m_mutex); m_cancelled = true; m_paused = false; m_condition.wakeAll();
}
qint64 LoadoutSearchWorker::activeElapsed() const
{
    return QDateTime::currentMSecsSinceEpoch() - m_startedMs - m_pausedMs;
}
bool LoadoutSearchWorker::stopRequested()
{
    QMutexLocker lock(&m_mutex);
    return m_cancelled || (!m_paused && activeElapsed() >= qint64(m_snapshot.request.maxSeconds) * 1000);
}
bool LoadoutSearchWorker::waitIfPaused()
{
    QMutexLocker lock(&m_mutex);
    while (m_paused && !m_cancelled) m_condition.wait(&m_mutex);
    return !m_cancelled;
}
void LoadoutSearchWorker::report(const QString &stage, qint64 checked)
{
    const qint64 elapsed = activeElapsed();
    if (elapsed - m_lastProgressMs < 150) return;
    m_lastProgressMs = elapsed;
    LoadoutSearchProgress value; value.stage = stage; value.checked = checked; value.elapsedMs = elapsed;
    value.remainingMs = qMax<qint64>(0, qint64(m_snapshot.request.maxSeconds) * 1000 - elapsed);
    { QMutexLocker lock(&m_mutex); value.paused = m_paused; }
    emit progress(value);
}

void LoadoutSearchWorker::run()
{
    if (!m_snapshot.complete) {
        emit finished(false, QStringLiteral("自动搜索数据未通过完整性门控：%1").arg(m_snapshot.missingData.join(QStringLiteral("、"))));
        return;
    }
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    qint64 checked = 0;
    bool foundAny = false;
    QMap<QString, LoadoutSearchResult> bestResults;
    QVector<JewelPattern> jewelPatterns[4];
    for (int capacity = 0; capacity <= 3; ++capacity) jewelPatterns[capacity] = patterns(m_snapshot, capacity);
    const int order[] = {1, 0, 2, 3, 4}; // chest first
    QVector<int> genders = m_snapshot.request.gender < 0 ? QVector<int>{0, 1} : QVector<int>{m_snapshot.request.gender};
    for (int gender : genders) {
        QVector<ArmorState> states(1);
        states[0].points = QVector<int>(m_snapshot.request.skills.size(), 0);
        states[0].chestPoints = states[0].points;
        for (int orderIndex = 0; orderIndex < 5 && !stopRequested(); ++orderIndex) {
            const int part = order[orderIndex];
            QVector<ArmorState> next;
            for (const ArmorState &base : states) {
                if (!waitIfPaused()) break;
                for (const LoadoutSearchArmor &candidate : m_snapshot.candidates[part]) {
                    if (candidate.gender != 2 && candidate.gender != gender) continue;
                    ArmorState state = base; state.armor[part] = candidate; state.slotCount[part] = candidate.slotCount;
                    if (part == 1) {
                        addCapped(state.points, candidate.points, m_snapshot.request);
                        state.chestPoints = candidate.points;
                    } else {
                        addCapped(state.points, candidate.points, m_snapshot.request);
                        // Native save skill 203 is Torso Up. It copies chest
                        // skills and chest jewels later, never chest capacity.
                        if (candidate.torsoUp) {
                            ++state.torsoUpCount;
                            addCapped(state.points, state.chestPoints, m_snapshot.request);
                        }
                    }
                    state.defense += candidate.maxDefense;
                    state.fire += candidate.fireRes; state.water += candidate.waterRes;
                    state.thunder += candidate.thunderRes; state.ice += candidate.iceRes; state.dragon += candidate.dragonRes;
                    next.push_back(state); ++checked;
                    if ((checked & 0x3ff) == 0 && stopRequested()) break;
                }
            }
            QMap<QString, ArmorState> unique;
            for (const ArmorState &state : next) {
                QString key = vectorKey(state.points);
                for (int slotCount : state.slotCount) key += QStringLiteral("/%1").arg(slotCount);
                key += QStringLiteral("/t%1").arg(state.torsoUpCount);
                if (!unique.contains(key) || state.defense > unique.value(key).defense) unique.insert(key, state);
            }
            states = QVector<ArmorState>::fromList(unique.values());
            std::sort(states.begin(), states.end(), [this](const ArmorState &a, const ArmorState &b) {
                const int as = pointScore(a.points, m_snapshot.request), bs = pointScore(b.points, m_snapshot.request);
                if (as != bs) return as > bs;
                return a.defense > b.defense;
            });
            if (states.size() > 16000) states.resize(16000);
            report(QStringLiteral("组合防具 %1/5").arg(orderIndex + 1), checked);
        }
        const int armorLimit = qMin(states.size(), 700);
        const int charmLimit = qMin(m_snapshot.charms.size(), 500);
        for (int armorIndex = 0; armorIndex < armorLimit && !stopRequested(); ++armorIndex) {
            const ArmorState &armor = states.at(armorIndex);
            for (int charmIndex = 0; charmIndex < charmLimit && !stopRequested(); ++charmIndex) {
                if (!waitIfPaused()) break;
                const LoadoutSearchCharm &charm = m_snapshot.charms.at(charmIndex);
                JewelState initial; initial.points = armor.points; addCapped(initial.points, charm.points, m_snapshot.request);
                QVector<JewelState> beam{initial};
                const int capacities[] = {m_snapshot.weaponSlots, armor.slotCount[0], armor.slotCount[1], armor.slotCount[2], armor.slotCount[3], armor.slotCount[4], charm.slotCount};
                for (int piece = 0; piece < 7 && !stopRequested(); ++piece) {
                    QVector<JewelState> next;
                    for (const JewelState &base : beam) for (const JewelPattern &pattern : jewelPatterns[qBound(0, capacities[piece], 3)]) {
                        JewelState state = base; state.ids[piece] = pattern.ids; state.used[piece] = pattern.used;
                        addCapped(state.points, pattern.points, m_snapshot.request,
                                  piece == 2 ? 1 + armor.torsoUpCount : 1);
                        next.push_back(state); ++checked;
                    }
                    QMap<QString, JewelState> unique;
                    for (const JewelState &state : next) {
                        const QString key = vectorKey(state.points);
                        int used = 0; for (int n : state.used) used += n;
                        if (!unique.contains(key)) unique.insert(key, state);
                        else { int old = 0; for (int n : unique.value(key).used) old += n; if (used < old) unique[key] = state; }
                    }
                    beam = QVector<JewelState>::fromList(unique.values());
                    std::sort(beam.begin(), beam.end(), [this](const JewelState &a, const JewelState &b) {
                        return pointScore(a.points, m_snapshot.request) > pointScore(b.points, m_snapshot.request);
                    });
                    if (beam.size() > 320) beam.resize(320);
                    report(QStringLiteral("分配装饰珠"), checked);
                }
                bool haveBest = false;
                LoadoutSearchResult bestResult;
                int bestUsed = 0;
                for (const JewelState &jewels : beam) {
                    if (!meets(jewels.points, m_snapshot.request)) continue;
                    LoadoutSearchResult candidateResult;
                    candidateResult.records[0] = m_snapshot.request.weapon; candidateResult.names[0] = m_snapshot.weaponName;
                    patchJewels(candidateResult.records[0], jewels.ids[0]);
                    for (int part = 0; part < 5; ++part) {
                        candidateResult.records[part + 1] = armor.armor[part].record;
                        candidateResult.names[part + 1] = armor.armor[part].name;
                        patchJewels(candidateResult.records[part + 1], jewels.ids[part + 1]);
                    }
                    candidateResult.records[6] = charm.record; candidateResult.names[6] = charm.name;
                    candidateResult.gender = gender;
                    patchJewels(candidateResult.records[6], jewels.ids[6]);
                    candidateResult.defense = armor.defense; candidateResult.fireRes = armor.fire; candidateResult.waterRes = armor.water;
                    candidateResult.thunderRes = armor.thunder; candidateResult.iceRes = armor.ice; candidateResult.dragonRes = armor.dragon;
                    int total = 0, used = 0; for (int capacity : capacities) total += capacity; for (int value : jewels.used) used += value;
                    candidateResult.freeSlots = total - used;
                    candidateResult.fingerprint = equipmentFingerprint(candidateResult);
                    candidateResult.combinationKey = equipmentCombinationKey(candidateResult);
                    if (!validateSearchResult(m_snapshot, armor, charm, candidateResult)) continue;
                    if (!haveBest || used < bestUsed ||
                            (used == bestUsed && candidateResult.fingerprint < bestResult.fingerprint)) {
                        haveBest = true;
                        bestUsed = used;
                        bestResult = candidateResult;
                    }
                }
                if (haveBest) {
                    const QString combination = bestResult.combinationKey;
                    bool accepted = !bestResults.contains(combination) || resultBetter(bestResult, bestResults.value(combination));
                    if (accepted && !bestResults.contains(combination) && bestResults.size() >= 100) {
                        auto worst = bestResults.begin();
                        for (auto it = bestResults.begin(); it != bestResults.end(); ++it)
                            if (resultBetter(worst.value(), it.value())) worst = it;
                        if (resultBetter(bestResult, worst.value())) bestResults.erase(worst);
                        else accepted = false;
                    }
                    if (accepted) {
                        bestResults.insert(combination, bestResult);
                        emit result(bestResult);
                        foundAny = true;
                    }
                }
            }
        }
    }
    const bool cancelled = [&] { QMutexLocker lock(&m_mutex); return m_cancelled; }();
    emit finished(cancelled, cancelled ? QStringLiteral("搜索已取消。") : foundAny
        ? QStringLiteral("搜索完成，保留 %1 套最优严格合法配装。").arg(bestResults.size())
        : QStringLiteral("暂无找到合适的配装。可减少技能目标或延长计算时间。"));
}
