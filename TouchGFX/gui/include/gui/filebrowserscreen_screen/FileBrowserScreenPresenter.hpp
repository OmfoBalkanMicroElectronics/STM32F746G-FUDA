#ifndef FILEBROWSERSCREENPRESENTER_HPP
#define FILEBROWSERSCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class FileBrowserScreenView;

class FileBrowserScreenPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    FileBrowserScreenPresenter(FileBrowserScreenView& v);

    virtual void activate();
    virtual void deactivate();
    virtual ~FileBrowserScreenPresenter() {}

    // 👇 ADD THIS LINE 👇
    void songSelected(int index);
    uint16_t trackCount() const { return model->trackCount(); }
    bool trackName(uint16_t index, char* name, uint16_t capacity) const { return model->trackName(index, name, capacity); }
    uint16_t folderCount() const { return model->folderCount(); }
    bool folderName(uint16_t index, char* name, uint16_t capacity) const { return model->folderName(index, name, capacity); }
    void currentFolder(char* name, uint16_t capacity) const { model->currentFolder(name, capacity); }
    void selectFolder(uint16_t index) { model->selectFolder(index); }
    void mediaStatus(char* status, uint16_t capacity) const { model->mediaStatus(status, capacity); }
    void mediaSnapshot(MediaSnapshot& snapshot) const { model->mediaSnapshot(snapshot); }
    void toggleStorage() { model->toggleStorage(); }
    virtual void mediaLibraryChanged();

private:
    FileBrowserScreenPresenter();
    FileBrowserScreenView& view;
};

#endif // FILEBROWSERSCREENPRESENTER_HPP
