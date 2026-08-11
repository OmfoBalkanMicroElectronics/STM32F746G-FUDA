#ifndef NETWORKSETTINGSINDEVELOPMENTVIEW_HPP
#define NETWORKSETTINGSINDEVELOPMENTVIEW_HPP

#include <gui_generated/networksettingsindevelopment_screen/networkSettingsInDevelopmentViewBase.hpp>
#include <gui/networksettingsindevelopment_screen/networkSettingsInDevelopmentPresenter.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class networkSettingsInDevelopmentView : public networkSettingsInDevelopmentViewBase
{
public:
    networkSettingsInDevelopmentView();
    virtual ~networkSettingsInDevelopmentView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
protected:
    touchgfx::Box statusBoxes[3];
    touchgfx::TextAreaWithOneWildcard statusTexts[3];
    touchgfx::Unicode::UnicodeChar linkBuffer[40];
    touchgfx::Unicode::UnicodeChar ipBuffer[40];
    touchgfx::Unicode::UnicodeChar pingBuffer[48];
    uint32_t networkRevision;
    uint8_t networkTick;
};

#endif // NETWORKSETTINGSINDEVELOPMENTVIEW_HPP
