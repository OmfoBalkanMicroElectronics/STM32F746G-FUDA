#include <gui/recorderui_screen/RecorderUIView.hpp>
#include <gui/recorderui_screen/RecorderUIPresenter.hpp>

RecorderUIPresenter::RecorderUIPresenter(RecorderUIView& v)
    : view(v)
{

}

void RecorderUIPresenter::activate()
{

}

void RecorderUIPresenter::deactivate()
{

}

void RecorderUIPresenter::recorderSnapshot(MediaRecorderSnapshot& snapshot) const
{
    model->recorderSnapshot(snapshot);
}

void RecorderUIPresenter::startRecording()
{
    model->startRecording();
}

void RecorderUIPresenter::stopRecording()
{
    model->stopRecording();
}

void RecorderUIPresenter::confirmRecording()
{
    model->confirmRecording();
}

void RecorderUIPresenter::discardRecording()
{
    model->discardRecording();
}

void RecorderUIPresenter::setGain(int16_t centiDb)
{
    model->setRecorderGain(centiDb);
}
