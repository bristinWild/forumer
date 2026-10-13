#include "image_prep.h"

#include <QBuffer>
#include <QColor>
#include <QColorSpace>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>

#include "forumer_core/media.h"

namespace {

constexpr qint64 kMaxInputBytes = 30 * 1024 * 1024;   // what we agree to open at all
constexpr int kMaxInputPixels = 60'000'000;           // ~ 9000 x 6600
constexpr int kMaxEdge = 2048;                         // longest side after scaling
constexpr int kThumbEdge = 240;
constexpr int kJpegQuality = 85;

QByteArray encode(const QImage &img, const char *format, int quality) {
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, format);
    if (quality >= 0)
        writer.setQuality(quality);
    if (!writer.write(img))
        return {};
    return out;
}

// Whether any pixel is not fully opaque (an alpha channel alone isn't
// enough: many PNGs carry one they never use).
bool usesAlpha(const QImage &img) {
    if (!img.hasAlphaChannel())
        return false;
    const QImage a = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < a.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            if (qAlpha(line[x]) != 255)
                return true;
    }
    return false;
}

// Only the pixels: QImage keeps the file's text chunks / comments (which
// QImageWriter would write back out) and its colour profile. Colours are
// first converted to sRGB so dropping the profile doesn't change them.
QImage stripped(QImage img) {
    if (img.colorSpace().isValid() && img.colorSpace() != QColorSpace(QColorSpace::SRgb))
        img.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
    QImage bare(img.constBits(), img.width(), img.height(), img.bytesPerLine(), img.format());
    return bare.copy();   // owns its pixels; no text, no profile
}

QImage fit(const QImage &img, int edge) {
    if (img.width() <= edge && img.height() <= edge)
        return img;
    return img.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

// The preview that travels with the post: as large as fits the limit.
static QByteArray previewOf(const QImage &img) {
    QImage flat = img;
    if (flat.hasAlphaChannel()) {   // JPEG: on white
        QImage bg(flat.size(), QImage::Format_RGB32);
        bg.fill(Qt::white);
        for (int y = 0; y < flat.height(); ++y)
            for (int x = 0; x < flat.width(); ++x) {
                const QRgb p = flat.pixel(x, y);
                const int a = qAlpha(p);
                bg.setPixel(x, y, qRgb((qRed(p) * a + 255 * (255 - a)) / 255, (qGreen(p) * a + 255 * (255 - a)) / 255,
                                       (qBlue(p) * a + 255 * (255 - a)) / 255));
            }
        flat = bg;
    }
    for (int edge : {1024, 860, 720, 600, 480, 360}) {
        for (int quality : {72, 60, 50}) {
            QByteArray bytes = encode(fit(flat, edge).convertToFormat(QImage::Format_RGB888), "jpg", quality);
            if (!bytes.isEmpty() && static_cast<size_t>(bytes.size()) <= forumer::media::kMaxPreviewBytes)
                return bytes;
        }
    }
    return {};
}

QByteArray thumbnailOf(const QByteArray &encoded) {
    QImage img;
    if (!img.loadFromData(encoded))
        return {};
    QImage small = stripped(fit(img, kThumbEdge));
    if (small.hasAlphaChannel()) {
        // JPEG has no transparency: put it on a neutral grey.
        QImage flat(small.size(), QImage::Format_RGB32);
        flat.fill(QColor(128, 128, 128));
        for (int y = 0; y < small.height(); ++y)
            for (int x = 0; x < small.width(); ++x) {
                const QRgb p = small.pixel(x, y);
                const int a = qAlpha(p);
                flat.setPixel(x, y, qRgb((qRed(p) * a + 128 * (255 - a)) / 255,
                                         (qGreen(p) * a + 128 * (255 - a)) / 255,
                                         (qBlue(p) * a + 128 * (255 - a)) / 255));
            }
        small = flat;
    }
    return encode(small.convertToFormat(QImage::Format_RGB888), "jpg", 70);
}

PreparedImage prepareImage(const QString &path) {
    PreparedImage out;
    const QFileInfo info(path);
    if (!info.isFile()) {
        out.error = QStringLiteral("That file isn't there any more");
        return out;
    }
    if (info.size() > kMaxInputBytes) {
        out.error = QStringLiteral("That file is too large (over 30 MB)");
        return out;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);   // apply the camera's rotation before it is dropped
    const QByteArray format = reader.format().toLower();
    if (format != "jpeg" && format != "jpg" && format != "png" && format != "webp") {
        out.error = format.isEmpty() ? QStringLiteral("That isn't an image Forumer can read")
                                     : QStringLiteral("%1 images aren't supported: use JPEG, PNG or WebP")
                                           .arg(QString::fromLatin1(format).toUpper());
        return out;
    }
    const QSize declared = reader.size();
    if (declared.isValid() && static_cast<qint64>(declared.width()) * declared.height() > kMaxInputPixels) {
        out.error = QStringLiteral("That image has too many pixels");
        return out;
    }
    QImage img = reader.read();
    if (img.isNull()) {
        out.error = QStringLiteral("Couldn't read the image: %1").arg(reader.errorString());
        return out;
    }
    img = stripped(std::move(img));

    // Shrink until it fits the limit (most pictures fit at the first size).
    const bool alpha = usesAlpha(img);
    int edge = kMaxEdge;
    for (int attempt = 0; attempt < 6; ++attempt, edge = edge * 3 / 4) {
        const QImage scaled = fit(img, edge);
        QByteArray bytes = alpha ? encode(scaled.convertToFormat(QImage::Format_ARGB32), "png", -1)
                                 : encode(scaled.convertToFormat(QImage::Format_RGB888), "jpg", kJpegQuality);
        if (bytes.isEmpty()) {
            out.error = QStringLiteral("Couldn't re-encode the image");
            return out;
        }
        if (static_cast<size_t>(bytes.size()) <= forumer::media::kMaxImageBytes) {
            out.bytes = std::move(bytes);
            out.mime = alpha ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
            out.width = scaled.width();
            out.height = scaled.height();
            out.thumbJpeg = thumbnailOf(out.bytes);
            out.previewJpeg = previewOf(scaled);
            return out;
        }
    }
    out.error = QStringLiteral("Couldn't make the image small enough (5 MB)");
    return out;
}
