#include <gui/internetdiag_screen/internetDiagView.hpp>
#include <gui/internetdiag_screen/internetDiagPresenter.hpp>

internetDiagPresenter::internetDiagPresenter(internetDiagView& v)
    : view(v)
{

}

void internetDiagPresenter::activate()
{

}

void internetDiagPresenter::deactivate()
{

}

void internetDiagPresenter::networkSnapshot(NetworkSnapshot& snapshot) const
{
    model->networkSnapshot(snapshot);
}

void internetDiagPresenter::startSpeedTest()
{
    model->startNetworkSpeedTest();
}
