#include "project/SvdStudioSetOrder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>

#include <cstring>
#include <utility>

namespace svdorder {

namespace {

constexpr int kStudioSetEntrySize = 1460;
constexpr int kCompanionEntrySize = 392;
constexpr int kFavoriteCount = 100;
constexpr int kFavoriteEntrySize = 11;

// Favorite entry: 3-bit kind, then Bank MSB / Bank LSB / Program (7 bits each).
constexpr int kFavMsbBit = 3;
constexpr int kFavLsbBit = 10;
constexpr int kFavPcBit = 17;
constexpr quint32 kStudioSetBankMsb = 85;

quint32 be32(const QByteArray &d, int pos)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(d.constData() + pos));
}

bool favoriteUserSlot(const char *entry, int *slot)
{
    if (readBits(entry, kFavMsbBit, 7) != kStudioSetBankMsb)
        return false;
    const quint32 lsb = readBits(entry, kFavLsbBit, 7);
    if (lsb >= kUserSlots / 128)
        return false;
    if (slot)
        *slot = int(lsb * 128 + readBits(entry, kFavPcBit, 7));
    return true;
}

bool isPermutation(const QVector<int> &order)
{
    if (order.size() != kUserSlots)
        return false;
    QVector<bool> seen(kUserSlots, false);
    for (int v : order) {
        if (v < 0 || v >= kUserSlots || seen[v])
            return false;
        seen[v] = true;
    }
    return true;
}

} // namespace

quint32 readBits(const char *entry, int start, int width)
{
    quint32 v = 0;
    for (int i = 0; i < width; ++i) {
        const int bit = start + i;
        const auto byte = static_cast<unsigned char>(entry[bit / 8]);
        v = (v << 1) | ((byte >> (7 - bit % 8)) & 1u);
    }
    return v;
}

void writeBits(char *entry, int start, int width, quint32 value)
{
    for (int i = 0; i < width; ++i) {
        const int bit = start + i;
        const unsigned mask = 1u << (7 - bit % 8);
        const bool on = (value >> (width - 1 - i)) & 1u;
        auto byte = static_cast<unsigned char>(entry[bit / 8]);
        byte = on ? (byte | mask) : (byte & ~mask);
        entry[bit / 8] = static_cast<char>(byte);
    }
}

QString decodeName(const char *entry)
{
    QString name;
    name.reserve(16);
    for (int i = 0; i < 16; ++i) {
        const quint32 c = readBits(entry, i * 7, 7);
        name.append((c >= 0x20 && c < 0x7f) ? QChar(char(c)) : QChar('?'));
    }
    return name.trimmed();
}

bool parse(const QByteArray &data, Backup *out, QString *error)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (data.size() < 32)
        return fail(QStringLiteral("File is too small to be an FA backup."));
    if (data.mid(2, 4) != "SVD1")
        return fail(QStringLiteral("Not a Roland SVD1 backup file."));

    const int headerLength = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData()));
    const int tableEnd = 2 + headerLength;
    if (tableEnd < 32 || tableEnd > data.size() || (tableEnd - 16) % 16 != 0)
        return fail(QStringLiteral("Backup header is damaged."));

    Backup b;
    b.data = data;
    const int areaCount = (tableEnd - 16) / 16;
    for (int i = 0; i < areaCount; ++i) {
        const int d = 16 + i * 16;
        Area a;
        a.id = data.mid(d, 8);
        a.offset = int(be32(data, d + 8));
        a.length = int(be32(data, d + 12));
        if (a.offset < tableEnd || a.length < 16 || qint64(a.offset) + a.length > data.size())
            return fail(QStringLiteral("Backup area %1 is out of range.").arg(QString::fromLatin1(a.id.toHex())));
        if (a.id.mid(4) != "MI73")
            return fail(QStringLiteral("This backup is not from an FA-06/07/08."));
        a.count = int(be32(data, a.offset));
        a.entrySize = int(be32(data, a.offset + 4));
        a.entryOffset = int(be32(data, a.offset + 8));
        if (a.count < 0 || a.entrySize <= 0 || a.entryOffset < 16
            || qint64(a.entryOffset) + qint64(a.count) * a.entrySize > a.length)
            return fail(QStringLiteral("Backup area %1 has an unexpected layout.")
                            .arg(QString::fromLatin1(a.id.left(4))));
        if (a.id == "PRFbMI73")
            b.studioSetArea = b.areas.size();
        else if (a.id == "PRXaMI73")
            b.companionArea = b.areas.size();
        else if (a.id == "VISaMI73")
            b.favoriteArea = b.areas.size();
        b.areas.push_back(a);
    }

    if (b.studioSetArea < 0 || b.companionArea < 0 || b.favoriteArea < 0)
        return fail(QStringLiteral("The backup is missing Studio Set or Favorite data."));
    const Area &ss = b.areas[b.studioSetArea];
    const Area &cx = b.areas[b.companionArea];
    const Area &fav = b.areas[b.favoriteArea];
    if (ss.count != kUserSlots || ss.entrySize != kStudioSetEntrySize
        || cx.count != kUserSlots || cx.entrySize != kCompanionEntrySize
        || fav.count != kFavoriteCount || fav.entrySize != kFavoriteEntrySize)
        return fail(QStringLiteral("This backup's Studio Set layout is not one FA Editor has been verified with "
                                   "(possibly a different FA firmware). Nothing was changed."));

    for (int i = 0; i < kUserSlots; ++i)
        b.names.push_back(decodeName(data.constData() + ss.entryPos(i)));

    *out = std::move(b);
    return true;
}

