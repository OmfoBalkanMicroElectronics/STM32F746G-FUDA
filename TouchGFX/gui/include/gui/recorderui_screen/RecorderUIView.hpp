#ifndef RECORDERUIVIEW_HPP
#define RECORDERUIVIEW_HPP

#include <gui_generated/recorderui_screen/RecorderUIViewBase.hpp>
#include <gui/recorderui_screen/RecorderUIPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/widgets/Box.hpp>

class RecorderUIView : public RecorderUIViewBase
{
public:
    RecorderUIView();
    virtual ~RecorderUIView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
protected:
    touchgfx::TextAreaWithOneWildcard statusValue;
    touchgfx::TextAreaWithOneWildcard fileValue;
    touchgfx::TextAreaWithOneWildcard timeValue;
    touchgfx::TextAreaWithOneWildcard gainValue;
    touchgfx::Unicode::UnicodeChar statusValueBuffer[96];
    touchgfx::Unicode::UnicodeChar fileValueBuffer[96];
    touchgfx::Unicode::UnicodeChar timeValueBuffer[48];
    touchgfx::Unicode::UnicodeChar gainValueBuffer[24];
    touchgfx::Box leftPeakAlarm;
    touchgfx::Box rightPeakAlarm;
    touchgfx::Callback<RecorderUIView, const touchgfx::AbstractButton&> recorderButtonCallback;
    touchgfx::Callback<RecorderUIView, const touchgfx::Slider&, int> gainSliderCallback;
    uint32_t lastRecorderRevision;
    uint8_t lastRecorderState;
    uint8_t leftPeakHold;
    uint8_t rightPeakHold;
    bool updatingGain;

    void recorderButtonHandler(const touchgfx::AbstractButton& source);
    void updateRecorderUi(const MediaRecorderSnapshot& snapshot);
    void gainSliderChanged(const touchgfx::Slider& source, int value);
    void updateGainLabel(int16_t centiDb);
};

#endif // RECORDERUIVIEW_HPP
