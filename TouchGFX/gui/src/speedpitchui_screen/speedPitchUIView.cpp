#include <gui/speedpitchui_screen/speedPitchUIView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstdio>

speedPitchUIView::speedPitchUIView()
    : sliderCallback(this, &speedPitchUIView::sliderChanged),
      adjustButtonCallback(this, &speedPitchUIView::adjustButtonPressed),
      updatingControls(false),
      effectEnabled(true)
{
}

void speedPitchUIView::setupScreen()
{
    speedPitchUIViewBase::setupScreen();
    speedValue.setVisible(false);
    pitchValue.setVisible(false);
    on_offStatus.setVisible(false);

    dynamicSpeedValue.setPosition(speedValue.getX() - 20, speedValue.getY(), 250, speedValue.getHeight());
    dynamicSpeedValue.setColor(speedValue.getColor());
    dynamicSpeedValue.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    dynamicSpeedValue.setWildcard(speedBuffer);
    add(dynamicSpeedValue);

    dynamicPitchValue.setPosition(pitchValue.getX() - 20, pitchValue.getY(), 250, pitchValue.getHeight());
    dynamicPitchValue.setColor(pitchValue.getColor());
    dynamicPitchValue.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    dynamicPitchValue.setWildcard(pitchBuffer);
    add(dynamicPitchValue);

    dynamicStatus.setPosition(on_offStatus.getX(), on_offStatus.getY(), 260, on_offStatus.getHeight());
    dynamicStatus.setColor(on_offStatus.getColor());
    dynamicStatus.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    dynamicStatus.setWildcard(statusBuffer);
    add(dynamicStatus);

    speedSlider.setValueRange(50, 250);
    pitchSlider.setValueRange(-1200, 1200);
    speedSlider.setNewValueCallback(sliderCallback);
    pitchSlider.setNewValueCallback(sliderCallback);
    speedIncreasebutton.setAction(adjustButtonCallback);
    speedDecreasebutton.setAction(adjustButtonCallback);
    pitchIncreasebutton.setAction(adjustButtonCallback);
    pitchDecreasebutton.setAction(adjustButtonCallback);
    on_offButton.setAction(adjustButtonCallback);
}

void speedPitchUIView::adjustButtonPressed(const touchgfx::AbstractButton& source)
{
    if (presenter == 0) return;
    int speed = speedSlider.getValue();
    int pitch = pitchSlider.getValue();
    if (&source == &on_offButton)
    {
        effectEnabled = !effectEnabled;
        presenter->setEnabled(effectEnabled);
        updateControls((uint16_t)speed, (int16_t)pitch, effectEnabled);
        return;
    }
    if (&source == &speedIncreasebutton) speed += 5;       // +0.05x
    else if (&source == &speedDecreasebutton) speed -= 5; // -0.05x
    else if (&source == &pitchIncreasebutton) pitch += 5;       // +0.05 semitone
    else if (&source == &pitchDecreasebutton) pitch -= 5;       // -0.05 semitone
    else return;

    if (speed < 50) speed = 50;
    if (speed > 250) speed = 250;
    if (pitch < -1200) pitch = -1200;
    if (pitch > 1200) pitch = 1200;
    speedSlider.setValue(speed);
    pitchSlider.setValue(pitch);
    speedSlider.invalidate();
    pitchSlider.invalidate();
    if (&source == &speedIncreasebutton || &source == &speedDecreasebutton)
        presenter->setSpeed((uint16_t)speed);
    else
        presenter->setPitch((int16_t)pitch);
    updateLabels((uint16_t)speed, (int16_t)pitch);
}

void speedPitchUIView::updateLabels(uint16_t speedPercent, int16_t pitchCents)
{
    char text[32];
    std::snprintf(text, sizeof(text), "H\xC4\xB1z: %u,%02ux",
                  speedPercent / 100U, speedPercent % 100U);
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), speedBuffer, 24);

    const int32_t signedPitch = pitchCents;
    const uint32_t magnitude = signedPitch < 0 ? (uint32_t)(-signedPitch) : (uint32_t)signedPitch;
    std::snprintf(text, sizeof(text), "Ton: %s%u,%02u",
                  signedPitch > 0 ? "+" : (signedPitch < 0 ? "-" : ""),
                  (unsigned int)(magnitude / 100U),
                  (unsigned int)(magnitude % 100U));
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), pitchBuffer, 24);
    dynamicSpeedValue.invalidate();
    dynamicPitchValue.invalidate();
}

void speedPitchUIView::updateControls(uint16_t speedPercent, int16_t pitchCents, bool enabled)
{
    updatingControls = true;
    effectEnabled = enabled;
    speedSlider.setValue(speedPercent);
    pitchSlider.setValue(pitchCents);
    speedSlider.invalidate();
    pitchSlider.invalidate();
    updateLabels(speedPercent, pitchCents);
    const char* status = effectEnabled ? "H\xC4\xB1z&Tempo ayar\xC4\xB1: A\xC3\xA7\xC4\xB1k" :
                                         "H\xC4\xB1z&Tempo ayar\xC4\xB1: Kapal\xC4\xB1";
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(status), statusBuffer, 32);
    dynamicStatus.invalidate();
    speedSlider.setTouchable(effectEnabled);
    pitchSlider.setTouchable(effectEnabled);
    speedIncreasebutton.setTouchable(effectEnabled);
    speedDecreasebutton.setTouchable(effectEnabled);
    pitchIncreasebutton.setTouchable(effectEnabled);
    pitchDecreasebutton.setTouchable(effectEnabled);
    updatingControls = false;
}

void speedPitchUIView::sliderChanged(const touchgfx::Slider& source, int value)
{
    if (updatingControls || presenter == 0) return;
    if (&source == &speedSlider)
    {
        presenter->setSpeed((uint16_t)value);
        updateLabels((uint16_t)value, pitchSlider.getValue());
    }
    else if (&source == &pitchSlider)
    {
        presenter->setPitch((int16_t)value);
        updateLabels((uint16_t)speedSlider.getValue(), (int16_t)value);
    }
}

void speedPitchUIView::tearDownScreen()
{
    speedPitchUIViewBase::tearDownScreen();
}
