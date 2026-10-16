#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

#include <iostream>

#include "image_prep.h"

int failures = 0;

void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

QByteArray readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    QTemporaryDir dir;
    check(dir.isValid(), "temporary directory");
    if (!dir.isValid())
        return 1;

    // PNG input without transparency should become JPEG.
    QImage opaque(128, 64, QImage::Format_RGB32);
    opaque.fill(Qt::red);
    opaque.setText("Comment", "SECRET-METADATA-123");

    const QString opaquePath = dir.filePath("opaque.png");
    check(opaque.save(opaquePath, "PNG"), "write opaque PNG");

    auto jpeg = prepareImage(opaquePath);
    check(jpeg.error.isEmpty(), "opaque image accepted");
    check(jpeg.mime == "image/jpeg", "opaque output is JPEG");
    check(jpeg.width == 128 && jpeg.height == 64, "dimensions preserved");
    check(!jpeg.thumbJpeg.isEmpty(), "thumbnail generated");
    check(!jpeg.previewJpeg.isEmpty(), "preview generated");
    check(!jpeg.bytes.contains("SECRET-METADATA-123"),
          "metadata removed");

    QImage jpegDecoded;
    check(jpegDecoded.loadFromData(jpeg.bytes, "JPEG"),
          "JPEG output decodes");

    // Transparency must survive as PNG.
    QImage transparent(64, 32, QImage::Format_ARGB32);
    transparent.fill(qRgba(20, 40, 60, 100));

    const QString transparentPath = dir.filePath("alpha.png");
    check(transparent.save(transparentPath, "PNG"),
          "write transparent PNG");

    auto png = prepareImage(transparentPath);
    check(png.error.isEmpty(), "transparent image accepted");
    check(png.mime == "image/png", "transparent output is PNG");

    QImage pngDecoded;
    check(pngDecoded.loadFromData(png.bytes, "PNG"),
          "PNG output decodes");
    check(pngDecoded.hasAlphaChannel(), "transparency preserved");

    // Oversized dimensions should be scaled down.
    QImage large(2500, 128, QImage::Format_RGB32);
    large.fill(Qt::blue);

    const QString largePath = dir.filePath("large.png");
    check(large.save(largePath, "PNG"), "write large image");

    auto scaled = prepareImage(largePath);
    check(scaled.error.isEmpty(), "large image accepted");
    check(scaled.width <= 2048 && scaled.height <= 2048,
          "maximum edge enforced");

    // Modify a small JPEG's dimensions in its SOF header.
    // No giant image allocation is needed to test the pixel guard.
    const QString oversizedPath = dir.filePath("oversized.jpg");

    QImage small(32, 32, QImage::Format_RGB32);
    small.fill(Qt::green);
    check(small.save(oversizedPath, "JPEG"), "write test JPEG");

    QByteArray bytes = readFile(oversizedPath);
    int sof = -1;

    for (int i = 0; i + 9 < bytes.size(); ++i) {
        const unsigned char first = static_cast<unsigned char>(bytes[i]);
        const unsigned char marker = static_cast<unsigned char>(bytes[i + 1]);

        if (first == 0xff &&
            (marker == 0xc0 || marker == 0xc1 || marker == 0xc2)) {
            sof = i;
            break;
        }
    }

    check(sof >= 0, "JPEG dimensions header found");

    if (sof >= 0) {
        // 8000 x 7501 = 60,008,000 pixels; limit is 60 million.
        bytes[sof + 5] = char(7501 >> 8);
        bytes[sof + 6] = char(7501 & 255);
        bytes[sof + 7] = char(8000 >> 8);
        bytes[sof + 8] = char(8000 & 255);

        QFile f(oversizedPath);
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
              "rewrite JPEG header");

        if (f.isOpen()) {
            f.write(bytes);
            f.close();
        }

        QImageReader reader(oversizedPath);
        if (reader.size() == QSize(8000, 7501)) {
            auto rejected = prepareImage(oversizedPath);
            check(rejected.error.contains("too many pixels"),
                  "60-million-pixel guard enforced");
        } else {
            check(false, "oversized JPEG dimensions recognized");
        }
    }

    if (failures) {
        std::cerr << failures << " image-pipeline test failures\n";
        return 1;
    }

    std::cout << "PASS: JPEG, PNG, scaling, previews, metadata, pixel guard\n";
    return 0;
}
