#include <gui/swequalizerui_screen/SWEqualizerUIView.hpp>

SWEqualizerUIView::SWEqualizerUIView() :
    sliderCallback(this, &SWEqualizerUIView::sliderChanged),
    presetCallback(this, &SWEqualizerUIView::presetPressed),
    backCallback(this, &SWEqualizerUIView::backPressed),
    updatingControls(false)
{
}

void SWEqualizerUIView::setupScreen()
{
    SWEqualizerUIViewBase::setupScreen();
    touchgfx::Slider* sliders[MEDIA_SW_EQ_BANDS] =
    {
        &eq31Slider, &eq62Slider, &eq125Slider, &eq250Slider, &eq500Slider,
        &eq1kSlider, &eq2kSlider, &eq4kSlider, &eq8kSlider, &eq16kSlider
    };
    for (uint8_t i = 0U; i < MEDIA_SW_EQ_BANDS; i++) sliders[i]->setNewValueCallback(sliderCallback);
    preampSlider.setNewValueCallback(sliderCallback);
    flatButton.setAction(presetCallback);
    classicButton.setAction(presetCallback);
    jazzButton.setAction(presetCallback);
    rockButton.setAction(presetCallback);
    popButton.setAction(presetCallback);
    vocalButton.setAction(presetCallback);
    backButton.setAction(backCallback);
}

void SWEqualizerUIView::updateControls(const uint8_t bands[MEDIA_SW_EQ_BANDS], uint8_t preamp)
{
    touchgfx::Slider* sliders[MEDIA_SW_EQ_BANDS] =
    {
        &eq31Slider, &eq62Slider, &eq125Slider, &eq250Slider, &eq500Slider,
        &eq1kSlider, &eq2kSlider, &eq4kSlider, &eq8kSlider, &eq16kSlider
    };
    updatingControls = true;
    for (uint8_t i = 0U; i < MEDIA_SW_EQ_BANDS; i++)
    {
        sliders[i]->setValue(bands[i]);
        sliders[i]->invalidate();
    }
    preampSlider.setValue(preamp);
    preampSlider.invalidate();
    updatingControls = false;
}

void SWEqualizerUIView::sliderChanged(const touchgfx::Slider& source, int value)
{
    if (updatingControls || presenter == 0) return;
    touchgfx::Slider* sliders[MEDIA_SW_EQ_BANDS] =
    {
        &eq31Slider, &eq62Slider, &eq125Slider, &eq250Slider, &eq500Slider,
        &eq1kSlider, &eq2kSlider, &eq4kSlider, &eq8kSlider, &eq16kSlider
    };
    if (&source == &preampSlider)
    {
        presenter->setPreamp((uint8_t)value);
        return;
    }
    for (uint8_t i = 0U; i < MEDIA_SW_EQ_BANDS; i++)
    {
        if (&source == sliders[i])
        {
            presenter->setBand(i, (uint8_t)value);
            return;
        }
    }
}

void SWEqualizerUIView::presetPressed(const touchgfx::AbstractButtonContainer& source)
{
    uint8_t preset;
    if (&source == &flatButton) preset = 0U;
    else if (&source == &classicButton) preset = 1U;
    else if (&source == &jazzButton) preset = 2U;
    else if (&source == &rockButton) preset = 3U;
    else if (&source == &popButton) preset = 4U;
    else if (&source == &vocalButton) preset = 5U;
    else return;
    presenter->applyPreset(preset);
}

void SWEqualizerUIView::backPressed(const touchgfx::AbstractButton& source)
{
    if (&source == &backButton) application().gotoMainPlayerUIScreenCoverTransitionEast();
}

void SWEqualizerUIView::tearDownScreen()
{
    SWEqualizerUIViewBase::tearDownScreen();
}
