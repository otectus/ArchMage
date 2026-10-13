#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace archmage::ui::theme {

/// The ArchMage violet (spec §2.3: a restrained accent).
[[nodiscard]] QColor accent();

/// Stage 1's one theme (ADR-0028): the Fusion style with a neutral dark palette, the violet accent for
/// selection and focus, and a small stylesheet. Call once, before the main window is created.
void apply(QApplication &app);

/// An original icon from resources/icons by name, e.g. "brush"; a null icon for unknown names.
[[nodiscard]] QIcon icon(const QString &name);

} // namespace archmage::ui::theme