QByteArray applyOrder(const Backup &backup, const QVector<int> &newToOld, int *favoritesUpdated,
                      QString *error)
{
    if (!isPermutation(newToOld)) {
        if (error)
            *error = QStringLiteral("Internal error: the new order is not a complete rearrangement of 512 slots.");
        return {};
    }
    QVector<int> oldToNew(kUserSlots);
    for (int n = 0; n < kUserSlots; ++n)
        oldToNew[newToOld[n]] = n;

    QByteArray out = backup.data;
    char *dst = out.data();
    const char *src = backup.data.constData();
    for (const int areaIndex : {backup.studioSetArea, backup.companionArea}) {
        const Area &a = backup.areas[areaIndex];
        for (int n = 0; n < kUserSlots; ++n)
            memcpy(dst + a.entryPos(n), src + a.entryPos(newToOld[n]), size_t(a.entrySize));
    }

    int updated = 0;
    const Area &fav = backup.areas[backup.favoriteArea];
    for (int i = 0; i < fav.count; ++i) {
        char *entry = dst + fav.entryPos(i);
        int oldSlot = -1;
        if (!favoriteUserSlot(entry, &oldSlot))
            continue;
        const int newSlot = oldToNew[oldSlot];
        if (newSlot == oldSlot)
            continue;
        writeBits(entry, kFavLsbBit, 7, quint32(newSlot / 128));
        writeBits(entry, kFavPcBit, 7, quint32(newSlot % 128));
        ++updated;
    }
    if (favoritesUpdated)
        *favoritesUpdated = updated;
    return out;
}

bool verify(const Backup &source, const QByteArray &result, const QVector<int> &newToOld, QString *error)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = QStringLiteral("Safety check failed: ") + e;
        return false;
    };
    if (!isPermutation(newToOld))
        return fail(QStringLiteral("order is not a permutation"));
    if (result.size() != source.data.size())
        return fail(QStringLiteral("file size changed"));

    Backup reparsed;
    QString parseError;
    if (!parse(result, &reparsed, &parseError))
        return fail(parseError);

    // Bytes outside the touched entry ranges must be identical.
    QVector<bool> touched(source.data.size(), false);
    for (const int areaIndex : {source.studioSetArea, source.companionArea, source.favoriteArea}) {
        const Area &a = source.areas[areaIndex];
        for (int i = a.entryPos(0); i < a.entryPos(a.count); ++i)
            touched[i] = true;
    }
    for (int i = 0; i < source.data.size(); ++i)
        if (!touched[i] && source.data[i] != result[i])
            return fail(QStringLiteral("unrelated data changed at byte %1").arg(i));

    for (const int areaIndex : {source.studioSetArea, source.companionArea}) {
        const Area &a = source.areas[areaIndex];
        for (int n = 0; n < kUserSlots; ++n)
            if (memcmp(result.constData() + a.entryPos(n), source.data.constData() + a.entryPos(newToOld[n]),
                       size_t(a.entrySize)) != 0)
                return fail(QStringLiteral("slot %1 does not hold the expected record").arg(n + 1));
    }
    for (int n = 0; n < kUserSlots; ++n)
        if (reparsed.names[n] != source.names[newToOld[n]])
            return fail(QStringLiteral("slot %1 name mismatch").arg(n + 1));

    QVector<int> oldToNew(kUserSlots);
    for (int n = 0; n < kUserSlots; ++n)
        oldToNew[newToOld[n]] = n;
    const Area &fav = source.areas[source.favoriteArea];
    for (int i = 0; i < fav.count; ++i) {
        const char *before = source.data.constData() + fav.entryPos(i);
        const char *after = result.constData() + fav.entryPos(i);
        int oldSlot = -1;
        if (!favoriteUserSlot(before, &oldSlot)) {
            if (memcmp(before, after, size_t(fav.entrySize)) != 0)
                return fail(QStringLiteral("favorite %1 changed unexpectedly").arg(i + 1));
            continue;
        }
        int newSlot = -1;
        if (!favoriteUserSlot(after, &newSlot) || newSlot != oldToNew[oldSlot])
            return fail(QStringLiteral("favorite %1 was not renumbered correctly").arg(i + 1));
        // Everything except the LSB/PC fields must be untouched.
        QByteArray a(before, fav.entrySize), b(after, fav.entrySize);
        writeBits(a.data(), kFavLsbBit, 14, 0);
        writeBits(b.data(), kFavLsbBit, 14, 0);
        if (a != b)
            return fail(QStringLiteral("favorite %1 changed beyond its slot number").arg(i + 1));
    }
    return true;
}

} // namespace svdorder

