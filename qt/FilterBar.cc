// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FilterBar.h"

#include <cstdint> // uint64_t
#include <unordered_map>
#include <utility>

#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "Application.h"
#include "FilterBarComboBox.h"
#include "FilterBarComboBoxDelegate.h"
#include "Filters.h"
#include "IconCache.h"
#include "NativeIcon.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "Theme.h"
#include "TorrentModel.h"
#include "StyleHelper.h"
#include "Utils.h"

// NOLINTNEXTLINE(performance-enum-size)
enum
{
    ACTIVITY_ROLE = FilterBarComboBox::UserRole,
    TRACKER_ROLE
};

/***
****
***/

FilterBarComboBox* FilterBar::createActivityCombo()
{
    auto* c = new FilterBarComboBox{ this };
    auto* delegate = new FilterBarComboBoxDelegate{ this, c };
    c->setItemDelegate(delegate);

    auto* model = new QStandardItemModel{ this };

    auto* row = new QStandardItem{ tr("All") };
    row->setData(QVariant::fromValue(ShowMode::ShowAll), ACTIVITY_ROLE);
    model->appendRow(row);

    model->appendRow(new QStandardItem{}); // separator
    FilterBarComboBoxDelegate::setSeparator(model, model->index(1, 0));

    auto add_row = [model](auto const show_mode, QString const& label, std::optional<icons::Type> const type)
    {
        auto* new_row = type ? new QStandardItem{ icons::icon(*type), label } : new QStandardItem{ label };
        new_row->setData(QVariant::fromValue(show_mode), ACTIVITY_ROLE);
        model->appendRow(new_row);
    };
    add_row(ShowMode::ShowActive, tr("Active"), icons::Type::TorrentStateActive);
    add_row(ShowMode::ShowSeeding, tr("Seeding"), icons::Type::TorrentStateSeeding);
    add_row(ShowMode::ShowDownloading, tr("Downloading"), icons::Type::TorrentStateDownloading);
    add_row(ShowMode::ShowPaused, tr("Paused"), icons::Type::TorrentStatePaused);
    add_row(ShowMode::ShowFinished, tr("Finished"), icons::Type::TorrentStateFinished);
    add_row(ShowMode::ShowVerifying, tr("Verifying"), icons::Type::TorrentStateVerifying);
    add_row(ShowMode::ShowError, tr("Error"), icons::Type::TorrentStateError);

    c->setModel(model);
    return c;
}

/***
****
***/

namespace
{

[[nodiscard]] auto getCountString(size_t n)
{
    return QStringLiteral("%L1").arg(n);
}

Torrent::fields_t constexpr TrackerFields = {
    static_cast<uint64_t>(1) << Torrent::TRACKER_STATS,
};

[[nodiscard]] auto displayName(QString const& sitename)
{
    auto name = sitename;

    if (!name.isEmpty())
    {
        name.front() = name.front().toTitleCase();
    }

    return name;
}

} // namespace

void FilterBar::refreshTrackers()
{
    // NOLINTNEXTLINE(performance-enum-size)
    enum
    {
        ROW_TOTALS = 0,
        ROW_SEPARATOR,
        ROW_FIRST_TRACKER
    };

    auto torrents_per_sitename = std::unordered_map<QString, int>{};
    for (auto const& tor : torrents_.torrents())
    {
        for (auto const& sitename : tor->sitenames())
        {
            ++torrents_per_sitename[sitename];
        }
    }

    // update the "All" row
    auto const num_trackers = torrents_per_sitename.size();
    auto* item = tracker_model_->item(ROW_TOTALS);
    item->setData(static_cast<int>(num_trackers), FilterBarComboBox::CountRole);
    item->setData(getCountString(num_trackers), FilterBarComboBox::CountStringRole);

    auto update_tracker_item = [](QStandardItem* i, auto const& it)
    {
        auto const& [sitename, count] = *it;
        auto const display_name = displayName(sitename);

        i->setData(display_name, Qt::DisplayRole);
        i->setData(display_name, TRACKER_ROLE);
        i->setData(getCountString(static_cast<size_t>(count)), FilterBarComboBox::CountStringRole);
        i->setData(trApp->find_favicon(sitename), Qt::DecorationRole);
        i->setData(static_cast<int>(count), FilterBarComboBox::CountRole);

        return i;
    };

    auto new_trackers = small::map<QString, int>{ torrents_per_sitename.begin(), torrents_per_sitename.end() };
    auto old_it = sitename_counts_.cbegin();
    auto new_it = new_trackers.cbegin();
    auto const old_end = sitename_counts_.cend();
    auto const new_end = new_trackers.cend();
    bool any_added = false;
    int row = ROW_FIRST_TRACKER;

    while ((old_it != old_end) || (new_it != new_end))
    {
        if ((old_it == old_end) || ((new_it != new_end) && (old_it->first > new_it->first)))
        {
            tracker_model_->insertRow(row, update_tracker_item(new QStandardItem{ 1 }, new_it));
            any_added = true;
            ++new_it;
            ++row;
        }
        else if ((new_it == new_end) || ((old_it != old_end) && (old_it->first < new_it->first)))
        {
            tracker_model_->removeRow(row);
            ++old_it;
        }
        else // update
        {
            update_tracker_item(tracker_model_->item(row), new_it);
            ++old_it;
            ++new_it;
            ++row;
        }
    }

    if (any_added) // the one added might match our filter...
    {
        refreshPref(TR_KEY_filter_trackers);
    }

    sitename_counts_.swap(new_trackers);
}

