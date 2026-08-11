#ifndef INTERNETDIAGPRESENTER_HPP
#define INTERNETDIAGPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>
#include "network_manager.h"

using namespace touchgfx;

class internetDiagView;

class internetDiagPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    internetDiagPresenter(internetDiagView& v);

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
    void networkSnapshot(NetworkSnapshot& snapshot) const;
    void startSpeedTest();

    virtual ~internetDiagPresenter() {}

private:
    internetDiagPresenter();

    internetDiagView& view;
};

#endif // INTERNETDIAGPRESENTER_HPP
