#ifndef SPEEDPITCHUIVIEW_HPP
#define SPEEDPITCHUIVIEW_HPP

#include <gui_generated/speedpitchui_screen/speedPitchUIViewBase.hpp>
#include <gui/speedpitchui_screen/speedPitchUIPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Callback.hpp>

class speedPitchUIView : public speedPitchUIViewBase
{
public:
    speedPitchUIView();
    virtual ~speedPitchUIView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    void updateControls(uint16_t speedPercent, int16_t pitchCents, bool enabled);
protected:
    touchgfx::TextAreaWithOneWildcard dynamicSpeedValue;
    touchgfx::TextAreaWithOneWildcard dynamicPitchValue;
    touchgfx::TextAreaWithOneWildcard dynamicStatus;
    touchgfx::Unicode::UnicodeChar speedBuffer[24];
    touchgfx::Unicode::UnicodeChar pitchBuffer[24];
    touchgfx::Unicode::UnicodeChar statusBuffer[32];
    touchgfx::Callback<speedPitchUIView, const touchgfx::Slider&, int> sliderCallback;
    touchgfx::Callback<speedPitchUIView, const touchgfx::AbstractButton&> adjustButtonCallback;
    bool updatingControls;
    bool effectEnabled;
    void sliderChanged(const touchgfx::Slider& source, int value);
    void adjustButtonPressed(const touchgfx::AbstractButton& source);
    void updateLabels(uint16_t speedPercent, int16_t pitchCents);
};

#endif // SPEEDPITCHUIVIEW_HPP
