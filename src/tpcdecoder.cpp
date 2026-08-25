#include "tpcdecoder.h"

#include <QImageIOHandler>
#include <QRegularExpression>
#include <QStringList>
#include <QtEndian>

#include <array>
#include <limits>
#include <utility>

namespace {

constexpr quint64 MaxDecodedBytes = 512ULL * 1024ULL * 1024ULL;
constexpr quint16 MaxDimension = 32767;

quint16 read16(const char *p)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

quint32 read32(const char *p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

QRgb rgb565(quint16 value)
{
    const int r5 = (value >> 11) & 0x1f;
    const int g6 = (value >> 5) & 0x3f;
    const int b5 = value & 0x1f;
    return qRgba((r5 * 255 + 15) / 31, (g6 * 255 + 31) / 63, (b5 * 255 + 15) / 31, 255);
}

QRgb mix(QRgb a, QRgb b, int wa, int wb, int divisor)
{
    const int rounding = divisor / 2;
    return qRgba((qRed(a) * wa + qRed(b) * wb + rounding) / divisor,
                 (qGreen(a) * wa + qGreen(b) * wb + rounding) / divisor,
                 (qBlue(a) * wa + qBlue(b) * wb + rounding) / divisor,
                 (qAlpha(a) * wa + qAlpha(b) * wb + rounding) / divisor);
}

std::array<QRgb, 4> bc1Palette(const uchar *block, bool allowTransparency)
{
    const quint16 c0 = qFromLittleEndian<quint16>(block);
    const quint16 c1 = qFromLittleEndian<quint16>(block + 2);
    std::array<QRgb, 4> colors{rgb565(c0), rgb565(c1), 0, 0};

    if (c0 > c1 || !allowTransparency) {
        colors[2] = mix(colors[0], colors[1], 2, 1, 3);
        colors[3] = mix(colors[0], colors[1], 1, 2, 3);
    } else {
        colors[2] = mix(colors[0], colors[1], 1, 1, 2);
        colors[3] = qRgba(0, 0, 0, 0);
    }
    return colors;
}

void writeBcColors(QImage *image, int blockX, int blockY, const uchar *block,
                   const std::array<quint8, 16> *alpha, bool allowTransparency)
{
    const auto colors = bc1Palette(block, allowTransparency);
    const quint32 indices = qFromLittleEndian<quint32>(block + 4);
    for (int y = 0; y < 4; ++y) {
        const int sourceY = blockY * 4 + y;
        if (sourceY >= image->height())
            break;
        const int py = image->height() - 1 - sourceY;
        auto *row = reinterpret_cast<QRgb *>(image->scanLine(py));
        for (int x = 0; x < 4; ++x) {
            const int px = blockX * 4 + x;
            if (px >= image->width())
                break;
            const int pixel = y * 4 + x;
            QRgb color = colors[(indices >> (2 * pixel)) & 0x3];
            if (alpha)
                color = qRgba(qRed(color), qGreen(color), qBlue(color), (*alpha)[pixel]);
            row[px] = color;
        }
    }
}

std::array<quint8, 16> bc3Alpha(const uchar *block)
{
    const quint8 a0 = block[0];
    const quint8 a1 = block[1];
    std::array<quint8, 8> table{a0, a1, 0, 0, 0, 0, 0, 0};
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i)
            table[i + 1] = quint8(((7 - i) * int(a0) + i * int(a1) + 3) / 7);
    } else {
        for (int i = 1; i <= 4; ++i)
            table[i + 1] = quint8(((5 - i) * int(a0) + i * int(a1) + 2) / 5);
        table[6] = 0;
        table[7] = 255;
    }

    quint64 bits = 0;
    for (int i = 0; i < 6; ++i)
        bits |= quint64(block[2 + i]) << (8 * i);

    std::array<quint8, 16> alpha{};
    for (int i = 0; i < 16; ++i)
        alpha[i] = table[(bits >> (3 * i)) & 0x7];
    return alpha;
}

