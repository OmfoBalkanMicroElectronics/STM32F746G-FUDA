#ifndef RECORDERUIPRESENTER_HPP
#define RECORDERUIPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class RecorderUIView;

class RecorderUIPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    RecorderUIPresenter(RecorderUIView& v);

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

    void recorderSnapshot(MediaRecorderSnapshot& snapshot) const;
    void startRecording();
    void stopRecording();
    void confirmRecording();
    void discardRecording();
    void setGain(int16_t centiDb);

    virtual ~RecorderUIPresenter() {}

private:
    RecorderUIPresenter();

    RecorderUIView& view;
};

#endif // RECORDERUIPRESENTER_HPP
