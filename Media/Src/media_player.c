#include "media_player.h"

#include "cmsis_os2.h"
#include "ff.h"
#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "usb_storage.h"
#include "usb_pc_audio.h"
#include "internet_radio.h"
#include "stm32746g_discovery_audio.h"
#include "aacdec.h"
#define ARM_MATH_CM7
#include "arm_math.h"

/* minimp3 is compiled into this translation unit so CubeIDE does not need an
   additional generated source/link entry. Cortex-M7 uses the portable path. */
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_STATIC_SCRATCH
#define MINIMP3_IMPLEMENTATION
#include "../../Middlewares/Third_Party/minimp3/minimp3.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MEDIA_PATH_SIZE          280U
#define MEDIA_AUDIO_BUFFER_BYTES 32768U
#define MEDIA_AUDIO_HALF_BYTES   (MEDIA_AUDIO_BUFFER_BYTES / 2U)
#define MEDIA_FRAMES_PER_HALF    (MEDIA_AUDIO_HALF_BYTES / 4U)
/* Convert large WAV DMA halves in bounded chunks; keeping this workspace at
   32 KiB avoids consuming another 32 KiB of scarce internal SRAM. */
#define MEDIA_RAW_BUFFER_BYTES   32768U
#define AUX_AUDIO_BUFFER_BYTES   8192U
#define AUX_AUDIO_HALF_BYTES     (AUX_AUDIO_BUFFER_BYTES / 2U)
#define MEDIA_COMMAND_DEPTH      12U
#define MEDIA_RESCAN_TICKS       2000U
#define MEDIA_SCAN_DEPTH         3U
#define MEDIA_FFT_SIZE           1024U
#define MEDIA_RADIO_FFT_SIZE     512U
#define MEDIA_SEEK_SETTLE_TICKS  75U
#define MP3_INPUT_BUFFER_BYTES   16384U
#define MP3_READ_CHUNK_BYTES     511U
#define AAC_MAX_OUTPUT_SAMPLES   4096U
#define AAC_DECODER_WORK_BYTES   81920U
#define AAC_ADTS_MAX_FRAME_BYTES 4096U
#define RADIO_PREDECODE_RETRY_TICKS 5U
#define TIMEPITCH_RING_FRAMES    32768U
#define TIMEPITCH_RING_MASK      (TIMEPITCH_RING_FRAMES - 1U)
#define TIMEPITCH_GRAIN_FRAMES   2048U
#define TIMEPITCH_OVERLAP_FRAMES 384U
#define TIMEPITCH_HOP_FRAMES     (TIMEPITCH_GRAIN_FRAMES - TIMEPITCH_OVERLAP_FRAMES)
#define TIMEPITCH_GRAINS         2U
#define TIMEPITCH_SEEK_FRAMES    384
#define TIMEPITCH_COARSE_STEP    32
#define TIMEPITCH_REFINE_STEP    4
#define TIMEPITCH_CORR_STRIDE    8U
#define RECORDER_SAMPLE_RATE     16000U
#define RECORDER_CHANNELS        2U
#define RECORDER_BITS_PER_SAMPLE 16U
#define RECORDER_BUFFER_BYTES    8192U
#define RECORDER_HALF_BYTES      (RECORDER_BUFFER_BYTES / 2U)
#define RECORDER_QUEUE_BLOCKS    8U

typedef enum
{
    CMD_SELECT = 0,
    CMD_TOGGLE,
    CMD_NEXT,
    CMD_PREVIOUS,
    CMD_VOLUME,
    CMD_EQ_BAND,
    CMD_EQ_PRESET,
    CMD_SW_EQ_BAND,
    CMD_SW_EQ_PREAMP,
    CMD_SW_EQ_PRESET,
    CMD_SPEED,
    CMD_PITCH,
    CMD_TIMEPITCH_ENABLE,
    CMD_SOURCE,
    CMD_STORAGE,
    CMD_FOLDER,
    CMD_DELETE,
    CMD_RECORD_START,
    CMD_RECORD_STOP,
    CMD_RECORD_CONFIRM,
    CMD_RECORD_DISCARD,
    CMD_RECORD_GAIN
} MediaCommandType;

typedef struct
{
    uint8_t type;
    uint32_t value;
} MediaCommand;

typedef struct
{
    char path[MEDIA_PATH_SIZE];
    char name[MEDIA_TRACK_NAME_SIZE];
} MediaTrack;

typedef struct
{
    char path[MEDIA_PATH_SIZE];
    char name[MEDIA_TRACK_NAME_SIZE];
} MediaFileEntry;

typedef struct
{
    char path[MEDIA_PATH_SIZE];
    char name[MEDIA_TRACK_NAME_SIZE];
} MediaFolderEntry;

typedef struct
{
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t byteRate;
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint16_t blockAlign;
} WavInfo;

typedef enum
{
    MEDIA_FORMAT_WAV = 0,
    MEDIA_FORMAT_MP3,
    MEDIA_FORMAT_AAC
} MediaFormat;

static osMessageQueueId_t commandQueue;
static osMutexId_t snapshotMutex;
static osMutexId_t i2cMutex;
static osMutexId_t spectrumMutex;
static MediaSnapshot sharedSnapshot;
/* Track metadata is CPU-only and relatively cold. Keep the 64-entry library
   in external SDRAM so internal SRAM remains available for RTOS/audio work. */
static MediaTrack tracks[MEDIA_MAX_TRACKS]
    __attribute__((section("MediaTrackSection"), aligned(32)));
static MediaFileEntry files[MEDIA_MAX_FILES]
    __attribute__((section("MediaTrackSection"), aligned(32)));
static MediaFolderEntry folders[MEDIA_MAX_FOLDERS]
    __attribute__((section("MediaTrackSection"), aligned(32)));
static FATFS fileSystem;
static FIL audioFile;
static FIL recorderFile;
static char drivePath[4];
static char mountedPath[4];
static WavInfo wavInfo;
static volatile uint8_t refillFlags;
static volatile uint8_t auxInputFlags;
static uint8_t audioBuffer[MEDIA_AUDIO_BUFFER_BYTES] __attribute__((aligned(32)));
static uint8_t auxInputBuffer[AUX_AUDIO_BUFFER_BYTES] __attribute__((aligned(32)));
static uint8_t recorderBuffer[RECORDER_BUFFER_BYTES] __attribute__((aligned(32)));
/* SD writes are intentionally serialized into CMD24 single-sector transfers.
   Queue completed DMA halves in SDRAM so a slow sector cannot make the next
   microphone half overwrite audio that has not been written yet. */
static uint8_t recorderWriteQueue[RECORDER_QUEUE_BLOCKS][RECORDER_HALF_BYTES]
    __attribute__((section("MediaTrackSection"), aligned(32)));
/* WAV conversion and MP3 decoding are mutually exclusive. Sharing this
   workspace avoids spending another ~20 KiB of the F746's internal SRAM when
   MP3 support is linked in. Keep it in internal SRAM: minimp3 touches these
   buffers heavily and is considerably slower from external SDRAM. */
static union
{
    uint8_t wavRaw[MEDIA_RAW_BUFFER_BYTES];
    struct
    {
        uint8_t input[MP3_INPUT_BUFFER_BYTES];
        int16_t frame[AAC_MAX_OUTPUT_SAMPLES];
    } compressed;
} decodeWorkspace __attribute__((aligned(32)));
#define rawBuffer       (decodeWorkspace.wavRaw)
#define mp3InputBuffer  (decodeWorkspace.compressed.input)
#define mp3FrameBuffer  (decodeWorkspace.compressed.frame)
static mp3dec_t mp3Decoder;
static HAACDecoder aacDecoder;
static uint8_t aacDecoderWorkspace[AAC_DECODER_WORK_BYTES]
    __attribute__((section("MediaTrackSection"), aligned(32)));
/* Readable over SWD/GDB without logging from the real-time audio path. */
volatile uint32_t aacDecodeCyclesTotal;
volatile uint32_t aacDecodeCyclesMax;
volatile uint32_t aacDecodeCalls;
volatile uint32_t aacFillCyclesTotal;
volatile uint32_t aacFillCyclesMax;
volatile uint32_t aacFillCalls;
volatile uint32_t mediaRefillCyclesMax;
volatile uint32_t mediaRefillCalls;
volatile uint32_t mediaRefillLateEvents;
volatile uint32_t swEqProcessCyclesMax;
volatile uint32_t swEqProcessCalls;
volatile uint32_t swEqClippedSamples;
volatile uint32_t radioAudioStarvationEvents;
volatile uint32_t aacInvalidFrames;
volatile uint32_t aacInputUnderflows;
static MediaFormat currentFormat = MEDIA_FORMAT_WAV;
static uint32_t mp3InputFilled;
static uint32_t mp3InputConsumed;
static uint32_t mp3FrameSamples;
static uint32_t mp3FramePosition;
static uint32_t mp3SampleRate;
static uint32_t mp3ByteRate;
static uint32_t mp3FileSize;
static uint32_t mp3DataOffset;
static uint8_t mp3Channels;
static uint8_t mp3Eof;
static uint8_t fileOpen;
static uint8_t codecReady;
static uint8_t driverLinked;
static uint8_t eqBands[MEDIA_EQ_BANDS] = {50U, 50U, 50U, 50U, 50U};
static uint8_t eqPreset = 0U;
static const uint8_t eqPresets[5][MEDIA_EQ_BANDS] =
{
    {50U, 50U, 50U, 50U, 50U}, /* Normal */
    {75U, 60U, 50U, 70U, 85U}, /* Classic */
    {80U, 55U, 65U, 80U, 70U}, /* Jazz */
    {85U, 70U, 40U, 65U, 80U}, /* Rock */
    {50U, 65U, 85U, 75U, 60U}  /* Pop */
};
static uint8_t swEqBands[MEDIA_SW_EQ_BANDS] =
    {50U, 50U, 50U, 50U, 50U, 50U, 50U, 50U, 50U, 50U};
static uint8_t swEqPreamp = 100U;
static uint8_t swEqPreset = 0U;
static const uint8_t swEqPresetBands[MEDIA_SW_EQ_PRESETS][MEDIA_SW_EQ_BANDS] =
{
    {50U, 50U, 50U, 50U, 50U, 50U, 50U, 50U, 50U, 50U}, /* Flat */
    {55U, 55U, 50U, 45U, 45U, 50U, 55U, 60U, 65U, 65U}, /* Classic */
    {65U, 60U, 50U, 55U, 60U, 65U, 65U, 60U, 65U, 70U}, /* Jazz */
    {70U, 65U, 55U, 45U, 40U, 50U, 60U, 65U, 70U, 70U}, /* Rock */
    {55U, 60U, 65U, 55U, 50U, 55U, 65U, 70U, 65U, 55U}, /* Pop */
    {40U, 45U, 50U, 55U, 65U, 70U, 70U, 60U, 50U, 45U}  /* Vocal */
};
static const uint8_t swEqPresetPreamp[MEDIA_SW_EQ_PRESETS] =
    {100U, 70U, 60U, 50U, 55U, 55U};
