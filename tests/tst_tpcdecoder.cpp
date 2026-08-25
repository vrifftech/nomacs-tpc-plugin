#include <QtTest>
#include <QtEndian>
#include <QColor>

#include "tpcdecoder.h"

namespace {

void put16(QByteArray &bytes, int offset, quint16 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void put32(QByteArray &bytes, int offset, quint32 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

QByteArray tpcHeader(quint16 width, quint16 height, quint8 encoding, quint8 mipCount,
                     quint32 dataSize = 0)
{
    QByteArray bytes(TpcDecoder::HeaderSize, '\0');
    put32(bytes, 0, dataSize);
    put16(bytes, 8, width);
    put16(bytes, 10, height);
    bytes[12] = char(encoding);
    bytes[13] = char(mipCount);
    return bytes;
}

} // namespace

class TestTpcDecoder final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsShortHeader()
    {
        QVERIFY(!TpcDecoder::probe(QByteArray(127, '\0')));
    }

    void rejectsInvalidDimensions()
    {
        QVERIFY(!TpcDecoder::probe(tpcHeader(0, 4, 2, 1)));
        QVERIFY(!TpcDecoder::probe(tpcHeader(4, 0, 2, 1)));
    }

    void acceptsZeroMipCountAsOne()
    {
        QByteArray bytes = tpcHeader(1, 1, 4, 0);
        bytes.append(QByteArray::fromHex("01020304"));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.size(), QSize(1, 1));
    }

    void decodesGrayscale()
    {
        QByteArray bytes = tpcHeader(2, 1, 1, 1);
        bytes.append(char(0));
        bytes.append(char(255));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 0, 255));
        QCOMPARE(image.pixelColor(1, 0), QColor(255, 255, 255, 255));
    }

    void decodesRgba()
    {
        QByteArray bytes = tpcHeader(1, 1, 4, 1);
        bytes.append(QByteArray::fromHex("11223344"));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 0), QColor(0x11, 0x22, 0x33, 0x44));
    }

    void flipsRawTpcToTopLeftOrientation()
    {
        QByteArray bytes = tpcHeader(1, 2, 4, 1);
        bytes.append(QByteArray::fromHex("ff0000ff0000ffff")); // bottom red, then top blue
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 255, 255));
        QCOMPARE(image.pixelColor(0, 1), QColor(255, 0, 0, 255));
    }

    void acceptsSizedGrayscalePayload()
    {
        QByteArray bytes = tpcHeader(1, 2, 1, 1, 2);
        bytes.append(QByteArray::fromHex("1020"));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 0), QColor(0x20, 0x20, 0x20, 255));
        QCOMPARE(image.pixelColor(0, 1), QColor(0x10, 0x10, 0x10, 255));
    }

    void decodesSwizzledBgra()
    {
        QByteArray bytes = tpcHeader(1, 2, 0x0c, 1);
        bytes.append(QByteArray::fromHex("0000ffffff0000ff")); // bottom red, then top blue in BGRA
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 255, 255));
        QCOMPARE(image.pixelColor(0, 1), QColor(255, 0, 0, 255));
    }

    void decodesBc1SolidRed()
    {
        QByteArray bytes = tpcHeader(4, 4, 2, 1, 8);
        bytes.append(QByteArray::fromHex("00f8000000000000"));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(3, 3), QColor(255, 0, 0, 255));
    }

    void flipsBc1TpcToTopLeftOrientation()
    {
        QByteArray bytes = tpcHeader(4, 4, 2, 1, 8);
        bytes.append(QByteArray::fromHex("00f81f0000005555"));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 255, 255));
        QCOMPARE(image.pixelColor(0, 3), QColor(255, 0, 0, 255));
    }

    void decodesBc3Alpha()
    {
        QByteArray bytes = tpcHeader(4, 4, 4, 1, 16);
        bytes.append(QByteArray::fromHex("ffff00000000000000f8000000000000"));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0, 255));
    }

    void roundsBc1Interpolation()
    {
        QByteArray bytes = tpcHeader(4, 4, 2, 1, 8);
        bytes.append(QByteArray::fromHex("0010000002000000"));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 3), QColor(11, 0, 0, 255));
    }

    void roundsBc3AlphaInterpolation()
    {
        QByteArray bytes = tpcHeader(4, 4, 4, 1, 16);
        bytes.append(QByteArray::fromHex("0a0002000000000000f8000000000000"));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 3), QColor(255, 0, 0, 9));
    }

    void decodesFirstAnimationFrame()
    {
        QByteArray bytes = tpcHeader(8, 4, 2, 1, 16);
        bytes.append(QByteArray::fromHex("00f8000000000000"));
        bytes.append(QByteArray::fromHex("1f00000000000000"));
        bytes.append("proceduretype cycle\nnumx 2\nnumy 1\nfps 12\n");
        TpcDecoder decoder(bytes);
        QCOMPARE(decoder.size(), QSize(4, 4));
        QVERIFY(decoder.embeddedTxi().contains(QStringLiteral("proceduretype cycle")));
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0, 255));
    }

    void exposesFirstCubeMapFace()
    {
        QByteArray bytes = tpcHeader(4, 24, 2, 1, 8);
        bytes.append(QByteArray::fromHex("00f8000000000000"));
        TpcDecoder decoder(bytes);
        QCOMPARE(decoder.size(), QSize(4, 4));
        QImage image;
        QVERIFY2(decoder.decode(&image), qPrintable(decoder.errorString()));
        QCOMPARE(image.pixelColor(3, 3), QColor(255, 0, 0, 255));
    }

    void rejectsTruncatedPayload()
    {
        QByteArray bytes = tpcHeader(4, 4, 4, 1, 16);
        bytes.append(QByteArray(15, '\0'));
        TpcDecoder decoder(bytes);
        QImage image;
        QVERIFY(!decoder.decode(&image));
    }
};

QTEST_MAIN(TestTpcDecoder)
#include "tst_tpcdecoder.moc"
