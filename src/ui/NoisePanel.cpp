#include "ui/NoisePanel.h"

#include "Application.h"
#include "D3D11Renderer.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <utility>

namespace SP {

namespace {

// Scale, as the outgoing panel offered it: 1.0 to 32.0 shown to one decimal. QSlider is
// integral, so the slider runs in tenths and the readout divides back down.
constexpr int kScaleTicksPerUnit = 10;
constexpr int kScaleMinTicks     = 1 * kScaleTicksPerUnit;
constexpr int kScaleMaxTicks     = 32 * kScaleTicksPerUnit;
constexpr int kScalePageTicks    = 10;   // one whole unit per PageUp

// The three sizes the outgoing panel offered, in the order it offered them.
constexpr int kSizes[] = { 256, 512, 1024 };
constexpr int kSizeCount = static_cast<int>(std::size(kSizes));
constexpr int kSizeDefaultIndex = 1;     // 512, matching NoiseSettings

constexpr int kLabelColumn = 56;         // "Scale" / "Size", on one vertical edge
constexpr int kValueColumn = 44;         // the readout beside the slider

constexpr int kTileMinSide = 96;
constexpr int kTileCaptionGap = 6;
constexpr int kTileRadius = Theme::kRadiusControl;

int TicksFromScale(float scale)
{
    const int ticks = static_cast<int>(std::lround(scale * kScaleTicksPerUnit));
    return std::clamp(ticks, kScaleMinTicks, kScaleMaxTicks);
}

float ScaleFromTicks(int ticks)
{
    return static_cast<float>(ticks) / kScaleTicksPerUnit;
}

int IndexOfSize(int size)
{
    for (int i = 0; i < kSizeCount; ++i) {
        if (kSizes[i] == size) return i;
    }
    return kSizeDefaultIndex;
}

// Read the two channels back off the texture that is actually bound at t1, rather than
// re-running Perlin and Voronoi on the CPU: a second implementation of the generator is a
// second thing to keep in step, and the whole value of a preview is that it cannot lie about
// what the shaders are sampling. The texture is IMMUTABLE and so has no CPU access of its
// own; a STAGING copy is the supported route, and it costs one GPU sync on a path that only
// runs when the texture has just been rebuilt.
bool ReadBackNoise(Application& app, QImage& perlin, QImage& voronoi)
{
    ID3D11ShaderResourceView* srv = app.GetRenderer().GetNoiseSRV();
    ID3D11Device* device = app.GetRenderer().GetDevice();
    ID3D11DeviceContext* context = app.GetRenderer().GetContext();
    if (!srv || !device || !context) return false;

    ComPtr<ID3D11Resource> resource;
    srv->GetResource(&resource);
    if (!resource) return false;

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) return false;
    if (desc.Width == 0 || desc.Height == 0) return false;

    D3D11_TEXTURE2D_DESC staged = desc;
    staged.Usage          = D3D11_USAGE_STAGING;
    staged.BindFlags      = 0;
    staged.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staged.MiscFlags      = 0;

    ComPtr<ID3D11Texture2D> readback;
    if (FAILED(device->CreateTexture2D(&staged, nullptr, &readback))) return false;

    context->CopyResource(readback.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    const int width  = static_cast<int>(desc.Width);
    const int height = static_cast<int>(desc.Height);

    // One byte per texel per channel: each channel is a scalar field and Grayscale8 is what
    // a shader reading .r or .g actually gets.
    QImage r(width, height, QImage::Format_Grayscale8);
    QImage g(width, height, QImage::Format_Grayscale8);

    const auto* base = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = base + static_cast<size_t>(y) * mapped.RowPitch;
        uint8_t* rLine = r.scanLine(y);
        uint8_t* gLine = g.scanLine(y);
        for (int x = 0; x < width; ++x) {
            rLine[x] = src[x * 4 + 0];
            gLine[x] = src[x * 4 + 1];
        }
    }

    context->Unmap(readback.Get(), 0);

    perlin = std::move(r);
    voronoi = std::move(g);
    return true;
}

}  // namespace

// =========================================================================================
// NoiseTile
// =========================================================================================

