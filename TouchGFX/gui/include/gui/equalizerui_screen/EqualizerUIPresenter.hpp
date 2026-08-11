#ifndef EQUALIZERUIPRESENTER_HPP
#define EQUALIZERUIPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class EqualizerUIView;

class EqualizerUIPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    EqualizerUIPresenter(EqualizerUIView& v);

    virtual void activate();
    virtual void deactivate();
    virtual ~EqualizerUIPresenter() {}

    // Called when the user manually drags a slider
    void updateEQBand(int bandIndex, int value);
    
    // Called when the user clicks a preset button
    void applyPreset(int presetIndex);

private:
    EqualizerUIPresenter();
    EqualizerUIView& view;
};

#endif // EQUALIZERUIPRESENTER_HPP