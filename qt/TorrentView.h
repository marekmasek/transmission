// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QListView>

#include "UserMetaType.h"

class TorrentView : public QListView
{
    Q_OBJECT

public:
    explicit TorrentView(QWidget* parent = nullptr);
    ~TorrentView() override = default;
    TorrentView(TorrentView&&) = delete;
    TorrentView(TorrentView const&) = delete;
    TorrentView& operator=(TorrentView&&) = delete;
    TorrentView& operator=(TorrentView const&) = delete;

    // column titles above TorrentDelegateRow rows; clicking one requests a sort
    void setColumnHeaderVisible(bool visible);
    void setSortIndicator(SortMode mode, bool reversed);

public slots:
    void setHeaderText(QString const& text);

signals:
    void headerDoubleClicked();
    void sortRequested(SortMode mode);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    class ColumnHeader;
    class HeaderWidget;

    void adjustHeaderPosition();

    HeaderWidget* const header_widget_ = {};
    ColumnHeader* const column_header_ = {};
};
