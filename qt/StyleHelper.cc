// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QStaticText>
#include <QStyleOptionProgressBar>

#include "StyleHelper.h"
#include "Theme.h"

namespace
{

void drawProgressText(QPainter& painter, QStyleOptionProgressBar const& option, QRect const& rect)
{
    auto text = QStaticText(option.text);
    text.setTextFormat(Qt::PlainText);
    text.prepare({}, painter.font());
    auto const text_pos = QStyle::alignedRect(option.direction, option.textAlignment, text.size().toSize(), rect).topLeft();

    auto const text_color = option.palette.color(QPalette::WindowText);
    auto const shadow_color = text_color.value() <= 128 ? QColor(255, 255, 255, 160) : QColor(0, 0, 0, 160);

    painter.setPen(shadow_color);
    for (int i = -1; i <= 1; ++i)
    {
        for (int j = -1; j <= 1; ++j)
        {
            if (i != 0 && j != 0)
            {
                painter.drawStaticText(text_pos.x() + i, text_pos.y() + j, text);
            }
        }
    }

    painter.setPen(text_color);
    painter.drawStaticText(text_pos, text);
}

// Rounded pill with a gradient fill and a glossy highlight.
void drawModernProgressBar(QPainter& painter, QStyleOptionProgressBar const& option)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    auto rect = QRectF{ option.rect };
    if (!option.textVisible)
    {
        auto const height = std::min(rect.height(), Theme::isTouch() ? 10.0 : 8.0);
        rect = QRectF{ rect.x(), rect.center().y() - height / 2, rect.width(), height };
    }

    auto const radius = rect.height() / 2;
    auto track = QPainterPath{};
    track.addRoundedRect(rect, radius, radius);
    painter.fillPath(track, option.palette.brush(QPalette::Window));

    auto const max_pos = std::max(option.maximum - option.minimum, 1);
    auto const pos = std::min(std::max(option.progress - option.minimum, 0), max_pos);
    auto const is_inverted = option.invertedAppearance != (option.direction == Qt::RightToLeft);

    if (pos > 0)
    {
        auto bar_rect = rect;
        bar_rect.setWidth(std::max(rect.width() * pos / max_pos, rect.height()));
        if (is_inverted)
        {
            bar_rect.moveRight(rect.right());
        }

        auto bar = QPainterPath{};
        bar.addRoundedRect(bar_rect, radius, radius);
        painter.setClipPath(track);
        painter.fillPath(bar, option.palette.brush(QPalette::Highlight));

        auto sheen = QLinearGradient{ bar_rect.topLeft(), bar_rect.bottomLeft() };
        sheen.setColorAt(0.0, QColor{ 255, 255, 255, 80 });
        sheen.setColorAt(0.55, QColor{ 255, 255, 255, 0 });
        painter.fillPath(bar, sheen);
        painter.setClipping(false);
    }

    if (option.textVisible)
    {
        drawProgressText(painter, option, option.rect.adjusted(1, 0, -1, 0));
    }

    painter.restore();
}

} // namespace

QIcon::Mode StyleHelper::getIconMode(QStyle::State const& state)
{
    if (!state.testFlag(QStyle::State_Enabled))
    {
        return QIcon::Disabled;
    }

    if (state.testFlag(QStyle::State_Selected))
    {
        return QIcon::Selected;
    }

    return QIcon::Normal;
}

void StyleHelper::drawProgressBar(QPainter& painter, QStyleOptionProgressBar const& option)
{
    if (Theme::isModern())
    {
        drawModernProgressBar(painter, option);
        return;
    }

    painter.save();

    auto rect = option.rect.adjusted(0, 0, -1, -1);

    painter.setPen(option.palette.color(QPalette::Base));
    painter.drawRect(rect);

    rect.adjust(1, 1, 0, 0);

    auto const max_pos = std::max(option.maximum - option.minimum, 1);
    auto const pos = std::min(std::max(option.progress - option.minimum, 0), max_pos);
    auto const bar_width = pos * rect.width() / max_pos;
    auto const is_inverted = option.invertedAppearance != (option.direction == Qt::RightToLeft);

    if (pos < max_pos)
    {
        auto back_rect = rect;
        if (pos > 0)
        {
            back_rect.setWidth(back_rect.width() - bar_width);
            if (!is_inverted)
            {
                back_rect.moveRight(rect.right());
            }
        }

        painter.fillRect(back_rect, option.palette.brush(QPalette::Window));
    }

    if (pos > 0)
    {
        auto bar_rect = rect;
        if (pos < max_pos)
        {
            bar_rect.setWidth(bar_width);
            if (is_inverted)
            {
                bar_rect.moveRight(rect.right());
            }
        }

        painter.fillRect(bar_rect, option.palette.brush(QPalette::Highlight));
    }

    if (option.textVisible)
    {
        drawProgressText(painter, option, rect.adjusted(1, 0, -1, 0));
    }

    painter.restore();
}
