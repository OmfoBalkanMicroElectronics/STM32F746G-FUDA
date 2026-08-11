#include <gui/mainplayerui_screen/MainPlayerUIView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Color.hpp>
#include <cstring>

#define MAIN_TITLE_MARQUEE_X     48
#define MAIN_TITLE_MARQUEE_WIDTH 384
#define MARQUEE_START_PAUSE      75U
#define MARQUEE_GAP_SPACES       6U
#define SPECTRUM_X               80
#define SPECTRUM_Y               47
#define SPECTRUM_WIDTH           326
#define SPECTRUM_HEIGHT          89
#define SPECTRUM_BAR_WIDTH       10
#define SPECTRUM_BAR_GAP         3
#define SPECTRUM_MAX_HEIGHT      79
#define SPECTRUM_BOTTOM          132
#define SPECTRUM_PEAK_HOLD       18U

static const uint8_t spectrumColors[MEDIA_SPECTRUM_BANDS][3] =
{
    {0, 170, 255}, {0, 195, 255}, {0, 220, 255}, {0, 240, 235},
    {0, 245, 200}, {0, 245, 160}, {0, 240, 115}, {20, 235, 70},
    {70, 230, 35}, {125, 225, 20}, {180, 220, 10}, {225, 215, 0},
    {255, 195, 0}, {255, 165, 0}, {255, 130, 0}, {255, 90, 15},
    {255, 50, 45}, {255, 25, 85}, {250, 15, 130}, {235, 15, 175},
    {210, 25, 220}, {175, 45, 245}, {135, 70, 255}, {100, 100, 255}
};

MainPlayerUIView::MainPlayerUIView() :
    mediaButtonCallback(this, &MainPlayerUIView::mediaButtonHandler),
    mediaSliderCallback(this, &MainPlayerUIView::mediaSliderHandler),
    durationStartCallback(this, &MainPlayerUIView::durationStartHandler),
    durationStopCallback(this, &MainPlayerUIView::durationStopHandler),
    spectrumTick(0U),
    spectrumRevision(0U),
    currentSource(MEDIA_SOURCE_WAV),
    updatingUi(false),
    durationDragging(false),
    marqueeActive(false),
    marqueeX(0),
    marqueeDelay(MARQUEE_START_PAUSE),
    marqueeTextWidth(0),
    marqueeLoopWidth(0)
{
    lastTitle[0] = '\0';
    std::memset(spectrumTargets, 0, sizeof(spectrumTargets));
    std::memset(spectrumLevels, 0, sizeof(spectrumLevels));
    std::memset(spectrumPeakLevels, 0, sizeof(spectrumPeakLevels));
    std::memset(spectrumPeakHold, 0, sizeof(spectrumPeakHold));
}

