#ifndef SPEEDPITCHUIPRESENTER_HPP
#define SPEEDPITCHUIPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class speedPitchUIView;

class speedPitchUIPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    speedPitchUIPresenter(speedPitchUIView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~speedPitchUIPresenter() {}
    void setSpeed(uint16_t percent) { model->setPlaybackSpeed(percent); }
    void setPitch(int16_t cents) { model->setPlaybackPitch(cents); }
    void setEnabled(bool enabled) { model->setTimePitchEnabled(enabled); }
    virtual void mediaStateChanged(const MediaSnapshot& snapshot);

private:
    speedPitchUIPresenter();

    speedPitchUIView& view;
};

#endif // SPEEDPITCHUIPRESENTER_HPP
