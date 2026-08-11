#include <gui/filedeletescreen_screen/FileDeleteScreenView.hpp>
#include <touchgfx/Color.hpp>
#include <touchgfx/Unicode.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <string.h>

FileDeleteScreenView::FileDeleteScreenView() :
    listInitialized(false),
    folderMode(false),
    awaitingDelete(false),
    pendingFile(0U),
    resultTicks(0U),
    buttonCallback(this, &FileDeleteScreenView::buttonHandler),
    listClickCallback(this, &FileDeleteScreenView::listClickHandler),
    updateItemCallback(this, &FileDeleteScreenView::updateItem)
{
    fileList.setPosition(52, 6, 300, 266);
    fileList.setHorizontal(false);
    fileList.setCircular(false);
    fileList.setDrawableSize(45, 0);
    fileList.setWindowSize(6);
    fileList.setSnapping(false);
    fileList.setClickAction(listClickCallback);
    add(fileList);

    listStatus.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    listStatus.setWildcard(listStatusBuffer);
    listStatus.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    listStatus.setPosition(70, 100, 300, 45);
    listStatusBuffer[0] = 0U;
    add(listStatus);

    storageLabel.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    storageLabel.setWildcard(storageLabelBuffer);
    storageLabel.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    storageLabel.setPosition(378, 84, 100, 28);
    storageLabelBuffer[0] = 0U;
    add(storageLabel);

    folderPathLabel.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    folderPathLabel.setWildcard(folderPathBuffer);
    folderPathLabel.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    folderPathLabel.setPosition(357, 225, 121, 30);
    folderPathBuffer[0] = 0U;
    add(folderPathLabel);
}

void FileDeleteScreenView::setupScreen()
{
    FileDeleteScreenViewBase::setupScreen();

    ScrollContainerforFiles.setVisible(false);
    ScrollContainerforFiles.setTouchable(false);
    usb_sd_select_text.setVisible(false);
    currentFolder_text.setVisible(false);
    usb_sdSelect.setAction(buttonCallback);
    FolderSelect_button.setAction(buttonCallback);
    deleteConfirm.setAction(buttonCallback);
    deleteDeny.setAction(buttonCallback);

    if (!listInitialized)
    {
        for (int i = 0; i < VISIBLE_FILE_ROWS; i++)
        {
            fileRows[i].initialize();
            fileRows[i].setTouchable(false);
        }
        fileList.setDrawables(fileRows, updateItemCallback);
        listInitialized = true;
    }

    /* Designer places these widgets before Background. Reinsert them at the
       top so the confirmation dialog is actually visible and touchable. */
    remove(saveConfirmationPrompt);
    remove(deleteDeny);
    remove(deleteConfirm);
    add(saveConfirmationPrompt);
    add(deleteDeny);
    add(deleteConfirm);
    hidePrompt();

    refreshLibrary();
}

void FileDeleteScreenView::tearDownScreen()
{
    FileDeleteScreenViewBase::tearDownScreen();
}

void FileDeleteScreenView::handleTickEvent()
{
    if (resultTicks != 0U)
    {
        resultTicks--;
        if (resultTicks == 0U)
        {
            awaitingDelete = false;
            refreshLibrary();
        }
    }
}

void FileDeleteScreenView::updateLabels()
{
    MediaSnapshot snapshot;
    char folder[64];
    presenter->mediaSnapshot(snapshot);
    presenter->currentFolder(folder, sizeof(folder));
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(
        snapshot.storage == MEDIA_STORAGE_USB ? "USB MSC" : "SD CARD"), storageLabelBuffer, 20);
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(folder), folderPathBuffer, 64);
    storageLabel.invalidate();
    folderPathLabel.invalidate();
}

void FileDeleteScreenView::refreshLibrary()
{
    uint16_t count;
    updateLabels();

    if (awaitingDelete)
    {
        MediaDeleteSnapshot snapshot;
        presenter->deleteSnapshot(snapshot);
        if (snapshot.state == MEDIA_DELETE_SUCCESS || snapshot.state == MEDIA_DELETE_ERROR)
        {
            showResult(snapshot.status);
            resultTicks = 60U;
        }
        return;
    }

    count = folderMode ? presenter->folderCount() : presenter->fileCount();
    fileList.setNumberOfItems((int16_t)count);
    fileList.setVisible(count != 0U);
    fileList.setTouchable(count != 0U);
    listStatus.setVisible(count == 0U);
    Return.setTouchable(true);

    if (count == 0U)
    {
        char status[96];
        if (folderMode)
            strcpy(status, "Klasör bulunamadı");
        else
            presenter->mediaStatus(status, sizeof(status));
        touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(status), listStatusBuffer, 96);
        listStatus.invalidate();
    }
    fileList.invalidate();
}

