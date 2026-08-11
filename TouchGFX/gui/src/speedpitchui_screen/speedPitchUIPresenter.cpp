#include <gui/speedpitchui_screen/speedPitchUIView.hpp>
#include <gui/speedpitchui_screen/speedPitchUIPresenter.hpp>

speedPitchUIPresenter::speedPitchUIPresenter(speedPitchUIView& v)
    : view(v)
{

}

void speedPitchUIPresenter::activate()
{
    MediaSnapshot snapshot;
    model->mediaSnapshot(snapshot);
    view.updateControls(snapshot.speedPercent, snapshot.pitchCents, snapshot.timePitchEnabled != 0U);
}

void speedPitchUIPresenter::mediaStateChanged(const MediaSnapshot& snapshot)
{
    view.updateControls(snapshot.speedPercent, snapshot.pitchCents, snapshot.timePitchEnabled != 0U);
}

void speedPitchUIPresenter::deactivate()
{

}
