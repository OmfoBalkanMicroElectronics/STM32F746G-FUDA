#include <gui/mainplayerui_screen/MainPlayerUIView.hpp>
#include <gui/mainplayerui_screen/MainPlayerUIPresenter.hpp>

MainPlayerUIPresenter::MainPlayerUIPresenter(MainPlayerUIView& v)
    : view(v)
{

}

void MainPlayerUIPresenter::activate()
{
    // A selection is processed while the file-browser presenter is still active.
    // Always pull the latest backend state when this screen becomes visible.
    MediaSnapshot snapshot;
    model->mediaSnapshot(snapshot);
    view.updateMedia(snapshot);
}

void MainPlayerUIPresenter::deactivate()
{

}

void MainPlayerUIPresenter::mediaStateChanged(const MediaSnapshot& snapshot)
{
    view.updateMedia(snapshot);
}