FilterBarComboBox* FilterBar::createTrackerCombo(QStandardItemModel* model)
{
    auto* c = new FilterBarComboBox{ this };
    auto* delegate = new FilterBarComboBoxDelegate{ this, c };
    c->setItemDelegate(delegate);

    auto* row = new QStandardItem{ tr("All") };
    row->setData(QString{}, TRACKER_ROLE);
    int const count = torrents_.rowCount();
    row->setData(count, FilterBarComboBox::CountRole);
    row->setData(getCountString(static_cast<size_t>(count)), FilterBarComboBox::CountStringRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem{}); // separator
    FilterBarComboBoxDelegate::setSeparator(model, model->index(1, 0));

    c->setModel(model);
    return c;
}

/***
****
***/

FilterBar::FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent)
    : QWidget{ parent }
    , prefs_{ prefs }
    , torrents_{ torrents }
    , filter_{ filter }
    , count_label_{ new QLabel{ tr("Show:"), this } }
    , is_bootstrapping_{ true }
{
    auto* h = new QHBoxLayout{ this };
    h->setContentsMargins(3, 3, 3, 3);

    h->addWidget(count_label_);
    h->addWidget(activity_combo_);
    h->addWidget(tracker_combo_);
    h->addStretch();
    h->addWidget(line_edit_, 1);

    line_edit_->setClearButtonEnabled(true);
    line_edit_->setPlaceholderText(tr("Search…"));
    line_edit_->setMaximumWidth(250);
    connect(line_edit_, &QLineEdit::textChanged, this, &FilterBar::onTextChanged);

    // listen for changes from the other players
    connect(&prefs_, qOverload<tr_quark>(&Prefs::changed), this, &FilterBar::refreshPref);
    connect(activity_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::onActivityIndexChanged);
    connect(tracker_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::onTrackerIndexChanged);
    connect(&torrents_, &TorrentModel::modelReset, this, &FilterBar::recountAllSoon);
    connect(&torrents_, &TorrentModel::rowsInserted, this, &FilterBar::recountAllSoon);
    connect(&torrents_, &TorrentModel::rowsRemoved, this, &FilterBar::recountAllSoon);
    connect(&torrents_, &TorrentModel::torrentsChanged, this, &FilterBar::onTorrentsChanged);
    connect(&recount_timer_, &QTimer::timeout, this, &FilterBar::recount);
    connect(trApp, &Application::faviconsChanged, this, &FilterBar::recountTrackersSoon);

    recountAllSoon();
    is_bootstrapping_ = false; // NOLINT cppcoreguidelines-prefer-member-initializer

    // initialize our state
    for (tr_quark const key : { TR_KEY_filter_mode, TR_KEY_filter_trackers })
    {
        refreshPref(key);
    }
}

/***
****
***/

void FilterBar::clear()
{
    activity_combo_->setCurrentIndex(0);
    tracker_combo_->setCurrentIndex(0);
    line_edit_->clear();
}

/***
****
***/

namespace
{

// Windows 11 navigation item: icon, label, and the torrent count at the end.
class SidebarDelegate : public QStyledItemDelegate
{
public:
    SidebarDelegate(QString first_row_label, QObject* parent)
        : QStyledItemDelegate{ parent }
        , first_row_label_{ std::move(first_row_label) }
    {
    }