void MainPlayerUIView::setupScreen()
{
    MainPlayerUIViewBase::setupScreen();
    songImage.setVisible(false);
    spectrumBackground.setPosition(SPECTRUM_X, SPECTRUM_Y, SPECTRUM_WIDTH, SPECTRUM_HEIGHT);
    spectrumBackground.setColor(touchgfx::Color::getColorFromRGB(4, 7, 20));
    add(spectrumBackground);
    for (uint8_t i = 0U; i < MEDIA_SPECTRUM_BANDS; i++)
    {
        const int16_t x = SPECTRUM_X + 8 + i * (SPECTRUM_BAR_WIDTH + SPECTRUM_BAR_GAP);
        spectrumBars[i].setPosition(x, SPECTRUM_BOTTOM, SPECTRUM_BAR_WIDTH, 0);
        spectrumBars[i].setColor(touchgfx::Color::getColorFromRGB(spectrumColors[i][0], spectrumColors[i][1], spectrumColors[i][2]));
        add(spectrumBars[i]);
        spectrumPeaks[i].setPosition(x, SPECTRUM_BOTTOM - 2, SPECTRUM_BAR_WIDTH, 2);
        spectrumPeaks[i].setColor(touchgfx::Color::getColorFromRGB(245, 250, 255));
        spectrumPeaks[i].setVisible(false);
        add(spectrumPeaks[i]);
    }
    songTextName.setVisible(false);
    wholeSongDur.setVisible(false);
    // A screen-centred, wider clipping window gives short titles a balanced
    // position and lets long titles remain visible for longer while scrolling.
    songNameClip.setPosition(MAIN_TITLE_MARQUEE_X, songTextName.getY(), MAIN_TITLE_MARQUEE_WIDTH, songTextName.getHeight());
    dynamicSongName.setPosition(0, 0, MAIN_TITLE_MARQUEE_WIDTH, songTextName.getHeight());
    dynamicSongName.setColor(songTextName.getColor());
    dynamicSongName.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    dynamicSongName.setWildcard(songNameBuffer);
    touchgfx::Unicode::snprintf(songNameBuffer, SONG_NAME_BUFFER_SIZE, "No track selected");
    songNameClip.add(dynamicSongName);
    add(songNameClip);
    dynamicWholeDuration.setPosition(wholeSongDur.getX(), wholeSongDur.getY(), wholeSongDur.getWidth(), wholeSongDur.getHeight());
    dynamicWholeDuration.setColor(wholeSongDur.getColor());
    dynamicWholeDuration.setTypedText(touchgfx::TypedText(T___SINGLEUSE_JWNJ));
    dynamicWholeDuration.setWildcard(wholeDurationBuffer);
    touchgfx::Unicode::snprintf(wholeDurationBuffer, 10, "0:00");
    add(dynamicWholeDuration);
    prev.setAction(mediaButtonCallback);
    play_stop.setAction(mediaButtonCallback);
    next.setAction(mediaButtonCallback);
    sourceSelect.setAction(mediaButtonCallback);
    // Replace the Designer's fixed hardware-EQ navigation with source-aware
    // routing: local WAV/MP3 uses the software EQ; live inputs use WM8994 EQ.
    equalizerButton.setAction(mediaButtonCallback);
    // Speed/pitch processing is intentionally local-file only.
    speedPitchButton.setAction(mediaButtonCallback);
    durationBar.setStartValueCallback(durationStartCallback);
    durationBar.setStopValueCallback(durationStopCallback);
    durationBar.setNewValueCallback(mediaSliderCallback);
    volumeSlider.setNewValueCallback(mediaSliderCallback);
}

void MainPlayerUIView::tearDownScreen()
{
    MainPlayerUIViewBase::tearDownScreen();
}

void MainPlayerUIView::handleTickEvent()
{
    if (marqueeActive)
    {
        if (marqueeDelay != 0U)
        {
            marqueeDelay--;
        }
        else
        {
            marqueeX--;
            if ((uint16_t)(-marqueeX) >= marqueeLoopWidth)
            {
                // The second copy is now exactly where the first copy started. Reset
                // invisibly and pause briefly with the title beginning on screen.
                marqueeX = 0;
                marqueeDelay = MARQUEE_START_PAUSE;
            }
            dynamicSongName.moveTo(marqueeX, 0);
            songNameClip.invalidate();
        }
    }

    updateSpectrum();
}

