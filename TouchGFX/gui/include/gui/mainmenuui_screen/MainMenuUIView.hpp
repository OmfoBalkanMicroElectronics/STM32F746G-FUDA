#ifndef MAINMENUUIVIEW_HPP
#define MAINMENUUIVIEW_HPP

#include <gui_generated/mainmenuui_screen/MainMenuUIViewBase.hpp>
#include <gui/mainmenuui_screen/MainMenuUIPresenter.hpp>

class MainMenuUIView : public MainMenuUIViewBase
{
public:
    MainMenuUIView();
    virtual ~MainMenuUIView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // MAINMENUUIVIEW_HPP