// ---------------------------------------------------------------------------

SvdStudioSetOrderModel::SvdStudioSetOrderModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SvdStudioSetOrderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_newToOld.size();
}

bool SvdStudioSetOrderModel::isEmptySlot(int oldIndex) const
{
    return m_backup.names.value(oldIndex).compare(QLatin1String("INIT STUDIO"), Qt::CaseInsensitive) == 0;
}

QVariant SvdStudioSetOrderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_newToOld.size())
        return {};
    const int row = index.row();
    const int old = m_newToOld[row];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return m_backup.names.value(old);
    case SlotRole:
        return row + 1;
    case OriginalSlotRole:
        return old + 1;
    case EmptyRole:
        return isEmptySlot(old);
    case MovedRole:
        return old != row;
    default:
        return {};
    }
}

QHash<int, QByteArray> SvdStudioSetOrderModel::roleNames() const
{
    return {{SlotRole, "slot"},
            {NameRole, "name"},
            {OriginalSlotRole, "originalSlot"},
            {EmptyRole, "empty"},
            {MovedRole, "moved"}};
}

int SvdStudioSetOrderModel::usedCount() const
{
    int n = 0;
    for (int i = 0; i < m_backup.names.size(); ++i)
        if (!isEmptySlot(i))
            ++n;
    return n;
}

int SvdStudioSetOrderModel::changedCount() const
{
    int n = 0;
    for (int i = 0; i < m_newToOld.size(); ++i)
        if (m_newToOld[i] != i)
            ++n;
    return n;
}

QUrl SvdStudioSetOrderModel::suggestedSaveUrl() const
{
    if (m_sourcePath.isEmpty())
        return {};
    const QFileInfo fi(m_sourcePath);
    // The FA lists backups by file name; keep it short and distinct from the original.
    return QUrl::fromLocalFile(fi.absoluteDir().filePath(QStringLiteral("REORDER.SVD")));
}

void SvdStudioSetOrderModel::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

void SvdStudioSetOrderModel::setStatus(const QString &s)
{
    if (m_statusText == s)
        return;
    m_statusText = s;
    emit statusTextChanged();
}

bool SvdStudioSetOrderModel::loadFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Could not open %1.").arg(QFileInfo(path).fileName()));
        return false;
    }
    if (!loadData(f.readAll(), QFileInfo(path).fileName()))
        return false;
    m_sourcePath = path;
    emit loadedChanged();
    return true;
}

bool SvdStudioSetOrderModel::loadData(const QByteArray &data, const QString &name)
{
    svdorder::Backup b;
    QString err;
    if (!svdorder::parse(data, &b, &err)) {
        setError(err);
        return false;
    }
    beginResetModel();
    m_backup = std::move(b);
    m_newToOld.resize(svdorder::kUserSlots);
    for (int i = 0; i < svdorder::kUserSlots; ++i)
        m_newToOld[i] = i;
    m_sourceName = name;
    m_sourcePath.clear();
    endResetModel();
    setError({});
    setStatus(QStringLiteral("Loaded %1 · %2 of 512 User slots in use").arg(name).arg(usedCount()));
    emit loadedChanged();
    emit orderChanged();
    return true;
}

bool SvdStudioSetOrderModel::move(int from, int to)
{
    const int n = m_newToOld.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return false;
    // Qt's beginMoveRows destination is the row *before which* the item lands.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to))
        return false;
    m_newToOld.move(from, to);
    endMoveRows();
    // Slot numbers and "moved" flags change for every row in between.
    emit dataChanged(index(qMin(from, to)), index(qMax(from, to)));
    emit orderChanged();
    setStatus(QStringLiteral("Moved \"%1\" to User %2").arg(m_backup.names.value(m_newToOld[to])).arg(to + 1, 3, 10, QChar('0')));
    return true;
}

bool SvdStudioSetOrderModel::moveToSlot(int row, int slot1Based)
{
    return move(row, slot1Based - 1);
}

