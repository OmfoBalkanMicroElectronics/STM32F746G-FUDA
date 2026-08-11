#include <gui/internetdiag_screen/internetDiagView.hpp>
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <cstdio>
#include <cstring>

internetDiagView::internetDiagView()
    : speedButtonCallback(this, &internetDiagView::speedButtonHandler),
      lastNetworkRevision(0U), pollTick(0U)
{
    diagEthBuffer[0] = 0;
    diagIpBuffer[0] = 0;
    diagDownBuffer[0] = 0;
    diagUpBuffer[0] = 0;
}

void internetDiagView::setupScreen()
{
    internetDiagViewBase::setupScreen();
    ETHStatus.setVisible(false);
    IPAdressText.setVisible(false);
    DownMBPSText.setVisible(false);
    UpMBPSText.setVisible(false);

    diagEth.setPosition(0, 34, 475, 24);
    diagIp.setPosition(0, 58, 475, 24);
    diagDown.setPosition(0, 82, 475, 24);
    diagUp.setPosition(0, 106, 475, 24);
    diagEth.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    diagIp.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    diagDown.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    diagUp.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    diagEth.setWildcard(diagEthBuffer);
    diagIp.setWildcard(diagIpBuffer);
    diagDown.setWildcard(diagDownBuffer);
    diagUp.setWildcard(diagUpBuffer);
    const touchgfx::colortype black = touchgfx::Color::getColorFromRGB(0, 0, 0);
    diagEth.setColor(black);
    diagIp.setColor(black);
    diagDown.setColor(black);
    diagUp.setColor(black);
    add(diagEth);
    add(diagIp);
    add(diagDown);
    add(diagUp);
    StartSpeedTestButton.setAction(speedButtonCallback);

    NetworkSnapshot snapshot;
    std::memset(&snapshot, 0, sizeof(snapshot));
    presenter->networkSnapshot(snapshot);
    updateDiagnostics(snapshot);
}

void internetDiagView::tearDownScreen()
{
    internetDiagViewBase::tearDownScreen();
}

void internetDiagView::handleTickEvent()
{
    if (++pollTick < 15U) return;
    pollTick = 0U;
    NetworkSnapshot snapshot;
    presenter->networkSnapshot(snapshot);
    if (snapshot.revision != lastNetworkRevision) updateDiagnostics(snapshot);
}

void internetDiagView::speedButtonHandler(const touchgfx::AbstractButton& source)
{
    if (&source == &StartSpeedTestButton) presenter->startSpeedTest();
}

void internetDiagView::updateDiagnostics(const NetworkSnapshot& snapshot)
{
    char text[64];
    lastNetworkRevision = snapshot.revision;

    if (snapshot.limitWarning != 0U)
    {
        std::snprintf(text, sizeof(text), "UYARI: Sonuc %uM PHY limitini asti!", snapshot.linkMbps);
        diagEth.setColor(touchgfx::Color::getColorFromRGB(220, 20, 20));
    }
    else if (snapshot.linkUp != 0U)
    {
        std::snprintf(text, sizeof(text), "Ethernet: Bagli %uM %s", snapshot.linkMbps,
                      snapshot.fullDuplex != 0U ? "Full" : "Half");
        diagEth.setColor(touchgfx::Color::getColorFromRGB(0, 120, 40));
    }
    else
    {
        std::snprintf(text, sizeof(text), "Ethernet: Bagli degil");
        diagEth.setColor(touchgfx::Color::getColorFromRGB(220, 20, 20));
    }
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), diagEthBuffer, 64);

    std::snprintf(text, sizeof(text), "IP: %s | %s", snapshot.ipAddress, snapshot.diagnostic);
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), diagIpBuffer, 40);
    touchgfx::Unicode::snprintf(diagDownBuffer, 40, "Down: %u.%02u Mbps",
                               (unsigned)(snapshot.downCentiMbps / 100U),
                               (unsigned)(snapshot.downCentiMbps % 100U));
    touchgfx::Unicode::snprintf(diagUpBuffer, 40, "Up: %u.%02u Mbps",
                               (unsigned)(snapshot.upCentiMbps / 100U),
                               (unsigned)(snapshot.upCentiMbps % 100U));
    diagEth.invalidate();
    diagIp.invalidate();
    diagDown.invalidate();
    diagUp.invalidate();
}
