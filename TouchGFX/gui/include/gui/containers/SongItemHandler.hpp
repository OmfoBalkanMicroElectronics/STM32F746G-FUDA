#ifndef SONGITEMHANDLER_HPP
#define SONGITEMHANDLER_HPP

#include <gui_generated/containers/SongItemHandlerBase.hpp>
#include <touchgfx/Callback.hpp>
#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include "media_player.h"

class SongItemHandler : public SongItemHandlerBase
{
public:
    SongItemHandler();
    virtual ~SongItemHandler() {}
    virtual void initialize();

    // Method to update the song name dynamically
    void setSongName(const touchgfx::Unicode::UnicodeChar* name);
    void tickMarquee();
    
    // Method to update the song image
    void setSongImage(touchgfx::BitmapId bitmapId);

    // Set a callback so the parent screen knows when this item is clicked
    void setClickAction(touchgfx::GenericCallback<const SongItemHandler&>& callback) 
    { 
        clickCallback = &callback; 
    }

protected:
    // FIXED: The handler now accepts the AbstractButtonContainer reference
    void flexButton1ClickedHandler(const touchgfx::AbstractButtonContainer& src);

private:
    static const uint16_t SONG_NAME_SIZE = MEDIA_TRACK_NAME_SIZE;
    static const uint16_t MARQUEE_BUFFER_SIZE = (SONG_NAME_SIZE * 2U) + 8U;
    touchgfx::Container textClip;
    touchgfx::TextAreaWithOneWildcard marqueeText;
    touchgfx::Unicode::UnicodeChar marqueeBuffer[MARQUEE_BUFFER_SIZE];
    bool marqueeActive;
    int16_t marqueeX;
    uint16_t marqueeDelay;
    uint16_t marqueeTextWidth;
    uint16_t marqueeLoopWidth;

    touchgfx::GenericCallback<const SongItemHandler&>* clickCallback;
    
    // FIXED: The Callback template now explicitly defines the argument type
    touchgfx::Callback<SongItemHandler, const touchgfx::AbstractButtonContainer&> flexButtonCallback;
};

#endif // SONGITEMHANDLER_HPP
