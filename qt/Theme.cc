// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Theme.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QFontDatabase>
#include <QGestureEvent>
#include <QItemSelectionModel>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QScrollArea>
#include <QScroller>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QStyleOptionViewItem>
#include <QTapAndHoldGesture>
#include <QWidget>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#elif defined(_WIN32)
#include <QSettings>
#endif

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace
{

auto constexpr TouchControlHeight = 40;
auto constexpr TouchItemHeight = 36;
auto constexpr TouchIndicatorSize = 22;

struct Scheme
{
    QColor window;
    QColor base;
    QColor alt_base;
    QColor text;
    QColor muted;
    QColor button;
    QColor border;
    QColor border_strong;
    QColor hover;
    QColor hover_solid;
    QColor accent;
    QColor accent2;
    QColor accent_soft;
    QColor accent_border;
    QColor accent_text;
    QColor scroll;
    QColor scroll_hover;
    QColor track;
    QColor error;
    QColor download_from;
    QColor download_to;
    QColor seed_from;
    QColor seed_to;
    QColor idle_from;
    QColor idle_to;
};

Scheme const LightScheme = {
    .window = QColor{ 0xF4, 0xF5, 0xFA },
    .base = QColor{ 0xFF, 0xFF, 0xFF },
    .alt_base = QColor{ 0xF7, 0xF8, 0xFC },
    .text = QColor{ 0x1B, 0x1F, 0x2A },
    .muted = QColor{ 0x6B, 0x72, 0x83 },
    .button = QColor{ 0xFF, 0xFF, 0xFF },
    .border = QColor{ 0xDC, 0xE0, 0xEA },
    .border_strong = QColor{ 0xB9, 0xC0, 0xCE },
    .hover = QColor{ 91, 95, 239, 20 },
    .hover_solid = QColor{ 0xF1, 0xF2, 0xFE },
    .accent = QColor{ 0x5B, 0x5F, 0xEF },
    .accent2 = QColor{ 0xA8, 0x55, 0xF7 },
    .accent_soft = QColor{ 91, 95, 239, 36 },
    .accent_border = QColor{ 91, 95, 239, 115 },
    .accent_text = QColor{ 0x4A, 0x4F, 0xD8 },
    .scroll = QColor{ 27, 31, 42, 70 },
    .scroll_hover = QColor{ 27, 31, 42, 130 },
    .track = QColor{ 27, 31, 42, 22 },
    .error = QColor{ 0xDC, 0x35, 0x45 },
    .download_from = QColor{ 0x3B, 0x82, 0xF6 },
    .download_to = QColor{ 0x8B, 0x5C, 0xF6 },
    .seed_from = QColor{ 0x10, 0xB9, 0x81 },
    .seed_to = QColor{ 0x06, 0xB6, 0xD4 },
    .idle_from = QColor{ 0x9A, 0xA3, 0xB2 },
    .idle_to = QColor{ 0xC2, 0xC8, 0xD2 },
};

Scheme const DarkScheme = {
    .window = QColor{ 0x0F, 0x11, 0x17 },
    .base = QColor{ 0x18, 0x1B, 0x23 },
    .alt_base = QColor{ 0x1D, 0x21, 0x2A },
    .text = QColor{ 0xE6, 0xE8, 0xEF },
    .muted = QColor{ 0x8E, 0x95, 0xA6 },
    .button = QColor{ 0x22, 0x26, 0x31 },
    .border = QColor{ 0x2A, 0x2F, 0x3B },
    .border_strong = QColor{ 0x44, 0x4B, 0x5A },
    .hover = QColor{ 124, 131, 255, 28 },
    .hover_solid = QColor{ 0x27, 0x2B, 0x38 },
    .accent = QColor{ 0x7C, 0x83, 0xFF },
    .accent2 = QColor{ 0xC0, 0x84, 0xFC },
    .accent_soft = QColor{ 124, 131, 255, 50 },
    .accent_border = QColor{ 124, 131, 255, 140 },
    .accent_text = QColor{ 0xA9, 0xAE, 0xFF },
    .scroll = QColor{ 230, 232, 240, 55 },
    .scroll_hover = QColor{ 230, 232, 240, 110 },
    .track = QColor{ 255, 255, 255, 22 },
    .error = QColor{ 0xFF, 0x6B, 0x6B },
    .download_from = QColor{ 0x60, 0xA5, 0xFA },
    .download_to = QColor{ 0xA7, 0x8B, 0xFA },
    .seed_from = QColor{ 0x34, 0xD3, 0x99 },
    .seed_to = QColor{ 0x22, 0xD3, 0xEE },
    .idle_from = QColor{ 0x5B, 0x62, 0x73 },
    .idle_to = QColor{ 0x7A, 0x82, 0x94 },
};

struct State
{
    bool initialized = false;
    QString id;
    bool modern = false;
    bool dark = false;
    bool touch = false;
    int generation = 0;
    QString native_style_key;
    QFont native_font;
    bool native_dark = false;
};

State& state()
{
    static auto instance = State{};
    return instance;
}

Scheme const& scheme()
{
    return state().dark ? DarkScheme : LightScheme;
}

bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#elif defined(_WIN32)
    auto const settings = QSettings{
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat
    };
    return settings.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#else
    return state().native_dark;
#endif
}