bool isPowerOfTwo(quint32 value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

quint32 integerLog2(quint32 value)
{
    quint32 result = 0;
    while (value > 1) {
        value >>= 1;
        ++result;
    }
    return result;
}

quint32 deSwizzleOffset(quint32 x, quint32 y, quint32 width, quint32 height)
{
    quint32 widthBits = integerLog2(width);
    quint32 heightBits = integerLog2(height);
    quint32 offset = 0;
    quint32 shift = 0;
    while (widthBits != 0 || heightBits != 0) {
        if (widthBits != 0) {
            offset |= (x & 1U) << shift++;
            x >>= 1;
            --widthBits;
        }
        if (heightBits != 0) {
            offset |= (y & 1U) << shift++;
            y >>= 1;
            --heightBits;
        }
    }
    return offset;
}

struct TxiLayout {
    QString procedureType;
    quint32 numX = 0;
    quint32 numY = 0;
    quint32 defaultWidth = 0;
    quint32 defaultHeight = 0;
};

TxiLayout parseTxiLayout(const QString &text)
{
    TxiLayout result;
    const auto lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        const qsizetype slashComment = line.indexOf(QStringLiteral("//"));
        const qsizetype hashComment = line.indexOf(QLatin1Char('#'));
        qsizetype comment = slashComment;
        if (comment < 0 || (hashComment >= 0 && hashComment < comment))
            comment = hashComment;
        if (comment >= 0)
            line.truncate(comment);
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;
        const QString key = tokens[0].toLower();
        const QString value = tokens.size() > 1 ? tokens[1] : QString();
        bool ok = false;
        const quint32 number = value.toUInt(&ok);
        if (key == QStringLiteral("proceduretype"))
            result.procedureType = value.toLower();
        else if (key == QStringLiteral("numx") && ok)
            result.numX = number;
        else if (key == QStringLiteral("numy") && ok)
            result.numY = number;
        else if (key == QStringLiteral("defaultwidth") && ok)
            result.defaultWidth = number;
        else if (key == QStringLiteral("defaultheight") && ok)
            result.defaultHeight = number;
    }
    return result;
}

} // namespace

bool TpcDecoder::probe(const QByteArray &header, qsizetype fileSize)
{
    if (header.size() < HeaderSize)
        return false;

    const quint32 dataSize = read32(header.constData());
    const quint16 width = read16(header.constData() + 8);
    const quint16 height = read16(header.constData() + 10);
    const quint8 encoding = quint8(header[12]);
    const quint8 mipCount = quint8(header[13]);

    if (width == 0 || height == 0 || width > MaxDimension || height > MaxDimension || mipCount >= 32)
        return false;
    if (encoding != 1 && encoding != 2 && encoding != 4 && encoding != 0x0c)
        return false;
    if (dataSize != 0 && encoding == 0x0c)
        return false;
    if (fileSize >= 0 && fileSize < HeaderSize)
        return false;
    return true;
}

TpcDecoder::TpcDecoder(QByteArray data)
    : m_data(std::move(data))
{
    parseHeader();
}

bool TpcDecoder::parseHeader()
{
    if (!probe(m_data.left(HeaderSize), m_data.size())) {
        fail(QStringLiteral("Invalid or unsupported TPC header"));
        return false;
    }

    m_header.dataSize = read32(m_data.constData());
    m_header.canvasWidth = read16(m_data.constData() + 8);
    m_header.canvasHeight = read16(m_data.constData() + 10);
    m_header.width = m_header.canvasWidth;
    m_header.height = m_header.canvasHeight;
    m_header.encoding = quint8(m_data[12]);
    m_header.mipCount = quint8(m_data[13]) == 0 ? 1 : quint8(m_data[13]);
    m_header.compressed = m_header.dataSize != 0 &&
                          (m_header.encoding == 2 || m_header.encoding == 4);

    // KOTOR stores compressed cube maps as six square layers while reporting
    // a height six times the width. A flat image cannot represent the cube,
    // so expose the first face instead of mixing later faces with mip data.
    if (m_header.compressed && quint32(m_header.canvasHeight) == quint32(m_header.canvasWidth) * 6U) {
        m_header.cubeMap = true;
        m_header.height = m_header.width;
    }

    if (!parseLayout())
        return false;

    const quint64 decodedBytes = quint64(m_header.width) * m_header.height * 4;
    if (decodedBytes > MaxDecodedBytes) {
        fail(QStringLiteral("TPC dimensions exceed the 512 MiB decoded-image limit"));
        return false;
    }
    m_headerParsed = true;
    return true;
}

