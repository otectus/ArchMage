#pragma once

#include <QColor>
#include <QObject>

namespace archmage::ui {

/// The foreground and background colors shared by every document in a window (plan S1-7.4, spec §16.1).
/// Colors are straight sRGB values in [0, 1]; Stage 1 documents are sRGB, so tools use them unconverted
/// (ADR-0010).
///
/// Thread safety: GUI thread only.
class ColorState final : public QObject {
    Q_OBJECT

public:
    explicit ColorState(QObject *parent = nullptr);

    [[nodiscard]] QColor foreground() const { return foreground_; }
    [[nodiscard]] QColor background() const { return background_; }
    void setForeground(const QColor &color);
    void setBackground(const QColor &color);
    /// X (App. B): exchanges the two colors.
    void swap();
    /// D (App. B): black foreground, white background.
    void reset();

signals:
    void changed();

private:
    QColor foreground_ = Qt::black;
    QColor background_ = Qt::white;
};

} // namespace archmage::ui
