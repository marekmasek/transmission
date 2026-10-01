// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <vector>

#include "UserMetaType.h"

#include "TorrentDelegate.h"

// Table-style torrent row used by the Modern theme: one row per torrent with
// Name, Size, Progress, Status, Down, Up and ETA columns. The regular row adds
// a second line under the name with the detailed status; the compact row
// keeps to one line. TorrentView draws the matching column header.
class TorrentDelegateRow : public TorrentDelegate
{
    Q_OBJECT

public:
    enum class Column : uint8_t
    {
        Name,
        Size,
        Progress,
        Status,
        Down,
        Up,
        Eta
    };

    struct Cell
    {
        Column column;
        int left;
        int width;
    };

    explicit TorrentDelegateRow(bool compact, QObject* parent = nullptr);
    ~TorrentDelegateRow() override = default;
    TorrentDelegateRow(TorrentDelegateRow&&) = delete;
    TorrentDelegateRow(TorrentDelegateRow const&) = delete;
    TorrentDelegateRow& operator=(TorrentDelegateRow&&) = delete;
    TorrentDelegateRow& operator=(TorrentDelegateRow const&) = delete;

    // Columns that fit in a row of this width, left to right. Narrow rows drop
    // the least important columns first; Name and Progress always stay.
    [[nodiscard]] static std::vector<Cell> layoutColumns(int width);
    [[nodiscard]] static QString columnTitle(Column column);
    [[nodiscard]] static SortMode columnSortMode(Column column);
    [[nodiscard]] static Qt::Alignment columnAlignment(Column column);

    // QAbstractItemDelegate
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, QStyleOptionViewItem const& option, QModelIndex const& index)
        override;

protected:
    // TorrentDelegate
    QSize sizeHint(QStyleOptionViewItem const& option, Torrent const& tor) const override;
    void drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor) const override;

private:
    void drawName(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor, QRect const& rect) const;
    static void drawProgress(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor, QRect const& rect);

    bool const compact_;
};
