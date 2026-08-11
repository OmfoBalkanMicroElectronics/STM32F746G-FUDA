#include <gui/common/FrontendApplication.hpp>
#include <gui/common/FrontendHeap.hpp>
#include <gui/swequalizerui_screen/SWEqualizerUIView.hpp>
#include <gui/swequalizerui_screen/SWEqualizerUIPresenter.hpp>
#include <gui/filedeletescreen_screen/FileDeleteScreenView.hpp>
#include <gui/filedeletescreen_screen/FileDeleteScreenPresenter.hpp>
#include <touchgfx/transitions/CoverTransition.hpp>

FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : FrontendApplicationBase(m, heap),
      swEqualizerTransitionCallback(),
      fileDeleteTransitionCallback()
{
    DisplayManager_Init();
}

void FrontendApplication::gotoFileDeleteScreenScreenCoverTransitionWest()
{
    fileDeleteTransitionCallback = touchgfx::Callback<FrontendApplication>(
        this, &FrontendApplication::gotoFileDeleteScreenScreenCoverTransitionWestImpl);
    pendingScreenTransitionCallback = &fileDeleteTransitionCallback;
}

void FrontendApplication::gotoFileDeleteScreenScreenCoverTransitionWestImpl()
{
    touchgfx::makeTransition<FileDeleteScreenView, FileDeleteScreenPresenter,
                             touchgfx::CoverTransition<touchgfx::WEST>, Model>(
        &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoSWEqualizerUIScreenCoverTransitionWest()
{
    swEqualizerTransitionCallback = touchgfx::Callback<FrontendApplication>(
        this, &FrontendApplication::gotoSWEqualizerUIScreenCoverTransitionWestImpl);
    pendingScreenTransitionCallback = &swEqualizerTransitionCallback;
}

void FrontendApplication::gotoSWEqualizerUIScreenCoverTransitionWestImpl()
{
    touchgfx::makeTransition<SWEqualizerUIView, SWEqualizerUIPresenter,
                             touchgfx::CoverTransition<touchgfx::WEST>, Model>(
        &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}
