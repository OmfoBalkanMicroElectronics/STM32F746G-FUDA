#ifndef MODEL_HPP
#define MODEL_HPP
#include "media_player.h"
#include "display_manager.h"
#include "network_manager.h"

class ModelListener;

class Model
{
public:
    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    void tick();
    void selectTrack(uint16_t index) { MediaPlayer_Select(index); }
    void togglePlayPause() { MediaPlayer_TogglePlayPause(); }
    void nextTrack() { MediaPlayer_Next(); }
    void previousTrack() { MediaPlayer_Previous(); }
    void setVolume(uint8_t value) { MediaPlayer_SetVolume(value); }
    void seek(uint32_t seconds) { MediaPlayer_Seek(seconds); }
    void setEQBand(uint8_t band, uint8_t value) { MediaPlayer_SetEQBand(band, value); }
    void applyEQPreset(uint8_t preset) { MediaPlayer_ApplyEQPreset(preset); }
    void setSoftwareEQBand(uint8_t band, uint8_t value) { MediaPlayer_SetSoftwareEQBand(band, value); }
    void setSoftwareEQPreamp(uint8_t value) { MediaPlayer_SetSoftwareEQPreamp(value); }
    void applySoftwareEQPreset(uint8_t preset) { MediaPlayer_ApplySoftwareEQPreset(preset); }
    void setPlaybackSpeed(uint16_t percent) { MediaPlayer_SetSpeed(percent); }
    void setPlaybackPitch(int16_t cents) { MediaPlayer_SetPitch(cents); }
    void setTimePitchEnabled(bool enabled) { MediaPlayer_SetTimePitchEnabled(enabled ? 1U : 0U); }
    void toggleSource() { MediaPlayer_ToggleSource(); }
    void toggleStorage() { MediaPlayer_ToggleStorage(); }
    void setStorage(MediaStorage storage) { MediaPlayer_SetStorage(storage); }
    void setScreenBrightness(uint8_t value) { DisplayManager_SetBrightness(value); }
    uint8_t screenBrightness() const { return DisplayManager_GetBrightness(); }
    void setScreenTimeout(uint16_t seconds) { DisplayManager_SetTimeout(seconds); }
    uint16_t screenTimeout() const { return DisplayManager_GetTimeout(); }
    uint16_t trackCount() const { return MediaPlayer_GetTrackCount(); }
    bool trackName(uint16_t index, char* name, uint16_t capacity) const { return MediaPlayer_GetTrackName(index, name, capacity) != 0U; }
    uint16_t fileCount() const { return MediaPlayer_GetFileCount(); }
    bool fileName(uint16_t index, char* name, uint16_t capacity) const { return MediaPlayer_GetFileName(index, name, capacity) != 0U; }
    uint16_t folderCount() const { return MediaPlayer_GetFolderCount(); }
    bool folderName(uint16_t index, char* name, uint16_t capacity) const { return MediaPlayer_GetFolderName(index, name, capacity) != 0U; }
    void currentFolder(char* name, uint16_t capacity) const { MediaPlayer_GetCurrentFolder(name, capacity); }
    void selectFolder(uint16_t index) { MediaPlayer_SelectFolder(index); }
    void deleteFile(uint16_t index) { MediaPlayer_DeleteFile(index); }
    void deleteSnapshot(MediaDeleteSnapshot& snapshot) const { MediaPlayer_GetDeleteSnapshot(&snapshot); }
    void mediaStatus(char* status, uint16_t capacity) const { MediaPlayer_GetStatus(status, capacity); }
    void mediaSnapshot(MediaSnapshot& snapshot) const { MediaPlayer_GetSnapshot(&snapshot); }
    void recorderSnapshot(MediaRecorderSnapshot& snapshot) const { MediaRecorder_GetSnapshot(&snapshot); }
    void startRecording() { MediaRecorder_Start(); }
    void stopRecording() { MediaRecorder_Stop(); }
    void confirmRecording() { MediaRecorder_ConfirmSave(); }
    void discardRecording() { MediaRecorder_Discard(); }
    void setRecorderGain(int16_t centiDb) { MediaRecorder_SetGain(centiDb); }
    uint32_t spectrum(uint8_t levels[MEDIA_SPECTRUM_BANDS]) const { return MediaPlayer_GetSpectrum(levels); }
    void networkSnapshot(NetworkSnapshot& snapshot) const { NetworkManager_GetSnapshot(&snapshot); }
    void startNetworkSpeedTest() { NetworkManager_StartSpeedTest(); }
protected:
    ModelListener* modelListener;
    uint32_t lastRevision;
    uint32_t lastLibraryRevision;
};

#endif // MODEL_HPP