QString styleKey(QStyle const* style)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    return style->name();
#else
    return style->objectName();
#endif
}

// ---

#ifdef _WIN32

void applyWindowFrame(QWidget* window)
{
    if (!window->isWindow() || !window->testAttribute(Qt::WA_WState_Created))
    {
        return;
    }

    auto* const hwnd = reinterpret_cast<HWND>(window->winId());
    auto const& st = state();

    // DWMWA_USE_IMMERSIVE_DARK_MODE: dark title bar to match a dark palette
    BOOL const dark = st.modern ? st.dark : systemPrefersDark();
    ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    // DWMWA_WINDOW_CORNER_PREFERENCE: Windows 11 rounds and shadows popups
    // natively, matching the menu radius in the Modern stylesheet
    if (qobject_cast<QMenu*>(window) != nullptr)
    {
        DWORD const corners = st.modern ? 2 /*DWMWCP_ROUND*/ : 0 /*DWMWCP_DEFAULT*/;
        ::DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
    }
}

#else

void applyWindowFrame(QWidget* /*window*/)
{
}

#endif

// ---

// Persistent event filter for touch input and native window frames.
// It outlives the AppStyle instances, which are replaced on every theme switch.
class InputHelper : public QObject
{
public:
    using QObject::QObject;

    void enableTouch(QAbstractScrollArea* area)
    {
        auto* const viewport = area->viewport();
        if (viewport == nullptr || viewport->property(TouchReadyProperty).toBool())
        {
            return;
        }

        viewport->setProperty(TouchReadyProperty, true);

        // Without a scroller, a finger drag on an item view starts a rubber-band
        // selection instead of scrolling. Kinetic scrolling needs per-pixel steps.
        if (auto* const view = qobject_cast<QAbstractItemView*>(area); view != nullptr)
        {
            view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        }

        QScroller::grabGesture(viewport, QScroller::TouchGesture);
        auto* const scroller = QScroller::scroller(viewport);
        auto props = scroller->scrollerProperties();
        props.setScrollMetric(QScrollerProperties::FrameRate, QVariant::fromValue(QScrollerProperties::Fps60));
        props.setScrollMetric(
            QScrollerProperties::HorizontalOvershootPolicy,
            QVariant::fromValue(QScrollerProperties::OvershootAlwaysOff));
        props.setScrollMetric(
            QScrollerProperties::VerticalOvershootPolicy,
            QVariant::fromValue(QScrollerProperties::OvershootWhenScrollable));
        scroller->setScrollerProperties(props);

        viewport->grabGesture(Qt::TapAndHoldGesture);
        viewport->installEventFilter(this);
    }

    void watchWindow(QWidget* window)
    {
        window->installEventFilter(this);
        applyWindowFrame(window);
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        switch (event->type())
        {
        case QEvent::Show:
            if (auto* const widget = qobject_cast<QWidget*>(object); widget != nullptr && widget->isWindow())
            {
                applyWindowFrame(widget);
            }
            break;

        case QEvent::TouchBegin:
            last_press_was_touch_ = true;
            break;

        case QEvent::MouseButtonPress:
            last_press_was_touch_ = !isRealMouse(static_cast<QMouseEvent const*>(event));
            break;

        case QEvent::Gesture:
            return onGesture(static_cast<QWidget*>(object), static_cast<QGestureEvent*>(event));

        default:
            break;
        }

        return QObject::eventFilter(object, event);
    }

private:
    static constexpr char const* TouchReadyProperty = "trTouchReady";

