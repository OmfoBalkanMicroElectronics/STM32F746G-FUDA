#include <gui/recorderui_screen/RecorderUIView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>
#include <cstdio>
#include <cstring>

RecorderUIView::RecorderUIView() :
    recorderButtonCallback(this, &RecorderUIView::recorderButtonHandler),
    gainSliderCallback(this, &RecorderUIView::gainSliderChanged),
    lastRecorderRevision(0xFFFFFFFFU),
    lastRecorderState(0xFFU),
    leftPeakHold(0U),
    rightPeakHold(0U),
    updatingGain(false)
{

}

void RecorderUIView::setupScreen()
{
    RecorderUIViewBase::setupScreen();

    /* The Designer labels are useful layout placeholders, but they contain
       literal *record...* text. Runtime wildcard fields replace them. */
    recordStatus.setVisible(false);
    recordFileName.setVisible(false);
    recordTimeValue.setVisible(false);
    recGainValue.setVisible(false);

    statusValue.setPosition(0, 201, 480, 30);
    statusValue.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    statusValue.setWildcard(statusValueBuffer);
    statusValue.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    statusValueBuffer[0] = 0;
    add(statusValue);

    fileValue.setPosition(0, 148, 480, 28);
    fileValue.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    fileValue.setWildcard(fileValueBuffer);
    fileValue.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    fileValueBuffer[0] = 0;
    add(fileValue);

    timeValue.setPosition(0, 176, 300, 28);
    timeValue.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    timeValue.setWildcard(timeValueBuffer);
    timeValue.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    timeValueBuffer[0] = 0;
    add(timeValue);

    gainValue.setPosition(recGainValue.getX(), recGainValue.getY(), 190, recGainValue.getHeight());
    gainValue.setTypedText(touchgfx::TypedText(T_TEXTFILENAME));
    gainValue.setWildcard(gainValueBuffer);
    gainValue.setColor(recGainValue.getColor());
    add(gainValue);

    recGainSlider.setValueRange(-3000, 3000);
    recGainSlider.setNewValueCallback(gainSliderCallback);

    /* A red hold indicator makes clipping visible without regenerating the
       Gauge needle bitmap. The gauge itself continues to show live level. */
    leftPeakAlarm.setPosition(250, 113, 100, 5);
    leftPeakAlarm.setColor(touchgfx::Color::getColorFromRGB(230, 20, 20));
    leftPeakAlarm.setVisible(false);
    add(leftPeakAlarm);
    rightPeakAlarm.setPosition(370, 113, 100, 5);
    rightPeakAlarm.setColor(touchgfx::Color::getColorFromRGB(230, 20, 20));
    rightPeakAlarm.setVisible(false);
    add(rightPeakAlarm);

    startRecording.setAction(recorderButtonCallback);
    stopRecording.setAction(recorderButtonCallback);
    saveConfirm.setAction(recorderButtonCallback);
    saveDeny.setAction(recorderButtonCallback);

    MediaRecorderSnapshot snapshot;
    presenter->recorderSnapshot(snapshot);
    updateRecorderUi(snapshot);
}

void RecorderUIView::tearDownScreen()
{
    RecorderUIViewBase::tearDownScreen();
}

void RecorderUIView::handleTickEvent()
{
    MediaRecorderSnapshot snapshot;
    presenter->recorderSnapshot(snapshot);
    if (snapshot.revision != lastRecorderRevision || snapshot.state != lastRecorderState ||
        snapshot.state == MEDIA_RECORDER_RECORDING)
        updateRecorderUi(snapshot);
}

void RecorderUIView::recorderButtonHandler(const touchgfx::AbstractButton& source)
{
    if (&source == &startRecording) presenter->startRecording();
    else if (&source == &stopRecording) presenter->stopRecording();
    else if (&source == &saveConfirm) presenter->confirmRecording();
    else if (&source == &saveDeny) presenter->discardRecording();
}

