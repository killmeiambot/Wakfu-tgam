#include <QtMath>
#include "qTgamhandler.h"
#include <QtCore/qendian.h>
#include <QtGui/QImage>
#include <QtCore/QFile>
#include <QtCore/QBuffer>
#include <qvariant.h>

QT_BEGIN_NAMESPACE

typedef struct
{
    quint8  bResizeMaskFlag;      // Read resize mask if this == 109
    quint8  bSkip[3];
    quint16 wWidth;               // Width of the image
    quint16 wHeight;              // Height of the image
    quint32 dwTgaSize;            // How many bytes in Raw data
    quint32 dwMaskSize;           // How many bytes in Mask data
    quint8  bResizeMask;      // Read resize mask if this == 109
} TgamHeaderData;
#define TGAMDIRENTRY_SIZE 16



int powerOfTwoCeil(int n) {
    unsigned int v = n;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}


class TgamReader
{
public:
    TgamReader(QIODevice * iodevice);
    static bool canRead(QIODevice *iodev);

    static QImage read(QIODevice * device);

    static bool write(QIODevice * device, const QImage & images);

private:
    bool readHeader();

    struct TgamAttrib
    {
        int h;
        int w;
    } TgamAttrib;

    QIODevice * iod;
    qint64 startpos;
    bool headerRead;

};

// Data readers and writers that takes care of alignment and endian stuff.


static int readTgamHeader(QIODevice *iodev, TgamHeaderData *tgamHeaderData)
{
    if (iodev) {
        uchar tmp[TGAMDIRENTRY_SIZE];
        if (iodev->read((char*)tmp, TGAMDIRENTRY_SIZE) == TGAMDIRENTRY_SIZE) {
            tgamHeaderData->bResizeMaskFlag = tmp[0];
            tgamHeaderData->bSkip[0] = tmp[1];
            tgamHeaderData->bSkip[1] = tmp[2];
            tgamHeaderData->bSkip[2] = tmp[3];
            tgamHeaderData->wWidth = qFromLittleEndian<quint16>(&tmp[4]);
            tgamHeaderData->wHeight = qFromLittleEndian<quint16>(&tmp[6]);
            tgamHeaderData->dwTgaSize = qFromLittleEndian<quint32>(&tmp[8]);
            tgamHeaderData->dwMaskSize = qFromLittleEndian<quint32>(&tmp[12]);
            if (tgamHeaderData->bResizeMaskFlag == 109) {
                return TGAMDIRENTRY_SIZE + iodev->read((char*)&(tgamHeaderData->bResizeMask), 1);
            }
            return TGAMDIRENTRY_SIZE;
        }
    }
    return 0;
}

static bool writeTgamHeader(QIODevice *iodev, const TgamHeaderData &tgamHeaderData)
{
    if (iodev) {
        uchar tmp[TGAMDIRENTRY_SIZE+1];
        tmp[0] = tgamHeaderData.bResizeMaskFlag;
        tmp[1] = tgamHeaderData.bSkip[0];
        tmp[2] = tgamHeaderData.bSkip[1];
        tmp[3] = tgamHeaderData.bSkip[2];
        qToLittleEndian<quint16>(tgamHeaderData.wWidth, &tmp[4]);
        qToLittleEndian<quint16>(tgamHeaderData.wHeight, &tmp[6]);
        qToLittleEndian<quint32>(tgamHeaderData.dwTgaSize, &tmp[8]);
        qToLittleEndian<quint32>(tgamHeaderData.dwMaskSize, &tmp[12]);
        if (tgamHeaderData.bResizeMask != 1) {
            tmp[0] = 109;
            tmp[TGAMDIRENTRY_SIZE] = tgamHeaderData.bResizeMask;
            return iodev->write((char*)tmp, TGAMDIRENTRY_SIZE + 1) == (TGAMDIRENTRY_SIZE + 1);
        }
        return iodev->write((char*)tmp, TGAMDIRENTRY_SIZE) == TGAMDIRENTRY_SIZE;
    }
    return false;
}

TgamReader::TgamReader(QIODevice * iodevice)
: iod(iodevice)
, startpos(0)
, headerRead(false)
{
}

bool TgamReader::canRead(QIODevice *iodev)
{
    if (iodev) {
        uchar tmp[4];
        if (iodev->peek((char*)tmp, 4) == 4) {
            return tmp[1] == 'A' && tmp[2] == 'G' && tmp[3] == 'T';
        }
    }
    return false;
}