static const float32_t swEqFrequencies[MEDIA_SW_EQ_BANDS] =
    {31.0f, 62.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
static arm_biquad_cascade_stereo_df2T_instance_f32 swEqFilter;
static float32_t swEqCoefficients[MEDIA_SW_EQ_BANDS * 5U] __attribute__((aligned(32)));
static float32_t swEqState[MEDIA_SW_EQ_BANDS * 4U] __attribute__((aligned(32)));
static float32_t swEqScratch[256U] __attribute__((aligned(32)));
static float32_t swEqPreampLinear = 1.0f;
static uint32_t swEqSampleRate = 44100U;
static uint8_t swEqEnabled;
static uint16_t playbackSpeedPercent = 100U;
static int16_t playbackPitchCents = 0;
static uint8_t timePitchEnabled = 0U;
static float32_t timePitchSpeed = 1.0f;
static uint32_t timePitchPitchQ16 = 65536U;
static uint8_t timePitchRunning;
static uint8_t timePitchSourceEof;
static uint32_t timePitchSourceWrite;
static uint64_t timePitchNextStartQ16;
static uint32_t timePitchOutputFrame;
static int16_t timePitchSource[TIMEPITCH_RING_FRAMES * 2U]
    __attribute__((section("MediaTrackSection"), aligned(32)));
/* Q15 windowing avoids two float multiplies and conversions for every stereo
   output frame.  The overlap/add shape is unchanged. */
static uint16_t timePitchWindowQ15[TIMEPITCH_GRAIN_FRAMES] __attribute__((aligned(32)));
typedef struct
{
    uint64_t startQ16;
    uint32_t pitchQ16;
    uint16_t age;
    uint8_t active;
} TimePitchGrain;
static TimePitchGrain timePitchGrains[TIMEPITCH_GRAINS];
volatile uint32_t timePitchProcessCyclesMax;
volatile uint32_t timePitchProcessCalls;
volatile uint32_t timePitchUnderruns;
/* 1: EOF is in first half, 2: EOF is in second half. */
static uint8_t endHalf;
static uint8_t volume = 80U;
static uint16_t currentTrack;
static uint32_t dataRemaining;
static uint32_t lastScanTick;
static volatile uint32_t pendingSeekSeconds;
static volatile uint32_t pendingSeekTick;
static volatile uint8_t seekPending;
static arm_rfft_fast_instance_f32 spectrumFft;
static arm_rfft_fast_instance_f32 spectrumRadioFft;
static float32_t spectrumInput[MEDIA_FFT_SIZE];
static float32_t spectrumOutput[MEDIA_FFT_SIZE];
static float32_t spectrumMagnitude[MEDIA_FFT_SIZE / 2U];
static float32_t spectrumWindow[MEDIA_FFT_SIZE];
static uint8_t sharedSpectrum[MEDIA_SPECTRUM_BANDS];
static uint32_t spectrumRevision;
static uint8_t spectrumReady;
static volatile uint8_t activeSource = MEDIA_SOURCE_WAV;
static volatile uint8_t activeStorage = MEDIA_STORAGE_SD;
static uint32_t usbStorageRevisionSeen;
static uint32_t usbPcRevisionSeen;
static uint32_t radioRevisionSeen;
static uint8_t radioPlaybackStarted;
static uint32_t radioPredecodeRetryTick;
static uint8_t savedWavState;
static uint16_t savedWavTrack;
static uint32_t savedWavElapsed;
static char savedWavName[MEDIA_TRACK_NAME_SIZE];
static char savedWavStatus[MEDIA_TRACK_NAME_SIZE];
static MediaRecorderSnapshot recorderSnapshot;
static volatile uint8_t recorderInputFlags;
static volatile uint8_t recorderActive;
static volatile uint8_t recorderQueueWrite;
static volatile uint8_t recorderQueueRead;
static volatile uint8_t recorderQueueCount;
static uint8_t recorderFileOpen;
static uint32_t recorderDataBytes;
static int16_t recorderGainCentiDb;
static uint32_t recorderGainQ15 = 32768U;
static char recorderTempPath[MEDIA_PATH_SIZE];
static char recorderFinalPath[MEDIA_PATH_SIZE];
static char currentFolders[2][MEDIA_PATH_SIZE];
static uint16_t mediaFileCount;
static uint16_t mediaFolderCount;
static MediaDeleteSnapshot deleteSnapshot;

/* Recorder diagnostics are intentionally visible to GDB. */
volatile uint32_t recorderWriteErrors;
volatile uint32_t recorderOverruns;
volatile uint32_t recorderBytesWritten;
volatile uint32_t recorderDmaHalves;
volatile uint32_t recorderQueueHighWater;
volatile uint32_t mediaSeekRequests;
volatile uint32_t mediaSeekSuccesses;
volatile uint32_t mediaSeekFailures;
volatile uint32_t mediaSeekLastSeconds;
volatile uint32_t mediaSeekLastPosition;
volatile uint32_t mediaSeekRecoveries;
volatile uint32_t mediaSeekRecoveryFailures;

static uint8_t startTrack(uint16_t index);
static void seekTo(uint32_t seconds);
static void leaveRadioMode(uint8_t restoreWav);

/* Log-spaced band edges give useful bass resolution without wasting most of
   the display on high-frequency linear FFT bins. */
static const uint16_t spectrumEdges[MEDIA_SPECTRUM_BANDS + 1U] =
{
    45U, 63U, 88U, 123U, 172U, 240U, 335U, 468U, 654U, 913U,
    1275U, 1781U, 2488U, 3475U, 4855U, 6000U, 7350U, 8850U,
    10400U, 12000U, 13700U, 15500U, 17400U, 19600U, 22000U
};

static void configureSoftwareEq(uint32_t sampleRate, uint8_t resetState)
{
    uint32_t band;
    uint8_t enabled = swEqPreamp != 100U;

    if (sampleRate == 0U) sampleRate = 44100U;
    swEqSampleRate = sampleRate;
    /* The preamp is deliberately attenuation-only: 0..100 maps to -12..0 dB,
       leaving headroom for boosted bands before the final saturator. */
    swEqPreampLinear = powf(10.0f, (-12.0f + 0.12f * (float32_t)swEqPreamp) / 20.0f);

    for (band = 0U; band < MEDIA_SW_EQ_BANDS; band++)
    {
        float32_t *coeff = &swEqCoefficients[band * 5U];
        float32_t gainDb = ((float32_t)swEqBands[band] - 50.0f) * 0.24f;
        float32_t frequency = swEqFrequencies[band];
        if (swEqBands[band] != 50U) enabled = 1U;

        if (frequency >= 0.45f * (float32_t)sampleRate || fabsf(gainDb) < 0.01f)
        {
            coeff[0] = 1.0f; coeff[1] = 0.0f; coeff[2] = 0.0f;
            coeff[3] = 0.0f; coeff[4] = 0.0f;
        }
        else
        {
            const float32_t q = 1.40f;
            float32_t a = powf(10.0f, gainDb / 40.0f);
            float32_t omega = 2.0f * PI * frequency / (float32_t)sampleRate;
            float32_t sine = arm_sin_f32(omega);
            float32_t cosine = arm_cos_f32(omega);
            float32_t alpha = sine / (2.0f * q);
            float32_t a0 = 1.0f + alpha / a;

            coeff[0] = (1.0f + alpha * a) / a0;
            coeff[1] = (-2.0f * cosine) / a0;
            coeff[2] = (1.0f - alpha * a) / a0;
            /* CMSIS DF2T adds feedback, so RBJ a1/a2 are negated here. */
            coeff[3] = (2.0f * cosine) / a0;
            coeff[4] = -(1.0f - alpha / a) / a0;
        }
    }

    swEqFilter.numStages = MEDIA_SW_EQ_BANDS;
    swEqFilter.pCoeffs = swEqCoefficients;
    swEqFilter.pState = swEqState;
    swEqEnabled = enabled;
    if (resetState != 0U) memset(swEqState, 0, sizeof(swEqState));
}

static void processSoftwareEq(int16_t *stereo, uint32_t frames)
{
    uint32_t position = 0U;
    uint32_t cycleStart;
    if (activeSource != MEDIA_SOURCE_WAV || swEqEnabled == 0U || frames == 0U) return;

    cycleStart = DWT->CYCCNT;
    while (position < frames)
    {
        uint32_t i;
        uint32_t count = frames - position;
        if (count > 128U) count = 128U;
        for (i = 0U; i < count * 2U; i++)
            swEqScratch[i] = ((float32_t)stereo[position * 2U + i] / 32768.0f) * swEqPreampLinear;

        arm_biquad_cascade_stereo_df2T_f32(&swEqFilter, swEqScratch, swEqScratch, count);
        for (i = 0U; i < count * 2U; i++)
        {
            float32_t sample = swEqScratch[i];
            if (sample > 0.999969f) { sample = 0.999969f; swEqClippedSamples++; }
            else if (sample < -1.0f) { sample = -1.0f; swEqClippedSamples++; }
            stereo[position * 2U + i] = (int16_t)(sample * 32768.0f);
        }
        position += count;
    }
    {
        uint32_t elapsed = DWT->CYCCNT - cycleStart;
        swEqProcessCalls++;
        if (elapsed > swEqProcessCyclesMax) swEqProcessCyclesMax = elapsed;
    }
}

static void clearSpectrum(void)
{
    if (spectrumMutex == NULL) return;
    osMutexAcquire(spectrumMutex, osWaitForever);
    memset(sharedSpectrum, 0, sizeof(sharedSpectrum));
    spectrumRevision++;
    osMutexRelease(spectrumMutex);
}

static void analyzeSpectrum(const int16_t *stereo, uint32_t frames)
{
    uint32_t i;
    const uint32_t fftSize = (activeSource == MEDIA_SOURCE_RADIO || timePitchRunning != 0U) ?
                             MEDIA_RADIO_FFT_SIZE : MEDIA_FFT_SIZE;
    const uint32_t windowStride = MEDIA_FFT_SIZE / fftSize;
    float32_t mean = 0.0f;
    uint8_t nextLevels[MEDIA_SPECTRUM_BANDS];

    if (spectrumReady == 0U || frames < 128U || wavInfo.sampleRate == 0U) return;

    for (i = 0U; i < fftSize; i++)
    {
        if (i < frames)
        {
            mean += ((float32_t)stereo[i * 2U] + (float32_t)stereo[i * 2U + 1U]) * (1.0f / 65536.0f);
        }
    }
    mean *= (1.0f / (float32_t)fftSize);
    for (i = 0U; i < fftSize; i++)
    {
        float32_t sample = 0.0f;
        if (i < frames)
        {
            sample = ((float32_t)stereo[i * 2U] + (float32_t)stereo[i * 2U + 1U]) * (1.0f / 65536.0f);
        }
        spectrumInput[i] = (sample - mean) * spectrumWindow[i * windowStride];
    }

    arm_rfft_fast_f32((activeSource == MEDIA_SOURCE_RADIO || timePitchRunning != 0U) ?
                      &spectrumRadioFft : &spectrumFft,
                      spectrumInput, spectrumOutput, 0U);
    spectrumMagnitude[0] = 0.0f;
    arm_cmplx_mag_f32(&spectrumOutput[2], &spectrumMagnitude[1], (fftSize / 2U) - 1U);

    for (i = 0U; i < MEDIA_SPECTRUM_BANDS; i++)
    {
        uint32_t first = ((uint32_t)spectrumEdges[i] * fftSize) / wavInfo.sampleRate;
        uint32_t last = ((uint32_t)spectrumEdges[i + 1U] * fftSize) / wavInfo.sampleRate;
        float32_t maximum = 0.0f;
        float32_t compressed = 0.0f;
        uint32_t bin;
        if (first < 1U) first = 1U;
        if (last <= first) last = first + 1U;
        if (first >= fftSize / 2U)
        {
            nextLevels[i] = 0U;
            continue;
        }
        if (last > fftSize / 2U) last = fftSize / 2U;
        for (bin = first; bin < last; bin++)
        {
            if (spectrumMagnitude[bin] > maximum) maximum = spectrumMagnitude[bin];
        }
        maximum *= 4.0f / (float32_t)fftSize;
        if (maximum > 0.00005f) (void)arm_sqrt_f32(maximum, &compressed);
        compressed *= 135.0f;
        if (compressed > 100.0f) compressed = 100.0f;
        nextLevels[i] = (uint8_t)compressed;
    }

    osMutexAcquire(spectrumMutex, osWaitForever);
    memcpy(sharedSpectrum, nextLevels, sizeof(sharedSpectrum));
    spectrumRevision++;
    osMutexRelease(spectrumMutex);
}

static void snapshotCommit(const char *status)
{
    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.revision++;
    sharedSnapshot.volume = volume;
    if (status != NULL)
    {
        strncpy(sharedSnapshot.status, status, sizeof(sharedSnapshot.status) - 1U);
        sharedSnapshot.status[sizeof(sharedSnapshot.status) - 1U] = '\0';
    }
    osMutexRelease(snapshotMutex);
}

static void setState(MediaState state, const char *status)
{
    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.state = (uint8_t)state;
    sharedSnapshot.volume = volume;
    sharedSnapshot.revision++;
    if (status != NULL)
    {
        strncpy(sharedSnapshot.status, status, sizeof(sharedSnapshot.status) - 1U);
        sharedSnapshot.status[sizeof(sharedSnapshot.status) - 1U] = '\0';
    }
    osMutexRelease(snapshotMutex);
}

static uint8_t hasExtension(const char *name, const char extension[4])
{
    size_t length = strlen(name);
    return (length >= 4U && name[length - 4U] == '.' &&
            tolower((unsigned char)name[length - 3U]) == extension[0] &&
            tolower((unsigned char)name[length - 2U]) == extension[1] &&
            tolower((unsigned char)name[length - 1U]) == extension[2]);
}

static uint8_t hasWavExtension(const char *name)
{
    return hasExtension(name, "wav");
}

static uint8_t hasMp3Extension(const char *name)
{
    return hasExtension(name, "mp3");
}

static uint16_t readLe16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t readLe32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int16_t decodePcmSample(const uint8_t *source, uint16_t bitsPerSample)
{
    int32_t sample;
    switch (bitsPerSample)
    {
    case 8U:
        sample = ((int32_t)source[0] - 128) << 8;
        break;
    case 16U:
        sample = (int16_t)readLe16(source);
        break;
    case 24U:
        sample = (int32_t)((uint32_t)source[0] | ((uint32_t)source[1] << 8) |
                           ((uint32_t)source[2] << 16));
        if ((sample & 0x00800000L) != 0L) sample |= (int32_t)0xFF000000L;
        sample >>= 8;
        break;
    case 32U:
        sample = (int32_t)readLe32(source) >> 16;
        break;
    default:
        sample = 0;
        break;
    }
    return (int16_t)sample;
}

static uint8_t parseWav(FIL *file, WavInfo *info)
{
    uint8_t header[12];
    uint8_t chunk[8];
    uint8_t fmt[16];
    UINT bytesRead;
    uint8_t haveFmt = 0U;
    uint8_t haveData = 0U;

    memset(info, 0, sizeof(*info));
    if (f_lseek(file, 0U) != FR_OK ||
        f_read(file, header, sizeof(header), &bytesRead) != FR_OK || bytesRead != sizeof(header) ||
        memcmp(header, "RIFF", 4U) != 0 || memcmp(&header[8], "WAVE", 4U) != 0)
    {
        return 0U;
    }

    while (f_tell(file) + sizeof(chunk) <= f_size(file))
    {
        uint32_t size;
        uint32_t next;
        if (f_read(file, chunk, sizeof(chunk), &bytesRead) != FR_OK || bytesRead != sizeof(chunk))
        {
            return 0U;
        }
        size = readLe32(&chunk[4]);
        next = f_tell(file) + size + (size & 1U);
        if (memcmp(chunk, "fmt ", 4U) == 0)
        {
            if (size < sizeof(fmt) || f_read(file, fmt, sizeof(fmt), &bytesRead) != FR_OK || bytesRead != sizeof(fmt))
            {
                return 0U;
            }
            if (readLe16(fmt) != 1U)
            {
                return 0U;
            }
            info->channels = readLe16(&fmt[2]);
            info->sampleRate = readLe32(&fmt[4]);
            info->byteRate = readLe32(&fmt[8]);
            info->blockAlign = readLe16(&fmt[12]);
            info->bitsPerSample = readLe16(&fmt[14]);
            haveFmt = 1U;
        }
        else if (memcmp(chunk, "data", 4U) == 0)
        {
            info->dataOffset = f_tell(file);
            info->dataSize = size;
            haveData = 1U;
            if (haveFmt != 0U)
            {
                break;
            }
        }
        if (f_lseek(file, next) != FR_OK)
        {
            return 0U;
        }
    }

    return (haveFmt != 0U && haveData != 0U &&
            (info->channels == 1U || info->channels == 2U) &&
            (info->bitsPerSample == 8U || info->bitsPerSample == 16U ||
             info->bitsPerSample == 24U || info->bitsPerSample == 32U) &&
            info->sampleRate != 0U && info->byteRate != 0U &&
            info->blockAlign == (uint16_t)(info->channels * (info->bitsPerSample / 8U)));
}

static uint32_t getMp3DataOffset(void)
{
    uint8_t header[10];
    UINT bytesRead = 0U;
    uint32_t offset = 0U;

    if (f_lseek(&audioFile, 0U) != FR_OK) return 0U;
    if (f_read(&audioFile, header, sizeof(header), &bytesRead) != FR_OK || bytesRead != sizeof(header))
    {
        (void)f_lseek(&audioFile, 0U);
        return 0U;
    }

    if (memcmp(header, "ID3", 3U) == 0 &&
        (header[6] & 0x80U) == 0U && (header[7] & 0x80U) == 0U &&
        (header[8] & 0x80U) == 0U && (header[9] & 0x80U) == 0U)
    {
        uint32_t tagSize = ((uint32_t)header[6] << 21) |
                           ((uint32_t)header[7] << 14) |
                           ((uint32_t)header[8] << 7) |
                           (uint32_t)header[9];
        offset = 10U + tagSize;
        /* ID3v2.4 may append a 10-byte footer when flag 0x10 is set. */
        if (header[3] == 4U && (header[5] & 0x10U) != 0U) offset += 10U;
        if (offset >= f_size(&audioFile)) offset = 0U;
    }

    (void)f_lseek(&audioFile, offset);
    return offset;
}

static uint8_t resetMp3Decoder(uint32_t position)
{
    if (f_lseek(&audioFile, position) != FR_OK) return 0U;
    mp3dec_init(&mp3Decoder);
    mp3InputFilled = 0U;
    mp3InputConsumed = 0U;
    mp3FrameSamples = 0U;
    mp3FramePosition = 0U;
    mp3Eof = 0U;
    return 1U;
}

static uint8_t refillMp3Input(void)
{
    uint32_t remaining = mp3InputFilled - mp3InputConsumed;

    if (remaining >= (MP3_INPUT_BUFFER_BYTES / 2U) || mp3Eof != 0U) return 1U;

    if (mp3InputConsumed != 0U)
    {
        memmove(mp3InputBuffer, &mp3InputBuffer[mp3InputConsumed], remaining);
        mp3InputFilled = remaining;
        mp3InputConsumed = 0U;
    }

    while (mp3InputFilled < MP3_INPUT_BUFFER_BYTES)
    {
        UINT bytesRead = 0U;
        UINT request = (UINT)(MP3_INPUT_BUFFER_BYTES - mp3InputFilled);
        if (request > MP3_READ_CHUNK_BYTES) request = MP3_READ_CHUNK_BYTES;
        if (activeSource == MEDIA_SOURCE_RADIO)
        {
            bytesRead = (UINT)InternetRadio_Read(&mp3InputBuffer[mp3InputFilled], request);
            if (bytesRead == 0U) break;
        }
        else
        {
            FRESULT result = f_read(&audioFile, &mp3InputBuffer[mp3InputFilled], request, &bytesRead);
            if (result != FR_OK) return 0U;
        }
        mp3InputFilled += bytesRead;
        if (activeSource != MEDIA_SOURCE_RADIO && bytesRead < request)
        {
            mp3Eof = 1U;
            break;
        }
    }
    return 1U;
}

/* 0 = decode/read error, 1 = decoded frame available, 2 = clean EOF. */
static uint8_t decodeNextMp3Frame(void)
{
    uint32_t attempts;
    for (attempts = 0U; attempts < 64U; attempts++)
    {
        mp3dec_frame_info_t info;
        uint32_t available;
        int samples;

        if (refillMp3Input() == 0U) return 0U;
        available = mp3InputFilled - mp3InputConsumed;
        if (available == 0U)
            return activeSource == MEDIA_SOURCE_RADIO ? 3U : (mp3Eof != 0U ? 2U : 0U);

        memset(&info, 0, sizeof(info));
        samples = mp3dec_decode_frame(&mp3Decoder,
                                      &mp3InputBuffer[mp3InputConsumed],
                                      (int)available,
                                      mp3FrameBuffer,
                                      &info);
        if (info.frame_bytes > 0)
        {
            mp3InputConsumed += (uint32_t)info.frame_bytes;
        }

        if (samples > 0)
        {
            if ((info.channels != 1 && info.channels != 2) || info.hz <= 0) return 0U;
            if (mp3SampleRate == 0U)
            {
                mp3SampleRate = (uint32_t)info.hz;
                mp3Channels = (uint8_t)info.channels;
                mp3ByteRate = info.bitrate_kbps > 0 ? (uint32_t)info.bitrate_kbps * 125U : 0U;
            }
            else if (mp3SampleRate != (uint32_t)info.hz || mp3Channels != (uint8_t)info.channels)
            {
                /* WM8994/SAI sample rate cannot be changed in the middle of a DMA stream. */
                return 0U;
            }
            if (mp3ByteRate == 0U && info.bitrate_kbps > 0)
                mp3ByteRate = (uint32_t)info.bitrate_kbps * 125U;

            mp3FrameSamples = (uint32_t)samples;
            mp3FramePosition = 0U;
            return 1U;
        }

        if (info.frame_bytes == 0)
        {
            if (mp3Eof != 0U) return 2U;
            /* A valid frame may straddle the current read window. Force a
               refill; 16 KB gives minimp3 ample sync look-ahead. */
            if (mp3InputConsumed != 0U)
            {
                uint32_t remaining = mp3InputFilled - mp3InputConsumed;
                memmove(mp3InputBuffer, &mp3InputBuffer[mp3InputConsumed], remaining);
                mp3InputFilled = remaining;
                mp3InputConsumed = 0U;
            }
            if (refillMp3Input() == 0U) return 0U;
            if ((mp3InputFilled - mp3InputConsumed) == available && mp3Eof == 0U)
                return activeSource == MEDIA_SOURCE_RADIO ? 3U : 0U;
        }
    }
    return 0U;
}

/* Decode one ADTS AAC/HE-AAC frame from the same compressed input window used
   by minimp3.  Helix expands SBR streams to their real output sample rate. */
static uint8_t validateAdtsFrame(const uint8_t *frame, uint32_t available,
                                 uint32_t *frameBytes)
{
    uint32_t headerBytes;
    uint32_t length;
    uint8_t sampleRateIndex;
    uint8_t channelConfig;

    if (available < 7U) return 2U;
    if (frame[0] != 0xFFU || (frame[1] & 0xF6U) != 0xF0U) return 0U;
    sampleRateIndex = (uint8_t)((frame[2] >> 2) & 0x0FU);
    channelConfig = (uint8_t)(((frame[2] & 0x01U) << 2) | (frame[3] >> 6));
    if (sampleRateIndex >= 12U || channelConfig == 0U || channelConfig > 2U ||
        (frame[6] & 0x03U) != 0U)
        return 0U;

    headerBytes = (frame[1] & 0x01U) != 0U ? 7U : 9U;
    length = ((uint32_t)(frame[3] & 0x03U) << 11) |
             ((uint32_t)frame[4] << 3) |
             ((uint32_t)frame[5] >> 5);
    if (length < headerBytes || length > AAC_ADTS_MAX_FRAME_BYTES) return 0U;
    *frameBytes = length;
    return available >= length ? 1U : 2U;
}

static uint8_t decodeNextAacFrame(void)
{
    uint32_t attempts;
    for (attempts = 0U; attempts < 64U; attempts++)
    {
        uint32_t available;
        uint32_t originalConsumed;
        int syncOffset;
        int bytesLeft;
        int result;
        uint32_t frameBytes;
        uint8_t frameState;
        unsigned char *input;
        AACFrameInfo info;

        if (refillMp3Input() == 0U) return 0U;
        available = mp3InputFilled - mp3InputConsumed;
        if (available < 2U) return activeSource == MEDIA_SOURCE_RADIO ? 3U : 2U;

        syncOffset = AACFindSyncWord(&mp3InputBuffer[mp3InputConsumed], (int)available);
        if (syncOffset < 0)
        {
            /* Preserve the final byte because it may be the first 0xff of an
               ADTS sync word split across two TCP packets. */
            mp3InputConsumed += available - 1U;
            continue;
        }
        mp3InputConsumed += (uint32_t)syncOffset;
        originalConsumed = mp3InputConsumed;
        available = mp3InputFilled - mp3InputConsumed;
        frameState = validateAdtsFrame(&mp3InputBuffer[mp3InputConsumed], available, &frameBytes);
        if (frameState == 0U)
        {
            aacInvalidFrames++;
            mp3InputConsumed = originalConsumed + 1U;
            continue;
        }
        if (frameState == 2U)
        {
            uint32_t before = available;
            if (refillMp3Input() == 0U) return 0U;
            if ((mp3InputFilled - mp3InputConsumed) == before)
            {
                aacInputUnderflows++;
                return activeSource == MEDIA_SOURCE_RADIO ? 3U : 0U;
            }
            continue;
        }
        input = &mp3InputBuffer[mp3InputConsumed];
        bytesLeft = (int)frameBytes;
        {
            uint32_t cycleStart = DWT->CYCCNT;
            uint32_t elapsed;
            result = AACDecode(aacDecoder, &input, &bytesLeft, mp3FrameBuffer);
            elapsed = DWT->CYCCNT - cycleStart;
            aacDecodeCyclesTotal += elapsed;
            aacDecodeCalls++;
            if (elapsed > aacDecodeCyclesMax) aacDecodeCyclesMax = elapsed;
        }
        if (result == ERR_AAC_INDATA_UNDERFLOW)
        {
            aacInputUnderflows++;
            mp3InputConsumed = originalConsumed + frameBytes;
            (void)AACFlushCodec(aacDecoder);
            continue;
        }
        if (result != ERR_AAC_NONE)
        {
            /* A station switch can leave a partial old frame in flight. Move
               one byte and let the ADTS scanner reacquire cleanly. */
            aacInvalidFrames++;
            mp3InputConsumed = originalConsumed + frameBytes;
            (void)AACFlushCodec(aacDecoder);
            continue;
        }

        /* ADTS length is authoritative.  Do not let an old decoder's internal
           bytes-left result move the TCP input cursor outside this frame. */
        mp3InputConsumed = originalConsumed + frameBytes;
        memset(&info, 0, sizeof(info));
        AACGetLastFrameInfo(aacDecoder, &info);
        if ((info.nChans != 1 && info.nChans != 2) || info.sampRateOut <= 0 ||
            info.outputSamps <= 0 || info.outputSamps > (int)AAC_MAX_OUTPUT_SAMPLES)
            return 0U;

        if (mp3SampleRate == 0U)
        {
            mp3SampleRate = (uint32_t)info.sampRateOut;
            mp3Channels = (uint8_t)info.nChans;
            mp3ByteRate = info.bitRate > 0 ? (uint32_t)info.bitRate / 8U : 0U;
        }
        else if (mp3SampleRate != (uint32_t)info.sampRateOut ||
                 mp3Channels != (uint8_t)info.nChans)
        {
            return 0U;
        }
        mp3FrameSamples = (uint32_t)info.outputSamps / (uint32_t)info.nChans;
        mp3FramePosition = 0U;
        return 1U;
    }
    return 0U;
}

static uint8_t prepareMp3(void)
{
    uint8_t result;
    mp3SampleRate = 0U;
    mp3ByteRate = 0U;
    mp3Channels = 0U;
    mp3FileSize = f_size(&audioFile);
    mp3DataOffset = getMp3DataOffset();
    if (resetMp3Decoder(mp3DataOffset) == 0U) return 0U;

    result = decodeNextMp3Frame();
    if (result != 1U || mp3SampleRate == 0U || mp3ByteRate == 0U) return 0U;

    memset(&wavInfo, 0, sizeof(wavInfo));
    wavInfo.dataOffset = mp3DataOffset;
    wavInfo.dataSize = mp3FileSize > mp3DataOffset ? mp3FileSize - mp3DataOffset : 0U;
    wavInfo.byteRate = mp3ByteRate;
    wavInfo.sampleRate = mp3SampleRate;
    wavInfo.channels = 2U;       /* Output is always expanded to stereo PCM. */
    wavInfo.bitsPerSample = 16U;
    wavInfo.blockAlign = 4U;
    return 1U;
}

/* Pull decoded local PCM without assuming that one decoder read must equal one
   SAI half.  The time/pitch engine deliberately consumes a variable number of
   source frames while always producing a fixed-size DMA half. */
static uint8_t decodeLocalCompressedFrames(int16_t *output, uint32_t wanted,
                                           uint32_t *written, uint8_t *eof)
{
    uint32_t framesWritten = 0U;
    *eof = 0U;
    while (framesWritten < wanted)
    {
        uint32_t available;
        uint32_t take;
        uint32_t i;
        if (mp3FramePosition >= mp3FrameSamples)
        {
            uint8_t result = decodeNextMp3Frame();
            if (result == 0U) return 0U;
            if (result == 2U)
            {
                *eof = 1U;
                break;
            }
        }
        available = mp3FrameSamples - mp3FramePosition;
        take = wanted - framesWritten;
        if (take > available) take = available;
        if (mp3Channels == 2U)
        {
            memcpy(&output[framesWritten * 2U],
                   &mp3FrameBuffer[mp3FramePosition * 2U], take * 4U);
        }
        else
        {
            for (i = 0U; i < take; i++)
            {
                int16_t sample = mp3FrameBuffer[mp3FramePosition + i];
                output[(framesWritten + i) * 2U] = sample;
                output[(framesWritten + i) * 2U + 1U] = sample;
            }
        }
        mp3FramePosition += take;
        framesWritten += take;
    }
    *written = framesWritten;
    return 1U;
}

static uint8_t decodeLocalWavFrames(int16_t *output, uint32_t wanted,
                                    uint32_t *written, uint8_t *eof)
{
    uint32_t framesWritten = 0U;
    uint16_t bytesPerSample = wavInfo.bitsPerSample / 8U;
    FRESULT result = FR_OK;
    *eof = 0U;

    while (framesWritten < wanted && dataRemaining >= wavInfo.blockAlign)
    {
        UINT requested;
        UINT bytesRead = 0U;
        uint32_t framesRead;
        uint32_t frame;
        uint32_t framesWanted = wanted - framesWritten;
        uint32_t rawFrames = MEDIA_RAW_BUFFER_BYTES / wavInfo.blockAlign;
        if (framesWanted > rawFrames) framesWanted = rawFrames;
        requested = (UINT)(framesWanted * wavInfo.blockAlign);
        if (requested > dataRemaining) requested = (UINT)dataRemaining;
        requested -= requested % wavInfo.blockAlign;
        if (requested == 0U) break;
        while (bytesRead < requested)
        {
            UINT chunkRead = 0U;
            UINT chunk = requested - bytesRead;
            if (chunk >= 512U) chunk = 511U;
            chunk -= chunk % wavInfo.blockAlign;
            if (chunk == 0U) break;
            result = f_read(&audioFile, &rawBuffer[bytesRead], chunk, &chunkRead);
            bytesRead += chunkRead;
            if (result != FR_OK || chunkRead != chunk) break;
        }
        if (result != FR_OK) return 0U;
        framesRead = bytesRead / wavInfo.blockAlign;
        bytesRead = (UINT)(framesRead * wavInfo.blockAlign);
        for (frame = 0U; frame < framesRead; frame++)
        {
            const uint8_t *input = &rawBuffer[frame * wavInfo.blockAlign];
            int16_t left = decodePcmSample(input, wavInfo.bitsPerSample);
            int16_t right = left;
            if (wavInfo.channels == 2U)
                right = decodePcmSample(input + bytesPerSample, wavInfo.bitsPerSample);
            output[(framesWritten + frame) * 2U] = left;
            output[(framesWritten + frame) * 2U + 1U] = right;
        }
        framesWritten += framesRead;
        dataRemaining -= bytesRead;
        if (bytesRead < requested || framesRead == 0U) break;
    }
    if (dataRemaining < wavInfo.blockAlign) dataRemaining = 0U;
    if (dataRemaining == 0U) *eof = 1U;
    *written = framesWritten;
    return 1U;
}

static void resetTimePitch(void)
{
    memset(timePitchGrains, 0, sizeof(timePitchGrains));
    timePitchSourceEof = 0U;
    timePitchSourceWrite = 0U;
    timePitchNextStartQ16 = 0U;
    timePitchOutputFrame = 0U;
}

static uint8_t ensureTimePitchSource(uint32_t needed)
{
    while (timePitchSourceWrite < needed && timePitchSourceEof == 0U)
    {
        uint32_t slot = timePitchSourceWrite & TIMEPITCH_RING_MASK;
        uint32_t request = needed - timePitchSourceWrite;
        uint32_t contiguous = TIMEPITCH_RING_FRAMES - slot;
        uint32_t written = 0U;
        uint8_t eof = 0U;
        uint8_t ok;
        if (request > contiguous) request = contiguous;
        if (request > 4096U) request = 4096U;
        ok = currentFormat == MEDIA_FORMAT_MP3 ?
             decodeLocalCompressedFrames(&timePitchSource[slot * 2U], request, &written, &eof) :
             decodeLocalWavFrames(&timePitchSource[slot * 2U], request, &written, &eof);
        if (ok == 0U) return 0U;
        timePitchSourceWrite += written;
        if (eof != 0U || written == 0U) timePitchSourceEof = 1U;
    }
    return 1U;
}

static int16_t timePitchSample(uint64_t positionQ16, uint8_t channel)
{
    uint32_t frame = (uint32_t)(positionQ16 >> 16);
    uint32_t fraction = (uint32_t)positionQ16 & 0xFFFFU;
    int32_t a;
    int32_t b;
    if (frame >= timePitchSourceWrite) return 0;
    a = timePitchSource[((frame & TIMEPITCH_RING_MASK) * 2U) + channel];
    if (frame + 1U >= timePitchSourceWrite) return (int16_t)a;
    b = timePitchSource[(((frame + 1U) & TIMEPITCH_RING_MASK) * 2U) + channel];
    return (int16_t)(a + (int32_t)(((int64_t)(b - a) * (int64_t)fraction) >> 16));
}

/* Normalized stereo correlation is the key quality step missing from a plain
   granular player.  It is equivalent in purpose to SoundTouch's WSOLA seek:
   choose the waveform-compatible join nearest the requested source position. */
static float32_t timePitchCorrelation(const TimePitchGrain *previous,
                                      uint64_t candidateQ16,
                                      uint32_t candidatePitchQ16)
{
    int64_t cross = 0;
    uint64_t previousEnergy = 0U;
    uint64_t candidateEnergy = 0U;
    uint32_t i;
    for (i = 0U; i < TIMEPITCH_OVERLAP_FRAMES; i += TIMEPITCH_CORR_STRIDE)
    {
        uint64_t previousPosition = previous->startQ16 +
                                    (uint64_t)(TIMEPITCH_HOP_FRAMES + i) * previous->pitchQ16;
        uint64_t candidatePosition = candidateQ16 + (uint64_t)i * candidatePitchQ16;
        int32_t a = ((int32_t)timePitchSample(previousPosition, 0U) +
                     (int32_t)timePitchSample(previousPosition, 1U)) >> 1;
        int32_t b = ((int32_t)timePitchSample(candidatePosition, 0U) +
                     (int32_t)timePitchSample(candidatePosition, 1U)) >> 1;
        cross += (int64_t)a * b;
        previousEnergy += (uint64_t)((int64_t)a * a);
        candidateEnergy += (uint64_t)((int64_t)b * b);
    }
    if (cross <= 0 || previousEnergy == 0U || candidateEnergy == 0U) return 0.0f;
    return ((float32_t)cross * (float32_t)cross) /
           ((float32_t)previousEnergy * (float32_t)candidateEnergy);
}

static uint64_t alignTimePitchStart(uint64_t predictedQ16,
                                    const TimePitchGrain *previous,
                                    uint32_t pitchQ16)
{
    int32_t offset;
    int32_t bestOffset = 0;
    float32_t bestScore = -1.0f;
    if (previous == NULL || previous->active == 0U) return predictedQ16;

    /* Quick-seek first, then refine around the winner. This retains nearly all
       of the correlation benefit without a full 1025-position scan. */
    for (offset = -TIMEPITCH_SEEK_FRAMES; offset <= TIMEPITCH_SEEK_FRAMES;
         offset += TIMEPITCH_COARSE_STEP)
    {
        int64_t candidateSigned = (int64_t)predictedQ16 + ((int64_t)offset << 16);
        uint64_t candidate;
        uint32_t lastFrame;
        float32_t score;
        if (candidateSigned < 0) continue;
        candidate = (uint64_t)candidateSigned;
        lastFrame = (uint32_t)((candidate +
                    (uint64_t)(TIMEPITCH_OVERLAP_FRAMES - 1U) * pitchQ16) >> 16);
        if (lastFrame >= timePitchSourceWrite) continue;
        score = timePitchCorrelation(previous, candidate, pitchQ16) -
                0.00002f * (float32_t)(offset < 0 ? -offset : offset);
        if (score > bestScore)
        {
            bestScore = score;
            bestOffset = offset;
        }
    }
    {
        int32_t first = bestOffset - (TIMEPITCH_COARSE_STEP - 1);
        int32_t last = bestOffset + (TIMEPITCH_COARSE_STEP - 1);
        if (first < -TIMEPITCH_SEEK_FRAMES) first = -TIMEPITCH_SEEK_FRAMES;
        if (last > TIMEPITCH_SEEK_FRAMES) last = TIMEPITCH_SEEK_FRAMES;
        for (offset = first; offset <= last; offset += TIMEPITCH_REFINE_STEP)
        {
            int64_t candidateSigned = (int64_t)predictedQ16 + ((int64_t)offset << 16);
            uint64_t candidate;
            uint32_t lastFrame;
            float32_t score;
            if (candidateSigned < 0) continue;
            candidate = (uint64_t)candidateSigned;
            lastFrame = (uint32_t)((candidate +
                        (uint64_t)(TIMEPITCH_OVERLAP_FRAMES - 1U) * pitchQ16) >> 16);
            if (lastFrame >= timePitchSourceWrite) continue;
            score = timePitchCorrelation(previous, candidate, pitchQ16) -
                    0.00002f * (float32_t)(offset < 0 ? -offset : offset);
            if (score > bestScore)
            {
                bestScore = score;
                bestOffset = offset;
            }
        }
    }
    return (uint64_t)((int64_t)predictedQ16 + ((int64_t)bestOffset << 16));
}

static uint8_t fillTimePitchHalf(uint32_t offset)
{
    int16_t *output = (int16_t *)&audioBuffer[offset];
    uint32_t futureStart = (uint32_t)(timePitchNextStartQ16 >> 16);
    uint32_t futureAdvance = (uint32_t)(timePitchSpeed * (float32_t)(MEDIA_FRAMES_PER_HALF + TIMEPITCH_HOP_FRAMES));
    uint32_t pitchSpan = (TIMEPITCH_GRAIN_FRAMES * timePitchPitchQ16) >> 16;
    uint32_t out;
    uint8_t anySignal = 0U;
    uint32_t cycleStart = DWT->CYCCNT;

    if (ensureTimePitchSource(futureStart + futureAdvance + pitchSpan +
                              (uint32_t)TIMEPITCH_SEEK_FRAMES + 2U) == 0U)
        return 0U;

    for (out = 0U; out < MEDIA_FRAMES_PER_HALF; out++, timePitchOutputFrame++)
    {
        int32_t left = 0;
        int32_t right = 0;
        uint8_t grain;
        if ((timePitchOutputFrame % TIMEPITCH_HOP_FRAMES) == 0U)
        {
            TimePitchGrain *next = &timePitchGrains[(timePitchOutputFrame / TIMEPITCH_HOP_FRAMES) % TIMEPITCH_GRAINS];
            TimePitchGrain *previous = NULL;
            for (grain = 0U; grain < TIMEPITCH_GRAINS; grain++)
            {
                if (&timePitchGrains[grain] != next && timePitchGrains[grain].active != 0U)
                {
                    previous = &timePitchGrains[grain];
                    break;
                }
            }
            next->startQ16 = alignTimePitchStart(timePitchNextStartQ16, previous,
                                                 timePitchPitchQ16);
            next->pitchQ16 = timePitchPitchQ16;
            next->age = 0U;
            next->active = (uint32_t)(next->startQ16 >> 16) < timePitchSourceWrite ? 1U : 0U;
            timePitchNextStartQ16 += (uint64_t)(timePitchSpeed * (float32_t)TIMEPITCH_HOP_FRAMES * 65536.0f);
        }
        for (grain = 0U; grain < TIMEPITCH_GRAINS; grain++)
        {
            TimePitchGrain *g = &timePitchGrains[grain];
            if (g->active != 0U)
            {
                uint64_t position = g->startQ16 + (uint64_t)g->age * g->pitchQ16;
                uint32_t window = timePitchWindowQ15[g->age];
                left += ((int32_t)timePitchSample(position, 0U) * (int32_t)window) >> 15;
                right += ((int32_t)timePitchSample(position, 1U) * (int32_t)window) >> 15;
                g->age++;
                if (g->age >= TIMEPITCH_GRAIN_FRAMES) g->active = 0U;
                anySignal = 1U;
            }
        }
        if (left > 32767) left = 32767;
        else if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        else if (right < -32768) right = -32768;
        output[out * 2U] = (int16_t)left;
        output[out * 2U + 1U] = (int16_t)right;
    }

    if (timePitchSourceEof != 0U && (uint32_t)(timePitchNextStartQ16 >> 16) >= timePitchSourceWrite && anySignal == 0U)
    {
        if (endHalf == 0U) endHalf = offset == 0U ? 1U : 2U;
    }
    processSoftwareEq(output, MEDIA_FRAMES_PER_HALF);
    analyzeSpectrum(output, MEDIA_FFT_SIZE);
    SCB_CleanDCache_by_Addr((uint32_t *)&audioBuffer[offset], MEDIA_AUDIO_HALF_BYTES);
    {
        uint32_t cycles = DWT->CYCCNT - cycleStart;
        timePitchProcessCalls++;
        if (cycles > timePitchProcessCyclesMax) timePitchProcessCyclesMax = cycles;
    }
    return 1U;
}

static uint8_t fillCompressedHalf(uint32_t offset)
{
    int16_t *output = (int16_t *)&audioBuffer[offset];
    uint32_t framesWritten = 0U;
    uint32_t fillCycleStart = DWT->CYCCNT;

    while (framesWritten < MEDIA_FRAMES_PER_HALF)
    {
        uint32_t available;
        uint32_t take;
        uint32_t i;

        if (mp3FramePosition >= mp3FrameSamples)
        {
            uint8_t decodeResult = currentFormat == MEDIA_FORMAT_AAC ?
                                   decodeNextAacFrame() : decodeNextMp3Frame();
            if (decodeResult == 0U)
            {
                memset(&output[framesWritten * 2U], 0,
                       MEDIA_AUDIO_HALF_BYTES - framesWritten * 4U);
                return 0U;
            }
            if (decodeResult == 2U)
            {
                memset(&output[framesWritten * 2U], 0,
                       MEDIA_AUDIO_HALF_BYTES - framesWritten * 4U);
                if (endHalf == 0U) endHalf = offset == 0U ? 1U : 2U;
                break;
            }
            if (decodeResult == 3U)
            {
                /* A live stream can briefly run behind the decoder. Keep DMA
                   alive with silence; the next half-buffer resumes at the next
                   complete MP3 frame without blocking the media task. */
                radioAudioStarvationEvents++;
                memset(&output[framesWritten * 2U], 0,
                       MEDIA_AUDIO_HALF_BYTES - framesWritten * 4U);
                framesWritten = MEDIA_FRAMES_PER_HALF;
                break;
            }
        }

        available = mp3FrameSamples - mp3FramePosition;
        take = MEDIA_FRAMES_PER_HALF - framesWritten;
        if (take > available) take = available;

        if (mp3Channels == 2U)
        {
            memcpy(&output[framesWritten * 2U],
                   &mp3FrameBuffer[mp3FramePosition * 2U],
                   take * 4U);
        }
        else
        {
            for (i = 0U; i < take; i++)
            {
                int16_t sample = mp3FrameBuffer[mp3FramePosition + i];
                output[(framesWritten + i) * 2U] = sample;
                output[(framesWritten + i) * 2U + 1U] = sample;
            }
        }
        mp3FramePosition += take;
        framesWritten += take;
        if (currentFormat == MEDIA_FORMAT_AAC && mp3FramePosition >= mp3FrameSamples &&
            framesWritten < MEDIA_FRAMES_PER_HALF)
        {
            /* Media runs above the GUI.  One tick between HE-AAC frames breaks
               a 20+ ms decode burst into scheduler-friendly pieces. */
            osDelay(1U);
        }
    }

    processSoftwareEq(output, framesWritten);
    analyzeSpectrum(output, framesWritten > MEDIA_FFT_SIZE ? MEDIA_FFT_SIZE : framesWritten);
    SCB_CleanDCache_by_Addr((uint32_t *)&audioBuffer[offset], MEDIA_AUDIO_HALF_BYTES);
    if (currentFormat == MEDIA_FORMAT_AAC)
    {
        uint32_t elapsed = DWT->CYCCNT - fillCycleStart;
        aacFillCyclesTotal += elapsed;
        aacFillCalls++;
        if (elapsed > aacFillCyclesMax) aacFillCyclesMax = elapsed;
    }
    return 1U;
}

static void closeTrack(void)
{
    if (codecReady != 0U)
    {
        BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
    }
    if (fileOpen != 0U)
    {
        f_close(&audioFile);
        fileOpen = 0U;
    }
    refillFlags = 0U;
    endHalf = 0U;
    dataRemaining = 0U;
    mp3InputFilled = 0U;
    mp3InputConsumed = 0U;
    mp3FrameSamples = 0U;
    mp3FramePosition = 0U;
    mp3Eof = 0U;
    timePitchRunning = 0U;
    resetTimePitch();
    clearSpectrum();
}

static uint8_t fillHalf(uint32_t offset)
{
    if (activeSource == MEDIA_SOURCE_WAV && timePitchRunning != 0U)
        return fillTimePitchHalf(offset);
    if (currentFormat == MEDIA_FORMAT_MP3 || currentFormat == MEDIA_FORMAT_AAC)
        return fillCompressedHalf(offset);

    uint32_t framesWritten = 0U;
    uint16_t bytesPerSample = wavInfo.bitsPerSample / 8U;
    uint8_t *destination = &audioBuffer[offset];
    int16_t *output = (int16_t *)destination;
    FRESULT result = FR_OK;

    while (framesWritten < MEDIA_FRAMES_PER_HALF && dataRemaining >= wavInfo.blockAlign)
    {
        UINT requested;
        UINT bytesRead = 0U;
        uint32_t framesRead;
        uint32_t frame;
        uint32_t framesWanted = MEDIA_FRAMES_PER_HALF - framesWritten;
        uint32_t rawFrames = MEDIA_RAW_BUFFER_BYTES / wavInfo.blockAlign;

        if (framesWanted > rawFrames) framesWanted = rawFrames;
        requested = (UINT)(framesWanted * wavInfo.blockAlign);
        if (requested > dataRemaining) requested = (UINT)dataRemaining;
        requested -= requested % wavInfo.blockAlign;
        if (requested == 0U) break;

        /* Keep each FatFs request below one sector. This avoids the
           problematic multi-block path on older SDSC cards. */
        while (bytesRead < requested)
        {
            UINT chunkRead = 0U;
            UINT chunk = requested - bytesRead;
            if (chunk >= 512U) chunk = 511U;
            chunk -= chunk % wavInfo.blockAlign;
            if (chunk == 0U) break;
            result = f_read(&audioFile, &rawBuffer[bytesRead], chunk, &chunkRead);
            bytesRead += chunkRead;
            if (result != FR_OK || chunkRead != chunk) break;
        }
        if (result != FR_OK)
        {
            memset(destination, 0, MEDIA_AUDIO_HALF_BYTES);
            return 0U;
        }

        framesRead = bytesRead / wavInfo.blockAlign;
        bytesRead = (UINT)(framesRead * wavInfo.blockAlign);
        for (frame = 0U; frame < framesRead; frame++)
        {
            const uint8_t *input = &rawBuffer[frame * wavInfo.blockAlign];
            int16_t left = decodePcmSample(input, wavInfo.bitsPerSample);
            int16_t right = left;
            if (wavInfo.channels == 2U)
                right = decodePcmSample(input + bytesPerSample, wavInfo.bitsPerSample);
            output[(framesWritten + frame) * 2U] = left;
            output[(framesWritten + frame) * 2U + 1U] = right;
        }

        framesWritten += framesRead;
        dataRemaining -= bytesRead;
        if (bytesRead < requested || framesRead == 0U) break;
    }

    memset(&output[framesWritten * 2U], 0, MEDIA_AUDIO_HALF_BYTES - framesWritten * 4U);
    processSoftwareEq(output, framesWritten);
    analyzeSpectrum(output, framesWritten > MEDIA_FFT_SIZE ? MEDIA_FFT_SIZE : framesWritten);

    if (dataRemaining < wavInfo.blockAlign) dataRemaining = 0U;
    if ((dataRemaining == 0U || framesWritten < MEDIA_FRAMES_PER_HALF) && endHalf == 0U)
    {
        endHalf = offset == 0U ? 1U : 2U;
    }
    SCB_CleanDCache_by_Addr((uint32_t *)destination, MEDIA_AUDIO_HALF_BYTES);
    return 1U;
}

/* Every SAI refill has a hard DMA deadline.  Briefly boost WAV/MP3 as well as
   AAC so an expensive TouchGFX frame cannot turn an otherwise fast SD read or
   decode into an audible click.  Restore the normal priority immediately. */
static uint8_t fillRealtimeHalf(uint32_t offset)
{
    osThreadId_t task = osThreadGetId();
    osPriority_t previous = osThreadGetPriority(task);
    uint32_t cycleStart = DWT->CYCCNT;
    uint32_t elapsed;
    uint8_t boosted = 0U;
    uint8_t result;

    if (previous >= osPriorityIdle && previous < osPriorityAboveNormal &&
        osThreadSetPriority(task, osPriorityAboveNormal) == osOK)
    {
        boosted = 1U;
    }
    result = fillHalf(offset);
    elapsed = DWT->CYCCNT - cycleStart;
    mediaRefillCalls++;
    if (elapsed > mediaRefillCyclesMax) mediaRefillCyclesMax = elapsed;
    if (boosted != 0U) (void)osThreadSetPriority(task, previous);
    return result;
}

/* Keep one decoded HE-AAC frame ready between DMA callbacks.  This preserves
   the decoder's original ordering and internal-SRAM PCM path while cutting
   the deadline-critical refill burst roughly in half. */
static void predecodeRadioAacFrame(void)
{
    uint32_t now;
    uint32_t compressedAvailable;
    uint8_t result;

    if (activeSource == MEDIA_SOURCE_RADIO && currentFormat == MEDIA_FORMAT_AAC &&
        radioPlaybackStarted != 0U && mp3FramePosition >= mp3FrameSamples)
    {
        now = osKernelGetTickCount();
        compressedAvailable = (mp3InputFilled - mp3InputConsumed) + InternetRadio_Available();
        if (compressedAvailable < 7U ||
            (int32_t)(now - radioPredecodeRetryTick) < 0)
            return;

        result = decodeNextAacFrame();
        radioPredecodeRetryTick = result == 1U ? now : now + RADIO_PREDECODE_RETRY_TICKS;
    }
}

/* WM8994 default-mode EQ on the STM32746G-DISCO playback path:
   AIF1 Timeslot 0 -> DAC1. Slider 0..100 maps to -12..+12 dB. */
static uint16_t eqGainCode(uint8_t value)
{
    if (value > 100U) value = 100U;
    return (uint16_t)(((uint32_t)value * 24U + 50U) / 100U);
}

static void applyCodecEq(void)
{
    uint16_t gains1;
    uint16_t gains2;
    const uint8_t *bands = eqBands;
    static const uint8_t flatBands[MEDIA_EQ_BANDS] = {50U, 50U, 50U, 50U, 50U};
    if (codecReady == 0U) return;

    /* Local WAV/MP3 is equalized in PCM before FFT/SAI. Keep WM8994 flat to
       prevent double filtering; AUX/radio retain the hardware five-band EQ. */
    if (activeSource == MEDIA_SOURCE_WAV) bands = flatBands;

    gains1 = (uint16_t)((eqGainCode(bands[0]) << 11) |
                        (eqGainCode(bands[1]) << 6)  |
                        (eqGainCode(bands[2]) << 1)  | 0x0001U);
    gains2 = (uint16_t)((eqGainCode(bands[3]) << 11) |
                        (eqGainCode(bands[4]) << 6));

    /* Program bands 4/5 before enabling/updating bands 1..3. AUDIO_IO_Write
       already uses the shared recursive I2C mutex. */
    AUDIO_IO_Write(AUDIO_I2C_ADDRESS, 0x0481U, gains2);
    AUDIO_IO_Write(AUDIO_I2C_ADDRESS, 0x0480U, gains1);
}

static void configureAuxInputPath(void)
{
    /* ST's shared SetVolume() also changes the active ADC from 0x00 (mute)
       through 0xEF (+17.625 dB). AUX volume must only control headphones, so
       hold AIF1 ADC1 L/R at the datasheet's 0 dB code (0xC0 + update bit). */
    AUDIO_IO_Write(AUDIO_I2C_ADDRESS, 0x0400U, 0x01C0U);
    AUDIO_IO_Write(AUDIO_I2C_ADDRESS, 0x0401U, 0x01C0U);

    /* Disable DRC on both AIF1 ADC1 record channels. Its long gain-release
       envelope made clipped line input sound like an echo or reverb tail. */
    AUDIO_IO_Write(AUDIO_I2C_ADDRESS, 0x0440U, 0x0000U);
}

static void processAuxHalf(uint32_t offset)
{
    uint8_t *input = &auxInputBuffer[offset];
    uint8_t *output = &audioBuffer[offset];

    /* SAI RX DMA writes behind the M7 data cache. Invalidate before the CPU
       reads the captured half, then clean the copied TX half for SAI DMA. */
    SCB_InvalidateDCache_by_Addr((uint32_t *)input, AUX_AUDIO_HALF_BYTES);
    memcpy(output, input, AUX_AUDIO_HALF_BYTES);
    analyzeSpectrum((const int16_t *)output, MEDIA_FFT_SIZE);
    SCB_CleanDCache_by_Addr((uint32_t *)output, AUX_AUDIO_HALF_BYTES);
}

static void stopAuxHardware(void)
{
    activeSource = MEDIA_SOURCE_WAV;
    auxInputFlags = 0U;
    refillFlags = 0U;
    (void)BSP_AUDIO_IN_Stop(CODEC_PDWN_SW);
    (void)BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
    codecReady = 0U;
    clearSpectrum();
}

static uint8_t startAux(void)
{
    osMutexAcquire(snapshotMutex, osWaitForever);
    savedWavState = sharedSnapshot.state;
    savedWavTrack = sharedSnapshot.currentTrack;
    savedWavElapsed = sharedSnapshot.elapsedSeconds;
    strncpy(savedWavName, sharedSnapshot.currentName, sizeof(savedWavName) - 1U);
    savedWavName[sizeof(savedWavName) - 1U] = '\0';
    strncpy(savedWavStatus, sharedSnapshot.status, sizeof(savedWavStatus) - 1U);
    savedWavStatus[sizeof(savedWavStatus) - 1U] = '\0';
    osMutexRelease(snapshotMutex);

    seekPending = 0U;
    closeTrack();
    memset(audioBuffer, 0, AUX_AUDIO_BUFFER_BYTES);
    memset(auxInputBuffer, 0, sizeof(auxInputBuffer));
    SCB_CleanDCache_by_Addr((uint32_t *)audioBuffer, AUX_AUDIO_BUFFER_BYTES);
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)auxInputBuffer, AUX_AUDIO_BUFFER_BYTES);

    if (BSP_AUDIO_IN_OUT_Init(INPUT_DEVICE_INPUT_LINE_1, OUTPUT_DEVICE_BOTH,
                              AUDIO_FREQUENCY_48K, 16U, 2U) != AUDIO_OK)
    {
        codecReady = 0U;
        activeSource = MEDIA_SOURCE_WAV;
        setState(MEDIA_STATE_ERROR, "AUX input initialization failed");
        return 0U;
    }

    codecReady = 1U;
    activeSource = MEDIA_SOURCE_AUX;
    wavInfo.sampleRate = AUDIO_FREQUENCY_48K;
    BSP_AUDIO_OUT_SetVolume(volume);
    configureAuxInputPath();
    applyCodecEq();
    auxInputFlags = 0U;
    refillFlags = 0U;

    /* Arm RX before starting the master TX clock. Both circular DMAs then
       stay phase-aligned: each captured half is copied into the idle TX half. */
    if (BSP_AUDIO_IN_Record((uint16_t *)auxInputBuffer, AUX_AUDIO_BUFFER_BYTES / 2U) != AUDIO_OK ||
        BSP_AUDIO_OUT_Play((uint16_t *)audioBuffer, AUX_AUDIO_BUFFER_BYTES) != AUDIO_OK)
    {
        stopAuxHardware();
        setState(MEDIA_STATE_ERROR, "AUX DMA start failed");
        return 0U;
    }

    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_AUX;
    sharedSnapshot.state = MEDIA_STATE_PLAYING;
    sharedSnapshot.elapsedSeconds = 0U;
    sharedSnapshot.durationSeconds = 0U;
    strcpy(sharedSnapshot.currentName, "AUX");
    strcpy(sharedSnapshot.status, "AUX input");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
    return 1U;
}

