#include "tpchandler.h"

#include "tpcdecoder.h"

#include <QIODevice>

bool TpcHandler::canRead(QIODevice *device)
{
    if (!device)
        return false;
    return TpcDecoder::probe(device->peek(TpcDecoder::HeaderSize), device->size());
}

bool TpcHandler::canRead() const
{
    if (canRead(device())) {
        setFormat("tpc");
        return true;
    }
    return false;
}

QByteArray TpcHandler::readAllPreservingPosition() const
{
    QIODevice *source = device();
    if (!source)
        return {};
    const qint64 original = source->isSequential() ? -1 : source->pos();
    if (original >= 0)
        source->seek(0);
    QByteArray bytes = source->readAll();
    if (original >= 0)
        source->seek(original);
    return bytes;
}

bool TpcHandler::read(QImage *image)
{
    TpcDecoder decoder(readAllPreservingPosition());
    if (!decoder.decode(image))
        return false;
    return true;
}

QVariant TpcHandler::option(ImageOption option) const
{
    if (option != Size && option != Description)
        return {};
    TpcDecoder decoder(readAllPreservingPosition());
    if (option == Size)
        return decoder.size();
    const QString txi = decoder.embeddedTxi();
    return txi.isEmpty() ? QVariant() : QVariant(QStringLiteral("TXI: %1").arg(txi));
}

bool TpcHandler::supportsOption(ImageOption option) const
{
    return option == Size || option == Description;
}
