#ifndef INTERNETDIAGVIEW_HPP
#define INTERNETDIAGVIEW_HPP

#include <gui_generated/internetdiag_screen/internetDiagViewBase.hpp>
#include <gui/internetdiag_screen/internetDiagPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Callback.hpp>

class internetDiagView : public internetDiagViewBase
{
public:
    internetDiagView();
    virtual ~internetDiagView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
protected:
    touchgfx::TextAreaWithOneWildcard diagEth;
    touchgfx::TextAreaWithOneWildcard diagIp;
    touchgfx::TextAreaWithOneWildcard diagDown;
    touchgfx::TextAreaWithOneWildcard diagUp;
    touchgfx::Unicode::UnicodeChar diagEthBuffer[64];
    touchgfx::Unicode::UnicodeChar diagIpBuffer[40];
    touchgfx::Unicode::UnicodeChar diagDownBuffer[40];
    touchgfx::Unicode::UnicodeChar diagUpBuffer[40];
    touchgfx::Callback<internetDiagView, const touchgfx::AbstractButton&> speedButtonCallback;
    uint32_t lastNetworkRevision;
    uint8_t pollTick;
    void speedButtonHandler(const touchgfx::AbstractButton& source);
    void updateDiagnostics(const NetworkSnapshot& snapshot);
};

#endif // INTERNETDIAGVIEW_HPP
