#include "archmage/render/ViewTransform.h"

#include <algorithm>
#include <cmath>

namespace archmage::render {
namespace {

double clampScale(double s) {
    if (!std::isfinite(s) || s <= 0.0) {
        return 1.0;
    }
    return std::clamp(s, ViewTransform::kMinScale, ViewTransform::kMaxScale);
}

double sanitizeRatio(double dpr) { return std::isfinite(dpr) && dpr > 0.0 ? dpr : 1.0; }

} // namespace

ViewTransform::ViewTransform(double scale, DPoint pan, double devicePixelRatio)
    : scale_(clampScale(scale)), pan_(pan), dpr_(sanitizeRatio(devicePixelRatio)) {}

DRect ViewTransform::documentToDevice(const DRect &r) const {
    const DPoint a = documentToDevice(DPoint{r.x, r.y});
    return {a.x, a.y, r.width * scale_, r.height * scale_};
}

DRect ViewTransform::deviceToDocument(const DRect &r) const {
    const DPoint a = deviceToDocument(DPoint{r.x, r.y});
    return {a.x, a.y, r.width / scale_, r.height / scale_};
}

DRect ViewTransform::visibleDocumentRect(DSize deviceSize) const {
    return deviceToDocument(DRect{0.0, 0.0, deviceSize.width, deviceSize.height});
}

ViewTransform ViewTransform::zoomedAbout(double newScale, DPoint deviceAnchor) const {
    const double s = clampScale(newScale);
    const DPoint documentPoint = deviceToDocument(deviceAnchor);
    return {s, deviceAnchor - documentPoint * s, dpr_};
}

ViewTransform ViewTransform::panned(DPoint deviceDelta) const { return {scale_, pan_ + deviceDelta, dpr_}; }

ViewTransform ViewTransform::withDevicePixelRatio(double newDpr, DSize logicalViewportSize) const {
    const double dpr = sanitizeRatio(newDpr);
    const DPoint oldCenter{logicalViewportSize.width * dpr_ * 0.5, logicalViewportSize.height * dpr_ * 0.5};
    const DPoint documentCenter = deviceToDocument(oldCenter);
    const DPoint newCenter{logicalViewportSize.width * dpr * 0.5, logicalViewportSize.height * dpr * 0.5};
    return {scale_, newCenter - documentCenter * scale_, dpr};
}

ViewTransform ViewTransform::fit(const DRect &document, DSize deviceViewport, double devicePixelRatio,
                                 double marginDevice) {
    const double availableW = deviceViewport.width - 2.0 * marginDevice;
    const double availableH = deviceViewport.height - 2.0 * marginDevice;
    double s = 1.0;
    if (!document.empty() && availableW > 0.0 && availableH > 0.0) {
        s = std::min({1.0, availableW / document.width, availableH / document.height});
    }
    s = clampScale(s);
    const DPoint viewportCenter{deviceViewport.width * 0.5, deviceViewport.height * 0.5};
    return {s, viewportCenter - document.center() * s, devicePixelRatio};
}

ViewTransform ViewTransform::actualPixels(DPoint documentPoint, DSize deviceViewport,
                                          double devicePixelRatio) {
    const DPoint viewportCenter{deviceViewport.width * 0.5, deviceViewport.height * 0.5};
    return {1.0, viewportCenter - documentPoint, devicePixelRatio};
}

} // namespace archmage::render