static void leaveAux(uint8_t restoreWav);

static void enterPcMode(void)
{
    if (activeSource == MEDIA_SOURCE_AUX) leaveAux(0U);
    else if (activeSource == MEDIA_SOURCE_RADIO) leaveRadioMode(0U);
    else closeTrack();
    activeSource = MEDIA_SOURCE_PC;
    USBPCAudio_SetPlaybackEnabled(1U);
    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_PC;
    sharedSnapshot.state = MEDIA_STATE_PLAYING;
    sharedSnapshot.elapsedSeconds = 0U;
    sharedSnapshot.durationSeconds = 0U;
    strcpy(sharedSnapshot.currentName, "PC Modu");
    strcpy(sharedSnapshot.status, "USB Audio 48kHz / 16-bit");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void leavePcMode(void)
{
    /* Stop USB playback through its owner; never call the BSP directly here.
       USB remains enumerated and its OUT packets are safely discarded. */
    USBPCAudio_SetPlaybackEnabled(0U);
    USBPCAudio_Process();
    activeSource = MEDIA_SOURCE_WAV;
    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_WAV;
    sharedSnapshot.state = MEDIA_STATE_READY;
    strcpy(sharedSnapshot.status, "File mode");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void leaveAux(uint8_t restoreWav)
{
    stopAuxHardware();

    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_WAV;
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);

    if (restoreWav != 0U &&
        (savedWavState == MEDIA_STATE_PLAYING || savedWavState == MEDIA_STATE_PAUSED) &&
        savedWavTrack < sharedSnapshot.trackCount && startTrack(savedWavTrack) != 0U)
    {
        if (savedWavElapsed != 0U) seekTo(savedWavElapsed);
        if (savedWavState == MEDIA_STATE_PAUSED)
        {
            (void)BSP_AUDIO_OUT_Pause();
            setState(MEDIA_STATE_PAUSED, "Paused");
        }
        return;
    }

    if (restoreWav != 0U)
    {
        osMutexAcquire(snapshotMutex, osWaitForever);
        sharedSnapshot.state = savedWavState;
        sharedSnapshot.currentTrack = savedWavTrack;
        sharedSnapshot.elapsedSeconds = savedWavElapsed;
        strncpy(sharedSnapshot.currentName, savedWavName, sizeof(sharedSnapshot.currentName) - 1U);
        sharedSnapshot.currentName[sizeof(sharedSnapshot.currentName) - 1U] = '\0';
        strncpy(sharedSnapshot.status, savedWavStatus, sizeof(sharedSnapshot.status) - 1U);
        sharedSnapshot.status[sizeof(sharedSnapshot.status) - 1U] = '\0';
        sharedSnapshot.revision++;
        osMutexRelease(snapshotMutex);
    }
}

static uint8_t startRadioAudio(void)
{
    uint8_t result;
    InternetRadioSnapshot radio;
    if (activeSource != MEDIA_SOURCE_RADIO) return 0U;

    InternetRadio_GetSnapshot(&radio);
    if (radio.codec == INTERNET_RADIO_CODEC_AAC)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        aacDecodeCyclesTotal = 0U;
        aacDecodeCyclesMax = 0U;
        aacDecodeCalls = 0U;
        aacFillCyclesTotal = 0U;
        aacFillCyclesMax = 0U;
        aacFillCalls = 0U;
        aacInvalidFrames = 0U;
        aacInputUnderflows = 0U;
        radioAudioStarvationEvents = 0U;
        radioPredecodeRetryTick = 0U;
        aacDecoder = AACInitDecoderPre(aacDecoderWorkspace, sizeof(aacDecoderWorkspace));
        if (aacDecoder == NULL) return 0U;
        currentFormat = MEDIA_FORMAT_AAC;
    }
    else
    {
        mp3dec_init(&mp3Decoder);
        currentFormat = MEDIA_FORMAT_MP3;
    }
    mp3InputFilled = 0U;
    mp3InputConsumed = 0U;
    mp3FrameSamples = 0U;
    mp3FramePosition = 0U;
    mp3SampleRate = 0U;
    mp3ByteRate = 0U;
    mp3Channels = 0U;
    mp3Eof = 0U;
    endHalf = 0U;
    refillFlags = 0U;

    result = currentFormat == MEDIA_FORMAT_AAC ? decodeNextAacFrame() : decodeNextMp3Frame();
    if (result != 1U || mp3SampleRate == 0U) return 0U;
    if (mp3ByteRate == 0U) mp3ByteRate = 16000U; /* Groove Salad is 128 kbps. */

    memset(&wavInfo, 0, sizeof(wavInfo));
    wavInfo.byteRate = mp3ByteRate;
    wavInfo.sampleRate = mp3SampleRate;
    wavInfo.channels = 2U;
    wavInfo.bitsPerSample = 16U;
    wavInfo.blockAlign = 4U;

    if (fillHalf(0U) == 0U || fillHalf(MEDIA_AUDIO_HALF_BYTES) == 0U) return 0U;
    if (BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, volume, wavInfo.sampleRate) != AUDIO_OK)
        return 0U;

    codecReady = 1U;
    BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
    applyCodecEq();
    BSP_AUDIO_OUT_Play((uint16_t *)audioBuffer, MEDIA_AUDIO_BUFFER_BYTES);
    radioPlaybackStarted = 1U;

    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_RADIO;
    sharedSnapshot.state = MEDIA_STATE_PLAYING;
    sharedSnapshot.elapsedSeconds = 0U;
    sharedSnapshot.durationSeconds = 0U;
    strncpy(sharedSnapshot.currentName, radio.stationName, sizeof(sharedSnapshot.currentName) - 1U);
    sharedSnapshot.currentName[sizeof(sharedSnapshot.currentName) - 1U] = '\0';
    snprintf(sharedSnapshot.status, sizeof(sharedSnapshot.status), "Internet Radio %s",
             radio.codec == INTERNET_RADIO_CODEC_AAC ? "AAC+" : "MP3");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
    return 1U;
}

