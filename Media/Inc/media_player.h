#ifndef MEDIA_PLAYER_H
#define MEDIA_PLAYER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_MAX_TRACKS       64U
#define MEDIA_MAX_FILES        128U
#define MEDIA_MAX_FOLDERS      64U
#define MEDIA_TRACK_NAME_SIZE  128U
#define MEDIA_EQ_BANDS         5U
#define MEDIA_EQ_CUSTOM        255U
#define MEDIA_SW_EQ_BANDS      10U
#define MEDIA_SW_EQ_PRESETS    6U
#define MEDIA_SPECTRUM_BANDS   24U

typedef enum
{
    MEDIA_STATE_NO_CARD = 0,
    MEDIA_STATE_READY,
    MEDIA_STATE_PLAYING,
    MEDIA_STATE_PAUSED,
    MEDIA_STATE_ERROR
} MediaState;

typedef enum
{
    MEDIA_SOURCE_WAV = 0,
    MEDIA_SOURCE_AUX,
    MEDIA_SOURCE_PC,
    MEDIA_SOURCE_RADIO
} MediaSource;

typedef enum
{
    MEDIA_STORAGE_SD = 0,
    MEDIA_STORAGE_USB
} MediaStorage;

typedef enum
{
    MEDIA_RECORDER_NO_STORAGE = 0,
    MEDIA_RECORDER_READY,
    MEDIA_RECORDER_RECORDING,
    MEDIA_RECORDER_AWAITING_SAVE,
    MEDIA_RECORDER_SAVED,
    MEDIA_RECORDER_ERROR
} MediaRecorderState;

typedef struct
{
    uint32_t revision;
    uint32_t elapsedSeconds;
    uint32_t dataBytes;
    uint8_t leftLevel;
    uint8_t rightLevel;
    uint8_t state;
    uint8_t storage;
    int16_t gainCentiDb;
    char fileName[32];
    char status[96];
} MediaRecorderSnapshot;

typedef enum
{
    MEDIA_DELETE_IDLE = 0,
    MEDIA_DELETE_PENDING,
    MEDIA_DELETE_SUCCESS,
    MEDIA_DELETE_ERROR
} MediaDeleteState;

typedef struct
{
    uint32_t revision;
    uint8_t state;
    char fileName[MEDIA_TRACK_NAME_SIZE];
    char status[64];
} MediaDeleteSnapshot;

typedef struct
{
    uint32_t revision;
    uint32_t libraryRevision;
    uint32_t elapsedSeconds;
    uint32_t durationSeconds;
    uint16_t currentTrack;
    uint16_t trackCount;
    uint8_t volume;
    uint8_t state;
    uint8_t source;
    uint8_t storage;
    uint8_t eqBands[MEDIA_EQ_BANDS];
    uint8_t eqPreset;
    uint8_t swEqBands[MEDIA_SW_EQ_BANDS];
    uint8_t swEqPreamp;
    uint8_t swEqPreset;
    uint8_t timePitchEnabled;
    uint16_t speedPercent;
    int16_t pitchCents;
    char currentName[MEDIA_TRACK_NAME_SIZE];
    char status[MEDIA_TRACK_NAME_SIZE];
} MediaSnapshot;

void MediaPlayer_Init(void);
void MediaPlayer_Task(void *argument);
void MediaPlayer_GetSnapshot(MediaSnapshot *snapshot);
uint16_t MediaPlayer_GetTrackCount(void);
uint8_t MediaPlayer_GetTrackName(uint16_t index, char *name, uint16_t capacity);
uint16_t MediaPlayer_GetFileCount(void);
uint8_t MediaPlayer_GetFileName(uint16_t index, char *name, uint16_t capacity);
uint16_t MediaPlayer_GetFolderCount(void);
uint8_t MediaPlayer_GetFolderName(uint16_t index, char *name, uint16_t capacity);
void MediaPlayer_GetCurrentFolder(char *name, uint16_t capacity);
void MediaPlayer_GetDeleteSnapshot(MediaDeleteSnapshot *snapshot);
void MediaPlayer_GetStatus(char *status, uint16_t capacity);
uint32_t MediaPlayer_GetSpectrum(uint8_t levels[MEDIA_SPECTRUM_BANDS]);

void MediaPlayer_Select(uint16_t index);
void MediaPlayer_TogglePlayPause(void);
void MediaPlayer_Next(void);
void MediaPlayer_Previous(void);
void MediaPlayer_SetVolume(uint8_t volume);
void MediaPlayer_Seek(uint32_t seconds);
void MediaPlayer_SetEQBand(uint8_t band, uint8_t value);
void MediaPlayer_ApplyEQPreset(uint8_t preset);
void MediaPlayer_SetSoftwareEQBand(uint8_t band, uint8_t value);
void MediaPlayer_SetSoftwareEQPreamp(uint8_t value);
void MediaPlayer_ApplySoftwareEQPreset(uint8_t preset);
void MediaPlayer_SetSpeed(uint16_t percent);
void MediaPlayer_SetPitch(int16_t cents);
void MediaPlayer_SetTimePitchEnabled(uint8_t enabled);
void MediaPlayer_ToggleSource(void);
void MediaPlayer_ToggleStorage(void);
void MediaPlayer_SetStorage(MediaStorage storage);
void MediaPlayer_SelectFolder(uint16_t index);
void MediaPlayer_DeleteFile(uint16_t index);

void MediaRecorder_GetSnapshot(MediaRecorderSnapshot *snapshot);
void MediaRecorder_Start(void);
void MediaRecorder_Stop(void);
void MediaRecorder_ConfirmSave(void);
void MediaRecorder_Discard(void);
void MediaRecorder_SetGain(int16_t centiDb);

/* Shared I2C3 guard used by the FT5336 touch controller and WM8994 codec. */
void MediaPlayer_I2CLock(void);
void MediaPlayer_I2CUnlock(void);

#ifdef __cplusplus
}
#endif

#endif
