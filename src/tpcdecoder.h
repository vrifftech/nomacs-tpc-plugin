#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

class TpcDecoder final
{
public:
    static constexpr qsizetype HeaderSize = 128;

    static bool probe(const QByteArray &header, qsizetype fileSize = -1);

    explicit TpcDecoder(QByteArray data);

    bool decode(QImage *image);
    QSize size() const;
    QString embeddedTxi() const;
    QString errorString() const;

private:
    struct Header {
        quint32 dataSize = 0;
        quint16 canvasWidth = 0;
        quint16 canvasHeight = 0;
        quint16 width = 0;
        quint16 height = 0;
        quint8 encoding = 0;
        quint8 mipCount = 0;
        bool compressed = false;
        bool cubeMap = false;
        bool animated = false;
    };

    bool parseHeader();
    bool parseLayout();
    bool decodeRaw(QImage *image) const;
    bool decodeBc1(QImage *image) const;
    bool decodeBc3(QImage *image) const;
    bool validateTopLevelSize(quint64 required) const;
    void fail(const QString &message);

    QByteArray m_data;
    Header m_header;
    QString m_txi;
    QString m_error;
    bool m_headerParsed = false;
};
