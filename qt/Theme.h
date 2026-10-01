// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <vector>

#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QString>

class QPainter;
class QStyleOptionViewItem;

// Application-wide look and feel.
//
// A theme id is one of:
// - "native": the platform's default widget style and palette
// - "modern": the Modern theme, light or dark following the OS setting
// - "modern_light", "modern_dark": the Modern theme with a fixed scheme
// - "style:<key>": any QStyleFactory style with its standard palette
//
// Touch mode enlarges controls, scroll bars and list rows in every theme.
// Kinetic touch scrolling and press-and-hold context menus are always on.
class Theme
{
    Q_DECLARE_TR_FUNCTIONS(Theme)

public:
    struct Choice
    {
        QString id;
        QString label;
    };

    enum class Bar
    {
        Downloading,
        Seeding,
        Idle
    };

    [[nodiscard]] static std::vector<Choice> choices();

    // Call once QApplication exists; later calls switch the theme live.
    static void apply(QString const& id, bool touch_mode);

    [[nodiscard]] static bool isModern() noexcept;
    [[nodiscard]] static bool isDark() noexcept;
    [[nodiscard]] static bool isTouch() noexcept;

    // Bumped on every apply() so that cached metrics can be invalidated.
    [[nodiscard]] static int generation() noexcept;

    // Modern-theme painting helpers for custom delegates.
    static void drawItemBackground(QPainter& painter, QStyleOptionViewItem const& option);
    [[nodiscard]] static QBrush progressBrush(Bar bar);
    [[nodiscard]] static QColor progressTrack();
    [[nodiscard]] static QColor errorColor();
};
