#include <gui/screensettings_screen/screenSettingsView.hpp>
#include <texts/TextKeysAndLanguages.hpp>

screenSettingsView::screenSettingsView() :
    brightnessCallback(this, &screenSettingsView::brightnessChangedHandler),
    timeoutCallback(this, &screenSettingsView::timeoutChangedHandler)
{
}

void screenSettingsView::setupScreen()
{
    screenSettingsViewBase::setupScreen();

    // Override the Designer's slider callback to route it through our custom handler
    slider1.setNewValueCallback(brightnessCallback);

    // Override the Designer's button callbacks for the timeout options
    timeoutNeverButton.setAction(timeoutCallback);
    timeout30secButton.setAction(timeoutCallback);
    timeout60secButton.setAction(timeoutCallback);
}

void screenSettingsView::tearDownScreen()
{
    screenSettingsViewBase::tearDownScreen();
}

void screenSettingsView::updateSettings(uint8_t brightness, uint16_t timeoutSeconds)
{
    slider1.setValue(brightness);
    slider1.invalidate();
    touchgfx::Unicode::snprintf(screenBrightnessPercBuffer, SCREENBRIGHTNESSPERC_SIZE, "%u", brightness);
    screenBrightnessPerc.invalidate();

    if (timeoutSeconds == 30U)
        screenTimeoutCurrMod.setTypedText(touchgfx::TypedText(T_TEXTTIMEOUT30SN));
    else if (timeoutSeconds == 60U)
        screenTimeoutCurrMod.setTypedText(touchgfx::TypedText(T_TEXTTIMEOUT60SN));
    else
        screenTimeoutCurrMod.setTypedText(touchgfx::TypedText(T_TEXTTIMEOUT));
    screenTimeoutCurrMod.invalidate();
}

void screenSettingsView::brightnessChangedHandler(const touchgfx::Slider& src, int value)
{
    // 1. Update the UI (replicating the Designer's logic)
    touchgfx::Unicode::snprintf(screenBrightnessPercBuffer, SCREENBRIGHTNESSPERC_SIZE, "%d", value);
    screenBrightnessPerc.invalidate();

    // 2. Tell the Presenter to handle the hardware change
    if (presenter)
    {
        presenter->setScreenBrightness(value);
    }
}

void screenSettingsView::timeoutChangedHandler(const touchgfx::AbstractButtonContainer& src)
{
    int timeoutSeconds = 0;
    touchgfx::TypedText newTimeoutText(T_TEXTTIMEOUT); // Default to "Never"

    // Figure out which button was clicked
    if (&src == &timeoutNeverButton) {
        timeoutSeconds = 0;
        newTimeoutText = touchgfx::TypedText(T_TEXTTIMEOUT);
    } else if (&src == &timeout30secButton) {
        timeoutSeconds = 30;
        newTimeoutText = touchgfx::TypedText(T_TEXTTIMEOUT30SN);
    } else if (&src == &timeout60secButton) {
        timeoutSeconds = 60;
        newTimeoutText = touchgfx::TypedText(T_TEXTTIMEOUT60SN);
    }

    // 1. Update the UI
    screenTimeoutCurrMod.setTypedText(newTimeoutText);
    screenTimeoutCurrMod.invalidate();

    // 2. Tell the Presenter to handle the timer logic
    if (presenter)
    {
        presenter->setScreenTimeout(timeoutSeconds);
    }
}