    [[nodiscard]] QSize sizeHint(QStyleOptionViewItem const& option, QModelIndex const& /*index*/) const override
    {
        auto const height = std::max(Theme::isTouch() ? 44 : 34, QFontMetrics{ option.font }.height() + 14);
        return { 120, height };
    }

    void paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override
    {
        painter->save();
        Theme::drawItemBackground(*painter, option);

        auto rect = option.rect.adjusted(16, 0, -14, 0);
        auto const icon_size = Theme::isTouch() ? 20 : 16;

        // labels line up whether or not their row has an icon
        auto const icon_rect = QStyle::alignedRect(
            option.direction,
            Qt::AlignLeft | Qt::AlignVCenter,
            QSize{ icon_size, icon_size },
            rect);
        Utils::getIconFromIndex(index)
            .paint(painter, icon_rect, Qt::AlignCenter, StyleHelper::getIconMode(option.state), QIcon::Off);
        Utils::narrowRect(rect, icon_size + 12, 0, option.direction);

        auto const count = index.data(FilterBarComboBox::CountStringRole).toString();
        auto const count_width = option.fontMetrics.horizontalAdvance(count);
        auto const count_rect = QStyle::alignedRect(
            option.direction,
            Qt::AlignRight | Qt::AlignVCenter,
            QSize{ count_width, rect.height() },
            rect);
        Utils::narrowRect(rect, 0, count_width + 8, option.direction);

        auto const label = index.row() == 0 && !first_row_label_.isEmpty() ? first_row_label_ :
                                                                             index.data(Qt::DisplayRole).toString();
        auto const left = static_cast<int>(QStyle::visualAlignment(option.direction, Qt::AlignLeft | Qt::AlignVCenter));
        auto const right = static_cast<int>(QStyle::visualAlignment(option.direction, Qt::AlignRight | Qt::AlignVCenter));

        painter->setFont(option.font);
        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(rect, left, option.fontMetrics.elidedText(label, Qt::ElideRight, rect.width()));
        painter->setPen(option.palette.color(QPalette::PlaceholderText));
        painter->drawText(count_rect, right, count);

        painter->restore();
    }

private:
    QString const first_row_label_;
};

// A sidebar list that mirrors one of the filter bar's combo boxes:
// both share the combo's model and selecting in one selects in the other.
QListView* createSidebarList(QComboBox* combo, QString const& first_row_label, QWidget* parent)
{
    auto* const list = new QListView{ parent };
    list->setModel(combo->model());
    list->setItemDelegate(new SidebarDelegate{ first_row_label, list });
    list->setFrameShape(QFrame::NoFrame);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->viewport()->setAttribute(Qt::WA_Hover);

    auto const hide_separators = [list]()
    {
        auto const* const model = list->model();
        for (int row = 0; row < model->rowCount(); ++row)
        {
            list->setRowHidden(row, FilterBarComboBoxDelegate::isSeparator(model->index(row, 0)));
        }
    };
    hide_separators();
    QObject::connect(list->model(), &QAbstractItemModel::rowsInserted, list, hide_separators);
    QObject::connect(list->model(), &QAbstractItemModel::modelReset, list, hide_separators);

    auto const select_combo_row = [list, combo]()
    {
        auto const blocker = QSignalBlocker{ list->selectionModel() };
        list->selectionModel()->setCurrentIndex(
            list->model()->index(combo->currentIndex(), 0),
            QItemSelectionModel::ClearAndSelect);
    };
    select_combo_row();
    QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), list, select_combo_row);

    QObject::connect(
        list->selectionModel(),
        &QItemSelectionModel::currentChanged,
        combo,
        [combo](QModelIndex const& current)
        {
            if (current.isValid() && current.row() != combo->currentIndex())
            {
                combo->setCurrentIndex(current.row());
            }
        });

    return list;
}

} // namespace

