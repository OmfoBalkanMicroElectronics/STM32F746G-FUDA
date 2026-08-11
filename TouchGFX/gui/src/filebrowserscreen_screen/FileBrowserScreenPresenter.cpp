#include <gui/filebrowserscreen_screen/FileBrowserScreenPresenter.hpp>
#include <gui/filebrowserscreen_screen/FileBrowserScreenView.hpp>

FileBrowserScreenPresenter::FileBrowserScreenPresenter(FileBrowserScreenView& v) : view(v)
{
}

void FileBrowserScreenPresenter::activate()
{
}

void FileBrowserScreenPresenter::deactivate()
{
}

// 👇 ADD THIS FUNCTION AT THE BOTTOM 👇
void FileBrowserScreenPresenter::songSelected(int index)
{
    model->selectTrack((uint16_t)index);
}

void FileBrowserScreenPresenter::mediaLibraryChanged()
{
    view.refreshLibrary();
}
