#pragma once

// NoisePanel.h — the body of the Noise Generator dock.
//
// The texture this panel generates is bound to *every* shader at t1, so a change made here
// is felt across the whole library at once. Two things follow from that:
//
//   The panel shows what is actually bound. The preview is read back off the GPU from the
//   very texture the sampler points at, not a second CPU simulation of the same function
//   that could quietly drift from it. Two tiles, because the two channels carry unrelated
//   fields (R is Perlin gradient noise, G is Voronoi F1 inverted) and a composited
//   red-over-green image makes neither of them readable.
//
//   Regenerating is not free. The texture is D3D11_USAGE_IMMUTABLE and is destroyed and
//   rebuilt in full on every call, up to 1024x1024 of CPU-side Perlin and Voronoi, so the
//   scale slider commits on release rather than on every tick of a drag. The readout moves
//   with the handle; the texture follows when the handle is let go.
//
// Application::RegenerateNoise is the only way in: it reads AppConfig::noise, calls the
// renderer and saves the config, so the panel never touches D3D11Renderer::UpdateNoiseTexture
// directly and never has to remember to persist.

#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QPaintEvent;
class QPushButton;
class QResizeEvent;
class QSlider;
class QVBoxLayout;
QT_END_NAMESPACE

namespace SP {

class Application;

// One channel of the noise texture, drawn as the scalar a shader actually samples: a
// greyscale field, because `noiseTexture.Sample(...).r` is one number per texel and tinting
// it would misreport its value. The caption underneath names the channel and the field, so
// the tile answers "which register is this" without a legend elsewhere in the panel.
class NoiseTile : public QWidget {
    Q_OBJECT
public:
    NoiseTile(const QString& channel, const QString& field, QWidget* parent = nullptr);

    // A null image is the "nothing generated yet" state and is drawn as such.
    void SetImage(const QImage& image);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect ImageRect() const;
    void Rescale();

    QString m_channel;
    QString m_field;
    QImage m_source;
    QPixmap m_scaled;      // m_source at the current image rect, rebuilt only on resize
};

class NoisePanel : public QWidget {
    Q_OBJECT
public:
    explicit NoisePanel(Application& app, QWidget* parent = nullptr);

private:
    void BuildPreview(QVBoxLayout* layout);
    void BuildControls(QVBoxLayout* layout);

    // Commit the controls to AppConfig::noise, rebuild the texture, refresh the preview.
    void Regenerate();

    // Pull the two channels off the bound texture. Silent no-op when there is nothing bound
    // yet (the renderer generates at startup, but this panel can outlive a failed generate).
    void RefreshPreview();

    void SyncReadout();

    Application& m_app;

    NoiseTile* m_perlin = nullptr;
    NoiseTile* m_voronoi = nullptr;

    QSlider* m_scale = nullptr;
    QLabel* m_scaleValue = nullptr;
    QComboBox* m_size = nullptr;
    QPushButton* m_regenerate = nullptr;
    QLabel* m_bound = nullptr;      // what the preview above is, in words
};

}  // namespace SP