bool SvdStudioSetOrderModel::swap(int a, int b)
{
    const int n = m_newToOld.size();
    if (a < 0 || a >= n || b < 0 || b >= n || a == b)
        return false;
    std::swap(m_newToOld[a], m_newToOld[b]);
    emit dataChanged(index(a), index(a));
    emit dataChanged(index(b), index(b));
    emit orderChanged();
    setStatus(QStringLiteral("Swapped User %1 and User %2").arg(a + 1, 3, 10, QChar('0')).arg(b + 1, 3, 10, QChar('0')));
    return true;
}

QString SvdStudioSetOrderModel::nameAt(int row) const
{
    if (row < 0 || row >= m_newToOld.size())
        return {};
    return m_backup.names.value(m_newToOld[row]);
}

void SvdStudioSetOrderModel::packUsedToTop()
{
    if (m_newToOld.isEmpty())
        return;
    QVector<int> used, empty;
    for (int old : std::as_const(m_newToOld))
        (isEmptySlot(old) ? empty : used).push_back(old);
    beginResetModel();
    m_newToOld = used + empty;
    endResetModel();
    emit orderChanged();
    setStatus(QStringLiteral("Moved all %1 used sets to the top").arg(used.size()));
}

void SvdStudioSetOrderModel::reset()
{
    if (m_newToOld.isEmpty())
        return;
    beginResetModel();
    for (int i = 0; i < m_newToOld.size(); ++i)
        m_newToOld[i] = i;
    endResetModel();
    emit orderChanged();
    setStatus(QStringLiteral("Back to the original order"));
}

QByteArray SvdStudioSetOrderModel::buildResult(QString *error, int *favoritesUpdated) const
{
    if (!loaded()) {
        if (error)
            *error = QStringLiteral("Open an FA backup first.");
        return {};
    }
    QByteArray out = svdorder::applyOrder(m_backup, m_newToOld, favoritesUpdated, error);
    if (out.isEmpty())
        return {};
    if (!svdorder::verify(m_backup, out, m_newToOld, error))
        return {};
    return out;
}

namespace {

// The FA stores each backup as a pair: NAME.SVD plus NAME.BIN. Restore fails
// with a read error when the .BIN partner is missing.
QString companionBinPath(const QString &svdPath)
{
    const QFileInfo fi(svdPath);
    const bool lower = fi.suffix() == fi.suffix().toLower() && !fi.suffix().isEmpty()
                       && fi.suffix() != fi.suffix().toUpper();
    return fi.dir().filePath(fi.completeBaseName() + (lower ? QStringLiteral(".bin") : QStringLiteral(".BIN")));
}

QString existingCompanionBin(const QString &svdPath)
{
    const QFileInfo fi(svdPath);
    for (const QString &ext : {QStringLiteral(".BIN"), QStringLiteral(".bin"), QStringLiteral(".Bin")}) {
        const QString p = fi.dir().filePath(fi.completeBaseName() + ext);
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

} // namespace

QByteArray SvdStudioSetOrderModel::companionBinData() const
{
    // Use the original backup's .BIN when it sits next to the .SVD.
    const QString src = m_sourcePath.isEmpty() ? QString() : existingCompanionBin(m_sourcePath);
    if (!src.isEmpty()) {
        QFile f(src);
        if (f.open(QIODevice::ReadOnly) && f.size() <= 1024 * 1024)
            return f.readAll();
    }
    // Every FA backup observed so far pairs the .SVD with 1024 zero bytes.
    return QByteArray(1024, '\0');
}

bool SvdStudioSetOrderModel::saveFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!m_sourcePath.isEmpty() && QFileInfo(path).canonicalFilePath() == QFileInfo(m_sourcePath).canonicalFilePath()) {
        setError(QStringLiteral("Save under a new name so your original backup stays untouched."));
        return false;
    }
    QString err;
    int favorites = 0;
    const QByteArray out = buildResult(&err, &favorites);
    if (out.isEmpty()) {
        setError(err);
        return false;
    }
    const QString binPath = companionBinPath(path);
    const QByteArray bin = companionBinData();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit()) {
        setError(QStringLiteral("Could not write %1.").arg(QFileInfo(path).fileName()));
        return false;
    }
    QSaveFile b(binPath);
    if (!b.open(QIODevice::WriteOnly) || b.write(bin) != bin.size() || !b.commit()) {
        setError(QStringLiteral("Could not write %1 (the FA needs it next to the .SVD).")
                     .arg(QFileInfo(binPath).fileName()));
        return false;
    }
    setError({});
    setStatus(QStringLiteral("Saved %1 + %2 · %3 slots changed, %4 favorite(s) updated. "
                             "Copy BOTH files to the SD card and Restore on the FA.")
                  .arg(QFileInfo(path).fileName())
                  .arg(QFileInfo(binPath).fileName())
                  .arg(changedCount())
                  .arg(favorites));
    return true;
}
