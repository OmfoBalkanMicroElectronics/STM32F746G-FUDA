#include <gui/equalizerui_screen/EqualizerUIView.hpp>
#include <gui/equalizerui_screen/EqualizerUIPresenter.hpp>

EqualizerUIPresenter::EqualizerUIPresenter(EqualizerUIView& v) : view(v)
{
}

void EqualizerUIPresenter::activate()
{
    MediaSnapshot snapshot;
    int values[MEDIA_EQ_BANDS];
    model->mediaSnapshot(snapshot);
    for (uint8_t i = 0U; i < MEDIA_EQ_BANDS; i++) values[i] = snapshot.eqBands[i];
    view.updateSliders(values);
}

void EqualizerUIPresenter::deactivate()
{
}

void EqualizerUIPresenter::updateEQBand(int bandIndex, int value)
{
    if (bandIndex >= 0 && bandIndex < (int)MEDIA_EQ_BANDS)
    {
        model->setEQBand((uint8_t)bandIndex, (uint8_t)value);
    }
}

void EqualizerUIPresenter::applyPreset(int presetIndex)
{
    if (presetIndex >= 0 && presetIndex < 5)
    {
        model->applyEQPreset((uint8_t)presetIndex);
    }
}
