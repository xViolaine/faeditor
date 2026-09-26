#include "project/SvdStudioSetOrder.h"

#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

using namespace svdorder;

namespace {

void putBe32(QByteArray &d, int pos, quint32 v)
{
    qToBigEndian(v, reinterpret_cast<uchar *>(d.data() + pos));
}

void putName(char *entry, const QString &name)
{
    const QString padded = name.leftJustified(16, QChar(' '), true);
    for (int i = 0; i < 16; ++i)
        writeBits(entry, i * 7, 7, quint32(padded.at(i).toLatin1()));
}

struct AreaSpec {
    QByteArray id;
    int count;
    int size;
};

// Original, minimal SVD1/MI73 container with the areas this feature relies on
// plus one unrelated area that must never change.
QByteArray makeBackup()
{
    const QList<AreaSpec> specs = {
        {"PRFbMI73", 512, 1460},
        {"SYSaMI73", 1, 128},
        {"VISaMI73", 100, 11},
        {"PRXaMI73", 512, 392},
    };
    const int tableEnd = 16 + 16 * specs.size();
    int total = tableEnd;
    for (const auto &s : specs)
        total += 16 + s.count * s.size;

    QByteArray d(total, '\0');
    qToBigEndian(quint16(tableEnd - 2), reinterpret_cast<uchar *>(d.data()));
    d.replace(2, 4, "SVD1");
    int offset = tableEnd;
    for (int i = 0; i < specs.size(); ++i) {
        const auto &s = specs[i];
        const int len = 16 + s.count * s.size;
        d.replace(16 + i * 16, 8, s.id);
        putBe32(d, 16 + i * 16 + 8, quint32(offset));
        putBe32(d, 16 + i * 16 + 12, quint32(len));
        putBe32(d, offset, quint32(s.count));
        putBe32(d, offset + 4, quint32(s.size));
        putBe32(d, offset + 8, 16);
        char *entries = d.data() + offset + 16;
        for (int e = 0; e < s.count; ++e) {
            char *entry = entries + e * s.size;
            if (s.id == "PRFbMI73") {
                putName(entry, e < 20 ? QStringLiteral("Song %1").arg(e + 1) : QStringLiteral("INIT STUDIO"));
                for (int b = 16; b < s.size; ++b)
                    entry[b] = char((e * 31 + b) & 0xff);
            } else if (s.id == "PRXaMI73") {
                for (int b = 0; b < s.size; ++b)
                    entry[b] = char((e * 7 + b * 3) & 0xff);
            } else if (s.id == "SYSaMI73") {
                for (int b = 0; b < s.size; ++b)
                    entry[b] = char(b);
            }
        }
        offset += len;
    }

    // Favorites: 0 → Preset 65, 1 → User 2, 2 → User 300, 3 → a tone (MSB 87), rest empty.
    const int favPos = tableEnd + 16 + 512 * 1460 + 16 + 128 + 16;
    auto fav = [&](int i, quint32 kind, quint32 msb, quint32 lsb, quint32 pc, char tail) {
        char *e = d.data() + favPos + i * 11;
        writeBits(e, 0, 3, kind);
        writeBits(e, 3, 7, msb);
        writeBits(e, 10, 7, lsb);
        writeBits(e, 17, 7, pc);
        e[5] = tail;
    };
    fav(0, 7, 85, 64, 64, 0);
    fav(1, 6, 85, 0, 1, 0x5a);
    fav(2, 6, 85, 2, 43, 0x33);
    fav(3, 4, 87, 68, 71, 0x11);
    return d;
}

QVector<int> identity()
{
    QVector<int> v(kUserSlots);
    for (int i = 0; i < kUserSlots; ++i)
        v[i] = i;
    return v;
}

int favoriteSlot(const QByteArray &d, const Backup &b, int i)
{
    const char *e = d.constData() + b.areas[b.favoriteArea].entryPos(i);
    return int(readBits(e, 10, 7) * 128 + readBits(e, 17, 7));
}

} // namespace

