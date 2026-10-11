#pragma once

// Getting a picture the user picked ready to attach to a post.
//
// The file is decoded and written out again (JPEG, or PNG when it has
// transparency): what goes on the network is only the pixels. Location,
// camera, editing history and anything else a file can carry stay behind -
// Qt writes none of it. Large pictures are scaled down so the result fits
// forumer::media::kMaxImageBytes.
//
// Plain functions, safe to call off the backend's thread.

#include <QByteArray>
#include <QString>

struct PreparedImage {
    QByteArray bytes;       // re-encoded image
    QString mime;           // "image/jpeg" | "image/png"
    int width = 0;
    int height = 0;
    QByteArray thumbJpeg;   // small preview for the composer
    QByteArray previewJpeg; // sent with the post over Delivery (media::kMaxPreviewBytes)
    QString error;          // non-empty: couldn't be used (and why, for the user)
};

PreparedImage prepareImage(const QString &path);

/// A small JPEG preview of an encoded image ("" if it doesn't decode).
QByteArray thumbnailOf(const QByteArray &encoded);
