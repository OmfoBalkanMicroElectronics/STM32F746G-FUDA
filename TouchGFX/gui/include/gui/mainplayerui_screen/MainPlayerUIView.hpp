#ifndef MAINPLAYERUIVIEW_HPP
#define MAINPLAYERUIVIEW_HPP

#include <gui_generated/mainplayerui_screen/MainPlayerUIViewBase.hpp>
#include <gui/mainplayerui_screen/MainPlayerUIPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/Callback.hpp>

class MainPlayerUIView : public MainPlayerUIViewBase
{
public:
    MainPlayerUIView();
    virtual ~MainPlayerUIView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
    void updateMedia(const MediaSnapshot& snapshot);
protected:
    touchgfx::Container songNameClip;
    touchgfx::TextAreaWithOneWildcard dynamicSongName;
    touchgfx::TextAreaWithOneWildcard dynamicWholeDuration;
    static const uint16_t SONG_NAME_BUFFER_SIZE = (MEDIA_TRACK_NAME_SIZE * 2U) + 8U;
    touchgfx::Unicode::UnicodeChar songNameBuffer[SONG_NAME_BUFFER_SIZE];
    touchgfx::Unicode::UnicodeChar wholeDurationBuffer[10];
    touchgfx::Callback<MainPlayerUIView, const touchgfx::AbstractButton&> mediaButtonCallback;
    touchgfx::Callback<MainPlayerUIView, const touchgfx::Slider&, int> mediaSliderCallback;
    touchgfx::Callback<MainPlayerUIView, const touchgfx::Slider&, int> durationStartCallback;
    touchgfx::Callback<MainPlayerUIView, const touchgfx::Slider&, int> durationStopCallback;
    touchgfx::Box spectrumBackground;
    touchgfx::Box spectrumBars[MEDIA_SPECTRUM_BANDS];
    touchgfx::Box spectrumPeaks[MEDIA_SPECTRUM_BANDS];
    uint8_t spectrumTargets[MEDIA_SPECTRUM_BANDS];
    uint8_t spectrumLevels[MEDIA_SPECTRUM_BANDS];
    uint8_t spectrumPeakLevels[MEDIA_SPECTRUM_BANDS];
    uint8_t spectrumPeakHold[MEDIA_SPECTRUM_BANDS];
    uint8_t spectrumTick;
    uint32_t spectrumRevision;
    uint8_t currentSource;
    bool updatingUi;
    bool durationDragging;
    bool marqueeActive;
    int16_t marqueeX;
    uint16_t marqueeDelay;
    uint16_t marqueeTextWidth;
    uint16_t marqueeLoopWidth;
    char lastTitle[MEDIA_TRACK_NAME_SIZE];
    void mediaButtonHandler(const touchgfx::AbstractButton& source);
    void mediaSliderHandler(const touchgfx::Slider& source, int value);
    void durationStartHandler(const touchgfx::Slider& source, int value);
    void durationStopHandler(const touchgfx::Slider& source, int value);
    void updateSpectrum();
};

#endif // MAINPLAYERUIVIEW_HPP
