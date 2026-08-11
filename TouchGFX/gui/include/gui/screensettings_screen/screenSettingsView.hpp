#ifndef SCREENSETTINGSVIEW_HPP
#define SCREENSETTINGSVIEW_HPP

#include <gui_generated/screensettings_screen/screenSettingsViewBase.hpp>
#include <gui/screensettings_screen/screenSettingsPresenter.hpp>

class screenSettingsView : public screenSettingsViewBase
{
public:
    screenSettingsView();
    virtual ~screenSettingsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    void updateSettings(uint8_t brightness, uint16_t timeoutSeconds);

private:
    // Callback for the brightness slider
    touchgfx::Callback<screenSettingsView, const touchgfx::Slider&, int> brightnessCallback;
    void brightnessChangedHandler(const touchgfx::Slider& src, int value);

    // Callback for the timeout buttons
    touchgfx::Callback<screenSettingsView, const touchgfx::AbstractButtonContainer&> timeoutCallback;
    void timeoutChangedHandler(const touchgfx::AbstractButtonContainer& src);
};

#endif // SCREENSETTINGSVIEW_HPP
