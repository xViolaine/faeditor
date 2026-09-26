#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

/**
 * Reordering of User Studio Sets inside an FA SD-card backup (SVD1 / MI73).
 *
 * The FA has no SysEx command that stores into a User Studio Set slot, so
 * slots are rearranged offline in a backup file which the user then restores
 * on the instrument (UTILITY → Restore).
 *
 * Only whole records are moved; no Studio Set parameter is decoded or
 * re-encoded. Three areas are touched:
 *   - PRFbMI73: 512 Studio Set records (one per User slot)
 *   - PRXaMI73: 512 per-slot companion records (moved in lockstep)
 *   - VISaMI73: Favorites; entries that select a User Studio Set
 *               (Bank MSB 85, LSB 0–3) are renumbered to follow their set.
 * Every other byte of the file is kept verbatim.
 */
namespace svdorder {

constexpr int kUserSlots = 512;

struct Area {
    QByteArray id;          // 8-byte area identifier, e.g. "PRFbMI73"
    int offset = 0;         // absolute file offset of the area
    int length = 0;         // area length in bytes
    int count = 0;          // number of entries
    int entrySize = 0;      // packed bytes per entry
    int entryOffset = 0;    // offset from area start to the first entry

    int entryPos(int index) const { return offset + entryOffset + index * entrySize; }
};

struct Backup {
    QByteArray data;
    QVector<Area> areas;
    int studioSetArea = -1;   // PRFbMI73
    int companionArea = -1;   // PRXaMI73
    int favoriteArea = -1;    // VISaMI73
    QStringList names;        // 512 Studio Set names, trimmed
};

/** Read `width` bits (<= 32) starting at MSB-first bit `start`. */
quint32 readBits(const char *entry, int start, int width);
/** Write `width` bits (<= 32) starting at MSB-first bit `start`. */
void writeBits(char *entry, int start, int width, quint32 value);

/** Studio Set name: the first 16 packed 7-bit ASCII characters of a record. */
QString decodeName(const char *entry);

/** Parse and validate an FA backup. Rejects anything that is not SVD1/MI73 with the expected layout. */
bool parse(const QByteArray &data, Backup *out, QString *error);

/**
 * Build a new backup where User slot i holds the Studio Set that was in slot
 * newToOld[i]. `newToOld` must be a permutation of 0..511.
 */
QByteArray applyOrder(const Backup &backup, const QVector<int> &newToOld,
                      int *favoritesUpdated, QString *error);

/**
 * Check that `result` is `source` rearranged by `newToOld`: same size, same
 * bytes outside the three touched areas, records moved exactly, names in the
 * expected order and every favorite either unchanged or renumbered correctly.
 */
bool verify(const Backup &source, const QByteArray &result, const QVector<int> &newToOld,
            QString *error);

} // namespace svdorder

class SvdStudioSetOrderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY loadedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int usedCount READ usedCount NOTIFY loadedChanged)
    Q_PROPERTY(int changedCount READ changedCount NOTIFY orderChanged)
    Q_PROPERTY(QUrl suggestedSaveUrl READ suggestedSaveUrl NOTIFY loadedChanged)

public:
    enum Roles {
        SlotRole = Qt::UserRole + 1, // new 1-based slot number
        NameRole,
        OriginalSlotRole,            // 1-based slot in the loaded backup
        EmptyRole,                   // INIT STUDIO
        MovedRole
    };

    explicit SvdStudioSetOrderModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool loaded() const { return !m_backup.data.isEmpty(); }
    QString sourceName() const { return m_sourceName; }
    QString lastError() const { return m_lastError; }
    QString statusText() const { return m_statusText; }
    int usedCount() const;
    int changedCount() const;
    QUrl suggestedSaveUrl() const;

    Q_INVOKABLE bool loadFile(const QUrl &url);
    bool loadData(const QByteArray &data, const QString &name);
    /** Move the set at row `from` to row `to`, shifting the sets in between. */
    Q_INVOKABLE bool move(int from, int to);
    /** Same as move(), with a 1-based slot number as the destination. */
    Q_INVOKABLE bool moveToSlot(int row, int slot1Based);
    Q_INVOKABLE bool swap(int a, int b);
    Q_INVOKABLE QString nameAt(int row) const;
    /** Move all used sets to the top, keeping their relative order. */
    Q_INVOKABLE void packUsedToTop();
    Q_INVOKABLE void reset();
    Q_INVOKABLE bool saveFile(const QUrl &url);
    QByteArray buildResult(QString *error, int *favoritesUpdated = nullptr) const;

    const QVector<int> &order() const { return m_newToOld; }

signals:
    void loadedChanged();
    void lastErrorChanged();
    void statusTextChanged();
    void orderChanged();

private:
    void setError(const QString &e);
    void setStatus(const QString &s);
    bool isEmptySlot(int oldIndex) const;

    svdorder::Backup m_backup;
    QVector<int> m_newToOld;
    QString m_sourceName;
    QString m_sourcePath;
    QString m_lastError;
    QString m_statusText;
};
