#include <gui/containers/SongItemHandler.hpp>
#include <texts/TextKeysAndLanguages.hpp>

#define SONG_ROW_MARQUEE_WIDTH 215
#define ROW_MARQUEE_START_PAUSE 75U
#define ROW_MARQUEE_GAP_SPACES  6U

// FIXED: Initialize the callback in the constructor
SongItemHandler::SongItemHandler() : 
    marqueeActive(false),
    marqueeX(0),
    marqueeDelay(ROW_MARQUEE_START_PAUSE),
    marqueeTextWidth(0),
    marqueeLoopWidth(0),
    clickCallback(nullptr),
    flexButtonCallback(this, &SongItemHandler::flexButton1ClickedHandler)
{
    marqueeBuffer[0] = 0;
}

void SongItemHandler::initialize()
{
    SongItemHandlerBase::initialize();
    
    // Assign the internal button action to our custom handler
    flexButton1.setAction(flexButtonCallback);
    /* A recycled row lives inside a ScrollList. If this full-row invisible
       button stays touchable it captures every gesture and the parent list
       never receives vertical drag events. Selection is handled by the
       ScrollList click listener instead. */
    flexButton1.setTouchable(false);

    // Replace the generated 40-character text with a clipped 64-character
    // field that can scroll independently inside this row.
    textsong.setVisible(false);
    textClip.setPosition(77, 10, SONG_ROW_MARQUEE_WIDTH, textsong.getHeight());
    marqueeText.setPosition(0, 0, SONG_ROW_MARQUEE_WIDTH, textsong.getHeight());
    marqueeText.setColor(textsong.getColor());
    marqueeText.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    marqueeText.setWildcard(marqueeBuffer);
    textClip.add(marqueeText);
    add(textClip);
}

void SongItemHandler::setSongName(const touchgfx::Unicode::UnicodeChar* name)
{
    textClip.invalidate();
    touchgfx::Unicode::UnicodeChar original[SONG_NAME_SIZE];
    touchgfx::Unicode::strncpy(original, name, SONG_NAME_SIZE);
    original[SONG_NAME_SIZE - 1U] = 0;
    touchgfx::Unicode::strncpy(marqueeBuffer, original, MARQUEE_BUFFER_SIZE);
    marqueeBuffer[MARQUEE_BUFFER_SIZE - 1U] = 0;
    marqueeText.resizeToCurrentText();
    marqueeTextWidth = marqueeText.getTextWidth();
    marqueeActive = marqueeTextWidth > textClip.getWidth();
    marqueeLoopWidth = 0U;

    if (marqueeActive)
    {
        uint16_t length = 0U;
        while (marqueeBuffer[length] != 0U && length < MARQUEE_BUFFER_SIZE - 1U) length++;
        for (uint16_t i = 0U; i < ROW_MARQUEE_GAP_SPACES && length < MARQUEE_BUFFER_SIZE - 1U; i++)
        {
            marqueeBuffer[length++] = (touchgfx::Unicode::UnicodeChar)' ';
        }
        for (uint16_t i = 0U; original[i] != 0U && length < MARQUEE_BUFFER_SIZE - 1U; i++)
        {
            marqueeBuffer[length++] = original[i];
        }
        marqueeBuffer[length] = 0U;
        marqueeText.resizeToCurrentText();
        marqueeLoopWidth = marqueeText.getTextWidth() - marqueeTextWidth;
    }
    marqueeX = 0;
    marqueeDelay = ROW_MARQUEE_START_PAUSE;
    marqueeText.moveTo(0, 0);
    textClip.invalidate();
}

void SongItemHandler::tickMarquee()
{
    if (!marqueeActive) return;
    if (marqueeDelay != 0U)
    {
        marqueeDelay--;
        return;
    }

    marqueeX--;
    if ((uint16_t)(-marqueeX) >= marqueeLoopWidth)
    {
        marqueeX = 0;
        marqueeDelay = ROW_MARQUEE_START_PAUSE;
    }
    marqueeText.moveTo(marqueeX, 0);
    textClip.invalidate();
}

void SongItemHandler::setSongImage(touchgfx::BitmapId bitmapId)
{
    songIcon.setBitmap(touchgfx::Bitmap(bitmapId));
    songIcon.invalidate();
}

// FIXED: Added the required argument to match the AbstractButtonContainer signature
void SongItemHandler::flexButton1ClickedHandler(const touchgfx::AbstractButtonContainer& src)
{
    if (clickCallback)
    {
        clickCallback->execute(*this);
    }
}
