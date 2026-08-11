#include <gui/screensettings_screen/screenSettingsView.hpp>
#include <gui/screensettings_screen/screenSettingsPresenter.hpp>

screenSettingsPresenter::screenSettingsPresenter(screenSettingsView& v) : view(v)
{
}

void screenSettingsPresenter::activate()
{
    view.updateSettings(model->screenBrightness(), model->screenTimeout());
}

void screenSettingsPresenter::deactivate()
{
}

// 👇 ADD THESE TWO FUNCTIONS 👇
void screenSettingsPresenter::setScreenBrightness(int value)
{
    model->setScreenBrightness((uint8_t)value);
}

void screenSettingsPresenter::setScreenTimeout(int seconds)
{
    model->setScreenTimeout((uint16_t)seconds);
}
