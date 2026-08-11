#include <gui/containers/FileItemHandler.hpp>

FileItemHandler::FileItemHandler()
{

}

void FileItemHandler::initialize()
{
    FileItemHandlerBase::initialize();
    flexButton1.setTouchable(false);
}

void FileItemHandler::setFileName(const touchgfx::Unicode::UnicodeChar* name)
{
    touchgfx::Unicode::strncpy(textfileBuffer, name, TEXTFILE_SIZE);
    textfileBuffer[TEXTFILE_SIZE - 1U] = 0U;
    textfile.resizeToCurrentText();
    textfile.invalidate();
}