/*!
    Reads all the Tgamns from the given \a device, and returns them as
    a list of QImage objects.

    Each image has an alpha channel that represents the mask from the
    corresponding Tgamn.

    \sa write()
*/
QImage TgamReader::read(QIODevice * device)
{
    if (device) {
        qint64 oldPos = device->pos();
        TgamHeaderData tgamHeaderData;
        qint64 readBytes = readTgamHeader(device, &tgamHeaderData);
        if (readBytes >= TGAMDIRENTRY_SIZE) {
            if (tgamHeaderData.bSkip[0]=='A' && tgamHeaderData.bSkip[1]=='G' && tgamHeaderData.bSkip[2]=='T') {
                int sizex = powerOfTwoCeil(tgamHeaderData.wWidth);
                int sizey = powerOfTwoCeil(tgamHeaderData.wHeight);
                QImage image(sizex, sizey, QImage::Format_RGBA8888);
                QRgb *p;
                QRgb  *end;
                uchar *buf = new uchar[image.bytesPerLine()];
                int    bpl = sizex*4;
                uchar *b;

                for (int h = 0; h < sizey; ++h) {
                    p = (QRgb *)image.scanLine(h);
                    end = p + sizex;
                    if (device->read((char *)buf, bpl) != bpl) {
                        image = QImage();
                        break;
                    }
                    b = buf;
                    while (p < end) {
                            *p++ = qRgba(*(b+2), *(b+1), *b, *(b+3));
                        b += 4;
                    }
                }
                delete[] buf;
                return image;
            }
        }
        if (!device->isSequential()) device->seek(oldPos);
    }
    return QImage();
}

bool TgamReader::write(QIODevice * device, const QImage & image_)
{
    bool retValue = false;
    if (device) {
            QBuffer buff;
            buff.open(QBuffer::WriteOnly);

            QImage image = image_.copy(0,0,powerOfTwoCeil(image_.width()),powerOfTwoCeil(image_.height()))
                                 .convertToFormat(QImage::Format_RGBA8888);
            QImage maskImage(image.width(), image.height(), QImage::Format_Mono);
            if (image.hasAlphaChannel()) {
                maskImage = image.createAlphaMask();
            } else {
                maskImage.fill(0xff);
            }
            maskImage = maskImage.convertToFormat(QImage::Format_Mono);

            TgamHeaderData dat;
            dat.bResizeMaskFlag = 0;
            dat.bSkip[0]='A'; dat.bSkip[1]='G'; dat.bSkip[2]='T';
            dat.wWidth = image_.width();
            dat.wHeight = image_.height();
            dat.dwTgaSize = image.width() * image.height() * 4;
            dat.dwMaskSize = maskImage.width() * maskImage.height() / 8;
            dat.bResizeMask = 1;

            writeTgamHeader(&buff, dat);
            int t = 0;
            for (int i = 0; i < image.height(); ++i) {
                t += buff.write((char*)image.constScanLine(i), image.bytesPerLine());
            }
            int q = 0;
            for (int i = 0; i < image.height(); ++i) {
                q += buff.write((char*)maskImage.constScanLine(i), maskImage.bytesPerLine());
            }
            if ( t == dat.dwTgaSize && q == dat.dwMaskSize ) {
                retValue = true;
                device->write(buff.data());
            }
    }
    return retValue;
}


/*!
    Constructs an instance of QtTgamHandler initialized to use \a device.
*/
QtTgamHandler::QtTgamHandler(QIODevice *device)
{
    m_currentTgamnIndex = 0;
    setDevice(device);
    m_pTgamReader = new TgamReader(device);
}

/*!
    Destructor for QtTgamHandler.
*/
QtTgamHandler::~QtTgamHandler()
{
    delete m_pTgamReader;
}

QVariant QtTgamHandler::option(ImageOption option) const
{
    return QVariant();
}

bool QtTgamHandler::supportsOption(ImageOption option) const
{
    return false;
}

/*!
 * Verifies if some values (magic bytes) are set as expected in the header of the file.
 * If the magic bytes were found, it is assumed that the QtTgamHandler can read the file.
 *
 */
bool QtTgamHandler::canRead() const
{
    bool bCanRead = false;
    QIODevice *device = QImageIOHandler::device();
    if (device) {
        bCanRead = TgamReader::canRead(device);
        if (bCanRead)
            setFormat("tgam");
    } else {
        qWarning("QtTgamHandler::canRead() called with no device");
    }
    return bCanRead;
}

/*! This static function is used by the plugin code, and is provided for convenience only.
    \a device must be an opened device with pointing to the start of the header data of the Tgam file.
*/
bool QtTgamHandler::canRead(QIODevice *device)
{
    Q_ASSERT(device);
    return TgamReader::canRead(device);
}

/*! \reimp

*/
bool QtTgamHandler::read(QImage *image)
{
    bool bSuccess = false;
    QImage img = m_pTgamReader->read(QImageIOHandler::device());

    // Make sure we only write to \a image when we succeed.
    if (!img.isNull()) {
        bSuccess = true;
        *image = img;
    }

    return bSuccess;
}


/*! \reimp

*/
bool QtTgamHandler::write(const QImage &image)
{
    return TgamReader::write(QImageIOHandler::device(), image);
}

QByteArray QtTgamHandler::name() const
{
    return "tgam";
}



QT_END_NAMESPACE
