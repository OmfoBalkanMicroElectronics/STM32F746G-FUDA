#ifndef ABOUTSECTIONVIEW_HPP
#define ABOUTSECTIONVIEW_HPP

#include <gui_generated/aboutsection_screen/aboutSectionViewBase.hpp>
#include <gui/aboutsection_screen/aboutSectionPresenter.hpp>

class aboutSectionView : public aboutSectionViewBase
{
public:
    aboutSectionView();
    virtual ~aboutSectionView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // ABOUTSECTIONVIEW_HPP
