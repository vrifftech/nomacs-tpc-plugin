#include "tpcplugin.h"

#include "tpchandler.h"

QImageIOPlugin::Capabilities TpcPlugin::capabilities(QIODevice *device, const QByteArray &format) const
{
    if (format.toLower() == "tpc")
        return CanRead;
    if (format.isEmpty() && TpcHandler::canRead(device))
        return CanRead;
    return {};
}

QImageIOHandler *TpcPlugin::create(QIODevice *device, const QByteArray &format) const
{
    auto *handler = new TpcHandler;
    handler->setDevice(device);
    handler->setFormat(format.isEmpty() ? QByteArray("tpc") : format);
    return handler;
}
