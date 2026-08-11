#ifndef NETWORKSETTINGSINDEVELOPMENTPRESENTER_HPP
#define NETWORKSETTINGSINDEVELOPMENTPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class networkSettingsInDevelopmentView;

class networkSettingsInDevelopmentPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    networkSettingsInDevelopmentPresenter(networkSettingsInDevelopmentView& v);

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

    virtual ~networkSettingsInDevelopmentPresenter() {}

private:
    networkSettingsInDevelopmentPresenter();

    networkSettingsInDevelopmentView& view;
};

#endif // NETWORKSETTINGSINDEVELOPMENTPRESENTER_HPP
