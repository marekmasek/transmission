// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "TorrentDelegateRow.h"

#include <algorithm>
#include <array>

#include <QAbstractItemView>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include <libtransmission/utils.h>

#include "Formatter.h"
#include "Theme.h"
#include "Torrent.h"
#include "TorrentModel.h"

namespace
{

// horizontal room for Theme::drawItemBackground()'s inset and selection pill
auto constexpr PadX = 20;
auto constexpr Gap = 12;
auto constexpr NameMinWidth = 260;

struct ColumnSpec
{
    TorrentDelegateRow::Column column;
    int width;
};

using Column = TorrentDelegateRow::Column;

auto constexpr FixedColumns = std::array<ColumnSpec, 6>{ {
    { .column = Column::Size, .width = 72 },
    { .column = Column::Progress, .width = 150 },
    { .column = Column::Status, .width = 110 },
    { .column = Column::Down, .width = 76 },
    { .column = Column::Up, .width = 76 },
    { .column = Column::Eta, .width = 60 },
} };

// the first column here is the first to go when the row is too narrow
auto constexpr DropOrder = std::array<Column, 5>{ Column::Eta, Column::Up, Column::Status, Column::Down, Column::Size };

QString const Dash = QStringLiteral("—");

[[nodiscard]] double progressFraction(Torrent const& tor)
{
    if (tor.isVerifying())
    {
        return tor.getVerifyProgress();
    }

    return tor.hasMetadata() ? tor.percentDone() : tor.metadataPercentDone();
}

[[nodiscard]] QString speedText(Speed const& speed)
{
    return speed.is_zero() ? Dash : speed.to_qstring();
}

} // namespace

TorrentDelegateRow::TorrentDelegateRow(bool compact, QObject* parent)
    : TorrentDelegate{ parent }
    , compact_{ compact }
{
}

std::vector<TorrentDelegateRow::Cell> TorrentDelegateRow::layoutColumns(int width)
{
    auto shown = std::vector<ColumnSpec>{ std::begin(FixedColumns), std::end(FixedColumns) };

    auto const fixed_width = [&shown]()
    {
        auto sum = 0;
        for (auto const& spec : shown)
        {
            sum += spec.width + Gap;
        }
        return sum;
    };

    for (auto const drop : DropOrder)
    {
        if (width - 2 * PadX - fixed_width() >= NameMinWidth)
        {
            break;
        }

        std::erase_if(shown, [drop](auto const& spec) { return spec.column == drop; });
    }

    auto cells = std::vector<Cell>{};
    auto const name_width = std::max(width - (2 * PadX) - fixed_width(), 0);
    cells.push_back({ .column = Column::Name, .left = PadX, .width = name_width });

    auto left = PadX + name_width + Gap;
    for (auto const& spec : shown)
    {
        cells.push_back({ .column = spec.column, .left = left, .width = spec.width });
        left += spec.width + Gap;
    }

    return cells;
}

QString TorrentDelegateRow::columnTitle(Column column)
{
    switch (column)
    {
    case Column::Name:
        return tr("Name");
    case Column::Size:
        return tr("Size");
    case Column::Progress:
        return tr("Progress");
    case Column::Status:
        return tr("Status");
    case Column::Down:
        //: Column title for download speed
        return tr("Down");
    case Column::Up:
        //: Column title for upload speed
        return tr("Up");
    case Column::Eta:
        //: Column title for estimated time remaining
        return tr("ETA");
    }

    return {};
}

SortMode TorrentDelegateRow::columnSortMode(Column column)
{
    switch (column)
    {
    case Column::Size:
        return SortMode::SortBySize;
    case Column::Progress:
        return SortMode::SortByProgress;
    case Column::Status:
        return SortMode::SortByState;
    case Column::Down:
    case Column::Up:
        return SortMode::SortByActivity;
    case Column::Eta:
        return SortMode::SortByEta;
    default:
        return SortMode::SortByName;
    }
}

Qt::Alignment TorrentDelegateRow::columnAlignment(Column column)
{
    switch (column)
    {
    case Column::Size:
    case Column::Down:
    case Column::Up:
    case Column::Eta:
        return Qt::AlignRight | Qt::AlignVCenter;
    default:
        return Qt::AlignLeft | Qt::AlignVCenter;
    }
}