class TestSvdReorder : public QObject
{
    Q_OBJECT

private slots:
    void bitsRoundTrip()
    {
        char buf[4] = {};
        writeBits(buf, 3, 7, 85);
        writeBits(buf, 10, 7, 3);
        writeBits(buf, 17, 7, 127);
        QCOMPARE(readBits(buf, 3, 7), 85u);
        QCOMPARE(readBits(buf, 10, 7), 3u);
        QCOMPARE(readBits(buf, 17, 7), 127u);
        QCOMPARE(readBits(buf, 0, 3), 0u);
    }

    void parsesNames()
    {
        Backup b;
        QString err;
        QVERIFY2(parse(makeBackup(), &b, &err), qPrintable(err));
        QCOMPARE(b.names.size(), 512);
        QCOMPARE(b.names.at(0), QStringLiteral("Song 1"));
        QCOMPARE(b.names.at(19), QStringLiteral("Song 20"));
        QCOMPARE(b.names.at(20), QStringLiteral("INIT STUDIO"));
    }

    void rejectsOtherLayouts()
    {
        Backup b;
        QString err;
        QByteArray d = makeBackup();
        d.replace(2, 4, "SVD0");
        QVERIFY(!parse(d, &b, &err));
        d = makeBackup();
        d.truncate(d.size() - 100);
        QVERIFY(!parse(d, &b, &err));
        d = makeBackup();
        d.replace(16 + 4, 4, "MI69"); // Integra-7 style area id
        QVERIFY(!parse(d, &b, &err));
    }

    void identityIsByteIdentical()
    {
        Backup b;
        QVERIFY(parse(makeBackup(), &b, nullptr));
        int favs = -1;
        const QByteArray out = applyOrder(b, identity(), &favs, nullptr);
        QCOMPARE(out, b.data);
        QCOMPARE(favs, 0);
    }

    void modelMoveShiftsAndRenumbersFavorites()
    {
        SvdStudioSetOrderModel m;
        QVERIFY(m.loadData(makeBackup(), QStringLiteral("test.svd")));
        QCOMPARE(m.usedCount(), 20);
        // Move Song 2 (row 1) to slot 10: Songs 3..10 shift up by one.
        QVERIFY(m.moveToSlot(1, 10));
        QCOMPARE(m.data(m.index(9), SvdStudioSetOrderModel::NameRole).toString(), QStringLiteral("Song 2"));
        QCOMPARE(m.data(m.index(1), SvdStudioSetOrderModel::NameRole).toString(), QStringLiteral("Song 3"));
        QCOMPARE(m.data(m.index(10), SvdStudioSetOrderModel::NameRole).toString(), QStringLiteral("Song 11"));
        QCOMPARE(m.changedCount(), 9);

        QString err;
        int favs = 0;
        const QByteArray out = m.buildResult(&err, &favs);
        QVERIFY2(!out.isEmpty(), qPrintable(err));
        QCOMPARE(favs, 1);
        Backup before, after;
        QVERIFY(parse(makeBackup(), &before, nullptr));
        QVERIFY(parse(out, &after, nullptr));
        QCOMPARE(after.names.at(9), QStringLiteral("Song 2"));
        QCOMPARE(favoriteSlot(out, after, 1), 9);   // followed Song 2
        QCOMPARE(favoriteSlot(out, after, 2), 299); // untouched slot
        const auto &fav = after.areas[after.favoriteArea];
        QCOMPARE(out.mid(fav.entryPos(0), 11), before.data.mid(fav.entryPos(0), 11)); // preset favorite
        QCOMPARE(out.mid(fav.entryPos(3), 11), before.data.mid(fav.entryPos(3), 11)); // tone favorite
        QCOMPARE(out.at(fav.entryPos(1) + 5), char(0x5a)); // rest of the entry preserved
    }

    void swapAndPack()
    {
        SvdStudioSetOrderModel m;
        QVERIFY(m.loadData(makeBackup(), QStringLiteral("test.svd")));
        QVERIFY(m.swap(0, 299));
        QCOMPARE(m.data(m.index(299), SvdStudioSetOrderModel::NameRole).toString(), QStringLiteral("Song 1"));
        m.packUsedToTop();
        for (int i = 0; i < 20; ++i)
            QVERIFY(!m.data(m.index(i), SvdStudioSetOrderModel::EmptyRole).toBool());
        QCOMPARE(m.data(m.index(19), SvdStudioSetOrderModel::NameRole).toString(), QStringLiteral("Song 1"));
        QString err;
        QVERIFY2(!m.buildResult(&err).isEmpty(), qPrintable(err));
        m.reset();
        QCOMPARE(m.changedCount(), 0);
    }

