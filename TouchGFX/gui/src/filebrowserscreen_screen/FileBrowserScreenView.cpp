#include <gui/filebrowserscreen_screen/FileBrowserScreenView.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include "stm32746g_discovery.h"

FileBrowserScreenView::FileBrowserScreenView() :
    songListInitialized(false),
    folderMode(false),
    userButtonRaw(0U),
    userButtonStable(0U),
    userButtonDebounce(0U),
    songItemClickCallback(this, &FileBrowserScreenView::songItemClickCallbackHandler),
    storageButtonCallback(this, &FileBrowserScreenView::storageButtonHandler),
    folderButtonCallback(this, &FileBrowserScreenView::folderButtonHandler),
    songListClickCallback(this, &FileBrowserScreenView::songListClickCallbackHandler),
    updateSongItemCallback(this, &FileBrowserScreenView::updateSongItem)
{
    /* The Designer still owns its original 12 placeholder rows, but the user
       code uses this virtualized ScrollList instead. Only eight row widgets are
       allocated and TouchGFX recycles them for the complete media library. */
    songList.setPosition(81, 6, 300, 266);
    songList.setHorizontal(false);
    songList.setCircular(false);
    songList.setDrawableSize(45, 0);
    songList.setWindowSize(6);
    songList.setSnapping(false);
    songList.setClickAction(songListClickCallback);
    add(songList);

    libraryStatus.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    libraryStatus.setWildcard(libraryStatusBuffer);
    libraryStatus.setColor(touchgfx::Color::getColorFromRGB(232, 246, 251));
    libraryStatusBuffer[0] = 0;
    add(libraryStatus);

    storageLabel.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    storageLabel.setWildcard(storageLabelBuffer);
    storageLabel.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    storageLabel.setPosition(378, 84, 100, 28);
    storageLabelBuffer[0] = 0;
    add(storageLabel);

    folderPathLabel.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    folderPathLabel.setWildcard(folderPathBuffer);
    folderPathLabel.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    folderPathLabel.setPosition(357, 225, 121, 30);
    folderPathBuffer[0] = 0;
    add(folderPathLabel);
}

void FileBrowserScreenView::setupScreen()
{
    FileBrowserScreenViewBase::setupScreen();

    /* Hide the Designer placeholders permanently. They can remain in the
       generated files without imposing a track-count limit on the UI. */
    ScrollContainerforSongs.setVisible(false);
    ScrollContainerforSongs.setTouchable(false);

    usb_sd_select_text.setVisible(false);
    currentFolder_text.setVisible(false);
    usb_sdSelect.setAction(storageButtonCallback);
    usb_sdSelect.setTouchable(true);
    FolderSelect_button.setAction(folderButtonCallback);
    FolderSelect_button.setTouchable(true);
    updateStorageLabel();
    updateFolderLabel();

    BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_GPIO);
    userButtonRaw = BSP_PB_GetState(BUTTON_KEY) != 0U ? 1U : 0U;
    userButtonStable = userButtonRaw;
    userButtonDebounce = 0U;

    if (!songListInitialized)
    {
        for (int i = 0; i < VISIBLE_SONG_ROWS; i++)
        {
            songRows[i].initialize();
            /* The ScrollList, not the recycled row, must own touch events so
               vertical drags reach ScrollBase::handleDragEvent(). */
            songRows[i].setTouchable(false);
        }
        songList.setDrawables(songRows, updateSongItemCallback);
        songListInitialized = true;
    }

    refreshLibrary();
}

void FileBrowserScreenView::handleTickEvent()
{
    const uint8_t raw = BSP_PB_GetState(BUTTON_KEY) != 0U ? 1U : 0U;
    for (int i = 0; i < VISIBLE_SONG_ROWS; i++)
    {
        if (songRows[i].isVisible()) songRows[i].tickMarquee();
    }

    if (raw != userButtonRaw)
    {
        userButtonRaw = raw;
        userButtonDebounce = 0U;
    }
    else if (userButtonDebounce < 3U)
    {
        userButtonDebounce++;
        if (userButtonDebounce == 3U && userButtonStable != raw)
        {
            userButtonStable = raw;
            if (raw != 0U) application().gotoFileDeleteScreenScreenCoverTransitionWest();
        }
    }
}

