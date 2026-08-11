#ifndef EQUALIZERUIVIEW_HPP
#define EQUALIZERUIVIEW_HPP

#include <gui_generated/equalizerui_screen/EqualizerUIViewBase.hpp>
#include <gui/equalizerui_screen/EqualizerUIPresenter.hpp>

class EqualizerUIView : public EqualizerUIViewBase
{
public:
    EqualizerUIView();
    virtual ~EqualizerUIView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    // Called by the Presenter to update the UI (e.g., when loading a saved preset)
    void updateSliders(int values[5]);

private:
    // Callback for when the user drags a slider manually
    touchgfx::Callback<EqualizerUIView, const touchgfx::Slider&, int> sliderValueChangedCallback;
    void sliderValueChangedCallbackHandler(const touchgfx::Slider& src, int value);

    // Callback for when the user clicks a preset button
    touchgfx::Callback<EqualizerUIView, const touchgfx::AbstractButtonContainer&> presetButtonCallback;
    void presetButtonCallbackHandler(const touchgfx::AbstractButtonContainer& src);
};

#endif // EQUALIZERUIVIEW_HPP
