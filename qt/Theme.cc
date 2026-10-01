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
#include <QItemSelectionModel>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QScrollArea>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QStyleOptionToolButton>
#include <QStyleOptionViewItem>
#include <QWidget>
#include <QtWidgets/qtwidgetsglobal.h>

// Some Qt builds, such as the one Transmission ships on Windows, can leave out
// kinetic scrolling and gestures; touch input then falls back to Qt's defaults.
#if QT_CONFIG(scroller)
#include <QScroller>
#endif
#if QT_CONFIG(gestures)
#include <QGestureEvent>
#include <QTapAndHoldGesture>
#endif

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
    QColor download;
    QColor seed;
    QColor idle;
    QColor on_accent;
    QColor surface;
};

Scheme const LightScheme = {
    .window = QColor{ 0xF3, 0xF3, 0xF3 },
    .base = QColor{ 0xFF, 0xFF, 0xFF },
    .alt_base = QColor{ 0xF9, 0xF9, 0xF9 },
    .text = QColor{ 0x1A, 0x1A, 0x1A },
    .muted = QColor{ 0x5F, 0x5F, 0x5F },
    .button = QColor{ 0xFB, 0xFB, 0xFB },
    .border = QColor{ 0xE5, 0xE5, 0xE5 },
    .border_strong = QColor{ 0xC8, 0xC8, 0xC8 },
    .hover = QColor{ 0, 0, 0, 10 },
    .hover_solid = QColor{ 0xF6, 0xF6, 0xF6 },
    .accent = QColor{ 0x00, 0x5F, 0xB8 },
    .accent2 = QColor{ 0x19, 0x6E, 0xBF },
    .accent_soft = QColor{ 0, 95, 184, 24 },
    .accent_border = QColor{ 0, 95, 184, 130 },
    .accent_text = QColor{ 0x00, 0x3E, 0x92 },
    .scroll = QColor{ 0, 0, 0, 90 },
    .scroll_hover = QColor{ 0, 0, 0, 140 },
    .track = QColor{ 0, 0, 0, 40 },
    .error = QColor{ 0xC4, 0x2B, 0x1C },
    .download = QColor{ 0x00, 0x5F, 0xB8 },
    .seed = QColor{ 0x0F, 0x7B, 0x0F },
    .idle = QColor{ 0x8A, 0x8A, 0x8A },
    .on_accent = QColor{ 0xFF, 0xFF, 0xFF },
    .surface = QColor{ 0xFB, 0xFB, 0xFB },
};

