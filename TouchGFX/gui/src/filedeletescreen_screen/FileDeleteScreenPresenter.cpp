#include <gui/filedeletescreen_screen/FileDeleteScreenView.hpp>
#include <gui/filedeletescreen_screen/FileDeleteScreenPresenter.hpp>

FileDeleteScreenPresenter::FileDeleteScreenPresenter(FileDeleteScreenView& v)
    : view(v)
{

}

void FileDeleteScreenPresenter::activate()
{

}

void FileDeleteScreenPresenter::deactivate()
{

}

void FileDeleteScreenPresenter::mediaLibraryChanged()
{
    view.refreshLibrary();
}