static void enterRadioMode(void)
{
    InternetRadioSnapshot radio;
    if (activeSource == MEDIA_SOURCE_WAV)
    {
        osMutexAcquire(snapshotMutex, osWaitForever);
        savedWavState = sharedSnapshot.state;
        savedWavTrack = sharedSnapshot.currentTrack;
        savedWavElapsed = sharedSnapshot.elapsedSeconds;
        strncpy(savedWavName, sharedSnapshot.currentName, sizeof(savedWavName) - 1U);
        savedWavName[sizeof(savedWavName) - 1U] = '\0';
        strncpy(savedWavStatus, sharedSnapshot.status, sizeof(savedWavStatus) - 1U);
        savedWavStatus[sizeof(savedWavStatus) - 1U] = '\0';
        osMutexRelease(snapshotMutex);
    }
    else if (activeSource == MEDIA_SOURCE_AUX)
    {
        leaveAux(0U);
    }
    else if (activeSource == MEDIA_SOURCE_PC)
    {
        leavePcMode();
    }

    seekPending = 0U;
    closeTrack();
    activeSource = MEDIA_SOURCE_RADIO;
    radioPlaybackStarted = 0U;
    radioRevisionSeen = 0U;
    InternetRadio_Start();
    InternetRadio_GetSnapshot(&radio);

    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_RADIO;
    sharedSnapshot.state = MEDIA_STATE_READY;
    sharedSnapshot.elapsedSeconds = 0U;
    sharedSnapshot.durationSeconds = 0U;
    strncpy(sharedSnapshot.currentName, radio.stationName, sizeof(sharedSnapshot.currentName) - 1U);
    sharedSnapshot.currentName[sizeof(sharedSnapshot.currentName) - 1U] = '\0';
    strcpy(sharedSnapshot.status, "Radio connecting...");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void changeRadioStation(uint8_t next)
{
    InternetRadioSnapshot radio;
    closeTrack();
    activeSource = MEDIA_SOURCE_RADIO;
    radioPlaybackStarted = 0U;
    if (next != 0U) InternetRadio_Next();
    else InternetRadio_Previous();
    InternetRadio_GetSnapshot(&radio);

    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_RADIO;
    sharedSnapshot.state = MEDIA_STATE_READY;
    sharedSnapshot.elapsedSeconds = 0U;
    sharedSnapshot.durationSeconds = 0U;
    strncpy(sharedSnapshot.currentName, radio.stationName, sizeof(sharedSnapshot.currentName) - 1U);
    sharedSnapshot.currentName[sizeof(sharedSnapshot.currentName) - 1U] = '\0';
    strcpy(sharedSnapshot.status, "Radio switching station...");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void leaveRadioMode(uint8_t restoreWav)
{
    InternetRadio_Stop();
    closeTrack();
    radioPlaybackStarted = 0U;
    activeSource = MEDIA_SOURCE_WAV;

    if (restoreWav != 0U &&
        (savedWavState == MEDIA_STATE_PLAYING || savedWavState == MEDIA_STATE_PAUSED) &&
        savedWavTrack < sharedSnapshot.trackCount && startTrack(savedWavTrack) != 0U)
    {
        if (savedWavElapsed != 0U) seekTo(savedWavElapsed);
        if (savedWavState == MEDIA_STATE_PAUSED)
        {
            (void)BSP_AUDIO_OUT_Pause();
            setState(MEDIA_STATE_PAUSED, "Paused");
        }
        return;
    }

    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_WAV;
    sharedSnapshot.state = MEDIA_STATE_READY;
    sharedSnapshot.elapsedSeconds = savedWavElapsed;
    sharedSnapshot.durationSeconds = 0U;
    strncpy(sharedSnapshot.currentName, savedWavName, sizeof(sharedSnapshot.currentName) - 1U);
    sharedSnapshot.currentName[sizeof(sharedSnapshot.currentName) - 1U] = '\0';
    strcpy(sharedSnapshot.status, "File mode");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static uint8_t startTrack(uint16_t index)
{
    uint32_t duration;
    uint8_t isMp3;

    closeTrack();
    if (index >= sharedSnapshot.trackCount || f_open(&audioFile, tracks[index].path, FA_READ) != FR_OK)
    {
        setState(MEDIA_STATE_ERROR, "Cannot open audio file");
        return 0U;
    }
    fileOpen = 1U;
    isMp3 = hasMp3Extension(tracks[index].name);
    currentFormat = isMp3 != 0U ? MEDIA_FORMAT_MP3 : MEDIA_FORMAT_WAV;

    if (currentFormat == MEDIA_FORMAT_MP3)
    {
        if (prepareMp3() == 0U)
        {
            closeTrack();
            setState(MEDIA_STATE_ERROR, "Invalid or unsupported MP3 file");
            return 0U;
        }
    }
    else
    {
        if (parseWav(&audioFile, &wavInfo) == 0U)
        {
            closeTrack();
            setState(MEDIA_STATE_ERROR, "Use PCM WAV or MP3 audio");
            return 0U;
        }
        dataRemaining = wavInfo.dataSize;
        if (f_lseek(&audioFile, wavInfo.dataOffset) != FR_OK)
        {
            closeTrack();
            setState(MEDIA_STATE_ERROR, "SD seek failed");
            return 0U;
        }
    }

    configureSoftwareEq(wavInfo.sampleRate, 1U);
    resetTimePitch();
    timePitchRunning = (timePitchEnabled != 0U &&
                        (playbackSpeedPercent != 100U || playbackPitchCents != 0)) ? 1U : 0U;
    if (fillHalf(0U) == 0U || fillHalf(MEDIA_AUDIO_HALF_BYTES) == 0U)
    {
        closeTrack();
        setState(MEDIA_STATE_ERROR, "Audio decode/read failed");
        return 0U;
    }

    /* WM8994 always receives 16-bit stereo PCM; MP3 is decoded/expanded before DMA. */
    if (BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, volume, wavInfo.sampleRate) != AUDIO_OK)
    {
        closeTrack();
        codecReady = 0U;
        setState(MEDIA_STATE_ERROR, "WM8994 initialization failed");
        return 0U;
    }
    codecReady = 1U;
    BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
    applyCodecEq();
    currentTrack = index;
    duration = wavInfo.byteRate != 0U ? wavInfo.dataSize / wavInfo.byteRate : 0U;
    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.source = MEDIA_SOURCE_WAV;
    sharedSnapshot.currentTrack = index;
    sharedSnapshot.elapsedSeconds = 0U;
    sharedSnapshot.durationSeconds = duration;
    sharedSnapshot.state = MEDIA_STATE_PLAYING;
    strncpy(sharedSnapshot.currentName, tracks[index].name, sizeof(sharedSnapshot.currentName) - 1U);
    sharedSnapshot.currentName[sizeof(sharedSnapshot.currentName) - 1U] = '\0';
    strcpy(sharedSnapshot.status, currentFormat == MEDIA_FORMAT_MP3 ? "Playing MP3" : "Playing WAV");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
    BSP_AUDIO_OUT_Play((uint16_t *)audioBuffer, MEDIA_AUDIO_BUFFER_BYTES);
    return 1U;
}

static uint8_t reopenTrackForSeek(uint32_t position)
{
    uint32_t end;
    if (currentTrack >= sharedSnapshot.trackCount) return 0U;

    if (fileOpen != 0U) (void)f_close(&audioFile);
    fileOpen = 0U;
    osDelay(2U);
    if (f_open(&audioFile, tracks[currentTrack].path, FA_READ) != FR_OK) return 0U;
    fileOpen = 1U;

    if (currentFormat == MEDIA_FORMAT_MP3)
    {
        mp3FileSize = f_size(&audioFile);
        if (resetMp3Decoder(position) == 0U) return 0U;
    }
    else
    {
        if (parseWav(&audioFile, &wavInfo) == 0U) return 0U;
        end = wavInfo.dataOffset + wavInfo.dataSize;
        if (position < wavInfo.dataOffset || position >= end) return 0U;
        position -= (position - wavInfo.dataOffset) % wavInfo.blockAlign;
        dataRemaining = end - position;
        if (f_lseek(&audioFile, position) != FR_OK) return 0U;
    }
    return 1U;
}

static void seekTo(uint32_t seconds)
{
    uint32_t position;
    uint32_t end;
    uint64_t requestedPosition;
    mediaSeekRequests++;
    mediaSeekLastSeconds = seconds;
    if (fileOpen == 0U || wavInfo.byteRate == 0U || wavInfo.dataSize == 0U)
    {
        mediaSeekFailures++;
        return;
    }

    end = wavInfo.dataOffset + wavInfo.dataSize;
    requestedPosition = (uint64_t)wavInfo.dataOffset +
                        (uint64_t)seconds * (uint64_t)wavInfo.byteRate;
    if (requestedPosition >= end)
    {
        position = currentFormat == MEDIA_FORMAT_MP3 ? (end > 1U ? end - 1U : 0U)
                                                     : end - wavInfo.blockAlign;
    }
    else
    {
        position = (uint32_t)requestedPosition;
    }
    if (currentFormat == MEDIA_FORMAT_WAV)
    {
        position -= (position - wavInfo.dataOffset) % wavInfo.blockAlign;
    }
    mediaSeekLastPosition = position;

    /* Stop circular DMA and rebuild both halves from the new file position.
       minimp3 will resynchronize to the next valid frame when seeking MP3. */
    (void)BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
    refillFlags = 0U;
    endHalf = 0U;
    if (currentFormat == MEDIA_FORMAT_MP3)
    {
        if (resetMp3Decoder(position) == 0U)
        {
            closeTrack();
            mediaSeekFailures++;
            setState(MEDIA_STATE_ERROR, "MP3 seek failed");
            return;
        }
    }
    else
    {
        dataRemaining = end - position;
        if (f_lseek(&audioFile, position) != FR_OK)
        {
            closeTrack();
            mediaSeekFailures++;
            setState(MEDIA_STATE_ERROR, "WAV seek failed");
            return;
        }
    }

    configureSoftwareEq(wavInfo.sampleRate, 1U);
    resetTimePitch();
    timePitchRunning = (timePitchEnabled != 0U &&
                        (playbackSpeedPercent != 100U || playbackPitchCents != 0)) ? 1U : 0U;
    if (fillHalf(0U) == 0U || fillHalf(MEDIA_AUDIO_HALF_BYTES) == 0U)
    {
        /* A transient SD command error makes FatFs latch FIL.err, so another
           f_read on the same FIL can never recover. Reopen the track, seek to
           the same byte and rebuild both DMA halves once before giving up. */
        mediaSeekRecoveries++;
        endHalf = 0U;
        refillFlags = 0U;
        if (reopenTrackForSeek(position) == 0U)
        {
            mediaSeekRecoveryFailures++;
            closeTrack();
            mediaSeekFailures++;
            setState(MEDIA_STATE_ERROR, "Seek reopen failed");
            return;
        }
        configureSoftwareEq(wavInfo.sampleRate, 1U);
        resetTimePitch();
        timePitchRunning = (timePitchEnabled != 0U &&
                            (playbackSpeedPercent != 100U || playbackPitchCents != 0)) ? 1U : 0U;
        if (fillHalf(0U) == 0U || fillHalf(MEDIA_AUDIO_HALF_BYTES) == 0U)
        {
            mediaSeekRecoveryFailures++;
            closeTrack();
            mediaSeekFailures++;
            setState(MEDIA_STATE_ERROR, "Seek decode/read failed");
            return;
        }
    }
    refillFlags = 0U;

    /* CODEC_PDWN_SW intentionally keeps the WM8994 configuration alive.
       ST's BSP documents that a full re-init is not required in this mode.
       Restart only the rebuilt DMA buffer to avoid an audible/UI stall on seek. */
    if (BSP_AUDIO_OUT_Play((uint16_t *)audioBuffer, MEDIA_AUDIO_BUFFER_BYTES) != AUDIO_OK)
    {
        closeTrack();
        mediaSeekFailures++;
        setState(MEDIA_STATE_ERROR, "Seek playback restart failed");
        return;
    }

    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.elapsedSeconds = seconds;
    sharedSnapshot.state = MEDIA_STATE_PLAYING;
    strcpy(sharedSnapshot.status, currentFormat == MEDIA_FORMAT_MP3 ? "Playing MP3" : "Playing WAV");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
    mediaSeekSuccesses++;
}

static uint8_t joinPath(char *destination, uint32_t capacity,
                        const char *directory, const char *name)
{
    size_t directoryLength = strlen(directory);
    return snprintf(destination, capacity, "%s%s%s", directory,
                    (directoryLength != 0U && directory[directoryLength - 1U] == '/') ? "" : "/",
                    name) < (int)capacity ? 1U : 0U;
}

static void addFolder(const char *root, const char *fullPath, uint16_t *folderCount)
{
    uint16_t index;
    const char *relative;
    if (*folderCount >= MEDIA_MAX_FOLDERS) return;
    index = *folderCount;
    relative = fullPath + strlen(root);
    if (relative[0] == '\0') relative = "/";
    strncpy(folders[index].path, relative, sizeof(folders[index].path) - 1U);
    folders[index].path[sizeof(folders[index].path) - 1U] = '\0';
    strncpy(folders[index].name, relative, sizeof(folders[index].name) - 1U);
    folders[index].name[sizeof(folders[index].name) - 1U] = '\0';
    *folderCount = (uint16_t)(index + 1U);
}

static FRESULT scanFolders(const char *root, const char *directory,
                           uint8_t depth, uint16_t *folderCount)
{
    DIR dir;
    FILINFO info;
    char fullPath[MEDIA_PATH_SIZE];
    FRESULT result = f_opendir(&dir, directory);
    if (result != FR_OK) return result;

    while (*folderCount < MEDIA_MAX_FOLDERS)
    {
        result = f_readdir(&dir, &info);
        if (result != FR_OK || info.fname[0] == '\0') break;
        if (info.fname[0] == '.' || (info.fattrib & (AM_HID | AM_SYS)) != 0U) continue;
        if ((info.fattrib & AM_DIR) != 0U)
        {
            if (joinPath(fullPath, sizeof(fullPath), directory, info.fname) == 0U) continue;
            addFolder(root, fullPath, folderCount);
            if (depth != 0U) (void)scanFolders(root, fullPath, (uint8_t)(depth - 1U), folderCount);
        }
    }
    f_closedir(&dir);
    return result;
}

static FRESULT scanCurrentDirectory(const char *directory, uint16_t *trackCount,
                                    uint16_t *fileCount)
{
    DIR dir;
    FILINFO info;
    char fullPath[MEDIA_PATH_SIZE];
    FRESULT result = f_opendir(&dir, directory);
    if (result != FR_OK) return result;

    while (*fileCount < MEDIA_MAX_FILES)
    {
        result = f_readdir(&dir, &info);
        if (result != FR_OK || info.fname[0] == '\0') break;
        if (info.fname[0] == '.' || (info.fattrib & (AM_HID | AM_SYS | AM_DIR)) != 0U) continue;
        if (joinPath(fullPath, sizeof(fullPath), directory, info.fname) == 0U) continue;

        {
            uint16_t fileIndex = *fileCount;
            strncpy(files[fileIndex].path, fullPath, sizeof(files[fileIndex].path) - 1U);
            files[fileIndex].path[sizeof(files[fileIndex].path) - 1U] = '\0';
            strncpy(files[fileIndex].name, info.fname, sizeof(files[fileIndex].name) - 1U);
            files[fileIndex].name[sizeof(files[fileIndex].name) - 1U] = '\0';
            *fileCount = (uint16_t)(fileIndex + 1U);
        }

        if (*trackCount < MEDIA_MAX_TRACKS &&
            (hasWavExtension(info.fname) != 0U || hasMp3Extension(info.fname) != 0U))
        {
            uint16_t index = *trackCount;
            strncpy(tracks[index].path, fullPath, sizeof(tracks[index].path) - 1U);
            tracks[index].path[sizeof(tracks[index].path) - 1U] = '\0';
            strncpy(tracks[index].name, info.fname, sizeof(tracks[index].name) - 1U);
            tracks[index].name[sizeof(tracks[index].name) - 1U] = '\0';
            *trackCount = (uint16_t)(index + 1U);
        }
    }
    f_closedir(&dir);
    return result;
}

static void mountAndScan(void)
{
    FRESULT result;
    uint16_t count = 0U;
    uint16_t fileCount = 0U;
    uint16_t folderCount = 0U;
    char status[MEDIA_TRACK_NAME_SIZE];
    char directory[MEDIA_PATH_SIZE];
    const char *selectedPath;
    const uint8_t usingUsb = activeStorage == MEDIA_STORAGE_USB ? 1U : 0U;

    lastScanTick = osKernelGetTickCount();

    /* SD must claim logical drive 0 before USBStorage_Init links MSC as 1:. */
    if (driverLinked == 0U)
    {
        if (FATFS_LinkDriver(&SD_Driver, drivePath) != 0U)
        {
            setState(MEDIA_STATE_ERROR, "FatFS SD driver link failed");
            return;
        }
        driverLinked = 1U;
    }

    if (usingUsb != 0U && USBStorage_IsReady() == 0U)
    {
        if (mountedPath[0] != '\0')
        {
            (void)f_mount(NULL, mountedPath, 0U);
            mountedPath[0] = '\0';
        }
        osMutexAcquire(snapshotMutex, osWaitForever);
        sharedSnapshot.trackCount = 0U;
        mediaFileCount = 0U;
        mediaFolderCount = 0U;
        sharedSnapshot.state = MEDIA_STATE_NO_CARD;
        sharedSnapshot.storage = MEDIA_STORAGE_USB;
        strcpy(sharedSnapshot.status, USBStorage_GetState() == USB_STORAGE_DISCONNECTED ?
               "USB MSC not connected" : "USB MSC preparing...");
        sharedSnapshot.libraryRevision++;
        sharedSnapshot.revision++;
        osMutexRelease(snapshotMutex);
        return;
    }

    selectedPath = usingUsb != 0U ? USBStorage_GetPath() : drivePath;
    if (selectedPath == NULL || selectedPath[0] == '\0')
    {
        setState(MEDIA_STATE_ERROR, usingUsb != 0U ? "USB MSC drive unavailable" : "SD drive unavailable");
        return;
    }

    if (mountedPath[0] != '\0' && strcmp(mountedPath, selectedPath) != 0)
    {
        (void)f_mount(NULL, mountedPath, 0U);
        mountedPath[0] = '\0';
    }

    (void)f_mount(NULL, selectedPath, 0U);
    result = f_mount(&fileSystem, selectedPath, 1U);
    if (result != FR_OK)
    {
        if (usingUsb != 0U)
            snprintf(status, sizeof(status), "USB MSC ERROR %u", (unsigned int)result);
        else
            snprintf(status, sizeof(status), BSP_SD_IsDetected() == SD_PRESENT ?
                     "SD ERROR %u" : "NO SD CARD", (unsigned int)result);

        osMutexAcquire(snapshotMutex, osWaitForever);
        sharedSnapshot.trackCount = 0U;
        mediaFileCount = 0U;
        mediaFolderCount = 0U;
        sharedSnapshot.state = MEDIA_STATE_NO_CARD;
        sharedSnapshot.storage = activeStorage;
        strncpy(sharedSnapshot.status, status, sizeof(sharedSnapshot.status) - 1U);
        sharedSnapshot.status[sizeof(sharedSnapshot.status) - 1U] = '\0';
        sharedSnapshot.libraryRevision++;
        sharedSnapshot.revision++;
        osMutexRelease(snapshotMutex);
        return;
    }

    strncpy(mountedPath, selectedPath, sizeof(mountedPath) - 1U);
    mountedPath[sizeof(mountedPath) - 1U] = '\0';
    if (currentFolders[activeStorage][0] == '\0') strcpy(currentFolders[activeStorage], "/");

    addFolder(selectedPath, selectedPath, &folderCount);
    result = scanFolders(selectedPath, selectedPath, MEDIA_SCAN_DEPTH, &folderCount);
    if (snprintf(directory, sizeof(directory), "%s%s", selectedPath,
                 currentFolders[activeStorage]) >= (int)sizeof(directory))
    {
        result = FR_INVALID_NAME;
    }
    else
    {
        result = scanCurrentDirectory(directory, &count, &fileCount);
        if (result != FR_OK && strcmp(currentFolders[activeStorage], "/") != 0)
        {
            strcpy(currentFolders[activeStorage], "/");
            count = 0U;
            fileCount = 0U;
            snprintf(directory, sizeof(directory), "%s/", selectedPath);
            result = scanCurrentDirectory(directory, &count, &fileCount);
        }
    }

    if (result != FR_OK)
        snprintf(status, sizeof(status), "%s directory failed (FR=%u)",
                 usingUsb != 0U ? "USB" : "SD", (unsigned int)result);
    else if (count != 0U)
        snprintf(status, sizeof(status), "%s: select a WAV or MP3 file", usingUsb != 0U ? "USB MSC" : "SD");
    else
        snprintf(status, sizeof(status), "%s mounted: no WAV/MP3 files", usingUsb != 0U ? "USB MSC" : "SD");

    osMutexAcquire(snapshotMutex, osWaitForever);
    sharedSnapshot.trackCount = count;
    mediaFileCount = fileCount;
    mediaFolderCount = folderCount;
    sharedSnapshot.storage = activeStorage;
    sharedSnapshot.state = result == FR_OK ? MEDIA_STATE_READY : MEDIA_STATE_ERROR;
    strncpy(sharedSnapshot.status, status, sizeof(sharedSnapshot.status) - 1U);
    sharedSnapshot.status[sizeof(sharedSnapshot.status) - 1U] = '\0';
    sharedSnapshot.libraryRevision++;
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void selectFolder(uint16_t index)
{
    char selected[MEDIA_PATH_SIZE];
    if (index >= mediaFolderCount) return;

    osMutexAcquire(snapshotMutex, osWaitForever);
    strncpy(selected, folders[index].path, sizeof(selected) - 1U);
    selected[sizeof(selected) - 1U] = '\0';
    osMutexRelease(snapshotMutex);

    if (activeSource == MEDIA_SOURCE_WAV) closeTrack();
    strncpy(currentFolders[activeStorage], selected,
            sizeof(currentFolders[activeStorage]) - 1U);
    currentFolders[activeStorage][sizeof(currentFolders[activeStorage]) - 1U] = '\0';
    currentTrack = 0U;
    mountAndScan();
}

static void commitDeleteResult(MediaDeleteState state, const char *name, const char *status)
{
    osMutexAcquire(snapshotMutex, osWaitForever);
    deleteSnapshot.state = (uint8_t)state;
    if (name != NULL)
    {
        strncpy(deleteSnapshot.fileName, name, sizeof(deleteSnapshot.fileName) - 1U);
        deleteSnapshot.fileName[sizeof(deleteSnapshot.fileName) - 1U] = '\0';
    }
    if (status != NULL)
    {
        strncpy(deleteSnapshot.status, status, sizeof(deleteSnapshot.status) - 1U);
        deleteSnapshot.status[sizeof(deleteSnapshot.status) - 1U] = '\0';
    }
    deleteSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void deleteFile(uint16_t index)
{
    char path[MEDIA_PATH_SIZE];
    char name[MEDIA_TRACK_NAME_SIZE];
    FRESULT result;

    if (index >= mediaFileCount)
    {
        commitDeleteResult(MEDIA_DELETE_ERROR, "", "Dosya artık mevcut değil");
        osMutexAcquire(snapshotMutex, osWaitForever);
        sharedSnapshot.libraryRevision++;
        osMutexRelease(snapshotMutex);
        return;
    }

    osMutexAcquire(snapshotMutex, osWaitForever);
    strncpy(path, files[index].path, sizeof(path) - 1U);
    path[sizeof(path) - 1U] = '\0';
    strncpy(name, files[index].name, sizeof(name) - 1U);
    name[sizeof(name) - 1U] = '\0';
    osMutexRelease(snapshotMutex);

    if (fileOpen != 0U && currentTrack < sharedSnapshot.trackCount &&
        strcmp(tracks[currentTrack].path, path) == 0)
    {
        closeTrack();
    }

    result = f_unlink(path);
    if (result == FR_OK)
    {
        commitDeleteResult(MEDIA_DELETE_SUCCESS, name, "Dosya silindi");
        mountAndScan();
    }
    else
    {
        char status[64];
        snprintf(status, sizeof(status), "Silme hatası (FR=%u)", (unsigned int)result);
        commitDeleteResult(MEDIA_DELETE_ERROR, name, status);
        osMutexAcquire(snapshotMutex, osWaitForever);
        sharedSnapshot.libraryRevision++;
        sharedSnapshot.revision++;
        osMutexRelease(snapshotMutex);
    }
}

static void recorderPutLe16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void recorderPutLe32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void recorderMakeWavHeader(uint8_t header[44], uint32_t dataBytes)
{
    memset(header, 0, 44U);
    memcpy(&header[0], "RIFF", 4U);
    recorderPutLe32(&header[4], 36U + dataBytes);
    memcpy(&header[8], "WAVEfmt ", 8U);
    recorderPutLe32(&header[16], 16U);
    recorderPutLe16(&header[20], 1U);
    recorderPutLe16(&header[22], RECORDER_CHANNELS);
    recorderPutLe32(&header[24], RECORDER_SAMPLE_RATE);
    recorderPutLe32(&header[28], RECORDER_SAMPLE_RATE * RECORDER_CHANNELS *
                                  (RECORDER_BITS_PER_SAMPLE / 8U));
    recorderPutLe16(&header[32], RECORDER_CHANNELS * (RECORDER_BITS_PER_SAMPLE / 8U));
    recorderPutLe16(&header[34], RECORDER_BITS_PER_SAMPLE);
    memcpy(&header[36], "data", 4U);
    recorderPutLe32(&header[40], dataBytes);
}

static void recorderCommit(MediaRecorderState state, const char *status)
{
    osMutexAcquire(snapshotMutex, osWaitForever);
    recorderSnapshot.state = (uint8_t)state;
    recorderSnapshot.storage = activeStorage;
    recorderSnapshot.revision++;
    if (status != NULL)
    {
        strncpy(recorderSnapshot.status, status, sizeof(recorderSnapshot.status) - 1U);
        recorderSnapshot.status[sizeof(recorderSnapshot.status) - 1U] = '\0';
    }
    osMutexRelease(snapshotMutex);
}

static uint8_t recorderStorageReady(void)
{
    const char *expectedPath;
    if (activeStorage == MEDIA_STORAGE_USB && USBStorage_IsReady() == 0U) return 0U;
    if (activeStorage == MEDIA_STORAGE_SD && BSP_SD_IsDetected() != SD_PRESENT) return 0U;
    expectedPath = activeStorage == MEDIA_STORAGE_USB ? USBStorage_GetPath() : drivePath;
    return expectedPath != NULL && expectedPath[0] != '\0' && mountedPath[0] != '\0' &&
           strcmp(expectedPath, mountedPath) == 0;
}

static void recorderCloseAndDelete(void)
{
    if (recorderFileOpen != 0U)
    {
        (void)f_close(&recorderFile);
        recorderFileOpen = 0U;
    }
    if (recorderTempPath[0] != '\0') (void)f_unlink(recorderTempPath);
}

static void recorderFail(const char *status)
{
    uint8_t wasActive = recorderActive;
    recorderActive = 0U;
    recorderInputFlags = 0U;
    recorderQueueCount = 0U;
    if (wasActive != 0U) (void)BSP_AUDIO_IN_Stop(CODEC_PDWN_SW);
    recorderCloseAndDelete();
    recorderWriteErrors++;
    recorderCommit(MEDIA_RECORDER_ERROR, status);
}

static uint8_t recorderChooseFileName(void)
{
    FILINFO info;
    FRESULT result;
    uint32_t number;
    size_t recordsLength;
    char recordsPath[MEDIA_PATH_SIZE];

    if (snprintf(recordsPath, sizeof(recordsPath), "%s/Records", mountedPath) >=
        (int)sizeof(recordsPath)) return 0U;
    result = f_mkdir(recordsPath);
    if (result != FR_OK && result != FR_EXIST) return 0U;
    recordsLength = strlen(recordsPath);
    if (recordsLength + 14U >= sizeof(recorderFinalPath)) return 0U;

    for (number = 1U; number <= 9999U; number++)
    {
        memcpy(recorderFinalPath, recordsPath, recordsLength);
        memcpy(recorderTempPath, recordsPath, recordsLength);
        snprintf(&recorderFinalPath[recordsLength], sizeof(recorderFinalPath) - recordsLength,
                 "/REC_%04lu.WAV", (unsigned long)number);
        snprintf(&recorderTempPath[recordsLength], sizeof(recorderTempPath) - recordsLength,
                 "/REC_%04lu.TMP", (unsigned long)number);
        if (f_stat(recorderFinalPath, &info) == FR_NO_FILE &&
            f_stat(recorderTempPath, &info) == FR_NO_FILE)
        {
            snprintf(recorderSnapshot.fileName, sizeof(recorderSnapshot.fileName),
                     "REC_%04lu.WAV", (unsigned long)number);
            return 1U;
        }
    }
    return 0U;
}

static uint8_t recorderWriteBlock(uint8_t *block)
{
    int16_t *samples = (int16_t *)block;
    uint32_t frame;
    uint32_t leftPeak = 0U;
    uint32_t rightPeak = 0U;
    UINT written = 0U;
    FRESULT result;

    /* Apply record gain before metering and storage. Q15 keeps the per-sample
       path deterministic while the logarithmic conversion runs only when the
       user moves the slider. */
    if (recorderGainQ15 != 32768U)
    {
        uint32_t sample;
        for (sample = 0U; sample < RECORDER_HALF_BYTES / 2U; sample++)
        {
            int32_t scaled = (int32_t)(((int64_t)samples[sample] * recorderGainQ15) >> 15);
            if (scaled > 32767) scaled = 32767;
            else if (scaled < -32768) scaled = -32768;
            samples[sample] = (int16_t)scaled;
        }
    }

    for (frame = 0U; frame < RECORDER_HALF_BYTES / 4U; frame++)
    {
        int32_t left = samples[frame * 2U];
        int32_t right = samples[frame * 2U + 1U];
        uint32_t leftAbs = (uint32_t)(left < 0 ? -left : left);
        uint32_t rightAbs = (uint32_t)(right < 0 ? -right : right);
        if (leftAbs > leftPeak) leftPeak = leftAbs;
        if (rightAbs > rightPeak) rightPeak = rightAbs;
    }

    result = f_write(&recorderFile, block, RECORDER_HALF_BYTES, &written);
    if (result != FR_OK || written != RECORDER_HALF_BYTES) return 0U;

    recorderDataBytes += written;
    recorderBytesWritten = recorderDataBytes;
    osMutexAcquire(snapshotMutex, osWaitForever);
    recorderSnapshot.dataBytes = recorderDataBytes;
    recorderSnapshot.elapsedSeconds = recorderDataBytes /
        (RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * (RECORDER_BITS_PER_SAMPLE / 8U));
    recorderSnapshot.leftLevel = (uint8_t)(sqrtf((float32_t)leftPeak / 32768.0f) * 100.0f);
    recorderSnapshot.rightLevel = (uint8_t)(sqrtf((float32_t)rightPeak / 32768.0f) * 100.0f);
    if (recorderSnapshot.leftLevel > 100U) recorderSnapshot.leftLevel = 100U;
    if (recorderSnapshot.rightLevel > 100U) recorderSnapshot.rightLevel = 100U;
    recorderSnapshot.revision++;
    osMutexRelease(snapshotMutex);
    return 1U;
}

static void recorderWritePending(uint8_t drainAll)
{
    uint8_t blocksWritten = 0U;
    while (recorderQueueCount != 0U)
    {
        uint8_t index;
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        index = recorderQueueRead;
        if (primask == 0U) __enable_irq();

        if (recorderWriteBlock(recorderWriteQueue[index]) == 0U)
        {
            recorderFail("Kayıt yazma hatası");
            return;
        }

        primask = __get_PRIMASK();
        __disable_irq();
        recorderQueueRead = (uint8_t)((recorderQueueRead + 1U) % RECORDER_QUEUE_BLOCKS);
        if (recorderQueueCount != 0U) recorderQueueCount--;
        if (primask == 0U) __enable_irq();
        blocksWritten++;
        /* During live capture return to the command queue after one block so
           Stop and the UI can run. Once DMA is stopped, drain everything. */
        if (drainAll == 0U && blocksWritten >= 1U) break;
    }
}

static void recorderQueueDmaHalf(uint32_t offset)
{
    uint8_t index;
    if (recorderActive == 0U) return;
    recorderDmaHalves++;
    if (recorderQueueCount >= RECORDER_QUEUE_BLOCKS)
    {
        recorderOverruns++;
        return;
    }

    SCB_InvalidateDCache_by_Addr((uint32_t *)&recorderBuffer[offset], RECORDER_HALF_BYTES);
    index = recorderQueueWrite;
    memcpy(recorderWriteQueue[index], &recorderBuffer[offset], RECORDER_HALF_BYTES);
    __DMB();
    recorderQueueWrite = (uint8_t)((index + 1U) % RECORDER_QUEUE_BLOCKS);
    recorderQueueCount++;
    if (recorderQueueCount > recorderQueueHighWater)
        recorderQueueHighWater = recorderQueueCount;
}

static void startRecording(void)
{
    uint8_t header[44];
    UINT written = 0U;

    if (recorderActive != 0U || recorderSnapshot.state == MEDIA_RECORDER_AWAITING_SAVE) return;
    if (recorderStorageReady() == 0U)
    {
        mountAndScan();
        if (recorderStorageReady() == 0U)
        {
            recorderCommit(MEDIA_RECORDER_NO_STORAGE,
                           activeStorage == MEDIA_STORAGE_USB ? "USB MSC bağlı değil" : "SD kart bağlı değil");
            return;
        }
    }
    if (recorderChooseFileName() == 0U)
    {
        recorderCommit(MEDIA_RECORDER_ERROR, "Kayıt dosya adı oluşturulamadı");
        return;
    }
    if (f_open(&recorderFile, recorderTempPath, FA_CREATE_NEW | FA_WRITE) != FR_OK)
    {
        recorderCommit(MEDIA_RECORDER_ERROR, "Kayıt dosyası açılamadı");
        return;
    }
    recorderFileOpen = 1U;
    recorderMakeWavHeader(header, 0U);
    if (f_write(&recorderFile, header, sizeof(header), &written) != FR_OK || written != sizeof(header))
    {
        recorderFail("WAV başlığı yazılamadı");
        return;
    }

    seekPending = 0U;
    if (activeSource == MEDIA_SOURCE_AUX) leaveAux(0U);
    else if (activeSource == MEDIA_SOURCE_RADIO) leaveRadioMode(0U);
    else if (activeSource == MEDIA_SOURCE_PC) leavePcMode();
    else closeTrack();
    activeSource = MEDIA_SOURCE_WAV;
    codecReady = 0U;

    memset(recorderBuffer, 0, sizeof(recorderBuffer));
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)recorderBuffer, RECORDER_BUFFER_BYTES);
    if (BSP_AUDIO_IN_Init(RECORDER_SAMPLE_RATE, RECORDER_BITS_PER_SAMPLE,
                          RECORDER_CHANNELS) != AUDIO_OK)
    {
        recorderFail("MEMS mikrofon başlatılamadı");
        return;
    }
    (void)BSP_AUDIO_IN_SetVolume(80U);
    recorderDataBytes = 0U;
    recorderBytesWritten = 0U;
    recorderInputFlags = 0U;
    recorderQueueWrite = 0U;
    recorderQueueRead = 0U;
    recorderQueueCount = 0U;
    recorderDmaHalves = 0U;
    recorderQueueHighWater = 0U;
    recorderActive = 1U;
    if (BSP_AUDIO_IN_Record((uint16_t *)recorderBuffer, RECORDER_BUFFER_BYTES / 2U) != AUDIO_OK)
    {
        recorderFail("Mikrofon DMA başlatılamadı");
        return;
    }

    osMutexAcquire(snapshotMutex, osWaitForever);
    recorderSnapshot.elapsedSeconds = 0U;
    recorderSnapshot.dataBytes = 0U;
    recorderSnapshot.leftLevel = 0U;
    recorderSnapshot.rightLevel = 0U;
    recorderSnapshot.storage = activeStorage;
    recorderSnapshot.gainCentiDb = recorderGainCentiDb;
    recorderSnapshot.state = MEDIA_RECORDER_RECORDING;
    strcpy(recorderSnapshot.status, "Kaydediliyor...");
    recorderSnapshot.revision++;
    sharedSnapshot.source = MEDIA_SOURCE_WAV;
    sharedSnapshot.state = MEDIA_STATE_READY;
    strcpy(sharedSnapshot.status, "MEMS recording active");
    sharedSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void stopRecording(void)
{
    uint8_t header[44];
    UINT written = 0U;
    if (recorderActive == 0U) return;

    recorderActive = 0U;
    (void)BSP_AUDIO_IN_Stop(CODEC_PDWN_SW);
    recorderWritePending(1U);
    if (recorderSnapshot.state == MEDIA_RECORDER_ERROR) return;

    recorderMakeWavHeader(header, recorderDataBytes);
    if (f_lseek(&recorderFile, 0U) != FR_OK ||
        f_write(&recorderFile, header, sizeof(header), &written) != FR_OK ||
        written != sizeof(header) || f_sync(&recorderFile) != FR_OK)
    {
        recorderFail("Kayıt tamamlanamadı");
        return;
    }
    (void)f_close(&recorderFile);
    recorderFileOpen = 0U;
    recorderInputFlags = 0U;
    osMutexAcquire(snapshotMutex, osWaitForever);
    recorderSnapshot.leftLevel = 0U;
    recorderSnapshot.rightLevel = 0U;
    recorderSnapshot.state = MEDIA_RECORDER_AWAITING_SAVE;
    strcpy(recorderSnapshot.status, "Kayıt durduruldu");
    recorderSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void confirmRecording(void)
{
    if (recorderSnapshot.state != MEDIA_RECORDER_AWAITING_SAVE) return;
    if (f_rename(recorderTempPath, recorderFinalPath) != FR_OK)
    {
        recorderCommit(MEDIA_RECORDER_AWAITING_SAVE, "Kayıt kaydedilemedi; tekrar deneyin");
        return;
    }
    recorderTempPath[0] = '\0';
    recorderCommit(MEDIA_RECORDER_SAVED, "Kayıt başarıyla kaydedildi");
    mountAndScan();
}

static void discardRecording(void)
{
    if (recorderSnapshot.state != MEDIA_RECORDER_AWAITING_SAVE &&
        recorderSnapshot.state != MEDIA_RECORDER_ERROR) return;
    recorderCloseAndDelete();
    recorderTempPath[0] = '\0';
    recorderFinalPath[0] = '\0';
    osMutexAcquire(snapshotMutex, osWaitForever);
    recorderSnapshot.elapsedSeconds = 0U;
    recorderSnapshot.dataBytes = 0U;
    recorderSnapshot.leftLevel = 0U;
    recorderSnapshot.rightLevel = 0U;
    recorderSnapshot.fileName[0] = '\0';
    recorderSnapshot.state = recorderStorageReady() != 0U ?
                             MEDIA_RECORDER_READY : MEDIA_RECORDER_NO_STORAGE;
    strcpy(recorderSnapshot.status, "Kayıt silindi");
    recorderSnapshot.revision++;
    osMutexRelease(snapshotMutex);
}

static void postCommand(MediaCommandType type, uint32_t value)
{
    MediaCommand command = {(uint8_t)type, value};
    if (recorderActive != 0U && type < CMD_RECORD_START) return;
    if (commandQueue != NULL)
    {
        (void)osMessageQueuePut(commandQueue, &command, 0U, 0U);
    }
}

void MediaPlayer_Init(void)
{
    const osMutexAttr_t mutexAttributes = {.attr_bits = osMutexRecursive};
    memset(&sharedSnapshot, 0, sizeof(sharedSnapshot));
    sharedSnapshot.volume = volume;
    memcpy(sharedSnapshot.eqBands, eqBands, sizeof(eqBands));
    sharedSnapshot.eqPreset = eqPreset;
    memcpy(sharedSnapshot.swEqBands, swEqBands, sizeof(swEqBands));
    sharedSnapshot.swEqPreamp = swEqPreamp;
    sharedSnapshot.swEqPreset = swEqPreset;
    sharedSnapshot.timePitchEnabled = timePitchEnabled;
    sharedSnapshot.speedPercent = playbackSpeedPercent;
    sharedSnapshot.pitchCents = playbackPitchCents;
    sharedSnapshot.source = MEDIA_SOURCE_WAV;
    sharedSnapshot.storage = MEDIA_STORAGE_SD;
    sharedSnapshot.state = MEDIA_STATE_NO_CARD;
    strcpy(sharedSnapshot.status, "Initializing media...");
    commandQueue = osMessageQueueNew(MEDIA_COMMAND_DEPTH, sizeof(MediaCommand), NULL);
    snapshotMutex = osMutexNew(NULL);
    i2cMutex = osMutexNew(&mutexAttributes);
    spectrumMutex = osMutexNew(NULL);

    memset(&recorderSnapshot, 0, sizeof(recorderSnapshot));
    recorderSnapshot.state = MEDIA_RECORDER_NO_STORAGE;
    recorderSnapshot.storage = MEDIA_STORAGE_SD;
    strcpy(currentFolders[MEDIA_STORAGE_SD], "/");
    strcpy(currentFolders[MEDIA_STORAGE_USB], "/");
    memset(&deleteSnapshot, 0, sizeof(deleteSnapshot));
    deleteSnapshot.state = MEDIA_DELETE_IDLE;
    strcpy(recorderSnapshot.status, "Depolama hazırlanıyor...");

    /* Reserve logical drive 0 for SD before USBStorage_Init links MSC as 1:. */
    if (FATFS_LinkDriver(&SD_Driver, drivePath) == 0U)
    {
        driverLinked = 1U;
    }
    else
    {
        sharedSnapshot.state = MEDIA_STATE_ERROR;
        strcpy(sharedSnapshot.status, "FatFS SD driver link failed");
    }

    if (arm_rfft_fast_init_f32(&spectrumFft, MEDIA_FFT_SIZE) == ARM_MATH_SUCCESS &&
        arm_rfft_fast_init_f32(&spectrumRadioFft, MEDIA_RADIO_FFT_SIZE) == ARM_MATH_SUCCESS)
    {
        uint32_t i;
        for (i = 0U; i < MEDIA_FFT_SIZE; i++)
        {
            spectrumWindow[i] = 0.5f - 0.5f * arm_cos_f32((2.0f * PI * (float32_t)i) /
                                                          (float32_t)(MEDIA_FFT_SIZE - 1U));
        }
        spectrumReady = 1U;
    }
    configureSoftwareEq(44100U, 1U);
    {
        uint32_t i;
        for (i = 0U; i < TIMEPITCH_GRAIN_FRAMES; i++)
        {
            if (i < TIMEPITCH_OVERLAP_FRAMES)
                timePitchWindowQ15[i] = (uint16_t)(((uint32_t)i * 32767U) /
                                                    TIMEPITCH_OVERLAP_FRAMES);
            else if (i >= TIMEPITCH_HOP_FRAMES)
                timePitchWindowQ15[i] = (uint16_t)(((TIMEPITCH_GRAIN_FRAMES - i) * 32767U) /
                                                    TIMEPITCH_OVERLAP_FRAMES);
            else
                timePitchWindowQ15[i] = 32767U;
        }
    }
}

void MediaPlayer_Task(void *argument)
{
    MediaCommand command;
    (void)argument;
    mountAndScan();
    for (;;)
    {
        /* Coalesce a whole slider gesture to the newest requested position.
           A short settle time prevents rebuilding the DMA buffer dozens of
           times while the user's finger is still moving. */
        {
            uint32_t seconds = 0U;
            uint8_t doSeek = 0U;
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            if (seekPending != 0U &&
                (uint32_t)(osKernelGetTickCount() - pendingSeekTick) >= MEDIA_SEEK_SETTLE_TICKS)
            {
                seconds = pendingSeekSeconds;
                seekPending = 0U;
                doSeek = 1U;
            }
            if (primask == 0U) __enable_irq();
            if (doSeek != 0U) seekTo(seconds);
        }
        if (osMessageQueueGet(commandQueue, &command, NULL, 2U) == osOK)
        {
            switch ((MediaCommandType)command.type)
            {
            case CMD_SELECT:
                if (activeSource == MEDIA_SOURCE_AUX) leaveAux(0U);
                else if (activeSource == MEDIA_SOURCE_RADIO) leaveRadioMode(0U);
                (void)startTrack((uint16_t)command.value);
                break;
            case CMD_TOGGLE:
                if (activeSource == MEDIA_SOURCE_PC) { USBPCAudio_SendPlayPause(); break; }
                if (activeSource == MEDIA_SOURCE_AUX) break;
                if (activeSource == MEDIA_SOURCE_RADIO)
                {
                    if (radioPlaybackStarted != 0U)
                    {
                        InternetRadio_Stop();
                        closeTrack();
                        radioPlaybackStarted = 0U;
                        activeSource = MEDIA_SOURCE_RADIO;
                        osMutexAcquire(snapshotMutex, osWaitForever);
                        sharedSnapshot.source = MEDIA_SOURCE_RADIO;
                        sharedSnapshot.state = MEDIA_STATE_PAUSED;
                        strcpy(sharedSnapshot.status, "Radio stopped");
                        sharedSnapshot.revision++;
                        osMutexRelease(snapshotMutex);
                    }
                    else
                    {
                        InternetRadio_Start();
                        osMutexAcquire(snapshotMutex, osWaitForever);
                        sharedSnapshot.state = MEDIA_STATE_READY;
                        strcpy(sharedSnapshot.status, "Radio connecting...");
                        sharedSnapshot.revision++;
                        osMutexRelease(snapshotMutex);
                    }
                    break;
                }
                if (sharedSnapshot.state == MEDIA_STATE_PLAYING)
                {
                    BSP_AUDIO_OUT_Pause();
                    clearSpectrum();
                    setState(MEDIA_STATE_PAUSED, "Paused");
                }
                else if (sharedSnapshot.state == MEDIA_STATE_PAUSED)
                {
                    BSP_AUDIO_OUT_Resume();
                    setState(MEDIA_STATE_PLAYING, "Playing");
                }
                else if (sharedSnapshot.trackCount != 0U)
                {
                    (void)startTrack(currentTrack);
                }
                break;
            case CMD_NEXT:
                if (activeSource == MEDIA_SOURCE_PC) { USBPCAudio_SendNext(); break; }
                if (activeSource == MEDIA_SOURCE_AUX) break;
                if (activeSource == MEDIA_SOURCE_RADIO) { changeRadioStation(1U); break; }
                if (sharedSnapshot.trackCount != 0U)
                    (void)startTrack((uint16_t)((currentTrack + 1U) % sharedSnapshot.trackCount));
                break;
            case CMD_PREVIOUS:
                if (activeSource == MEDIA_SOURCE_PC) { USBPCAudio_SendPrevious(); break; }
                if (activeSource == MEDIA_SOURCE_AUX) break;
                if (activeSource == MEDIA_SOURCE_RADIO) { changeRadioStation(0U); break; }
                if (sharedSnapshot.trackCount != 0U)
                    (void)startTrack(currentTrack == 0U ? sharedSnapshot.trackCount - 1U : currentTrack - 1U);
                break;
            case CMD_VOLUME:
                volume = command.value > 100U ? 100U : (uint8_t)command.value;
                if (codecReady != 0U)
                {
                    BSP_AUDIO_OUT_SetVolume(volume);
                    if (activeSource == MEDIA_SOURCE_AUX) configureAuxInputPath();
                }
                snapshotCommit(NULL);
                break;
            case CMD_EQ_BAND:
            {
                uint8_t band = (uint8_t)(command.value >> 8);
                uint8_t value = (uint8_t)command.value;
                if (band < MEDIA_EQ_BANDS)
                {
                    eqBands[band] = value > 100U ? 100U : value;
                    eqPreset = MEDIA_EQ_CUSTOM;
                    applyCodecEq();
                    osMutexAcquire(snapshotMutex, osWaitForever);
                    memcpy(sharedSnapshot.eqBands, eqBands, sizeof(eqBands));
                    sharedSnapshot.eqPreset = eqPreset;
                    sharedSnapshot.revision++;
                    osMutexRelease(snapshotMutex);
                }
                break;
            }
            case CMD_EQ_PRESET:
                if (command.value < 5U)
                {
                    eqPreset = (uint8_t)command.value;
                    memcpy(eqBands, eqPresets[eqPreset], sizeof(eqBands));
                    applyCodecEq();
                    osMutexAcquire(snapshotMutex, osWaitForever);
                    memcpy(sharedSnapshot.eqBands, eqBands, sizeof(eqBands));
                    sharedSnapshot.eqPreset = eqPreset;
                    sharedSnapshot.revision++;
                    osMutexRelease(snapshotMutex);
                }
                break;
            case CMD_SW_EQ_BAND:
            {
                uint8_t band = (uint8_t)(command.value >> 8);
                uint8_t value = (uint8_t)command.value;
                if (band < MEDIA_SW_EQ_BANDS)
                {
                    swEqBands[band] = value > 100U ? 100U : value;
                    swEqPreset = MEDIA_EQ_CUSTOM;
                    /* Preserve filter history while dragging to avoid a click
                       on every GUI value update. */
                    configureSoftwareEq(swEqSampleRate, 0U);
                    osMutexAcquire(snapshotMutex, osWaitForever);
                    memcpy(sharedSnapshot.swEqBands, swEqBands, sizeof(swEqBands));
                    sharedSnapshot.swEqPreset = swEqPreset;
                    sharedSnapshot.revision++;
                    osMutexRelease(snapshotMutex);
                }
                break;
            }
            case CMD_SW_EQ_PREAMP:
                swEqPreamp = command.value > 100U ? 100U : (uint8_t)command.value;
                swEqPreset = MEDIA_EQ_CUSTOM;
                configureSoftwareEq(swEqSampleRate, 0U);
                osMutexAcquire(snapshotMutex, osWaitForever);
                sharedSnapshot.swEqPreamp = swEqPreamp;
                sharedSnapshot.swEqPreset = swEqPreset;
                sharedSnapshot.revision++;
                osMutexRelease(snapshotMutex);
                break;
            case CMD_SW_EQ_PRESET:
                if (command.value < MEDIA_SW_EQ_PRESETS)
                {
                    swEqPreset = (uint8_t)command.value;
                    memcpy(swEqBands, swEqPresetBands[swEqPreset], sizeof(swEqBands));
                    swEqPreamp = swEqPresetPreamp[swEqPreset];
                    configureSoftwareEq(swEqSampleRate, 0U);
                    osMutexAcquire(snapshotMutex, osWaitForever);
                    memcpy(sharedSnapshot.swEqBands, swEqBands, sizeof(swEqBands));
                    sharedSnapshot.swEqPreamp = swEqPreamp;
                    sharedSnapshot.swEqPreset = swEqPreset;
                    sharedSnapshot.revision++;
                    osMutexRelease(snapshotMutex);
                }
                break;
            case CMD_SPEED:
                playbackSpeedPercent = command.value < 50U ? 50U :
                                       (command.value > 250U ? 250U : (uint16_t)command.value);
                timePitchSpeed = (float32_t)playbackSpeedPercent / 100.0f;
                if (timePitchEnabled != 0U && activeSource == MEDIA_SOURCE_WAV &&
                    fileOpen != 0U && timePitchRunning == 0U &&
                    (playbackSpeedPercent != 100U || playbackPitchCents != 0))
                {
                    resetTimePitch();
                    timePitchRunning = 1U;
                }
                osMutexAcquire(snapshotMutex, osWaitForever);
                sharedSnapshot.speedPercent = playbackSpeedPercent;
                sharedSnapshot.revision++;
                osMutexRelease(snapshotMutex);
                break;
            case CMD_PITCH:
            {
                int32_t cents = (int32_t)command.value;
                if (cents > 1200) cents = 1200;
                if (cents < -1200) cents = -1200;
                playbackPitchCents = (int16_t)cents;
                timePitchPitchQ16 = (uint32_t)(powf(2.0f, (float32_t)playbackPitchCents / 1200.0f) * 65536.0f);
                if (timePitchEnabled != 0U && activeSource == MEDIA_SOURCE_WAV &&
                    fileOpen != 0U && timePitchRunning == 0U &&
                    (playbackSpeedPercent != 100U || playbackPitchCents != 0))
                {
                    resetTimePitch();
                    timePitchRunning = 1U;
                }
                osMutexAcquire(snapshotMutex, osWaitForever);
                sharedSnapshot.pitchCents = playbackPitchCents;
                sharedSnapshot.revision++;
                osMutexRelease(snapshotMutex);
                break;
            }
            case CMD_TIMEPITCH_ENABLE:
            {
                uint8_t enable = command.value != 0U ? 1U : 0U;
                if (enable != timePitchEnabled)
                {
                    uint32_t resumeSecond;
                    osMutexAcquire(snapshotMutex, osWaitForever);
                    resumeSecond = sharedSnapshot.elapsedSeconds;
                    osMutexRelease(snapshotMutex);
                    timePitchEnabled = enable;

                    /* The WSOLA ring reads ahead of the audible position. A
                       controlled seek prevents a short jump when bypassing it. */
                    if (activeSource == MEDIA_SOURCE_WAV && fileOpen != 0U)
                    {
                        timePitchRunning = 0U;
                        resetTimePitch();
                        seekTo(resumeSecond);
                    }
                }
                osMutexAcquire(snapshotMutex, osWaitForever);
                sharedSnapshot.timePitchEnabled = timePitchEnabled;
                sharedSnapshot.revision++;
                osMutexRelease(snapshotMutex);
                break;
            }
            case CMD_SOURCE:
                if (activeSource == MEDIA_SOURCE_WAV)
                {
                    (void)startAux();
                }
                else if (activeSource == MEDIA_SOURCE_AUX)
                {
                    enterRadioMode();
                }
                else if (activeSource == MEDIA_SOURCE_RADIO)
                {
                    leaveRadioMode(1U);
                }
                else
                {
                    leavePcMode();
                }
                break;
            case CMD_STORAGE:
            {
                uint8_t requested = command.value <= MEDIA_STORAGE_USB ? (uint8_t)command.value :
                                    (activeStorage == MEDIA_STORAGE_SD ? MEDIA_STORAGE_USB : MEDIA_STORAGE_SD);
                if (requested != activeStorage)
                {
                    seekPending = 0U;
                    if (activeSource == MEDIA_SOURCE_AUX) leaveAux(0U);
                    else if (activeSource == MEDIA_SOURCE_RADIO) leaveRadioMode(0U);
                    else closeTrack();
                    activeStorage = requested;
                    currentTrack = 0U;

                    osMutexAcquire(snapshotMutex, osWaitForever);
                    sharedSnapshot.storage = activeStorage;
                    sharedSnapshot.trackCount = 0U;
                    sharedSnapshot.currentTrack = 0U;
                    sharedSnapshot.elapsedSeconds = 0U;
                    sharedSnapshot.durationSeconds = 0U;
                    sharedSnapshot.currentName[0] = '\0';
                    sharedSnapshot.state = MEDIA_STATE_NO_CARD;
                    strcpy(sharedSnapshot.status, activeStorage == MEDIA_STORAGE_USB ?
                           "Switching to USB MSC..." : "Switching to SD...");
                    sharedSnapshot.libraryRevision++;
                    sharedSnapshot.revision++;
                    osMutexRelease(snapshotMutex);
                    mountAndScan();
                }
                break;
            }
            case CMD_FOLDER:
                selectFolder((uint16_t)command.value);
                break;
            case CMD_DELETE:
                deleteFile((uint16_t)command.value);
                break;
            case CMD_RECORD_START:
                startRecording();
                break;
            case CMD_RECORD_STOP:
                stopRecording();
                break;
            case CMD_RECORD_CONFIRM:
                confirmRecording();
                break;
            case CMD_RECORD_DISCARD:
                discardRecording();
                break;
            case CMD_RECORD_GAIN:
            {
                int32_t centiDb = (int32_t)command.value;
                if (centiDb > 3000) centiDb = 3000;
                if (centiDb < -3000) centiDb = -3000;
                recorderGainCentiDb = (int16_t)centiDb;
                recorderGainQ15 = (uint32_t)(powf(10.0f, (float32_t)centiDb / 2000.0f) * 32768.0f);
                osMutexAcquire(snapshotMutex, osWaitForever);
                recorderSnapshot.gainCentiDb = recorderGainCentiDb;
                recorderSnapshot.revision++;
                osMutexRelease(snapshotMutex);
                break;
            }
            }
        }

        if (recorderActive != 0U && recorderQueueCount != 0U)
            recorderWritePending(0U);

        if ((auxInputFlags & 1U) != 0U)
        {
            auxInputFlags &= (uint8_t)~1U;
            if (activeSource == MEDIA_SOURCE_AUX) processAuxHalf(0U);
        }
        if ((auxInputFlags & 2U) != 0U)
        {
            auxInputFlags &= (uint8_t)~2U;
            if (activeSource == MEDIA_SOURCE_AUX) processAuxHalf(AUX_AUDIO_HALF_BYTES);
        }

        if ((activeSource == MEDIA_SOURCE_WAV || activeSource == MEDIA_SOURCE_RADIO) &&
            (refillFlags & 1U) != 0U)
        {
            refillFlags &= (uint8_t)~1U;
            if (endHalf == 1U) (void)startTrack((uint16_t)((currentTrack + 1U) % sharedSnapshot.trackCount));
            else if (fillRealtimeHalf(0U) == 0U)
            {
                closeTrack();
                setState(MEDIA_STATE_ERROR, activeSource == MEDIA_SOURCE_RADIO ?
                         "RADIO DECODE ERROR" : "SD STREAM ERROR");
            }
        }
        if ((activeSource == MEDIA_SOURCE_WAV || activeSource == MEDIA_SOURCE_RADIO) &&
            (refillFlags & 2U) != 0U)
        {
            refillFlags &= (uint8_t)~2U;
            if (endHalf == 2U) (void)startTrack((uint16_t)((currentTrack + 1U) % sharedSnapshot.trackCount));
            else if (fillRealtimeHalf(MEDIA_AUDIO_HALF_BYTES) == 0U)
            {
                closeTrack();
                setState(MEDIA_STATE_ERROR, activeSource == MEDIA_SOURCE_RADIO ?
                         "RADIO DECODE ERROR" : "SD STREAM ERROR");
            }
        }

        if (fileOpen != 0U && sharedSnapshot.state != MEDIA_STATE_READY && wavInfo.byteRate != 0U)
        {
            uint32_t position = f_tell(&audioFile);
            uint32_t elapsed;
            if (currentFormat == MEDIA_FORMAT_MP3)
            {
                uint32_t buffered = mp3InputFilled - mp3InputConsumed;
                if (position > buffered) position -= buffered;
                else position = 0U;
            }
            elapsed = position > wavInfo.dataOffset ? (position - wavInfo.dataOffset) / wavInfo.byteRate : 0U;
            if (elapsed > sharedSnapshot.durationSeconds && sharedSnapshot.durationSeconds != 0U)
                elapsed = sharedSnapshot.durationSeconds;
            if (elapsed != sharedSnapshot.elapsedSeconds)
            {
                osMutexAcquire(snapshotMutex, osWaitForever);
                sharedSnapshot.elapsedSeconds = elapsed;
                sharedSnapshot.revision++;
                osMutexRelease(snapshotMutex);
            }
        }

        {
            InternetRadioSnapshot radio;
            InternetRadio_GetSnapshot(&radio);
            if (activeSource == MEDIA_SOURCE_RADIO)
            {
                if (radio.revision != radioRevisionSeen)
                {
                    uint8_t uiChanged = 0U;
                    radioRevisionSeen = radio.revision;
                    osMutexAcquire(snapshotMutex, osWaitForever);
                    if (strncmp(sharedSnapshot.status, radio.status,
                                sizeof(sharedSnapshot.status)) != 0)
                    {
                        strncpy(sharedSnapshot.status, radio.status,
                                sizeof(sharedSnapshot.status) - 1U);
                        sharedSnapshot.status[sizeof(sharedSnapshot.status) - 1U] = '\0';
                        uiChanged = 1U;
                    }
                    if (strncmp(sharedSnapshot.currentName, radio.stationName,
                                sizeof(sharedSnapshot.currentName)) != 0)
                    {
                        strncpy(sharedSnapshot.currentName, radio.stationName,
                                sizeof(sharedSnapshot.currentName) - 1U);
                        sharedSnapshot.currentName[sizeof(sharedSnapshot.currentName) - 1U] = '\0';
                        uiChanged = 1U;
                    }
                    if (uiChanged != 0U) sharedSnapshot.revision++;
                    osMutexRelease(snapshotMutex);
                }
                if (radioPlaybackStarted == 0U && radio.state == INTERNET_RADIO_STREAMING)
                {
                    if (startRadioAudio() == 0U)
                    {
                        osMutexAcquire(snapshotMutex, osWaitForever);
                        strcpy(sharedSnapshot.status, "Radio waiting for MP3 frame...");
                        sharedSnapshot.revision++;
                        osMutexRelease(snapshotMutex);
                    }
                }
            }
        }

        {
            uint32_t pcRevision = USBPCAudio_GetRevision();
            if (pcRevision != usbPcRevisionSeen)
            {
                usbPcRevisionSeen = pcRevision;
                if (USBPCAudio_IsConfigured() != 0U && activeSource != MEDIA_SOURCE_PC &&
                    recorderActive == 0U)
                    enterPcMode();
                else if (USBPCAudio_IsConfigured() == 0U && activeSource == MEDIA_SOURCE_PC)
                    leavePcMode();
            }
        }

        /* USB class callbacks only queue work.  Run codec/SAI operations
           after PC-mode switching has stopped the previous audio path. */
        USBPCAudio_Process();

        {
            uint32_t usbRevision = USBStorage_GetRevision();
            if (usbRevision != usbStorageRevisionSeen)
            {
                usbStorageRevisionSeen = usbRevision;
                if (activeStorage == MEDIA_STORAGE_USB && recorderActive == 0U &&
                    recorderSnapshot.state != MEDIA_RECORDER_AWAITING_SAVE)
                {
                    if (USBStorage_IsReady() == 0U && activeSource == MEDIA_SOURCE_WAV)
                        closeTrack();
                    mountAndScan();
                }
            }
        }

        if (fileOpen == 0U && sharedSnapshot.trackCount == 0U && recorderActive == 0U &&
            recorderSnapshot.state != MEDIA_RECORDER_AWAITING_SAVE &&
            (uint32_t)(osKernelGetTickCount() - lastScanTick) >= MEDIA_RESCAN_TICKS)
        {
            mountAndScan();
        }

        if (refillFlags == 0U) predecodeRadioAacFrame();

    }
}

void MediaPlayer_GetSnapshot(MediaSnapshot *snapshot)
{
    if (snapshot == NULL || snapshotMutex == NULL) return;
    osMutexAcquire(snapshotMutex, osWaitForever);
    *snapshot = sharedSnapshot;
    osMutexRelease(snapshotMutex);
}

void MediaRecorder_GetSnapshot(MediaRecorderSnapshot *snapshot)
{
    if (snapshot == NULL || snapshotMutex == NULL) return;
    osMutexAcquire(snapshotMutex, osWaitForever);
    *snapshot = recorderSnapshot;
    osMutexRelease(snapshotMutex);

    /* Availability is intentionally evaluated at read time so an unplugged
       SD/USB device disables Start immediately, even before the next rescan. */
    if ((snapshot->state == MEDIA_RECORDER_READY || snapshot->state == MEDIA_RECORDER_SAVED ||
         snapshot->state == MEDIA_RECORDER_ERROR || snapshot->state == MEDIA_RECORDER_NO_STORAGE) &&
        recorderStorageReady() == 0U)
    {
        snapshot->state = MEDIA_RECORDER_NO_STORAGE;
        strncpy(snapshot->status, activeStorage == MEDIA_STORAGE_USB ?
                "USB MSC bağlı değil" : "SD kart bağlı değil", sizeof(snapshot->status) - 1U);
        snapshot->status[sizeof(snapshot->status) - 1U] = '\0';
    }
    else if (snapshot->state == MEDIA_RECORDER_NO_STORAGE && recorderStorageReady() != 0U)
    {
        snapshot->state = MEDIA_RECORDER_READY;
        strcpy(snapshot->status, "Kayıt için hazır");
    }
    snapshot->storage = activeStorage;
}

uint16_t MediaPlayer_GetTrackCount(void)
{
    return sharedSnapshot.trackCount;
}

uint8_t MediaPlayer_GetTrackName(uint16_t index, char *name, uint16_t capacity)
{
    if (name == NULL || capacity == 0U || index >= sharedSnapshot.trackCount) return 0U;
    osMutexAcquire(snapshotMutex, osWaitForever);
    strncpy(name, tracks[index].name, capacity - 1U);
    name[capacity - 1U] = '\0';
    osMutexRelease(snapshotMutex);
    return 1U;
}

uint16_t MediaPlayer_GetFileCount(void)
{
    uint16_t count;
    if (snapshotMutex == NULL) return 0U;
    osMutexAcquire(snapshotMutex, osWaitForever);
    count = mediaFileCount;
    osMutexRelease(snapshotMutex);
    return count;
}

uint8_t MediaPlayer_GetFileName(uint16_t index, char *name, uint16_t capacity)
{
    if (name == NULL || capacity == 0U || snapshotMutex == NULL) return 0U;
    osMutexAcquire(snapshotMutex, osWaitForever);
    if (index >= mediaFileCount)
    {
        osMutexRelease(snapshotMutex);
        return 0U;
    }
    strncpy(name, files[index].name, capacity - 1U);
    name[capacity - 1U] = '\0';
    osMutexRelease(snapshotMutex);
    return 1U;
}

uint16_t MediaPlayer_GetFolderCount(void)
{
    uint16_t count;
    if (snapshotMutex == NULL) return 0U;
    osMutexAcquire(snapshotMutex, osWaitForever);
    count = mediaFolderCount;
    osMutexRelease(snapshotMutex);
    return count;
}

uint8_t MediaPlayer_GetFolderName(uint16_t index, char *name, uint16_t capacity)
{
    if (name == NULL || capacity == 0U || snapshotMutex == NULL) return 0U;
    osMutexAcquire(snapshotMutex, osWaitForever);
    if (index >= mediaFolderCount)
    {
        osMutexRelease(snapshotMutex);
        return 0U;
    }
    strncpy(name, folders[index].name, capacity - 1U);
    name[capacity - 1U] = '\0';
    osMutexRelease(snapshotMutex);
    return 1U;
}

void MediaPlayer_GetCurrentFolder(char *name, uint16_t capacity)
{
    if (name == NULL || capacity == 0U || snapshotMutex == NULL) return;
    osMutexAcquire(snapshotMutex, osWaitForever);
    strncpy(name, currentFolders[activeStorage], capacity - 1U);
    name[capacity - 1U] = '\0';
    osMutexRelease(snapshotMutex);
}

void MediaPlayer_GetDeleteSnapshot(MediaDeleteSnapshot *snapshot)
{
    if (snapshot == NULL || snapshotMutex == NULL) return;
    osMutexAcquire(snapshotMutex, osWaitForever);
    *snapshot = deleteSnapshot;
    osMutexRelease(snapshotMutex);
}

void MediaPlayer_GetStatus(char *status, uint16_t capacity)
{
    if (status == NULL || capacity == 0U || snapshotMutex == NULL) return;
    osMutexAcquire(snapshotMutex, osWaitForever);
    strncpy(status, sharedSnapshot.status, capacity - 1U);
    status[capacity - 1U] = '\0';
    osMutexRelease(snapshotMutex);
}

uint32_t MediaPlayer_GetSpectrum(uint8_t levels[MEDIA_SPECTRUM_BANDS])
{
    uint32_t revision = spectrumRevision;
    if (levels == NULL || spectrumMutex == NULL) return revision;
    if (osMutexAcquire(spectrumMutex, 0U) == osOK)
    {
        memcpy(levels, sharedSpectrum, sizeof(sharedSpectrum));
        revision = spectrumRevision;
        osMutexRelease(spectrumMutex);
    }
    return revision;
}

void MediaPlayer_Select(uint16_t index) { postCommand(CMD_SELECT, index); }
void MediaPlayer_TogglePlayPause(void) { postCommand(CMD_TOGGLE, 0U); }
void MediaPlayer_Next(void) { postCommand(CMD_NEXT, 0U); }
void MediaPlayer_Previous(void) { postCommand(CMD_PREVIOUS, 0U); }
void MediaPlayer_SetVolume(uint8_t value) { postCommand(CMD_VOLUME, value); }
void MediaPlayer_Seek(uint32_t seconds)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    pendingSeekSeconds = seconds;
    pendingSeekTick = osKernelGetTickCount();
    seekPending = 1U;
    if (primask == 0U) __enable_irq();
}
void MediaPlayer_SetEQBand(uint8_t band, uint8_t value)
{
    if (band < MEDIA_EQ_BANDS) postCommand(CMD_EQ_BAND, ((uint32_t)band << 8) | value);
}
void MediaPlayer_ApplyEQPreset(uint8_t preset)
{
    if (preset < 5U) postCommand(CMD_EQ_PRESET, preset);
}
void MediaPlayer_SetSoftwareEQBand(uint8_t band, uint8_t value)
{
    if (band < MEDIA_SW_EQ_BANDS) postCommand(CMD_SW_EQ_BAND, ((uint32_t)band << 8) | value);
}
void MediaPlayer_SetSoftwareEQPreamp(uint8_t value)
{
    postCommand(CMD_SW_EQ_PREAMP, value);
}
void MediaPlayer_ApplySoftwareEQPreset(uint8_t preset)
{
    if (preset < MEDIA_SW_EQ_PRESETS) postCommand(CMD_SW_EQ_PRESET, preset);
}
void MediaPlayer_SetSpeed(uint16_t percent)
{
    postCommand(CMD_SPEED, percent);
}
void MediaPlayer_SetPitch(int16_t cents)
{
    postCommand(CMD_PITCH, (uint32_t)(int32_t)cents);
}

void MediaPlayer_SetTimePitchEnabled(uint8_t enabled)
{
    postCommand(CMD_TIMEPITCH_ENABLE, enabled != 0U ? 1U : 0U);
}

void MediaPlayer_ToggleSource(void) { postCommand(CMD_SOURCE, 0U); }
void MediaPlayer_ToggleStorage(void) { postCommand(CMD_STORAGE, 0xFFFFFFFFU); }
void MediaPlayer_SetStorage(MediaStorage storage)
{
    if (storage <= MEDIA_STORAGE_USB) postCommand(CMD_STORAGE, (uint32_t)storage);
}
void MediaPlayer_SelectFolder(uint16_t index) { postCommand(CMD_FOLDER, index); }
void MediaPlayer_DeleteFile(uint16_t index)
{
    char name[MEDIA_TRACK_NAME_SIZE];
    if (MediaPlayer_GetFileName(index, name, sizeof(name)) == 0U) return;
    commitDeleteResult(MEDIA_DELETE_PENDING, name, "Siliniyor...");
    postCommand(CMD_DELETE, index);
}

void MediaRecorder_Start(void) { postCommand(CMD_RECORD_START, 0U); }
void MediaRecorder_Stop(void) { postCommand(CMD_RECORD_STOP, 0U); }
void MediaRecorder_ConfirmSave(void) { postCommand(CMD_RECORD_CONFIRM, 0U); }
void MediaRecorder_Discard(void) { postCommand(CMD_RECORD_DISCARD, 0U); }
void MediaRecorder_SetGain(int16_t centiDb) { postCommand(CMD_RECORD_GAIN, (uint32_t)(int32_t)centiDb); }

void MediaPlayer_I2CLock(void)
{
    if (i2cMutex != NULL && osKernelGetState() == osKernelRunning) osMutexAcquire(i2cMutex, osWaitForever);
}

void MediaPlayer_I2CUnlock(void)
{
    if (i2cMutex != NULL && osKernelGetState() == osKernelRunning) osMutexRelease(i2cMutex);
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
    if (activeSource == MEDIA_SOURCE_PC) USBPCAudio_AudioCompleteCallback();
    else if (activeSource == MEDIA_SOURCE_WAV || activeSource == MEDIA_SOURCE_RADIO)
    {
        if ((refillFlags & 2U) != 0U) mediaRefillLateEvents++;
        refillFlags |= 2U;
    }
}
void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
    if (activeSource == MEDIA_SOURCE_PC) USBPCAudio_AudioHalfCallback();
    else if (activeSource == MEDIA_SOURCE_WAV || activeSource == MEDIA_SOURCE_RADIO)
    {
        if ((refillFlags & 1U) != 0U) mediaRefillLateEvents++;
        refillFlags |= 1U;
    }
}
void BSP_AUDIO_IN_TransferComplete_CallBack(void)
{
    if (recorderActive != 0U)
    {
        recorderQueueDmaHalf(RECORDER_HALF_BYTES);
    }
    else if (activeSource == MEDIA_SOURCE_AUX) auxInputFlags |= 2U;
}
void BSP_AUDIO_IN_HalfTransfer_CallBack(void)
{
    if (recorderActive != 0U)
    {
        recorderQueueDmaHalf(0U);
    }
    else if (activeSource == MEDIA_SOURCE_AUX) auxInputFlags |= 1U;
}