void MainPlayerUIView::updateSpectrum()
{
    spectrumTick++;
    if ((spectrumTick & 1U) == 0U)
    {
        uint8_t latest[MEDIA_SPECTRUM_BANDS];
        const uint32_t revision = presenter->spectrum(latest);
        if (revision != spectrumRevision)
        {
            std::memcpy(spectrumTargets, latest, sizeof(spectrumTargets));
            spectrumRevision = revision;
        }
    }

    for (uint8_t i = 0U; i < MEDIA_SPECTRUM_BANDS; i++)
    {
        uint8_t level = spectrumLevels[i];
        const uint8_t target = spectrumTargets[i];
        if (target > level)
        {
            uint8_t rise = (uint8_t)((target - level + 2U) / 3U);
            if (rise < 2U) rise = 2U;
            level = (uint8_t)((level + rise) > target ? target : level + rise);
        }
        else if (level > target)
        {
            uint8_t fall = (uint8_t)((level - target + 7U) / 8U);
            if (fall == 0U) fall = 1U;
            level = level > fall ? (uint8_t)(level - fall) : 0U;
        }
        spectrumLevels[i] = level;

        if (level >= spectrumPeakLevels[i])
        {
            spectrumPeakLevels[i] = level;
            spectrumPeakHold[i] = SPECTRUM_PEAK_HOLD;
        }
        else if (spectrumPeakHold[i] != 0U)
        {
            spectrumPeakHold[i]--;
        }
        else if (spectrumPeakLevels[i] != 0U)
        {
            spectrumPeakLevels[i]--;
        }

        const int16_t barHeight = (int16_t)(((uint32_t)level * SPECTRUM_MAX_HEIGHT) / 100U);
        const int16_t peakHeight = (int16_t)(((uint32_t)spectrumPeakLevels[i] * SPECTRUM_MAX_HEIGHT) / 100U);
        spectrumBars[i].invalidate();
        spectrumBars[i].setPosition(spectrumBars[i].getX(), SPECTRUM_BOTTOM - barHeight,
                                    SPECTRUM_BAR_WIDTH, barHeight);
        spectrumBars[i].invalidate();
        spectrumPeaks[i].invalidate();
        spectrumPeaks[i].setVisible(spectrumPeakLevels[i] != 0U);
        spectrumPeaks[i].moveTo(spectrumPeaks[i].getX(), SPECTRUM_BOTTOM - peakHeight - 2);
        spectrumPeaks[i].invalidate();
    }
}

void MainPlayerUIView::mediaButtonHandler(const touchgfx::AbstractButton& source)
{
    if (&source == &prev) presenter->previous();
    else if (&source == &play_stop) presenter->playPause();
    else if (&source == &next) presenter->next();
    else if (&source == &sourceSelect) presenter->toggleSource();
    else if (&source == &equalizerButton)
    {
        if (currentSource == MEDIA_SOURCE_WAV)
            application().gotoSWEqualizerUIScreenCoverTransitionWest();
        else
            application().gotoEqualizerUIScreenCoverTransitionWest();
    }
    else if (&source == &speedPitchButton && currentSource == MEDIA_SOURCE_WAV)
    {
        application().gotospeedPitchUIScreenCoverTransitionEast();
    }
}

void MainPlayerUIView::mediaSliderHandler(const touchgfx::Slider& source, int value)
{
    if (updatingUi) return;
    if (&source == &volumeSlider)
    {
        touchgfx::Unicode::snprintf(volume_percentageBuffer, VOLUME_PERCENTAGE_SIZE, "%d%%", value);
        volume_percentage.invalidate();
        presenter->setVolume((uint8_t)value);
    }
    else if (&source == &durationBar && durationDragging)
    {
        /* Preview only while dragging. Do not seek on every touch move. */
        touchgfx::Unicode::snprintf(currentSongDurBuffer, CURRENTSONGDUR_SIZE, "%u:%02u",
                                    (unsigned int)((uint32_t)value / 60U),
                                    (unsigned int)((uint32_t)value % 60U));
        currentSongDur.invalidate();
    }
}

void MainPlayerUIView::durationStartHandler(const touchgfx::Slider& source, int value)
{
    (void)source;
    (void)value;
    if (!updatingUi) durationDragging = true;
}

void MainPlayerUIView::durationStopHandler(const touchgfx::Slider& source, int value)
{
    (void)source;
    if (updatingUi) return;
    durationDragging = false;
    presenter->seek((uint32_t)value);
}