QSize TorrentDelegateRow::sizeHint(QStyleOptionViewItem const& option, Torrent const& /*tor*/) const
{
    auto const line_height = QFontMetrics{ option.font }.height();
    auto const touch = Theme::isTouch();

    auto height = 0;
    if (compact_)
    {
        height = std::max(touch ? 44 : 32, line_height + 14);
    }
    else
    {
        height = std::max(touch ? 64 : 52, (line_height * 2) + 18);
    }

    return { option.rect.width(), height };
}

void TorrentDelegateRow::drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    Theme::drawItemBackground(*painter, option);

    auto const dimmed = tor.isPaused() || (option.state & QStyle::State_Enabled) == 0;
    auto const muted = option.palette.color(QPalette::PlaceholderText);

    for (auto const& cell : layoutColumns(option.rect.width()))
    {
        auto const logical = QRect{ option.rect.left() + cell.left, option.rect.top(), cell.width, option.rect.height() };
        auto const rect = QStyle::visualRect(option.direction, option.rect, logical);

        auto text = QString{};
        auto color = muted;

        switch (cell.column)
        {
        case Column::Name:
            drawName(painter, option, tor, rect);
            continue;

        case Column::Progress:
            drawProgress(painter, option, tor, rect);
            continue;

        case Column::Size:
            text = Formatter::storage_to_string(tor.sizeWhenDone());
            break;

        case Column::Status:
            if (tor.hasError())
            {
                text = tr("Error");
                color = Theme::errorColor();
            }
            else
            {
                text = tor.activityString();
            }
            break;

        case Column::Down:
            text = speedText(tor.downloadSpeed());
            break;

        case Column::Up:
            text = speedText(tor.uploadSpeed());
            break;

        case Column::Eta:
            text = tor.hasETA() && (tor.isDownloading() || tor.isSeeding()) ? Formatter::time_to_string(tor.getETA()) : Dash;
            break;
        }

        if (dimmed && cell.column != Column::Status)
        {
            color.setAlphaF(0.7);
        }

        painter->setPen(color);
        painter->setFont(option.font);
        auto const alignment = QStyle::visualAlignment(option.direction, columnAlignment(cell.column));
        painter->drawText(
            rect,
            static_cast<int>(alignment),
            painter->fontMetrics().elidedText(text, Qt::ElideRight, rect.width()));
    }

    painter->restore();
}

void TorrentDelegateRow::drawName(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor, QRect const& rect)
    const
{
    auto const dimmed = tor.isPaused() || (option.state & QStyle::State_Enabled) == 0;
    auto icon_size = Theme::isTouch() ? 28 : 24;
    if (compact_)
    {
        icon_size = Theme::isTouch() ? 20 : 16;
    }
    auto const icon_mode = dimmed ? QIcon::Disabled : QIcon::Normal;
    auto const icon_state = tor.isPaused() ? QIcon::Off : QIcon::On;

    auto const icon_rect = QStyle::alignedRect(
        option.direction,
        Qt::AlignLeft | Qt::AlignVCenter,
        QSize{ icon_size, icon_size },
        rect);
    tor.getMimeTypeIcon().paint(painter, icon_rect, Qt::AlignCenter, icon_mode, icon_state);

    if (tor.hasError())
    {
        auto const emblem = warningEmblem();
        auto const emblem_rect = QStyle::alignedRect(
            option.direction,
            Qt::AlignRight | Qt::AlignBottom,
            emblem.actualSize(icon_rect.size() / 2),
            icon_rect);
        emblem.paint(painter, emblem_rect, Qt::AlignCenter);
    }

    auto text_rect = rect;
    if (option.direction == Qt::RightToLeft)
    {
        text_rect.setRight(icon_rect.left() - 10);
    }
    else
    {
        text_rect.setLeft(icon_rect.right() + 10);
    }

    auto name_font = option.font;
    name_font.setWeight(QFont::DemiBold);
    auto name_color = option.palette.color(QPalette::Text);
    if (dimmed)
    {
        name_color = option.palette.color(QPalette::PlaceholderText);
    }

    auto const alignment = static_cast<int>(QStyle::visualAlignment(option.direction, Qt::AlignLeft | Qt::AlignVCenter));

    if (compact_)
    {
        painter->setFont(name_font);
        painter->setPen(name_color);
        painter->drawText(
            text_rect,
            alignment,
            QFontMetrics{ name_font }.elidedText(tor.name(), Qt::ElideRight, text_rect.width()));
        return;
    }

    // two lines: the name, then the detailed status the classic view shows
    auto detail_font = option.font;
    if (detail_font.pointSizeF() > 0)
    {
        detail_font.setPointSizeF(detail_font.pointSizeF() * 0.92);
    }

    auto const name_height = QFontMetrics{ name_font }.height();
    auto const detail_height = QFontMetrics{ detail_font }.height();
    auto const top = text_rect.top() + ((text_rect.height() - name_height - detail_height - 2) / 2);

    auto const name_rect = QRect{ text_rect.left(), top, text_rect.width(), name_height };
    auto const detail_rect = QRect{ text_rect.left(), top + name_height + 2, text_rect.width(), detail_height };

    painter->setFont(name_font);
    painter->setPen(name_color);
    painter->drawText(
        name_rect,
        alignment,
        QFontMetrics{ name_font }.elidedText(tor.name(), Qt::ElideRight, name_rect.width()));

    auto const detail = tor.hasError() ? tor.getError() : progressString(tor);
    painter->setFont(detail_font);
    painter->setPen(tor.hasError() ? Theme::errorColor() : option.palette.color(QPalette::PlaceholderText));
    painter->drawText(
        detail_rect,
        alignment,
        QFontMetrics{ detail_font }.elidedText(detail, Qt::ElideRight, detail_rect.width()));
}