    static bool isRealMouse(QMouseEvent const* event)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto const* const device = event->pointingDevice();
        return device == nullptr || device->type() == QInputDevice::DeviceType::Mouse;
#else
        return event->source() == Qt::MouseEventNotSynthesized;
#endif
    }

    // Press-and-hold with a finger opens the context menu, as right-click does.
    bool onGesture(QWidget* viewport, QGestureEvent* event) const
    {
        auto* const gesture = static_cast<QTapAndHoldGesture*>(event->gesture(Qt::TapAndHoldGesture));
        if (gesture == nullptr || gesture->state() != Qt::GestureFinished || !last_press_was_touch_)
        {
            return false;
        }

        if (auto const scroller_state = QScroller::scroller(viewport)->state();
            scroller_state == QScroller::Dragging || scroller_state == QScroller::Scrolling)
        {
            return false;
        }

        auto const global_pos = gesture->position().toPoint();
        auto const local_pos = viewport->mapFromGlobal(global_pos);

        if (auto* const view = qobject_cast<QAbstractItemView*>(viewport->parentWidget()); view != nullptr)
        {
            if (auto const index = view->indexAt(local_pos);
                index.isValid() && view->selectionModel() != nullptr && !view->selectionModel()->isSelected(index))
            {
                view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
        }

        auto menu_event = QContextMenuEvent{ QContextMenuEvent::Mouse, local_pos, global_pos };
        QCoreApplication::sendEvent(viewport, &menu_event);
        event->accept(gesture);
        return true;
    }

    bool last_press_was_touch_ = false;
};

InputHelper& inputHelper()
{
    static auto* const instance = new InputHelper{ qApp };
    return *instance;
}

// ---

class AppStyle : public QProxyStyle
{
public:
    explicit AppStyle(QStyle* base)
        : QProxyStyle{ base }
    {
    }

    int pixelMetric(PixelMetric metric, QStyleOption const* option, QWidget const* widget) const override
    {
        auto const& st = state();
        auto const base = QProxyStyle::pixelMetric(metric, option, widget);

        if (st.touch)
        {
            switch (metric)
            {
            case PM_ToolBarIconSize:
                return 32;

            case PM_ScrollBarExtent:
                return std::max(base, 18);

            case PM_IndicatorWidth:
            case PM_IndicatorHeight:
            case PM_ExclusiveIndicatorWidth:
            case PM_ExclusiveIndicatorHeight:
                return std::max(base, TouchIndicatorSize);

            case PM_SmallIconSize:
                return std::max(base, 20);

            default:
                break;
            }
        }

        if (st.modern && metric == PM_ToolBarIconSize)
        {
            return 24;
        }

        return base;
    }

    QSize sizeFromContents(ContentsType type, QStyleOption const* option, QSize const& size, QWidget const* widget)
        const override
    {
        auto result = QProxyStyle::sizeFromContents(type, option, size, widget);

        if (!state().touch)
        {
            return result;
        }

        switch (type)
        {
        case CT_PushButton:
        case CT_ToolButton:
        case CT_ComboBox:
        case CT_LineEdit:
        case CT_SpinBox:
        case CT_TabBarTab:
        case CT_MenuBarItem:
            result.setHeight(std::max(result.height(), TouchControlHeight));
            break;

        case CT_MenuItem:
            if (auto const* const item = qstyleoption_cast<QStyleOptionMenuItem const*>(option);
                item != nullptr && item->menuItemType != QStyleOptionMenuItem::Separator)
            {
                result.setHeight(std::max(result.height(), TouchControlHeight));
            }
            break;

        case CT_ItemViewItem:
            result.setHeight(std::max(result.height(), TouchItemHeight));
            break;

        case CT_CheckBox:
        case CT_RadioButton:
            result.setHeight(std::max(result.height(), TouchItemHeight));
            break;

        default:
            break;
        }

        return result;
    }