void FileDeleteScreenView::updateItem(touchgfx::DrawableListItemsInterface* items,
                                      int16_t drawableIndex, int16_t itemIndex)
{
    FileItemHandler* row = static_cast<FileItemHandler*>(items->getDrawable(drawableIndex));
    char name[MEDIA_TRACK_NAME_SIZE];
    touchgfx::Unicode::UnicodeChar buffer[MEDIA_TRACK_NAME_SIZE];
    uint16_t count = folderMode ? presenter->folderCount() : presenter->fileCount();
    bool available = itemIndex >= 0 && (uint16_t)itemIndex < count &&
                     (folderMode ? presenter->folderName((uint16_t)itemIndex, name, sizeof(name)) :
                                   presenter->fileName((uint16_t)itemIndex, name, sizeof(name)));
    row->setVisible(available);
    row->setTouchable(false);
    if (available)
    {
        touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(name), buffer, MEDIA_TRACK_NAME_SIZE);
        row->setFileName(buffer);
    }
    row->invalidate();
}

void FileDeleteScreenView::listClickHandler(const touchgfx::ScrollList& src,
                                            const touchgfx::ClickEvent& event)
{
    (void)src;
    if (event.getType() != touchgfx::ClickEvent::RELEASED) return;
    uint16_t count = folderMode ? presenter->folderCount() : presenter->fileCount();
    int16_t y = event.getY();
    for (int16_t i = 0; i < VISIBLE_FILE_ROWS; i++)
    {
        if (!fileRows[i].isVisible()) continue;
        int16_t top = fileRows[i].getY();
        if (y >= top && y < top + fileRows[i].getHeight())
        {
            int16_t itemIndex = fileList.getItem(i);
            if (itemIndex >= 0 && (uint16_t)itemIndex < count)
            {
                if (folderMode)
                {
                    folderMode = false;
                    fileList.setVisible(false);
                    presenter->selectFolder((uint16_t)itemIndex);
                }
                else
                {
                    showPrompt((uint16_t)itemIndex);
                }
            }
            return;
        }
    }
}

void FileDeleteScreenView::showPrompt(uint16_t index)
{
    pendingFile = index;
    fileList.setVisible(false);
    fileList.setTouchable(false);
    listStatus.setVisible(false);
    saveConfirmationPrompt.setVisible(true);
    deleteDeny.setVisible(true);
    deleteConfirm.setVisible(true);
    deleteDeny.setTouchable(true);
    deleteConfirm.setTouchable(true);
    Return.setTouchable(false);
    saveConfirmationPrompt.invalidate();
    deleteDeny.invalidate();
    deleteConfirm.invalidate();
}

void FileDeleteScreenView::showResult(const char* text)
{
    hidePrompt();
    fileList.setVisible(false);
    fileList.setTouchable(false);
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), listStatusBuffer, 96);
    listStatus.setVisible(true);
    listStatus.invalidate();
}

void FileDeleteScreenView::hidePrompt()
{
    /* Invalidate while the widgets are still visible. If visibility is
       cleared first, TouchGFX no longer contributes their old rectangles to
       the dirty region and the Evet/Hayır glyph pixels remain in framebuffer. */
    saveConfirmationPrompt.invalidate();
    deleteDeny.invalidate();
    deleteConfirm.invalidate();
    saveConfirmationPrompt.setVisible(false);
    deleteDeny.setVisible(false);
    deleteConfirm.setVisible(false);
    deleteDeny.setTouchable(false);
    deleteConfirm.setTouchable(false);
    invalidate();
}

void FileDeleteScreenView::buttonHandler(const touchgfx::AbstractButton& src)
{
    if (&src == &usb_sdSelect)
    {
        if (awaitingDelete) return;
        folderMode = false;
        presenter->toggleStorage();
    }
    else if (&src == &FolderSelect_button)
    {
        if (awaitingDelete || deleteConfirm.isVisible()) return;
        folderMode = !folderMode;
        refreshLibrary();
    }
    else if (&src == &deleteDeny)
    {
        hidePrompt();
        refreshLibrary();
    }
    else if (&src == &deleteConfirm)
    {
        awaitingDelete = true;
        showResult("Siliniyor...");
        presenter->deleteFile(pendingFile);
    }
}
