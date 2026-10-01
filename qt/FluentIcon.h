// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

// Bundled Fluent UI System Icons, used by the Modern theme.
// The monochrome glyphs are tinted when painted, so they follow theme switches
// and use the palette's disabled color for disabled actions.
namespace fluent
{

// `name` is a file in qt/icons-fluent without the .svg suffix
[[nodiscard]] QIcon icon(QString const& name, QPalette::ColorRole role = QPalette::WindowText);
[[nodiscard]] QIcon icon(QString const& name, QColor const& color);

// `off_name` for the unchecked state; the checked state is `on_name` in the accent color
[[nodiscard]] QIcon toggleIcon(QString const& off_name, QString const& on_name);

// muted glyph for a file of this MIME type, or a folder for multi-file torrents
[[nodiscard]] QIcon mimeTypeIcon(QString const& mime_type, bool multifile);

} // namespace fluent