void TorrentDelegateRow::drawProgress(
    QPainter* painter,
    QStyleOptionViewItem const& option,
    Torrent const& tor,
    QRect const& rect)
{
    auto constexpr PercentWidth = 40;
    auto const bar_height = Theme::isTouch() ? 6.0 : 4.0;
    auto const fraction = std::clamp(progressFraction(tor), 0.0, 1.0);

    auto bar_logical = QRectF{ static_cast<double>(rect.left()),
                               rect.center().y() - (bar_height / 2) + 1,
                               static_cast<double>(rect.width() - PercentWidth - 8),
                               bar_height };
    auto const bar_rect = QStyle::visualRect(option.direction, rect, bar_logical.toRect());

    painter->setPen(Qt::NoPen);
    painter->setBrush(Theme::progressTrack());
    painter->drawRoundedRect(bar_rect, bar_height / 2, bar_height / 2);

    if (fraction > 0)
    {
        auto fill = QRectF{ bar_rect };
        auto const fill_width = std::max(fill.width() * fraction, bar_height);
        if (option.direction == Qt::RightToLeft)
        {
            fill.setLeft(fill.right() - fill_width);
        }
        else
        {
            fill.setWidth(fill_width);
        }

        auto bar = Theme::Bar::Idle;
        if (tor.isDownloading())
        {
            bar = Theme::Bar::Downloading;
        }
        else if (tor.isSeeding() || (tor.isFinished() && !tor.isPaused()))
        {
            bar = Theme::Bar::Seeding;
        }

        painter->setBrush(Theme::progressBrush(bar));
        painter->drawRoundedRect(fill, bar_height / 2, bar_height / 2);
    }

    auto const percent_logical = QRect{ rect.right() - PercentWidth + 1, rect.top(), PercentWidth, rect.height() };
    auto const percent_rect = QStyle::visualRect(option.direction, rect, percent_logical);
    painter->setFont(option.font);
    painter->setPen(option.palette.color(QPalette::PlaceholderText));
    painter->drawText(
        percent_rect,
        static_cast<int>(QStyle::visualAlignment(option.direction, Qt::AlignRight | Qt::AlignVCenter)),
        QStringLiteral("%1%").arg(static_cast<int>(tr_truncd(100.0 * fraction, 0))));
}

bool TorrentDelegateRow::helpEvent(
    QHelpEvent* event,
    QAbstractItemView* view,
    QStyleOptionViewItem const& option,
    QModelIndex const& index)
{
    if (event == nullptr || view == nullptr || event->type() != QEvent::ToolTip)
    {
        return TorrentDelegate::helpEvent(event, view, option, index);
    }

    auto const* const tor = index.data(TorrentModel::TorrentRole).value<Torrent const*>();
    if (tor == nullptr)
    {
        return false;
    }

    auto lines = QStringList{ tor->name(), progressString(*tor), statusString(*tor) };
    if (tor->hasError())
    {
        lines << tor->getError();
    }

    QToolTip::showText(event->globalPos(), lines.join(QLatin1Char('\n')), view);
    return true;
}
