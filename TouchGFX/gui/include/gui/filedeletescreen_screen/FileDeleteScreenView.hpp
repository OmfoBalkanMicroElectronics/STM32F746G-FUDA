#ifndef FILEDELETESCREENVIEW_HPP
#define FILEDELETESCREENVIEW_HPP

#include <gui_generated/filedeletescreen_screen/FileDeleteScreenViewBase.hpp>
#include <gui/filedeletescreen_screen/FileDeleteScreenPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/containers/scrollers/ScrollList.hpp>
#include <touchgfx/containers/scrollers/DrawableList.hpp>
#include <touchgfx/mixins/ClickListener.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <touchgfx/events/DragEvent.hpp>

class DeleteTapOnlyScrollList : public touchgfx::ClickListener<touchgfx::ScrollList>
{
public:
    DeleteTapOnlyScrollList() : dragDistance(0U) {}
    virtual void handleClickEvent(const touchgfx::ClickEvent& event)
    {
        if (event.getType() == touchgfx::ClickEvent::PRESSED) dragDistance = 0U;
        touchgfx::ScrollList::handleClickEvent(event);
        if (event.getType() == touchgfx::ClickEvent::RELEASED && dragDistance <= 8U &&
            clickAction && clickAction->isValid()) clickAction->execute(*this, event);
    }
    virtual void handleDragEvent(const touchgfx::DragEvent& event)
    {
        int16_t dx = event.getDeltaX();
        int16_t dy = event.getDeltaY();
        uint16_t movement = (uint16_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
        dragDistance = (uint32_t)dragDistance + movement > 0xFFFFU ? 0xFFFFU :
                       (uint16_t)(dragDistance + movement);
        touchgfx::ScrollList::handleDragEvent(event);
    }
private:
    uint16_t dragDistance;
};

class FileDeleteScreenView : public FileDeleteScreenViewBase
{
public:
    FileDeleteScreenView();
    virtual ~FileDeleteScreenView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
    void refreshLibrary();
private:
    static const int VISIBLE_FILE_ROWS = 8;
    DeleteTapOnlyScrollList fileList;
    touchgfx::DrawableListItems<FileItemHandler, VISIBLE_FILE_ROWS> fileRows;
    bool listInitialized;
    bool folderMode;
    bool awaitingDelete;
    uint16_t pendingFile;
    uint16_t resultTicks;

    touchgfx::TextAreaWithOneWildcard listStatus;
    touchgfx::Unicode::UnicodeChar listStatusBuffer[96];
    touchgfx::TextAreaWithOneWildcard storageLabel;
    touchgfx::Unicode::UnicodeChar storageLabelBuffer[20];
    touchgfx::TextAreaWithOneWildcard folderPathLabel;
    touchgfx::Unicode::UnicodeChar folderPathBuffer[64];

    touchgfx::Callback<FileDeleteScreenView, const touchgfx::AbstractButton&> buttonCallback;
    touchgfx::Callback<FileDeleteScreenView, const touchgfx::ScrollList&, const touchgfx::ClickEvent&> listClickCallback;
    touchgfx::Callback<FileDeleteScreenView, touchgfx::DrawableListItemsInterface*, int16_t, int16_t> updateItemCallback;

    void buttonHandler(const touchgfx::AbstractButton& src);
    void listClickHandler(const touchgfx::ScrollList& src, const touchgfx::ClickEvent& event);
    void updateItem(touchgfx::DrawableListItemsInterface* items, int16_t drawableIndex, int16_t itemIndex);
    void updateLabels();
    void showPrompt(uint16_t index);
    void hidePrompt();
    void showResult(const char* text);
};

#endif // FILEDELETESCREENVIEW_HPP
