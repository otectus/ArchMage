#include "archmage/ui/canvas/RhiCanvasSurface.h"

#include <QEvent>
#include <QFile>
#include <QRhiWidget>
#include <rhi/qrhi.h>

#include <memory>

namespace archmage::ui::canvas {
namespace {

QShader loadShader(const QString &resource) {
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("RhiCanvasSurface: missing shader resource %s", qPrintable(resource));
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

QRhiWidget::Api apiFromEnvironment() {
    const QByteArray value = qgetenv("ARCHMAGE_RHI_API").toLower();
    if (value == "opengl" || value == "gl") {
        return QRhiWidget::Api::OpenGL;
    }
    return QRhiWidget::Api::Vulkan;
}

} // namespace

class RhiCanvasSurface::Widget final : public QRhiWidget {
public:
    explicit Widget(RhiCanvasSurface *surface) : surface_(surface) {
        setObjectName(QStringLiteral("archmageCanvas"));
        setApi(apiFromEnvironment());
        setAttribute(Qt::WA_TabletTracking);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(64, 64);
    }

    void detach() { surface_ = nullptr; }

    [[nodiscard]] QString describe() const {
        if (QRhi *r = rhi()) {
            return QStringLiteral("QRhiWidget, %1 backend on %2 (driver %3)")
                .arg(QString::fromLatin1(r->backendName()), QString::fromUtf8(r->driverInfo().deviceName),
                     QString::number(r->driverInfo().deviceId, 16));
        }
        return QStringLiteral("QRhiWidget (%1 requested, not initialized)")
            .arg(api() == Api::Vulkan ? QStringLiteral("Vulkan") : QStringLiteral("OpenGL"));
    }

protected:
    bool event(QEvent *event) override {
        if (surface_ != nullptr && CanvasSurface::isCanvasInputEvent(event) &&
            surface_->deliverInput(event)) {
            return true;
        }
        return QRhiWidget::event(event);
    }

    void initialize(QRhiCommandBuffer * /*cb*/) override {
        QRhi *r = rhi();
        if (r != rhi_) {
            releaseResources();
            rhi_ = r;
        }
        if (!pipeline_) {
            sampler_.reset(r->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            sampler_->create();
            params_.reset(r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::UniformBuffer, 16));
            params_->create();
            paramsUploaded_ = false;
            ensureTexture(renderTarget()->pixelSize());
            srb_.reset(r->newShaderResourceBindings());
            setBindings();
            pipeline_.reset(r->newGraphicsPipeline());
            pipeline_->setShaderStages(
                {{QRhiShaderStage::Vertex,
                  loadShader(QStringLiteral(":/archmage/shaders/present_rhi.vert.qsb"))},
                 {QRhiShaderStage::Fragment,
                  loadShader(QStringLiteral(":/archmage/shaders/present.frag.qsb"))}});
            pipeline_->setVertexInputLayout({});
            pipeline_->setShaderResourceBindings(srb_.get());
            pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
            if (!pipeline_->create()) {
                qWarning("RhiCanvasSurface: pipeline creation failed");
            }
        } else {
            ensureTexture(renderTarget()->pixelSize());
        }
    }

    void render(QRhiCommandBuffer *cb) override {
        if (surface_ == nullptr || !pipeline_) {
            return;
        }
        const QSize pixelSize = renderTarget()->pixelSize();
        ensureTexture(pixelSize);
        QRhiResourceUpdateBatch *updates = rhi()->nextResourceUpdateBatch();
        if (!paramsUploaded_) {
            const float values[4] = {rhi()->isYUpInNDC() ? -1.0f : 1.0f, 0.0f, 0.0f, 0.0f};
            updates->uploadStaticBuffer(params_.get(), values);
            paramsUploaded_ = true;
        }
        updates->uploadTexture(texture_.get(), surface_->produceFrame(pixelSize, devicePixelRatioF()));
        cb->beginPass(renderTarget(), QColor(Qt::black), {1.0f, 0}, updates);
        cb->setGraphicsPipeline(pipeline_.get());
        cb->setViewport(
            {0.0f, 0.0f, static_cast<float>(pixelSize.width()), static_cast<float>(pixelSize.height())});
        cb->setShaderResources();
        cb->draw(3);
        cb->endPass();
    }

    void releaseResources() override {
        pipeline_.reset();
        srb_.reset();
        texture_.reset();
        params_.reset();
        sampler_.reset();
    }

private:
    void ensureTexture(QSize pixelSize) {
        if (texture_ && texture_->pixelSize() == pixelSize) {
            return;
        }
        texture_.reset(rhi()->newTexture(QRhiTexture::RGBA8, pixelSize));
        texture_->create();
        if (srb_) {
            setBindings();
        }
    }

    void setBindings() {
        srb_->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                                    params_.get()),
                           QRhiShaderResourceBinding::sampledTexture(
                               1, QRhiShaderResourceBinding::FragmentStage, texture_.get(), sampler_.get())});
        srb_->create();
    }

    RhiCanvasSurface *surface_;
    QRhi *rhi_ = nullptr;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    std::unique_ptr<QRhiShaderResourceBindings> srb_;
    std::unique_ptr<QRhiTexture> texture_;
    std::unique_ptr<QRhiBuffer> params_;
    std::unique_ptr<QRhiSampler> sampler_;
    bool paramsUploaded_ = false;
};

RhiCanvasSurface::RhiCanvasSurface(QObject *parent) : CanvasSurface(parent) {
    auto *w = new Widget(this);
    widget_ = w;
    connect(w, &QRhiWidget::frameSubmitted, this, [this] { frameSubmitted(); });
    connect(w, &QRhiWidget::renderFailed, this,
            [this] { reportDeviceLost(QStringLiteral("QRhiWidget reported renderFailed")); });
}

RhiCanvasSurface::~RhiCanvasSurface() {
    if (auto *w = static_cast<Widget *>(widget_.data())) {
        w->detach();
        disconnect(w, nullptr, this, nullptr);
        if (w->parent() == nullptr) {
            delete w;
        }
    }
}

QWidget *RhiCanvasSurface::widget() { return widget_; }

QString RhiCanvasSurface::backendDescription() const {
    if (auto *w = static_cast<Widget *>(widget_.data())) {
        return w->describe();
    }
    return QStringLiteral("QRhiWidget (destroyed)");
}

void RhiCanvasSurface::requestFrame() {
    if (widget_ != nullptr) {
        widget_->update();
    }
}

} // namespace archmage::ui::canvas
