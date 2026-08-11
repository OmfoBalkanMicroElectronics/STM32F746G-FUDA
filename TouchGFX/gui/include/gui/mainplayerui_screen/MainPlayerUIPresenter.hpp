#ifndef MAINPLAYERUIPRESENTER_HPP
#define MAINPLAYERUIPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class MainPlayerUIView;

class MainPlayerUIPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MainPlayerUIPresenter(MainPlayerUIView& v);

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

    virtual ~MainPlayerUIPresenter() {}
    void playPause() { model->togglePlayPause(); }
    void next() { model->nextTrack(); }
    void previous() { model->previousTrack(); }
    void setVolume(uint8_t value) { model->setVolume(value); }
    void seek(uint32_t seconds) { model->seek(seconds); }
    void toggleSource() { model->toggleSource(); }
    uint32_t spectrum(uint8_t levels[MEDIA_SPECTRUM_BANDS]) const { return model->spectrum(levels); }
    virtual void mediaStateChanged(const MediaSnapshot& snapshot);

private:
    MainPlayerUIPresenter();

    MainPlayerUIView& view;
};

#endif // MAINPLAYERUIPRESENTER_HPP