void RecorderUIView::updateRecorderUi(const MediaRecorderSnapshot& snapshot)
{
    char line[128];
    const bool recording = snapshot.state == MEDIA_RECORDER_RECORDING;
    const bool awaitingSave = snapshot.state == MEDIA_RECORDER_AWAITING_SAVE;
    const bool canStart = snapshot.state == MEDIA_RECORDER_READY ||
                          snapshot.state == MEDIA_RECORDER_SAVED ||
                          snapshot.state == MEDIA_RECORDER_ERROR;

    std::snprintf(line, sizeof(line), "Durum: %s", snapshot.status);
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(line), statusValueBuffer,
                               sizeof(statusValueBuffer) / sizeof(statusValueBuffer[0]));
    statusValue.setColor(touchgfx::Color::getColorFromRGB(
        snapshot.state == MEDIA_RECORDER_ERROR || snapshot.state == MEDIA_RECORDER_NO_STORAGE ? 190 : 0,
        snapshot.state == MEDIA_RECORDER_SAVED ? 125 : 0,
        0));
    statusValue.invalidate();

    std::snprintf(line, sizeof(line), "Dosya: %s  WAV 16 kHz / 16-bit",
                  snapshot.fileName[0] != '\0' ? snapshot.fileName : "-");
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(line), fileValueBuffer,
                               sizeof(fileValueBuffer) / sizeof(fileValueBuffer[0]));
    fileValue.invalidate();

    std::snprintf(line, sizeof(line), "Kayıt Süresi: %02lu:%02lu",
                  (unsigned long)(snapshot.elapsedSeconds / 60U),
                  (unsigned long)(snapshot.elapsedSeconds % 60U));
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(line), timeValueBuffer,
                               sizeof(timeValueBuffer) / sizeof(timeValueBuffer[0]));
    timeValue.invalidate();

    updatingGain = true;
    recGainSlider.setValue(snapshot.gainCentiDb);
    recGainSlider.invalidate();
    updateGainLabel(snapshot.gainCentiDb);
    updatingGain = false;

    leftGauge.setValue(snapshot.leftLevel);
    rightGauge.setValue(snapshot.rightLevel);
    leftGauge.invalidate();
    rightGauge.invalidate();
    if (!recording)
    {
        leftPeakHold = 0U;
        rightPeakHold = 0U;
    }
    else
    {
        if (snapshot.leftLevel >= 90U) leftPeakHold = 30U;
        else if (leftPeakHold != 0U) leftPeakHold--;
        if (snapshot.rightLevel >= 90U) rightPeakHold = 30U;
        else if (rightPeakHold != 0U) rightPeakHold--;
    }
    leftPeakAlarm.setVisible(leftPeakHold != 0U);
    rightPeakAlarm.setVisible(rightPeakHold != 0U);
    leftPeakAlarm.invalidate();
    rightPeakAlarm.invalidate();

    startRecording.setTouchable(canStart && !awaitingSave);
    startRecording.setAlpha(canStart && !awaitingSave ? 255U : 100U);
    stopRecording.setTouchable(recording);
    stopRecording.setAlpha(recording ? 255U : 100U);
    returnButton.setTouchable(!recording && !awaitingSave);
    returnButton.setAlpha(!recording && !awaitingSave ? 255U : 100U);

    saveConfirmationPrompt.setVisible(awaitingSave);
    saveConfirm.setVisible(awaitingSave);
    saveDeny.setVisible(awaitingSave);
    saveConfirmationPrompt.invalidate();
    saveConfirm.invalidate();
    saveDeny.invalidate();
    startRecording.invalidate();
    stopRecording.invalidate();
    returnButton.invalidate();

    lastRecorderRevision = snapshot.revision;
    lastRecorderState = snapshot.state;
}

void RecorderUIView::gainSliderChanged(const touchgfx::Slider& source, int value)
{
    if (updatingGain || &source != &recGainSlider || presenter == 0) return;
    presenter->setGain((int16_t)value);
    updateGainLabel((int16_t)value);
}

void RecorderUIView::updateGainLabel(int16_t centiDb)
{
    char text[24];
    int32_t signedValue = centiDb;
    uint32_t magnitude = signedValue < 0 ? (uint32_t)(-signedValue) : (uint32_t)signedValue;
    std::snprintf(text, sizeof(text), "Gain:%s%lu,%02lu",
                  signedValue > 0 ? "+" : (signedValue < 0 ? "-" : ""),
                  (unsigned long)(magnitude / 100U),
                  (unsigned long)(magnitude % 100U));
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), gainValueBuffer,
                               sizeof(gainValueBuffer) / sizeof(gainValueBuffer[0]));
    gainValue.invalidate();
}
