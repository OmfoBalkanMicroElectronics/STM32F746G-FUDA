#include <gui/networksettingsindevelopment_screen/networkSettingsInDevelopmentView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>
#include "network_manager.h"

networkSettingsInDevelopmentView::networkSettingsInDevelopmentView()
    : networkRevision(0U), networkTick(0U)
{
}

void networkSettingsInDevelopmentView::setupScreen()
{
    networkSettingsInDevelopmentViewBase::setupScreen();
    touchgfx::Unicode::UnicodeChar* buffers[3] = {linkBuffer, ipBuffer, pingBuffer};
    const uint16_t capacities[3] = {40U, 40U, 48U};
    for (uint8_t i = 0U; i < 3U; i++)
    {
        statusBoxes[i].setPosition(45, (int16_t)(48 + i * 55), 390, 42);
        statusBoxes[i].setColor(touchgfx::Color::getColorFromRGB(5, 10, 24));
        statusBoxes[i].setAlpha(190U);
        add(statusBoxes[i]);

        statusTexts[i].setPosition(58, (int16_t)(55 + i * 55), 364, 30);
        statusTexts[i].setColor(touchgfx::Color::getColorFromRGB(235, 245, 255));
        statusTexts[i].setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
        statusTexts[i].setWildcard(buffers[i]);
        add(statusTexts[i]);
        buffers[i][0] = 0;
        (void)capacities[i];
    }
    touchgfx::Unicode::snprintf(linkBuffer, 40, "Ethernet: Starting...");
    touchgfx::Unicode::snprintf(ipBuffer, 40, "IP: 0.0.0.0");
    touchgfx::Unicode::snprintf(pingBuffer, 48, "Ping Google: Waiting...");
}

void networkSettingsInDevelopmentView::tearDownScreen()
{
    networkSettingsInDevelopmentViewBase::tearDownScreen();
}

void networkSettingsInDevelopmentView::handleTickEvent()
{
    if (++networkTick < 15U) return;
    networkTick = 0U;

    NetworkSnapshot snapshot;
    NetworkManager_GetSnapshot(&snapshot);
    if (snapshot.revision == networkRevision) return;
    networkRevision = snapshot.revision;

    if (snapshot.linkUp != 0U)
        touchgfx::Unicode::snprintf(linkBuffer, 40, "Ethernet: Connected");
    else
        touchgfx::Unicode::snprintf(linkBuffer, 40, "Ethernet: Disconnected");

    touchgfx::Unicode::snprintf(ipBuffer, 40, "IP: ");
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(snapshot.ipAddress),
                                &ipBuffer[4], 36U);
    if (snapshot.linkUp == 0U)
        touchgfx::Unicode::snprintf(pingBuffer, 48, "Ping Google: No link");
    else if (snapshot.hasAddress == 0U)
        touchgfx::Unicode::snprintf(pingBuffer, 48, "Ping Google: Waiting for DHCP");
    else if (snapshot.pingState == NETWORK_PING_OK)
        touchgfx::Unicode::snprintf(pingBuffer, 48, "Ping Google: %u ms",
                                    (unsigned int)snapshot.pingMilliseconds);
    else if (snapshot.pingState == NETWORK_PING_TIMEOUT)
        touchgfx::Unicode::snprintf(pingBuffer, 48, "Ping Google: Timeout");
    else if (snapshot.pingState == NETWORK_PING_ERROR)
        touchgfx::Unicode::snprintf(pingBuffer, 48, "Ping Google: Send error");
    else
        touchgfx::Unicode::snprintf(pingBuffer, 48, "Ping Google: Pinging 8.8.8.8");

    for (uint8_t i = 0U; i < 3U; i++) statusTexts[i].invalidate();
}
