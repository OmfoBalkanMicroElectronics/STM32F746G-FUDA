#ifndef FILEBROWSERSCREENVIEW_HPP
#define FILEBROWSERSCREENVIEW_HPP

#include <gui_generated/filebrowserscreen_screen/FileBrowserScreenViewBase.hpp>
#include <gui/filebrowserscreen_screen/FileBrowserScreenPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/containers/scrollers/ScrollList.hpp>
#include <touchgfx/containers/scrollers/DrawableList.hpp>
#include <touchgfx/mixins/ClickListener.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <touchgfx/events/DragEvent.hpp>

/* TouchGFX emits a RELEASED click after a drag. The stock ClickListener calls
   its action for that release as well, which selects the row where scrolling
   started. Preserve ScrollList's own gesture handling, but only forward a
   click action when the accumulated finger movement stayed within tap jitter. */
class TapOnlyScrollList : public touchgfx::ClickListener<touchgfx::ScrollList>
{
public:
    TapOnlyScrollList() : dragDistance(0U) {}

    virtual void handleClickEvent(const touchgfx::ClickEvent& event)
    {
        if (event.getType() == touchgfx::ClickEvent::PRESSED) dragDistance = 0U;
        touchgfx::ScrollList::handleClickEvent(event);
        if (event.getType() == touchgfx::ClickEvent::RELEASED &&
            dragDistance <= TAP_JITTER_PIXELS && clickAction && clickAction->isValid())
        {
            clickAction->execute(*this, event);
        }
    }

    virtual void handleDragEvent(const touchgfx::DragEvent& event)
    {
        int16_t dx = event.getDeltaX();
        int16_t dy = event.getDeltaY();
        uint16_t movement = (uint16_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
        if ((uint32_t)dragDistance + movement > 0xFFFFU) dragDistance = 0xFFFFU;
        else dragDistance = (uint16_t)(dragDistance + movement);
        touchgfx::ScrollList::handleDragEvent(event);
    }

private:
    static const uint16_t TAP_JITTER_PIXELS = 8U;
    uint16_t dragDistance;
};

class FileBrowserScreenView : public FileBrowserScreenViewBase
{
public:
    FileBrowserScreenView();
    virtual ~FileBrowserScreenView() {}
   
    virtual void setupScreen();
    virtual void handleTickEvent();
    void refreshLibrary();

private:
    // Array to hold pointers to all 12 song items for easy iteration
    static const int VISIBLE_SONG_ROWS = 8;
    TapOnlyScrollList songList;
    touchgfx::DrawableListItems<SongItemHandler, VISIBLE_SONG_ROWS> songRows;
    bool songListInitialized;

    // Standalone library state. This is deliberately outside every song row.
    touchgfx::TextAreaWithOneWildcard libraryStatus;
    touchgfx::Unicode::UnicodeChar libraryStatusBuffer[64];

    // Dynamic label for the Designer SD/USB selector.
    touchgfx::TextAreaWithOneWildcard storageLabel;
    touchgfx::Unicode::UnicodeChar storageLabelBuffer[20];
    touchgfx::TextAreaWithOneWildcard folderPathLabel;
    touchgfx::Unicode::UnicodeChar folderPathBuffer[64];
    bool folderMode;
    uint8_t userButtonRaw;
    uint8_t userButtonStable;
    uint8_t userButtonDebounce;

    // Callback for when a song item is clicked
    touchgfx::Callback<FileBrowserScreenView, const SongItemHandler&> songItemClickCallback;
    touchgfx::Callback<FileBrowserScreenView, const touchgfx::AbstractButton&> storageButtonCallback;
    touchgfx::Callback<FileBrowserScreenView, const touchgfx::AbstractButton&> folderButtonCallback;
    touchgfx::Callback<FileBrowserScreenView, const touchgfx::ScrollList&, const touchgfx::ClickEvent&> songListClickCallback;
    touchgfx::Callback<FileBrowserScreenView, touchgfx::DrawableListItemsInterface*, int16_t, int16_t> updateSongItemCallback;
    void updateSongItem(touchgfx::DrawableListItemsInterface* items, int16_t drawableIndex, int16_t itemIndex);
    void songItemClickCallbackHandler(const SongItemHandler& src);
    void songListClickCallbackHandler(const touchgfx::ScrollList& src, const touchgfx::ClickEvent& event);
    void storageButtonHandler(const touchgfx::AbstractButton& src);
    void folderButtonHandler(const touchgfx::AbstractButton& src);
    void updateStorageLabel();
    void updateFolderLabel();
};

#endif // FILEBROWSERSCREENVIEW_HPP
