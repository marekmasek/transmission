// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <optional>

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionHeader>
#include <QStylePainter>

#include "Theme.h"
#include "TorrentDelegateRow.h"
#include "TorrentView.h"

class TorrentView::HeaderWidget : public QWidget
{
public:
    explicit HeaderWidget(TorrentView* parent)
        : QWidget{ parent }
    {
        setFont(QApplication::font("QMiniFont"));
    }

    void setText(QString const& text)
    {
        text_ = text;
        update();
    }

    // QWidget
    [[nodiscard]] QSize sizeHint() const override
    {
        QStyleOptionHeader option;
        option.rect = QRect{ 0, 0, 100, 100 };

        QRect const label_rect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);

        return { 100, fontMetrics().height() + (option.rect.height() - label_rect.height()) };
    }

protected:
    // QWidget
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QStyleOptionHeader option;
        option.initFrom(this);
        option.state = QStyle::State_Enabled;
        option.position = QStyleOptionHeader::OnlyOneSection;

        QStylePainter painter{ this };
        painter.drawControl(QStyle::CE_HeaderSection, option);

        option.rect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);
        painter.drawItemText(option.rect, Qt::AlignCenter, option.palette, true, text_, QPalette::ButtonText);
    }

    void mouseDoubleClickEvent(QMouseEvent* /*event*/) override
    {
        emit dynamic_cast<TorrentView*>(parent())->headerDoubleClicked();
    }

private:
    QString text_;
};

class TorrentView::ColumnHeader : public QWidget
{
public:
    using Column = TorrentDelegateRow::Column;

    explicit ColumnHeader(TorrentView* parent)
        : QWidget{ parent }
        , view_{ parent }
    {
        setMouseTracking(true);
        hide();
    }

    void setSort(SortMode mode, bool reversed)
    {
        mode_ = mode;
        reversed_ = reversed;
        update();
    }

    // QWidget
    [[nodiscard]] QSize sizeHint() const override
    {
        return { 100, fontMetrics().height() + (Theme::isTouch() ? 22 : 14) };
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        auto painter = QPainter{ this };
        painter.setRenderHint(QPainter::Antialiasing);

        auto const text = palette().color(QPalette::Text);
        auto const muted = palette().color(QPalette::PlaceholderText);
        auto sorted_drawn = false;

        for (auto const& cell : TorrentDelegateRow::layoutColumns(width()))
        {
            auto const cell_rect = QStyle::visualRect(layoutDirection(), rect(), QRect{ cell.left, 0, cell.width, height() });

            if (hovered_ == cell.column)
            {
                auto hover = text;
                hover.setAlpha(12);
                painter.setPen(Qt::NoPen);
                painter.setBrush(hover);
                painter.drawRoundedRect(QRectF{ cell_rect }.adjusted(-6, 3, 6, -3), 4, 4);
            }

            auto const is_sorted = !sorted_drawn && TorrentDelegateRow::columnSortMode(cell.column) == mode_;
            sorted_drawn = sorted_drawn || is_sorted;

            auto const title = TorrentDelegateRow::columnTitle(cell.column);
            auto const alignment = QStyle::visualAlignment(layoutDirection(), TorrentDelegateRow::columnAlignment(cell.column));
            painter.setPen(is_sorted ? text : muted);
            painter.setFont(font());
            auto bounds = QRect{};
            painter.drawText(cell_rect, static_cast<int>(alignment), title, &bounds);

            if (is_sorted)
            {
                drawChevron(painter, bounds, alignment, muted);
            }
        }

        auto line = palette().color(QPalette::Midlight);
        painter.setPen(QPen{ line, 1.0 });
        painter.drawLine(QPointF{ 0, height() - 0.5 }, QPointF{ static_cast<double>(width()), height() - 0.5 });
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        setHovered(columnAt(event->pos()));
    }

    void leaveEvent(QEvent* /*event*/) override
    {
        setHovered({});
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            if (auto const column = columnAt(event->pos()); column)
            {
                emit view_->sortRequested(TorrentDelegateRow::columnSortMode(*column));
            }
        }
    }

private:
    [[nodiscard]] std::optional<Column> columnAt(QPoint const& pos) const
    {
        for (auto const& cell : TorrentDelegateRow::layoutColumns(width()))
        {
            if (QStyle::visualRect(layoutDirection(), rect(), QRect{ cell.left, 0, cell.width, height() }).contains(pos))
            {
                return cell.column;
            }
        }

        return {};
    }

    void setHovered(std::optional<Column> column)
    {
        if (hovered_ != column)
        {
            hovered_ = column;
            update();
        }
    }

    void drawChevron(QPainter& painter, QRect const& text_bounds, Qt::Alignment alignment, QColor const& color) const
    {
        auto constexpr Size = 7.0;
        auto const right_aligned = (alignment & Qt::AlignRight) != 0;
        auto const x = right_aligned ? text_bounds.left() - 6 - Size : text_bounds.right() + 6;
        auto const y = text_bounds.center().y() + 1.0;

        // up when ascending, as in Windows Explorer
        auto const dy = reversed_ ? Size / 4 : -Size / 4;
        auto path = QPainterPath{};
        path.moveTo(x, y - dy);
        path.lineTo(x + (Size / 2), y + dy);
        path.lineTo(x + Size, y - dy);

        painter.setPen(QPen{ color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin });
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    TorrentView* const view_;
    SortMode mode_ = DefaultSortMode;
    bool reversed_ = false;
    std::optional<Column> hovered_;
};

TorrentView::TorrentView(QWidget* parent)
    : QListView{ parent }
    , header_widget_{ new HeaderWidget{ this } }
    , column_header_{ new ColumnHeader{ this } }
{
    // lets delegates highlight the row under the pointer
    viewport()->setAttribute(Qt::WA_Hover);
}

void TorrentView::setHeaderText(QString const& text)
{
    header_widget_->setText(text);
    header_widget_->setVisible(!text.isEmpty());
    adjustHeaderPosition();
}

void TorrentView::setColumnHeaderVisible(bool visible)
{
    column_header_->setVisible(visible);
    adjustHeaderPosition();
}

void TorrentView::setSortIndicator(SortMode mode, bool reversed)
{
    column_header_->setSort(mode, reversed);
}

void TorrentView::resizeEvent(QResizeEvent* event)
{
    QListView::resizeEvent(event);
    adjustHeaderPosition();
}

// stacks the visible headers above the viewport
void TorrentView::adjustHeaderPosition()
{
    auto top = 0;

    for (auto* const header : { static_cast<QWidget*>(header_widget_), static_cast<QWidget*>(column_header_) })
    {
        if (!header->isVisibleTo(this))
        {
            continue;
        }

        auto rect = contentsRect();
        rect.setTop(rect.top() + top);
        rect.setWidth(viewport()->width());
        rect.setHeight(header->sizeHint().height());
        header->setGeometry(rect);
        top += rect.height();
    }

    setViewportMargins(0, top, 0, 0);
}