NoiseTile::NoiseTile(const QString& channel, const QString& field, QWidget* parent)
    : QWidget(parent)
    , m_channel(channel)
    , m_field(field)
{
    setObjectName(QStringLiteral("NoiseTile"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(tr("The %1 channel of the texture bound at t1, read back from the GPU.")
                   .arg(channel));
}

void NoiseTile::SetImage(const QImage& image)
{
    m_source = image;
    Rescale();
    update();
}

QSize NoiseTile::minimumSizeHint() const
{
    const int caption = QFontMetrics(font()).height() + kTileCaptionGap;
    return QSize(kTileMinSide, kTileMinSide + caption);
}

QSize NoiseTile::sizeHint() const
{
    return minimumSizeHint();
}

QRect NoiseTile::ImageRect() const
{
    const int caption = QFontMetrics(font()).height() + kTileCaptionGap;
    QRect area = rect().adjusted(0, 0, 0, -caption);
    if (area.width() <= 0 || area.height() <= 0) return QRect();

    // The texture is square, so the preview is too: a stretched preview would misreport the
    // frequency in one axis, which is the one number this panel exists to set.
    const int side = std::min(area.width(), area.height());
    return QRect(area.x() + (area.width() - side) / 2,
                 area.y() + (area.height() - side) / 2,
                 side, side);
}

void NoiseTile::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    Rescale();
}

void NoiseTile::Rescale()
{
    const QRect area = ImageRect();
    if (m_source.isNull() || area.isEmpty()) {
        m_scaled = QPixmap();
        return;
    }
    // Smooth, because a 1024px field shown at 120px without filtering is aliasing rather
    // than noise, and the difference between two scales would stop being readable.
    m_scaled = QPixmap::fromImage(
        m_source.scaled(area.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

void NoiseTile::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect area = ImageRect();
    if (!area.isEmpty()) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(area), kTileRadius, kTileRadius);

        if (m_scaled.isNull()) {
            // Nothing bound yet: a recessed empty field rather than a black square that
            // reads as a generated texture of solid zero.
            painter.fillPath(clip, Theme::kInputFill);
            painter.setPen(Theme::kTextSecondary);
            painter.drawText(area, Qt::AlignCenter, tr("not generated"));
        } else {
            painter.save();
            painter.setClipPath(clip);
            painter.drawPixmap(area, m_scaled);
            painter.restore();
        }

        painter.setPen(QPen(Theme::kPanelBorder, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5),
                                kTileRadius, kTileRadius);
    }

    // Caption: the channel in the region hue, the field it carries beside it. The hue is
    // present at rest, so the tile identifies itself without a pointer on it.
    const QFontMetrics metrics(font());
    const QRect caption(rect().x(), rect().bottom() - metrics.height() + 1,
                        rect().width(), metrics.height());

    QFont bold = font();
    bold.setBold(true);
    const int channelWidth = QFontMetrics(bold).horizontalAdvance(m_channel + QLatin1Char(' '));

    painter.setFont(bold);
    painter.setPen(Theme::kRegionNoise);
    painter.drawText(caption, Qt::AlignLeft | Qt::AlignVCenter, m_channel);

    painter.setFont(font());
    painter.setPen(Theme::kTextSecondary);
    painter.drawText(caption.adjusted(channelWidth, 0, 0, 0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(m_field, Qt::ElideRight,
                                        std::max(caption.width() - channelWidth, 8)));
}

// =========================================================================================
// NoisePanel
// =========================================================================================

NoisePanel::NoisePanel(Application& app, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
{
    setObjectName(QStringLiteral("NoisePanel"));

    // The application stylesheet paints every QWidget with the canvas, which would lay an
    // opaque black slab over this dock's glass. Named containers opt out; the controls keep
    // their own fills.
    setStyleSheet(QStringLiteral(
        "QWidget#NoisePanel, QWidget#NoiseRow, QWidget#NoisePreview,"
        "QWidget#NoiseTile { background: transparent; }"
        "QLabel#NoiseScaleValue { font-family: %1; }")
        .arg(QString::fromLatin1(Theme::kFontMono)));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    auto* blurb = new QLabel(
        tr("A tiling texture bound to every shader at t1, through a wrapping sampler at s1."),
        this);
    blurb->setObjectName(QStringLiteral("Caption"));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);

    BuildPreview(layout);
    BuildControls(layout);
    layout->addStretch(1);

    SyncReadout();

    // The renderer generates the texture at startup, but this panel can be built either side
    // of that, and a failed generate leaves nothing to read. Asking once the event loop is
    // running covers the ordering; the empty tiles cover the failure.
    QTimer::singleShot(0, this, [this] { RefreshPreview(); });
}

void NoisePanel::BuildPreview(QVBoxLayout* layout)
{
    auto* preview = new QWidget(this);
    preview->setObjectName(QStringLiteral("NoisePreview"));
    auto* row = new QHBoxLayout(preview);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Theme::kSpaceUnit);

    // Two tiles rather than one composite: R and G carry unrelated fields, and red-over-green
    // makes a colour neither channel has and a shape neither channel is.
    m_perlin = new NoiseTile(QStringLiteral("R"), tr("Perlin"), preview);
    m_voronoi = new NoiseTile(QStringLiteral("G"), tr("Voronoi F1"), preview);
    row->addWidget(m_perlin, 1);
    row->addWidget(m_voronoi, 1);

    layout->addWidget(preview, 1);
}

void NoisePanel::BuildControls(QVBoxLayout* layout)
{
    const NoiseSettings& noise = m_app.GetConfig().noise;

    auto* heading = new QLabel(tr("Generation"), this);
    heading->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(heading);

    // ---- scale -----------------------------------------------------------------------
    auto* scaleRow = new QWidget(this);
    scaleRow->setObjectName(QStringLiteral("NoiseRow"));
    auto* scaleLine = new QHBoxLayout(scaleRow);
    scaleLine->setContentsMargins(0, 0, 0, 0);
    scaleLine->setSpacing(Theme::kSpaceUnit);

    auto* scaleLabel = new QLabel(tr("Scale"), scaleRow);
    scaleLabel->setMinimumWidth(kLabelColumn);
    scaleLine->addWidget(scaleLabel);

    m_scale = new QSlider(Qt::Horizontal, scaleRow);
    m_scale->setRange(kScaleMinTicks, kScaleMaxTicks);
    m_scale->setPageStep(kScalePageTicks);
    m_scale->setValue(TicksFromScale(noise.scale));
    m_scale->setToolTip(tr("Frequency of noise in the texture (higher = more repetitions).\n"
                           "The texture is rebuilt when the handle is released."));
    scaleLine->addWidget(m_scale, 1);

    m_scaleValue = new QLabel(scaleRow);
    m_scaleValue->setObjectName(QStringLiteral("NoiseScaleValue"));
    m_scaleValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_scaleValue->setMinimumWidth(kValueColumn);
    scaleLine->addWidget(m_scaleValue);

    // The readout follows the handle so the drag is legible; the rebuild waits for the
    // release, because each one destroys and recreates up to a megapixel of IMMUTABLE
    // texture. A keyboard or wheel change is not a drag and commits at once.
    connect(m_scale, &QSlider::valueChanged, this, [this](int ticks) {
        m_scaleValue->setText(QString::number(ScaleFromTicks(ticks), 'f', 1));
        if (!m_scale->isSliderDown()) Regenerate();
    });
    connect(m_scale, &QSlider::sliderReleased, this, &NoisePanel::Regenerate);

    m_scaleValue->setText(QString::number(ScaleFromTicks(m_scale->value()), 'f', 1));
    layout->addWidget(scaleRow);

    // ---- texture size ----------------------------------------------------------------
    auto* sizeRow = new QWidget(this);
    sizeRow->setObjectName(QStringLiteral("NoiseRow"));
    auto* sizeLine = new QHBoxLayout(sizeRow);
    sizeLine->setContentsMargins(0, 0, 0, 0);
    sizeLine->setSpacing(Theme::kSpaceUnit);

    auto* sizeLabel = new QLabel(tr("Size"), sizeRow);
    sizeLabel->setMinimumWidth(kLabelColumn);
    sizeLine->addWidget(sizeLabel);

    m_size = new QComboBox(sizeRow);
    for (const int side : kSizes) {
        m_size->addItem(tr("%1 x %1").arg(side), side);
    }
    m_size->setCurrentIndex(IndexOfSize(noise.textureSize));
    m_size->setToolTip(tr("Texture dimensions. Larger holds finer detail and costs more "
                          "memory and more time to generate."));
    connect(m_size, &QComboBox::currentIndexChanged, this, [this](int) { Regenerate(); });
    sizeLine->addWidget(m_size, 1);
    layout->addWidget(sizeRow);

    // ---- regenerate --------------------------------------------------------------------
    m_regenerate = new QPushButton(tr("Regenerate"), this);
    m_regenerate->setCursor(Qt::PointingHandCursor);
    m_regenerate->setToolTip(tr("Rebuild the texture from the settings above and refresh "
                                "the preview. Every shader sampling t1 picks it up on the "
                                "next frame."));
    connect(m_regenerate, &QPushButton::clicked, this, &NoisePanel::Regenerate);
    layout->addWidget(m_regenerate);

    m_bound = new QLabel(this);
    m_bound->setObjectName(QStringLiteral("Caption"));
    m_bound->setWordWrap(true);
    layout->addWidget(m_bound);
}

void NoisePanel::Regenerate()
{
    NoiseSettings& noise = m_app.GetConfig().noise;
    noise.scale = ScaleFromTicks(m_scale->value());
    noise.textureSize = m_size->currentData().toInt();

    // RegenerateNoise, never UpdateNoiseTexture: it reads AppConfig::noise, calls the
    // renderer and persists the result in one step.
    m_app.RegenerateNoise();

    RefreshPreview();
    SyncReadout();
}

void NoisePanel::RefreshPreview()
{
    QImage perlin;
    QImage voronoi;
    if (!ReadBackNoise(m_app, perlin, voronoi)) {
        m_perlin->SetImage(QImage());
        m_voronoi->SetImage(QImage());
        return;
    }
    m_perlin->SetImage(perlin);
    m_voronoi->SetImage(voronoi);
}

void NoisePanel::SyncReadout()
{
    const NoiseSettings& noise = m_app.GetConfig().noise;
    m_bound->setText(tr("Bound: %1 x %1 at scale %2.")
                         .arg(noise.textureSize)
                         .arg(QString::number(noise.scale, 'f', 1)));
}

}  // namespace SP
