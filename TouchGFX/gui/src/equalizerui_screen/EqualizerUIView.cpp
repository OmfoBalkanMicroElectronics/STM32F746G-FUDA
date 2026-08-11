#include <gui/equalizerui_screen/EqualizerUIView.hpp>

EqualizerUIView::EqualizerUIView() :
    sliderValueChangedCallback(this, &EqualizerUIView::sliderValueChangedCallbackHandler),
    presetButtonCallback(this, &EqualizerUIView::presetButtonCallbackHandler)
{
}

void EqualizerUIView::setupScreen()
{
    EqualizerUIViewBase::setupScreen();

    // 1. Assign callbacks to the 5 sliders so we know when the user drags them
    slider100hz.setNewValueCallback(sliderValueChangedCallback);
    slider300hz.setNewValueCallback(sliderValueChangedCallback);
    slider875hz.setNewValueCallback(sliderValueChangedCallback);
    slider2_4khz.setNewValueCallback(sliderValueChangedCallback);
    slider9_6khz.setNewValueCallback(sliderValueChangedCallback);

    // 2. Override the Designer's preset button actions to route them through the Presenter
    normalEq.setAction(presetButtonCallback);
    classicEq.setAction(presetButtonCallback);
    jazzEq.setAction(presetButtonCallback);
    rockEq.setAction(presetButtonCallback);
    popEq.setAction(presetButtonCallback);
}

void EqualizerUIView::tearDownScreen()
{
    EqualizerUIViewBase::tearDownScreen();
}

void EqualizerUIView::sliderValueChangedCallbackHandler(const touchgfx::Slider& src, int value)
{
    if (!presenter) return;

    // Map the slider to an index (0 to 4) and tell the Presenter
    if (&src == &slider100hz) presenter->updateEQBand(0, value);
    else if (&src == &slider300hz) presenter->updateEQBand(1, value);
    else if (&src == &slider875hz) presenter->updateEQBand(2, value);
    else if (&src == &slider2_4khz) presenter->updateEQBand(3, value);
    else if (&src == &slider9_6khz) presenter->updateEQBand(4, value);
}

void EqualizerUIView::presetButtonCallbackHandler(const touchgfx::AbstractButtonContainer& src)
{
    if (!presenter) return;

    int presetValues[5];

    // Define the exact values you had in the Designer for each preset
    if (&src == &normalEq) {
        presetValues[0] = 50; presetValues[1] = 50; presetValues[2] = 50; presetValues[3] = 50; presetValues[4] = 50;
        presenter->applyPreset(0);
    } else if (&src == &classicEq) {
        presetValues[0] = 75; presetValues[1] = 60; presetValues[2] = 50; presetValues[3] = 70; presetValues[4] = 85;
        presenter->applyPreset(1);
    } else if (&src == &jazzEq) {
        presetValues[0] = 80; presetValues[1] = 55; presetValues[2] = 65; presetValues[3] = 80; presetValues[4] = 70;
        presenter->applyPreset(2);
    } else if (&src == &rockEq) {
        presetValues[0] = 85; presetValues[1] = 70; presetValues[2] = 40; presetValues[3] = 65; presetValues[4] = 80;
        presenter->applyPreset(3);
    } else if (&src == &popEq) {
        presetValues[0] = 50; presetValues[1] = 65; presetValues[2] = 85; presetValues[3] = 75; presetValues[4] = 60;
        presenter->applyPreset(4);
    }

    // Update the sliders visually
    updateSliders(presetValues);
}

void EqualizerUIView::updateSliders(int values[5])
{
    slider100hz.setValue(values[0]); slider100hz.invalidate();
    slider300hz.setValue(values[1]); slider300hz.invalidate();
    slider875hz.setValue(values[2]); slider875hz.invalidate();
    slider2_4khz.setValue(values[3]); slider2_4khz.invalidate();
    slider9_6khz.setValue(values[4]); slider9_6khz.invalidate();
}