bool TpcDecoder::parseLayout()
{
    auto mipSize = [this](quint32 width, quint32 height) -> quint64 {
        if (m_header.compressed && m_header.encoding == 2)
            return qMax<quint64>(8, ((quint64(width) + 3) / 4) * ((quint64(height) + 3) / 4) * 8);
        if (m_header.compressed && m_header.encoding == 4)
            return qMax<quint64>(16, ((quint64(width) + 3) / 4) * ((quint64(height) + 3) / 4) * 16);
        const quint64 channels = m_header.encoding == 1 ? 1 : (m_header.encoding == 2 ? 3 : 4);
        return quint64(width) * height * channels;
    };

    quint32 layerWidth = m_header.cubeMap ? m_header.canvasWidth : m_header.canvasWidth;
    quint32 layerHeight = m_header.cubeMap ? m_header.canvasHeight / 6 : m_header.canvasHeight;
    quint64 oneLayer = m_header.dataSize != 0 ? m_header.dataSize : mipSize(layerWidth, layerHeight);
    quint32 w = layerWidth;
    quint32 h = layerHeight;
    for (quint8 mip = 1; mip < m_header.mipCount; ++mip) {
        w = qMax<quint32>(1, w / 2);
        h = qMax<quint32>(1, h / 2);
        if (oneLayer > std::numeric_limits<quint64>::max() - mipSize(w, h)) {
            fail(QStringLiteral("TPC mip payload size overflow"));
            return false;
        }
        oneLayer += mipSize(w, h);
    }

    quint64 payloadSize = oneLayer;
    if (m_header.cubeMap) {
        if (oneLayer > std::numeric_limits<quint64>::max() / 6) {
            fail(QStringLiteral("TPC cube-map payload size overflow"));
            return false;
        }
        payloadSize = oneLayer * 6;
    }
    const quint64 available = quint64(m_data.size() - HeaderSize);
    const quint64 txiOffset = qMin(payloadSize, available);
    if (txiOffset < available)
        m_txi = QString::fromLatin1(m_data.constData() + HeaderSize + qsizetype(txiOffset),
                                    qsizetype(available - txiOffset));

    if (!m_header.cubeMap && !m_txi.isEmpty()) {
        const TxiLayout layout = parseTxiLayout(m_txi);
        if (layout.procedureType == QStringLiteral("cycle") && layout.numX > 0 && layout.numY > 0) {
            const quint32 width = layout.defaultWidth > 0
                                      ? layout.defaultWidth
                                      : m_header.canvasWidth / layout.numX;
            const quint32 height = layout.defaultHeight > 0
                                       ? layout.defaultHeight
                                       : m_header.canvasHeight / layout.numY;
            if (width == 0 || height == 0 ||
                quint64(width) * layout.numX > m_header.canvasWidth ||
                quint64(height) * layout.numY > m_header.canvasHeight) {
                fail(QStringLiteral("TPC animation grid does not fit the header dimensions"));
                return false;
            }
            m_header.width = quint16(width);
            m_header.height = quint16(height);
            m_header.animated = true;
        }
    }
    return true;
}

bool TpcDecoder::decode(QImage *image)
{
    if (!image) {
        fail(QStringLiteral("No destination image supplied"));
        return false;
    }
    if (!m_headerParsed)
        return false;

    bool ok = false;
    if (!m_header.compressed)
        ok = decodeRaw(image);
    else if (m_header.encoding == 2)
        ok = decodeBc1(image);
    else if (m_header.encoding == 4)
        ok = decodeBc3(image);

    if (!ok && m_error.isEmpty())
        fail(QStringLiteral("Unsupported TPC encoding"));
    return ok;
}