    int styleHint(StyleHint hint, QStyleOption const* option, QWidget const* widget, QStyleHintReturn* return_data)
        const override
    {
        // The torrent list activates on double-click on every platform,
        // matching the GTK client.
        if (hint == SH_ItemView_ActivateItemOnSingleClick && widget != nullptr && widget->inherits("TorrentView"))
        {
            return 0;
        }

        if (hint == SH_ToolButtonStyle && state().touch)
        {
            return Qt::ToolButtonTextUnderIcon;
        }

        return QProxyStyle::styleHint(hint, option, widget, return_data);
    }

    void polish(QWidget* widget) override
    {
        QProxyStyle::polish(widget);

        if (auto* const area = qobject_cast<QAbstractScrollArea*>(widget); area != nullptr &&
            (qobject_cast<QAbstractItemView*>(area) != nullptr || qobject_cast<QScrollArea*>(area) != nullptr))
        {
            inputHelper().enableTouch(area);
        }

        if (widget->isWindow())
        {
            inputHelper().watchWindow(widget);
        }
    }

    using QProxyStyle::polish;
};

// ---

QString qssColor(QColor const& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

QString makeStyleSheet()
{
    auto file = QFile{ QStringLiteral(":/themes/modern.qss") };
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }

    auto qss = QString::fromUtf8(file.readAll());
    auto const& s = scheme();
    auto const touch = state().touch;

#ifdef _WIN32
    auto const menu_radius = 8; // matches DWMWCP_ROUND
#else
    auto const menu_radius = 0; // popups have square, opaque corners
#endif

    auto const tokens = std::initializer_list<std::pair<char const*, QString>>{
        { "window", qssColor(s.window) },
        { "base", qssColor(s.base) },
        { "alt_base", qssColor(s.alt_base) },
        { "text", qssColor(s.text) },
        { "muted", qssColor(s.muted) },
        { "button", qssColor(s.button) },
        { "border_strong", qssColor(s.border_strong) },
        { "border", qssColor(s.border) },
        { "hover_solid", qssColor(s.hover_solid) },
        { "hover", qssColor(s.hover) },
        { "accent_soft", qssColor(s.accent_soft) },
        { "accent_border", qssColor(s.accent_border) },
        { "accent_text", qssColor(s.accent_text) },
        { "accent2", qssColor(s.accent2) },
        { "accent", qssColor(s.accent) },
        { "scroll_hover", qssColor(s.scroll_hover) },
        { "scroll_w", QString::number(touch ? 16 : 10) },
        { "scroll_r", QString::number(touch ? 6 : 3) },
        { "scroll", qssColor(s.scroll) },
        { "track", qssColor(s.track) },
        { "pad_s", QString::number(touch ? 8 : 4) },
        { "ctl_h", QString::number(touch ? TouchControlHeight - 2 * 9 : 20) },
        { "radius_l", QString::number(touch ? 14 : 12) },
        { "radius", QString::number(touch ? 10 : 7) },
        { "menu_pad", QString::number(touch ? 11 : 6) },
        { "menu_radius", QString::number(menu_radius) },
        { "ind_round", QString::number((touch ? TouchIndicatorSize : 16) / 2) },
        { "ind_r", QString::number(touch ? 6 : 4) },
        { "ind", QString::number(touch ? TouchIndicatorSize : 16) },
        { "spin_w", QString::number(touch ? 32 : 22) },
        { "item_pad", QString::number(touch ? 9 : 3) },
        { "tab_h", QString::number(touch ? TouchControlHeight - 2 * 9 : 18) },
    };

    for (auto const& [name, value] : tokens)
    {
        qss.replace(QLatin1Char('@') + QLatin1String(name) + QLatin1Char('@'), value);
    }

    return qss;
}

