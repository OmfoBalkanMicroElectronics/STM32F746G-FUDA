#ifndef SWEQUALIZERUIVIEW_HPP
#define SWEQUALIZERUIVIEW_HPP

#include <gui_generated/swequalizerui_screen/SWEqualizerUIViewBase.hpp>
#include <gui/swequalizerui_screen/SWEqualizerUIPresenter.hpp>

class SWEqualizerUIView : public SWEqualizerUIViewBase
{
public:
    SWEqualizerUIView();
    virtual ~SWEqualizerUIView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    void updateControls(const uint8_t bands[MEDIA_SW_EQ_BANDS], uint8_t preamp);
protected:
    touchgfx::Callback<SWEqualizerUIView, const touchgfx::Slider&, int> sliderCallback;
    touchgfx::Callback<SWEqualizerUIView, const touchgfx::AbstractButtonContainer&> presetCallback;
    touchgfx::Callback<SWEqualizerUIView, const touchgfx::AbstractButton&> backCallback;
    bool updatingControls;

    void sliderChanged(const touchgfx::Slider& source, int value);
    void presetPressed(const touchgfx::AbstractButtonContainer& source);
    void backPressed(const touchgfx::AbstractButton& source);
};

#endif // SWEQUALIZERUIVIEW_HPP
