#ifndef SWEQUALIZERUIPRESENTER_HPP
#define SWEQUALIZERUIPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class SWEqualizerUIView;

class SWEqualizerUIPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SWEqualizerUIPresenter(SWEqualizerUIView& v);

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

    virtual ~SWEqualizerUIPresenter() {}
    void setBand(uint8_t band, uint8_t value) { model->setSoftwareEQBand(band, value); }
    void setPreamp(uint8_t value) { model->setSoftwareEQPreamp(value); }
    void applyPreset(uint8_t preset) { model->applySoftwareEQPreset(preset); }
    virtual void mediaStateChanged(const MediaSnapshot& snapshot);

private:
    SWEqualizerUIPresenter();

    SWEqualizerUIView& view;
};

#endif // SWEQUALIZERUIPRESENTER_HPP
