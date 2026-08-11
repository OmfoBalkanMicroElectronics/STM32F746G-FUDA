#ifndef FILEDELETESCREENPRESENTER_HPP
#define FILEDELETESCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class FileDeleteScreenView;

class FileDeleteScreenPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    FileDeleteScreenPresenter(FileDeleteScreenView& v);

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

    virtual ~FileDeleteScreenPresenter() {}
    uint16_t fileCount() const { return model->fileCount(); }
    bool fileName(uint16_t index, char* name, uint16_t capacity) const { return model->fileName(index, name, capacity); }
    uint16_t folderCount() const { return model->folderCount(); }
    bool folderName(uint16_t index, char* name, uint16_t capacity) const { return model->folderName(index, name, capacity); }
    void currentFolder(char* name, uint16_t capacity) const { model->currentFolder(name, capacity); }
    void selectFolder(uint16_t index) { model->selectFolder(index); }
    void deleteFile(uint16_t index) { model->deleteFile(index); }
    void deleteSnapshot(MediaDeleteSnapshot& snapshot) const { model->deleteSnapshot(snapshot); }
    void mediaSnapshot(MediaSnapshot& snapshot) const { model->mediaSnapshot(snapshot); }
    void mediaStatus(char* status, uint16_t capacity) const { model->mediaStatus(status, capacity); }
    void toggleStorage() { model->toggleStorage(); }
    virtual void mediaLibraryChanged();

private:
    FileDeleteScreenPresenter();

    FileDeleteScreenView& view;
};

#endif // FILEDELETESCREENPRESENTER_HPP