    void verifyCatchesCorruption()
    {
        Backup b;
        QVERIFY(parse(makeBackup(), &b, nullptr));
        QVector<int> order = identity();
        order.move(1, 9);
        QByteArray out = applyOrder(b, order, nullptr, nullptr);
        QString err;
        QVERIFY2(verify(b, out, order, &err), qPrintable(err));
        QByteArray bad = out;
        bad[b.areas[1].entryPos(0) + 5] ^= 1; // unrelated area
        QVERIFY(!verify(b, bad, order, &err));
        bad = out;
        bad[b.areas[b.companionArea].entryPos(9)] ^= 1; // companion record not moved correctly
        QVERIFY(!verify(b, bad, order, &err));
        QVector<int> notPerm = order;
        notPerm[0] = notPerm[1];
        QVERIFY(applyOrder(b, notPerm, nullptr, &err).isEmpty());
    }

    void saveWritesSvdAndBinPair()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString src = dir.filePath(QStringLiteral("031026.SVD"));
        QFile out(src);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(makeBackup());
        out.close();
        QByteArray origBin(1024, '\0');
        origBin[7] = 0x42; // prove the original .BIN is copied, not regenerated
        QFile ob(dir.filePath(QStringLiteral("031026.BIN")));
        QVERIFY(ob.open(QIODevice::WriteOnly));
        ob.write(origBin);
        ob.close();

        SvdStudioSetOrderModel m;
        QVERIFY(m.loadFile(QUrl::fromLocalFile(src)));
        QVERIFY(!m.saveFile(QUrl::fromLocalFile(src))); // never overwrite the original
        QVERIFY(m.swap(0, 1));
        QVERIFY2(m.saveFile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("RE031026.SVD")))),
                 qPrintable(m.lastError()));
        QFile bin(dir.filePath(QStringLiteral("RE031026.BIN")));
        QVERIFY(bin.open(QIODevice::ReadOnly));
        QCOMPARE(bin.readAll(), origBin);

        // Without an original .BIN next to the source, 1024 zero bytes are written.
        SvdStudioSetOrderModel m2;
        QVERIFY(m2.loadData(makeBackup(), QStringLiteral("x.svd")));
        QVERIFY(m2.swap(0, 1));
        QVERIFY(m2.saveFile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("test.svd")))));
        QFile bin2(dir.filePath(QStringLiteral("test.bin")));
        QVERIFY(bin2.open(QIODevice::ReadOnly));
        QCOMPARE(bin2.readAll(), QByteArray(1024, '\0'));
    }

    // Optional: FAEDITOR_SVD_FIXTURE=/path/to/backup.SVD runs against a real FA backup.
    void realBackupRandomOrders()
    {
        const QByteArray path = qgetenv("FAEDITOR_SVD_FIXTURE");
        if (path.isEmpty())
            QSKIP("FAEDITOR_SVD_FIXTURE not set");
        QFile f(QString::fromLocal8Bit(path));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray data = f.readAll();
        Backup b;
        QString err;
        QVERIFY2(parse(data, &b, &err), qPrintable(err));
        auto *rng = QRandomGenerator::global();
        for (int round = 0; round < 50; ++round) {
            QVector<int> order = identity();
            for (int i = kUserSlots - 1; i > 0; --i)
                std::swap(order[i], order[int(rng->bounded(i + 1))]);
            const QByteArray out = applyOrder(b, order, nullptr, &err);
            QVERIFY2(verify(b, out, order, &err), qPrintable(err));
            // Applying the inverse must give back the original file exactly.
            Backup mid;
            QVERIFY(parse(out, &mid, nullptr));
            QVector<int> inverse(kUserSlots);
            for (int n = 0; n < kUserSlots; ++n)
                inverse[order[n]] = n;
            QCOMPARE(applyOrder(mid, inverse, nullptr, nullptr), data);
        }
    }
};

QTEST_APPLESS_MAIN(TestSvdReorder)
#include "test_svd_reorder.moc"
