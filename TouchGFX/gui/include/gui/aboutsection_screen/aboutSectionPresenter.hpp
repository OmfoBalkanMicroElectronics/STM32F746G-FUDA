#ifndef ABOUTSECTIONPRESENTER_HPP
#define ABOUTSECTIONPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class aboutSectionView;

class aboutSectionPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    aboutSectionPresenter(aboutSectionView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~aboutSectionPresenter() {}

private:
    aboutSectionPresenter();

    aboutSectionView& view;
};

#endif // ABOUTSECTIONPRESENTER_HPP
