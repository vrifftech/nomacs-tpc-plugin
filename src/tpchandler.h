#pragma once

#include <QImageIOHandler>

class TpcHandler final : public QImageIOHandler
{
public:
    bool canRead() const override;
    bool read(QImage *image) override;
    QVariant option(ImageOption option) const override;
    bool supportsOption(ImageOption option) const override;

    static bool canRead(QIODevice *device);

private:
    QByteArray readAllPreservingPosition() const;
};
