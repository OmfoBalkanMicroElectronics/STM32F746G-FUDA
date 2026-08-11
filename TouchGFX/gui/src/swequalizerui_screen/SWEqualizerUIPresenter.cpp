#include <gui/swequalizerui_screen/SWEqualizerUIView.hpp>
#include <gui/swequalizerui_screen/SWEqualizerUIPresenter.hpp>

SWEqualizerUIPresenter::SWEqualizerUIPresenter(SWEqualizerUIView& v)
    : view(v)
{

}

void SWEqualizerUIPresenter::activate()
{
    MediaSnapshot snapshot;
    model->mediaSnapshot(snapshot);
    view.updateControls(snapshot.swEqBands, snapshot.swEqPreamp);
}

void SWEqualizerUIPresenter::mediaStateChanged(const MediaSnapshot& snapshot)
{
    view.updateControls(snapshot.swEqBands, snapshot.swEqPreamp);
}

void SWEqualizerUIPresenter::deactivate()
{

}