void MainPlayerUIView::updateMedia(const MediaSnapshot& snapshot)
{
    updatingUi = true;
    currentSource = snapshot.source;
    // AUX, USB Audio and Internet Radio are live sources with no finite
    // timeline.  Keep the transport slider exclusive to local media.
    const bool liveMode = snapshot.source != MEDIA_SOURCE_WAV;
    speedPitchButton.setTouchable(!liveMode);
    const char* sourceText = snapshot.currentName[0] != '\0' ? snapshot.currentName : snapshot.status;
    if (std::strncmp(lastTitle, sourceText, sizeof(lastTitle)) != 0)
    {
        // The new title can be shorter than the old one. Mark the full screen
        // dirty so the background erases every pixel of the previous title.
        invalidate();
        std::strncpy(lastTitle, sourceText, sizeof(lastTitle) - 1U);
        lastTitle[sizeof(lastTitle) - 1U] = '\0';
        touchgfx::Unicode::UnicodeChar original[MEDIA_TRACK_NAME_SIZE];
        touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(sourceText), original, MEDIA_TRACK_NAME_SIZE);
        touchgfx::Unicode::strncpy(songNameBuffer, original, SONG_NAME_BUFFER_SIZE);
        songNameBuffer[SONG_NAME_BUFFER_SIZE - 1U] = 0;
        dynamicSongName.resizeToCurrentText();
        marqueeTextWidth = dynamicSongName.getTextWidth();
        marqueeActive = marqueeTextWidth > songNameClip.getWidth();
        marqueeLoopWidth = 0U;

        if (marqueeActive)
        {
            uint16_t length = 0U;
            while (songNameBuffer[length] != 0U && length < SONG_NAME_BUFFER_SIZE - 1U) length++;
            for (uint16_t i = 0U; i < MARQUEE_GAP_SPACES && length < SONG_NAME_BUFFER_SIZE - 1U; i++)
            {
                songNameBuffer[length++] = (touchgfx::Unicode::UnicodeChar)' ';
            }
            for (uint16_t i = 0U; original[i] != 0U && length < SONG_NAME_BUFFER_SIZE - 1U; i++)
            {
                songNameBuffer[length++] = original[i];
            }
            songNameBuffer[length] = 0U;
            dynamicSongName.resizeToCurrentText();
            marqueeLoopWidth = dynamicSongName.getTextWidth() - marqueeTextWidth;
            marqueeX = 0;
        }
        else
        {
            // Centre titles which fit; scrolling titles begin at the left edge.
            marqueeX = ((int16_t)songNameClip.getWidth() - (int16_t)marqueeTextWidth) / 2;
        }
        marqueeDelay = MARQUEE_START_PAUSE;
        dynamicSongName.moveTo(marqueeX, 0);
        invalidate();
    }
    if (!durationDragging)
    {
        touchgfx::Unicode::snprintf(currentSongDurBuffer, CURRENTSONGDUR_SIZE, "%u:%02u",
                                    (unsigned int)(snapshot.elapsedSeconds / 60U),
                                    (unsigned int)(snapshot.elapsedSeconds % 60U));
        durationBar.setValueRange(0, snapshot.durationSeconds > 0U ? (int)snapshot.durationSeconds : 1);
        durationBar.setValue((int)snapshot.elapsedSeconds);
    }
    touchgfx::Unicode::snprintf(wholeDurationBuffer, 10, "%u:%02u",
                                (unsigned int)(snapshot.durationSeconds / 60U),
                                (unsigned int)(snapshot.durationSeconds % 60U));
    touchgfx::Unicode::snprintf(volume_percentageBuffer, VOLUME_PERCENTAGE_SIZE, "%u%%", snapshot.volume);
    volumeSlider.setValue(snapshot.volume);
    durationBar.setVisible(!liveMode);
    currentSongDur.setVisible(!liveMode);
    dynamicWholeDuration.setVisible(!liveMode);
    songNameClip.invalidate();
    currentSongDur.invalidate();
    dynamicWholeDuration.invalidate();
    volume_percentage.invalidate();
    durationBar.invalidate();
    volumeSlider.invalidate();
    updatingUi = false;
}