Scheme const DarkScheme = {
    .window = QColor{ 0x20, 0x20, 0x20 },
    .base = QColor{ 0x2D, 0x2D, 0x2D },
    .alt_base = QColor{ 0x2A, 0x2A, 0x2A },
    .text = QColor{ 0xFF, 0xFF, 0xFF },
    .muted = QColor{ 0xC5, 0xC5, 0xC5 },
    .button = QColor{ 0x2D, 0x2D, 0x2D },
    .border = QColor{ 0x35, 0x35, 0x35 },
    .border_strong = QColor{ 0x47, 0x47, 0x47 },
    .hover = QColor{ 255, 255, 255, 15 },
    .hover_solid = QColor{ 0x32, 0x32, 0x32 },
    .accent = QColor{ 0x60, 0xCD, 0xFF },
    .accent2 = QColor{ 0x5A, 0xB9, 0xE6 },
    .accent_soft = QColor{ 96, 205, 255, 30 },
    .accent_border = QColor{ 96, 205, 255, 150 },
    .accent_text = QColor{ 0x99, 0xEB, 0xFF },
    .scroll = QColor{ 255, 255, 255, 90 },
    .scroll_hover = QColor{ 255, 255, 255, 150 },
    .track = QColor{ 255, 255, 255, 45 },
    .error = QColor{ 0xFF, 0x99, 0xA4 },
    .download = QColor{ 0x60, 0xCD, 0xFF },
    .seed = QColor{ 0x6C, 0xCB, 0x5F },
    .idle = QColor{ 0x9A, 0x9A, 0x9A },
    .on_accent = QColor{ 0x00, 0x00, 0x00 },
    .surface = QColor{ 0x27, 0x27, 0x27 },
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

#if QT_CONFIG(scroller)
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
#endif

#if QT_CONFIG(gestures)
        viewport->grabGesture(Qt::TapAndHoldGesture);
#endif
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

#if QT_CONFIG(gestures)
        case QEvent::Gesture:
            return onGesture(static_cast<QWidget*>(object), static_cast<QGestureEvent*>(event));
#endif

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

#if QT_CONFIG(gestures)
    // Press-and-hold with a finger opens the context menu, as right-click does.
    bool onGesture(QWidget* viewport, QGestureEvent* event) const
    {
        auto* const gesture = static_cast<QTapAndHoldGesture*>(event->gesture(Qt::TapAndHoldGesture));
        if (gesture == nullptr || gesture->state() != Qt::GestureFinished || !last_press_was_touch_)
        {
            return false;
        }

#if QT_CONFIG(scroller)
        if (auto const scroller_state = QScroller::scroller(viewport)->state();
            scroller_state == QScroller::Dragging || scroller_state == QScroller::Scrolling)
        {
            return false;
        }
#endif

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
#endif

    bool last_press_was_touch_ = false;
};

InputHelper& inputHelper()
{
    static auto* const instance = new InputHelper{ qApp };
    return *instance;
}

// ---

QIcon tinted(QIcon const& icon, QSize const& size, qreal dpr, QColor const& color)
{
    auto image = icon.pixmap(size * dpr).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        auto painter = QPainter{ &image };
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
    }

    auto pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(dpr);
    return QIcon{ pixmap };
}

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
                return st.modern ? 20 : 32;

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
            return 16;
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

        if (hint == SH_ToolButtonStyle && (state().modern || state().touch))
        {
            // a Windows 11 command bar labels its buttons
            return state().modern ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonTextUnderIcon;
        }

        return QProxyStyle::styleHint(hint, option, widget, return_data);
    }

    void drawControl(ControlElement element, QStyleOption const* option, QPainter* painter, QWidget const* widget)
        const override
    {
        // the primary command bar button draws its icon in the text color on the accent fill
        if (auto const* const button = qstyleoption_cast<QStyleOptionToolButton const*>(option); button != nullptr &&
            element == CE_ToolButtonLabel && state().modern && widget != nullptr &&
            widget->objectName() == QStringLiteral("primaryAction") && !button->icon.isNull())
        {
            auto copy = *button;
            copy.icon = tinted(button->icon, button->iconSize, widget->devicePixelRatioF(), scheme().on_accent);
            QProxyStyle::drawControl(element, &copy, painter, widget);
            return;
        }

        QProxyStyle::drawControl(element, option, painter, widget);
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
    auto const dark = state().dark;

#ifdef _WIN32
    auto const menu_radius = 8; // matches DWMWCP_ROUND
#else
    auto const menu_radius = 0; // popups have square, opaque corners
#endif

    auto const tokens = std::initializer_list<std::pair<char const*, QString>>{
        { "window", qssColor(s.window) },
        { "check", dark ? QStringLiteral(":/themes/check-black.svg") : QStringLiteral(":/themes/check.svg") },
        { "dash", dark ? QStringLiteral(":/themes/dash-black.svg") : QStringLiteral(":/themes/dash.svg") },
        { "dot", dark ? QStringLiteral(":/themes/dot-black.svg") : QStringLiteral(":/themes/dot.svg") },
        { "menu_check", dark ? QStringLiteral(":/themes/check.svg") : QStringLiteral(":/themes/check-text-light.svg") },
        { "nav_pad", QString::number(touch ? 10 : 5) },
        { "split_w", QString::number(touch ? 32 : 22) },
        { "on_accent_line", qssColor(QColor{ s.on_accent.red(), s.on_accent.green(), s.on_accent.blue(), 90 }) },
        { "on_accent_chevron",
          dark ? QStringLiteral(":/themes/chevron-down-black.svg") : QStringLiteral(":/themes/chevron-down-white.svg") },
        { "on_accent", qssColor(s.on_accent) },
        { "surface", qssColor(s.surface) },
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
        { "scroll_w", QString::number(touch ? 14 : 8) },
        { "scroll_r", QString::number(touch ? 5 : 2) },
        { "scroll", qssColor(s.scroll) },
        { "track", qssColor(s.track) },
        { "pad_s", QString::number(touch ? 8 : 4) },
        { "ctl_h", QString::number(touch ? TouchControlHeight - 2 * 9 : 22) },
        { "radius_l", QString::number(8) },
        { "radius", QString::number(touch ? 6 : 4) },
        { "menu_pad", QString::number(touch ? 11 : 5) },
        { "menu_radius", QString::number(menu_radius) },
        { "menubar_pad_x", QString::number(touch ? 16 : 10) },
        { "ind_round", QString::number((touch ? TouchIndicatorSize : 18) / 2) },
        { "ind_r", QString::number(4) },
        { "ind", QString::number(touch ? TouchIndicatorSize : 18) },
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
    set_all(QPalette::HighlightedText, s.on_accent);
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

void Theme::drawItemBackground(QPainter& painter, QStyleOptionViewItem const& option)
{
    auto const& s = scheme();
    auto const selected = option.state.testFlag(QStyle::State_Selected);
    auto const hovered = option.state.testFlag(QStyle::State_MouseOver);
    auto const focused = option.state.testFlag(QStyle::State_HasFocus);
    auto const active = option.state.testFlag(QStyle::State_Active);

    // Windows 11 list item: inset rounded fill, accent pill marks the selection
    auto const rect = QRectF{ option.rect }.adjusted(4.5, 2.5, -4.5, -2.5);
    auto constexpr Radius = 4.0;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    auto fill = QColor{ Qt::transparent };
    if (selected)
    {
        fill = s.accent_soft;
        if (!active)
        {
            fill.setAlpha(fill.alpha() * 2 / 3);
        }
    }
    else if (hovered)
    {
        fill = s.hover;
    }

    if (fill.alpha() > 0)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(rect, Radius, Radius);
    }

    if (selected)
    {
        auto const pill_height = std::min(16.0, rect.height() - 12.0);
        auto const pill = QRectF{ rect.left(), rect.center().y() - pill_height / 2, 3.0, pill_height };
        painter.setBrush(s.accent);
        painter.drawRoundedRect(pill, 1.5, 1.5);
    }

    // the selection pill already marks the usual case of a selected current item
    if (focused && !selected)
    {
        painter.setPen(QPen{ s.text, 1.0 });
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect, Radius, Radius);
    }

    painter.restore();
}

QBrush Theme::progressBrush(Bar bar)
{
    auto const& s = scheme();

    switch (bar)
    {
    case Bar::Downloading:
        return s.download;

    case Bar::Seeding:
        return s.seed;

    default:
        return s.idle;
    }
}

QColor Theme::progressTrack()
{
    return scheme().track;
}

QColor Theme::errorColor()
{
    return scheme().error;
}
