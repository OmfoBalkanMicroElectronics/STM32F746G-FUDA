#ifndef FILEITEMHANDLER_HPP
#define FILEITEMHANDLER_HPP

#include <gui_generated/containers/FileItemHandlerBase.hpp>

class FileItemHandler : public FileItemHandlerBase
{
public:
    FileItemHandler();
    virtual ~FileItemHandler() {}

    virtual void initialize();
    void setFileName(const touchgfx::Unicode::UnicodeChar* name);
protected:
};

#endif // FILEITEMHANDLER_HPP