void FileBrowserScreenView::refreshLibrary()
{
    const uint16_t count = folderMode ? presenter->folderCount() : presenter->trackCount();
    updateStorageLabel();
    updateFolderLabel();

    /* Invalidate the old geometry before moving the status widget. When an
       empty USB status at x=75 changes to the small SD track count at x=4,
       invalidating only the new rectangle leaves a thin strip of old text
       visible just to the left of the song list. */
    libraryStatus.invalidate();

    if (count != 0U)
    {
        if (folderMode)
            touchgfx::Unicode::snprintf(libraryStatusBuffer, 64, "%u DIR", count);
        else
            touchgfx::Unicode::snprintf(libraryStatusBuffer, 64, "%u", count);
        libraryStatus.setPosition(4, 8, 70, 30);
        songList.setVisible(true);
        songList.setTouchable(true);
    }
    else
    {
        char status[64];
        presenter->mediaStatus(status, sizeof(status));
        touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(status), libraryStatusBuffer, 64);
        libraryStatus.setPosition(75, 105, 330, 35);
        songList.setVisible(false);
        songList.setTouchable(false);
    }

    songList.setNumberOfItems((int16_t)count);
    libraryStatus.invalidate();
    songList.invalidate();
}

void FileBrowserScreenView::updateStorageLabel()
{
    MediaSnapshot snapshot;
    presenter->mediaSnapshot(snapshot);
    const char* label = snapshot.storage == MEDIA_STORAGE_USB ? "USB MSC" : "SD CARD";
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(label), storageLabelBuffer, 20);
    storageLabel.invalidate();
}

void FileBrowserScreenView::updateFolderLabel()
{
    char path[64];
    presenter->currentFolder(path, sizeof(path));
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(path), folderPathBuffer, 64);
    folderPathLabel.invalidate();
}

void FileBrowserScreenView::storageButtonHandler(const touchgfx::AbstractButton& src)
{
    if (&src != &usb_sdSelect) return;
    folderMode = false;
    presenter->toggleStorage();
}

void FileBrowserScreenView::folderButtonHandler(const touchgfx::AbstractButton& src)
{
    if (&src != &FolderSelect_button) return;
    folderMode = !folderMode;
    refreshLibrary();
}

void FileBrowserScreenView::updateSongItem(touchgfx::DrawableListItemsInterface* items,
                                           int16_t drawableIndex,
                                           int16_t itemIndex)
{
    SongItemHandler* row = static_cast<SongItemHandler*>(items->getDrawable(drawableIndex));
    char name[MEDIA_TRACK_NAME_SIZE];
    touchgfx::Unicode::UnicodeChar buffer[MEDIA_TRACK_NAME_SIZE];
    const uint16_t count = folderMode ? presenter->folderCount() : presenter->trackCount();
    const bool available = itemIndex >= 0 &&
                           (uint16_t)itemIndex < count &&
                           (folderMode ? presenter->folderName((uint16_t)itemIndex, name, sizeof(name)) :
                                         presenter->trackName((uint16_t)itemIndex, name, sizeof(name)));

    row->setVisible(available);
    row->setTouchable(false);
    if (available)
    {
        touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(name), buffer, MEDIA_TRACK_NAME_SIZE);
        row->setSongName(buffer);
    }
    row->invalidate();
}

void FileBrowserScreenView::songListClickCallbackHandler(const touchgfx::ScrollList& src,
                                                         const touchgfx::ClickEvent& event)
{
    (void)src;
    if (event.getType() != touchgfx::ClickEvent::RELEASED) return;

    const uint16_t count = folderMode ? presenter->folderCount() : presenter->trackCount();
    const int16_t y = event.getY();
    for (int16_t i = 0; i < VISIBLE_SONG_ROWS; i++)
    {
        if (!songRows[i].isVisible()) continue;
        const int16_t top = songRows[i].getY();
        if (y >= top && y < top + songRows[i].getHeight())
        {
            const int16_t itemIndex = songList.getItem(i);
            if (itemIndex >= 0 && (uint16_t)itemIndex < count)
            {
                if (folderMode)
                {
                    folderMode = false;
                    songList.setVisible(false);
                    presenter->selectFolder((uint16_t)itemIndex);
                }
                else
                {
                    presenter->songSelected((uint16_t)itemIndex);
                    application().gotoMainPlayerUIScreenCoverTransitionEast();
                }
            }
            return;
        }
    }
}

void FileBrowserScreenView::songItemClickCallbackHandler(const SongItemHandler& src)
{
    const uint16_t count = folderMode ? presenter->folderCount() : presenter->trackCount();

    for (int i = 0; i < VISIBLE_SONG_ROWS; i++)
    {
        if (&songRows[i] == &src)
        {
            const int16_t itemIndex = songList.getItem((int16_t)i);
            if (itemIndex >= 0 && (uint16_t)itemIndex < count)
            {
                if (folderMode)
                {
                    folderMode = false;
                    songList.setVisible(false);
                    presenter->selectFolder((uint16_t)itemIndex);
                }
                else
                {
                    presenter->songSelected((uint16_t)itemIndex);
                    application().gotoMainPlayerUIScreenCoverTransitionEast();
                }
            }
            break;
        }
    }
}
