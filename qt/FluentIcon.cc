// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FluentIcon.h"

#include <optional>
#include <utility>

#include <QApplication>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QSvgRenderer>

namespace
{

class TintedSvgEngine : public QIconEngine
{
public:
    TintedSvgEngine(QString path, QPalette::ColorRole role, std::optional<QColor> color, QString on_path = {})
        : path_{ std::move(path) }
        , on_path_{ std::move(on_path) }
        , role_{ role }
        , color_{ std::move(color) }
    {
    }

    void paint(QPainter* painter, QRect const& rect, QIcon::Mode mode, QIcon::State state) override
    {
        auto const dpr = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : qreal{ 1 };
        auto pix = pixmap(rect.size() * dpr, mode, state);
        pix.setDevicePixelRatio(dpr);
        painter->drawPixmap(rect, pix);
    }

    QPixmap pixmap(QSize const& size, QIcon::Mode mode, QIcon::State state) override
    {
        auto const is_on = state == QIcon::On && !on_path_.isEmpty();
        auto const& path = is_on ? on_path_ : path_;
        auto const color = is_on && mode != QIcon::Disabled ? QApplication::palette().color(QPalette::Highlight) : tint(mode);
        auto const
            key = QStringLiteral("fluent:%1:%2x%3:%4").arg(path).arg(size.width()).arg(size.height()).arg(color.rgba(), 0, 16);

        auto pix = QPixmap{};
        if (QPixmapCache::find(key, &pix))
        {
            return pix;
        }

        auto image = QImage{ size, QImage::Format_ARGB32_Premultiplied };
        image.fill(Qt::transparent);
        {
            auto painter = QPainter{ &image };
            QSvgRenderer{ path }.render(&painter, QRectF{ QPointF{}, QSizeF{ size } });
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(image.rect(), color);
        }

        pix = QPixmap::fromImage(std::move(image));
        QPixmapCache::insert(key, pix);
        return pix;
    }

    QSize actualSize(QSize const& size, QIcon::Mode /*mode*/, QIcon::State /*state*/) override
    {
        return size;
    }

    [[nodiscard]] QIconEngine* clone() const override
    {
        return new TintedSvgEngine{ path_, role_, color_, on_path_ };
    }

private:
    [[nodiscard]] QColor tint(QIcon::Mode mode) const
    {
        auto const palette = QApplication::palette();

        if (mode == QIcon::Disabled)
        {
            return palette.color(QPalette::Disabled, QPalette::WindowText);
        }

        return color_ ? *color_ : palette.color(QPalette::Active, role_);
    }

    QString const path_;
    QString const on_path_;
    QPalette::ColorRole const role_;
    std::optional<QColor> const color_;
};

[[nodiscard]] QString resourcePath(QString const& name)
{
    return QStringLiteral(":/fluent/%1.svg").arg(name);
}

} // namespace

namespace fluent
{

QIcon icon(QString const& name, QPalette::ColorRole role)
{
    return QIcon{ new TintedSvgEngine{ resourcePath(name), role, {} } };
}

QIcon icon(QString const& name, QColor const& color)
{
    return QIcon{ new TintedSvgEngine{ resourcePath(name), QPalette::WindowText, color } };
}

QIcon toggleIcon(QString const& off_name, QString const& on_name)
{
    return QIcon{ new TintedSvgEngine{ resourcePath(off_name), QPalette::WindowText, {}, resourcePath(on_name) } };
}

QIcon mimeTypeIcon(QString const& mime_type, bool multifile)
{
    if (multifile)
    {
        return icon(QStringLiteral("folder"), QPalette::PlaceholderText);
    }

    auto const starts = [&mime_type](char const* prefix)
    {
        return mime_type.startsWith(QLatin1String(prefix));
    };
    auto const contains = [&mime_type](char const* part)
    {
        return mime_type.contains(QLatin1String(part));
    };

    if (starts("video/"))
    {
        return icon(QStringLiteral("video_clip"), QPalette::PlaceholderText);
    }

    if (starts("audio/"))
    {
        return icon(QStringLiteral("music_note_2"), QPalette::PlaceholderText);
    }

    if (starts("image/") && !contains("disk") && !contains("iso"))
    {
        return icon(QStringLiteral("image"), QPalette::PlaceholderText);
    }

    if (contains("iso9660") || contains("disk-image") || contains("cd-image") || contains("raw-disk"))
    {
        return icon(QStringLiteral("hard_drive"), QPalette::PlaceholderText);
    }

    if (contains("zip") || contains("compressed") || contains("tar") || contains("rar") || contains("7z") ||
        contains("archive"))
    {
        return icon(QStringLiteral("archive"), QPalette::PlaceholderText);
    }

    if (contains("pdf"))
    {
        return icon(QStringLiteral("document_pdf"), QPalette::PlaceholderText);
    }

    if (contains("epub") || contains("mobipocket") || contains("djvu"))
    {
        return icon(QStringLiteral("book"), QPalette::PlaceholderText);
    }

    if (contains("executable") || contains("msdownload") || contains("msi") || contains("x-deb") || contains("x-rpm") ||
        contains("appimage"))
    {
        return icon(QStringLiteral("apps"), QPalette::PlaceholderText);
    }

    if (starts("text/x-") || contains("json") || contains("xml") || contains("javascript"))
    {
        return icon(QStringLiteral("code"), QPalette::PlaceholderText);
    }

    return icon(QStringLiteral("document"), QPalette::PlaceholderText);
}

} // namespace fluent