QPalette makeModernPalette()
{
    auto const& s = scheme();
    auto pal = QPalette{};

    auto const set_all = [&pal](QPalette::ColorRole role, QColor const& color)
    {
        pal.setColor(QPalette::Active, role, color);
        pal.setColor(QPalette::Inactive, role, color);
        pal.setColor(QPalette::Disabled, role, color);
    };

    set_all(QPalette::Window, s.window);
    set_all(QPalette::Base, s.base);
    set_all(QPalette::AlternateBase, s.alt_base);
    set_all(QPalette::Button, s.button);
    set_all(QPalette::ToolTipBase, s.base);
    set_all(QPalette::ToolTipText, s.text);
    set_all(QPalette::Highlight, s.accent);
    set_all(QPalette::HighlightedText, Qt::white);
    set_all(QPalette::Link, s.accent_text);
    set_all(QPalette::LinkVisited, s.accent2);
    set_all(QPalette::BrightText, Qt::white);
    set_all(QPalette::Light, s.base.lighter(110));
    set_all(QPalette::Midlight, s.border);
    set_all(QPalette::Mid, s.border_strong);
    set_all(QPalette::Dark, s.border_strong.darker(130));
    set_all(QPalette::Shadow, QColor{ 0, 0, 0, 80 });
    set_all(QPalette::PlaceholderText, s.muted);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    set_all(QPalette::Accent, s.accent);
#endif

    for (auto const role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
    {
        pal.setColor(QPalette::Active, role, s.text);
        pal.setColor(QPalette::Inactive, role, s.text);
        pal.setColor(QPalette::Disabled, role, s.muted);
    }

    pal.setColor(QPalette::Disabled, QPalette::Highlight, s.border_strong);

    return pal;
}

QPalette makeNativePalette()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // a palette with no roles set resolves entirely to the style and platform palette
    return QPalette{};
#else
    return QApplication::style()->standardPalette();
#endif
}

#ifdef _WIN32
QStringList fontFamilies()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QFontDatabase::families();
#else
    return QFontDatabase{}.families();
#endif
}
#endif

QFont makeFont()
{
    auto const& st = state();
    auto font = st.native_font;

#ifdef _WIN32
    if (auto const family = QStringLiteral("Segoe UI Variable Text"); st.modern && fontFamilies().contains(family))
    {
        font.setFamily(family);
    }
#endif

    if (st.touch && font.pointSizeF() > 0)
    {
        font.setPointSizeF(font.pointSizeF() + 1.5);
    }

    return font;
}

} // namespace

// ---

std::vector<Theme::Choice> Theme::choices()
{
    auto ret = std::vector<Choice>{
        { QStringLiteral("modern"), tr("Modern (follow system)") },
        { QStringLiteral("modern_light"), tr("Modern Light") },
        { QStringLiteral("modern_dark"), tr("Modern Dark") },
        { QStringLiteral("native"), tr("System default") },
    };

    for (auto const& key : QStyleFactory::keys())
    {
        //: Theme menu entry for one of Qt's built-in widget styles, e.g. "Classic: Fusion"
        ret.push_back({ QStringLiteral("style:") + key, tr("Classic: %1").arg(key) });
    }

    return ret;
}

void Theme::apply(QString const& id, bool touch_mode)
{
    auto& st = state();

    if (!st.initialized)
    {
        st.initialized = true;
        st.native_style_key = styleKey(QApplication::style());
        st.native_font = QApplication::font();
        st.native_dark = QApplication::palette().color(QPalette::Window).lightness() < 128;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QObject::connect(
            QGuiApplication::styleHints(),
            &QStyleHints::colorSchemeChanged,
            qApp,
            []()
            {
                if (auto const& cur = state(); cur.id == QStringLiteral("modern"))
                {
                    apply(cur.id, cur.touch);
                }
            });
#endif
    }

    st.id = id;
    st.touch = touch_mode;
    st.modern = id.startsWith(QStringLiteral("modern"));
    st.dark = id == QStringLiteral("modern_dark") || (id == QStringLiteral("modern") && systemPrefersDark());
    ++st.generation;

    auto base_key = st.native_style_key;
    if (st.modern)
    {
        base_key = QStringLiteral("Fusion");
    }
    else if (auto const prefix = QStringLiteral("style:"); id.startsWith(prefix))
    {
        base_key = id.mid(prefix.size());
    }

    auto* base = QStyleFactory::create(base_key);
    if (base == nullptr)
    {
        base = QStyleFactory::create(st.native_style_key);
    }

    QApplication::setStyle(new AppStyle{ base });
    QApplication::setPalette(st.modern ? makeModernPalette() : makeNativePalette());
    QApplication::setFont(makeFont());
    qApp->setStyleSheet(st.modern ? makeStyleSheet() : QString{});

    for (auto* const widget : QApplication::topLevelWidgets())
    {
        applyWindowFrame(widget);
    }
}