bool TpcDecoder::decodeRaw(QImage *image) const
{
    const int channels = m_header.encoding == 1 ? 1 : (m_header.encoding == 2 ? 3 : 4);
    const quint64 required = quint64(m_header.width) * m_header.height * channels;
    if (!validateTopLevelSize(required))
        return false;

    QImage result;
    if (!QImageIOHandler::allocateImage(QSize(m_header.width, m_header.height),
                                        QImage::Format_RGBA8888, &result))
        return false;

    const auto *payload = reinterpret_cast<const uchar *>(m_data.constData() + HeaderSize);
    const bool swizzled = m_header.encoding == 0x0c && isPowerOfTwo(m_header.width);
    for (int sourceY = 0; sourceY < result.height(); ++sourceY) {
        uchar *dst = result.scanLine(result.height() - 1 - sourceY);
        for (int x = 0; x < result.width(); ++x) {
            const quint64 pixelIndex = swizzled
                                           ? deSwizzleOffset(quint32(x), quint32(sourceY),
                                                             m_header.width, m_header.height)
                                           : quint64(sourceY) * m_header.width + x;
            const auto *src = payload + pixelIndex * channels;
            if (channels == 1) {
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = 255;
            } else if (m_header.encoding == 0x0c) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
            } else {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = channels == 4 ? src[3] : 255;
            }
            dst += 4;
        }
    }
    *image = std::move(result);
    return true;
}

bool TpcDecoder::decodeBc1(QImage *image) const
{
    const quint64 blocksX = (quint64(m_header.width) + 3) / 4;
    const quint64 blocksY = (quint64(m_header.height) + 3) / 4;
    const quint64 required = blocksX * blocksY * 8;
    if (!validateTopLevelSize(required))
        return false;

    QImage result;
    if (!QImageIOHandler::allocateImage(QSize(m_header.width, m_header.height),
                                        QImage::Format_ARGB32, &result))
        return false;
    const auto *src = reinterpret_cast<const uchar *>(m_data.constData() + HeaderSize);
    for (quint64 by = 0; by < blocksY; ++by)
        for (quint64 bx = 0; bx < blocksX; ++bx, src += 8)
            writeBcColors(&result, int(bx), int(by), src, nullptr, true);
    *image = std::move(result);
    return true;
}

bool TpcDecoder::decodeBc3(QImage *image) const
{
    const quint64 blocksX = (quint64(m_header.width) + 3) / 4;
    const quint64 blocksY = (quint64(m_header.height) + 3) / 4;
    const quint64 required = blocksX * blocksY * 16;
    if (!validateTopLevelSize(required))
        return false;

    QImage result;
    if (!QImageIOHandler::allocateImage(QSize(m_header.width, m_header.height),
                                        QImage::Format_ARGB32, &result))
        return false;
    const auto *src = reinterpret_cast<const uchar *>(m_data.constData() + HeaderSize);
    for (quint64 by = 0; by < blocksY; ++by) {
        for (quint64 bx = 0; bx < blocksX; ++bx, src += 16) {
            const auto alpha = bc3Alpha(src);
            writeBcColors(&result, int(bx), int(by), src + 8, &alpha, false);
        }
    }
    *image = std::move(result);
    return true;
}

bool TpcDecoder::validateTopLevelSize(quint64 required) const
{
    const quint64 available = quint64(m_data.size() - HeaderSize);
    if (required > available)
        return false;
    if (m_header.dataSize != 0 && m_header.dataSize < required && !m_header.animated)
        return false;
    return true;
}

QSize TpcDecoder::size() const
{
    return m_headerParsed ? QSize(m_header.width, m_header.height) : QSize();
}

QString TpcDecoder::errorString() const
{
    return m_error;
}

QString TpcDecoder::embeddedTxi() const
{
    return m_txi;
}

void TpcDecoder::fail(const QString &message)
{
    m_error = message;
}