QWidget* FilterBar::createSidebar(QWidget* parent)
{
    auto* const sidebar = new QWidget{ parent };
    sidebar->setObjectName(QStringLiteral("filterSidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground);

    auto* const layout = new QVBoxLayout{ sidebar };
    layout->setContentsMargins(4, 0, 4, 4);
    layout->setSpacing(0);

    auto* const status_list = createSidebarList(activity_combo_, {}, sidebar);
    auto* const tracker_list = createSidebarList(tracker_combo_, tr("All trackers"), sidebar);

    // the status list never scrolls; the tracker list takes the remaining height
    auto const fit_status_list = [status_list]()
    {
        auto height = 2 * status_list->frameWidth();
        for (int row = 0; row < status_list->model()->rowCount(); ++row)
        {
            if (!status_list->isRowHidden(row))
            {
                height += status_list->sizeHintForRow(row);
            }
        }
        status_list->setFixedHeight(height);
    };
    fit_status_list();
    connect(this, &FilterBar::sidebarMetricsChanged, status_list, fit_status_list);

    layout->addWidget(new QLabel{ tr("Status"), sidebar });
    layout->addWidget(status_list);
    layout->addWidget(new QLabel{ tr("Trackers"), sidebar });
    layout->addWidget(tracker_list, 1);

    sidebar->setMinimumWidth(180);
    sidebar->setMaximumWidth(230);
    return sidebar;
}

void FilterBar::focusSearch()
{
    line_edit_->setFocus(Qt::ShortcutFocusReason);
    line_edit_->selectAll();
}

void FilterBar::refreshPref(tr_quark key)
{
    switch (key)
    {
    case TR_KEY_filter_mode:
        {
            auto const show_mode = prefs_.get<ShowMode>(key);
            QAbstractItemModel const* const model = activity_combo_->model();
            QModelIndexList indices = model->match(model->index(0, 0), ACTIVITY_ROLE, QVariant::fromValue(show_mode));
            activity_combo_->setCurrentIndex(indices.isEmpty() ? 0 : indices.first().row());
            break;
        }

    case TR_KEY_filter_trackers:
        {
            auto const display_name = prefs_.get<QString>(key);

            if (auto rows = tracker_model_->findItems(display_name); !rows.isEmpty())
            {
                tracker_combo_->setCurrentIndex(rows.front()->row());
            }
            else // hm, we don't seem to have this tracker anymore...
            {
                bool const is_bootstrapping = tracker_model_->rowCount() <= 2;

                if (!is_bootstrapping)
                {
                    prefs_.set(key, QString{});
                }
            }

            break;
        }

    case TR_KEY_filter_text:
        if (auto const text = prefs_.get<QString>(key); line_edit_->text().trimmed() != text)
        {
            auto const blocker = QSignalBlocker{ line_edit_ };
            line_edit_->setText(text);
        }
        break;

    default:
        break;
    }
}

void FilterBar::onTorrentsChanged(torrent_ids_t const& ids, Torrent::fields_t const& changed_fields)
{
    Q_UNUSED(ids)

    if ((changed_fields & TrackerFields).any())
    {
        recountTrackersSoon();
    }

    if ((changed_fields & ShowModeFields).any())
    {
        recountActivitySoon();
    }
}

void FilterBar::onTextChanged(QString const& str)
{
    if (!is_bootstrapping_)
    {
        prefs_.set(TR_KEY_filter_text, str.trimmed());
    }
}

void FilterBar::onTrackerIndexChanged(int i)
{
    if (!is_bootstrapping_)
    {
        auto const display_name = tracker_combo_->itemData(i, TRACKER_ROLE).toString();
        prefs_.set(TR_KEY_filter_trackers, display_name);
    }
}

void FilterBar::onActivityIndexChanged(int i)
{
    if (!is_bootstrapping_)
    {
        auto const show_mode = activity_combo_->itemData(i, ACTIVITY_ROLE).value<ShowMode>();
        prefs_.set(TR_KEY_filter_mode, show_mode);
    }
}

/***
****
***/

void FilterBar::recountSoon(Pending const& fields)
{
    pending_ |= fields;

    if (!recount_timer_.isActive())
    {
        recount_timer_.setSingleShot(true);
        recount_timer_.start(800);
    }
}

void FilterBar::recount()
{
    QAbstractItemModel* model = activity_combo_->model();

    decltype(pending_) pending = {};
    std::swap(pending_, pending);

    if (pending[ACTIVITY])
    {
        auto const torrents_per_mode = filter_.countTorrentsPerMode();

        for (int row = 0, n = model->rowCount(); row < n; ++row)
        {
            auto const index = model->index(row, 0);
            auto const show_mode = index.data(ACTIVITY_ROLE).value<ShowMode>();
            auto const count = torrents_per_mode[static_cast<int>(show_mode)];
            model->setData(index, count, FilterBarComboBox::CountRole);
            model->setData(index, getCountString(count), FilterBarComboBox::CountStringRole);
        }
    }

    if (pending[TRACKERS])
    {
        refreshTrackers();
    }
}