bool Theme::isModern() noexcept
{
    return state().modern;
}

bool Theme::isDark() noexcept
{
    return state().dark;
}

bool Theme::isTouch() noexcept
{
    return state().touch;
}

int Theme::generation() noexcept
{
    return state().generation;
}

void Theme::drawItemCard(QPainter& painter, QStyleOptionViewItem const& option)
{
    auto const& s = scheme();
    auto const touch = isTouch();
    auto const selected = option.state.testFlag(QStyle::State_Selected);
    auto const hovered = option.state.testFlag(QStyle::State_MouseOver);
    auto const focused = option.state.testFlag(QStyle::State_HasFocus);
    auto const active = option.state.testFlag(QStyle::State_Active);

    auto const gap_x = 8.0;
    auto const gap_y = touch ? 5.0 : 4.0;
    auto const radius = touch ? 14.0 : 11.0;
    auto const rect = QRectF{ option.rect }.adjusted(gap_x + 0.5, gap_y + 0.5, -gap_x - 0.5, -gap_y - 0.5);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    auto card = QPainterPath{};
    card.addRoundedRect(rect, radius, radius);

    if (selected)
    {
        // soft glow around the selected card
        for (int i = 3; i >= 1; --i)
        {
            auto glow_color = s.accent;
            glow_color.setAlpha(active ? 22 : 10);
            auto glow = QPainterPath{};
            glow.addRoundedRect(rect.adjusted(-i, -i, i, i), radius + i, radius + i);
            painter.fillPath(glow, glow_color);
        }

        auto gradient = QLinearGradient{ rect.topLeft(), rect.topRight() };
        auto from = s.accent;
        auto to = s.accent2;
        if (!active)
        {
            from.setAlpha(190);
            to.setAlpha(190);
        }
        gradient.setColorAt(0.0, from);
        gradient.setColorAt(1.0, to);
        painter.fillPath(card, gradient);

        auto sheen = QLinearGradient{ rect.topLeft(), rect.bottomLeft() };
        sheen.setColorAt(0.0, QColor{ 255, 255, 255, 40 });
        sheen.setColorAt(0.5, QColor{ 255, 255, 255, 0 });
        painter.fillPath(card, sheen);
    }
    else
    {
        painter.fillPath(card, s.base);

        if (hovered)
        {
            painter.fillPath(card, s.hover);
        }

        painter.setPen(QPen{ hovered ? s.accent_border : s.border, 1.0 });
        painter.drawPath(card);
    }

    if (focused)
    {
        auto ring = selected ? QColor{ 255, 255, 255, 170 } : s.accent;
        painter.setPen(QPen{ ring, 1.5 });
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1.5, radius - 1.5);
    }

    painter.restore();
}

QBrush Theme::progressBrush(Bar bar, bool on_accent)
{
    if (on_accent)
    {
        return QColor{ 255, 255, 255, 235 };
    }

    auto const& s = scheme();
    auto gradient = QLinearGradient{ 0, 0, 1, 0 };
    gradient.setCoordinateMode(QGradient::ObjectBoundingMode);

    switch (bar)
    {
    case Bar::Downloading:
        gradient.setColorAt(0.0, s.download_from);
        gradient.setColorAt(1.0, s.download_to);
        break;

    case Bar::Seeding:
        gradient.setColorAt(0.0, s.seed_from);
        gradient.setColorAt(1.0, s.seed_to);
        break;

    case Bar::Idle:
        gradient.setColorAt(0.0, s.idle_from);
        gradient.setColorAt(1.0, s.idle_to);
        break;
    }

    return gradient;
}

QColor Theme::progressTrack(bool on_accent)
{
    return on_accent ? QColor{ 255, 255, 255, 60 } : scheme().track;
}

QColor Theme::errorColor()
{
    return scheme().error;
}
