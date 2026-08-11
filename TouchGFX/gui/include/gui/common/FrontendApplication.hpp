#ifndef FRONTENDAPPLICATION_HPP
#define FRONTENDAPPLICATION_HPP

#include <gui_generated/common/FrontendApplicationBase.hpp>
#include "display_manager.h"

class FrontendHeap;

using namespace touchgfx;

class FrontendApplication : public FrontendApplicationBase
{
public:
    FrontendApplication(Model& m, FrontendHeap& heap);
    virtual ~FrontendApplication() { }

    void gotoSWEqualizerUIScreenCoverTransitionWest();
    void gotoFileDeleteScreenScreenCoverTransitionWest();

    virtual void handleTickEvent()
    {
        model.tick();
        DisplayManager_Tick();
        FrontendApplicationBase::handleTickEvent();
    }
private:
    void gotoSWEqualizerUIScreenCoverTransitionWestImpl();
    void gotoFileDeleteScreenScreenCoverTransitionWestImpl();
    touchgfx::Callback<FrontendApplication> swEqualizerTransitionCallback;
    touchgfx::Callback<FrontendApplication> fileDeleteTransitionCallback;
};

#endif // FRONTENDAPPLICATION_HPP
