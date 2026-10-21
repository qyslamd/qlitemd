#pragma once

#include <QFrame>

class ExSpectrumWidget;
class SineWaveGenerator;
class QTimer;

/**
 * @brief ExSpectrumWidget 演示页（Qt 5 Gallery 使用模拟 PCM）。
 */
class PageSpectrum : public QFrame
{
    Q_OBJECT

public:
    explicit PageSpectrum(QWidget *parent = nullptr);
    ~PageSpectrum() override;

private slots:
    void feedSimulatedAudio();

private:
    ExSpectrumWidget *m_spectrum{nullptr};
    SineWaveGenerator *m_generator{nullptr};
    QTimer *m_feedTimer{nullptr};
};
