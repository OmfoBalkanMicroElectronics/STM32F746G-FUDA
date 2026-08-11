#ifndef SCREENSETTINGSPRESENTER_HPP
#define SCREENSETTINGSPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class screenSettingsView;

class screenSettingsPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    screenSettingsPresenter(screenSettingsView& v);

    virtual void activate();
    virtual void deactivate();
    virtual ~screenSettingsPresenter() {}

    // Called when the user changes the brightness slider
    void setScreenBrightness(int value);
    
    // Called when the user selects a timeout duration
    void setScreenTimeout(int seconds);

private:
    screenSettingsPresenter();
    screenSettingsView& view;
};

#endif // SCREENSETTINGSPRESENTER_HPP