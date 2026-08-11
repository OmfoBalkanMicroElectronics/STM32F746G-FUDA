#include "internet_radio.h"

#include "cmsis_os2.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "stm32f7xx.h"

#include <stdio.h>
#include <string.h>

#define RADIO_RING_BYTES           65536U
#define RADIO_RING_MASK            (RADIO_RING_BYTES - 1U)
#define RADIO_TCP_WINDOW_BYTES     16384U
#define RADIO_HEADER_BYTES         1024U
#define RADIO_PREBUFFER_BYTES      32768U
#define RADIO_FLOW_HIGH_BYTES      49152U
#define RADIO_FLOW_LOW_BYTES       32768U
#define RADIO_RECONNECT_MS         3000U
#define RADIO_STALL_MS             7000U

#if (RADIO_RING_BYTES & RADIO_RING_MASK) != 0
#error RADIO_RING_BYTES must be a power of two
#endif

/* The stream is CPU-only, so SDRAM is safe and preserves scarce internal SRAM.
   Producer and consumer run on the same M7 core; publishing the monotonic
   counters after a DMB gives a lock-free single-producer/single-consumer FIFO. */
static uint8_t radioRing[RADIO_RING_BYTES]
    __attribute__((section("MediaTrackSection"), aligned(32)));
static volatile uint32_t radioWriteCount;
static volatile uint32_t radioReadCount;
static volatile uint8_t radioDesired;
static volatile uint8_t radioStartQueued;
static volatile uint8_t radioStationIndex;
static struct tcp_pcb *radioPcb;
static InternetRadioSnapshot radioSnapshot;
static char radioHeader[RADIO_HEADER_BYTES];
static uint16_t radioHeaderLength;
static uint8_t radioHeaderComplete;
static uint32_t radioLastAttemptTick;
static uint32_t radioLastDataTick;
/* Bytes accepted and freed from lwIP without reopening its receive window.
   Holding this credit is TCP backpressure: Icecast then slows down to the
   decoder's real consumption rate instead of overflowing the SDRAM FIFO. */
static uint32_t radioWithheldCredit;

typedef struct
{
    const char *name;
    const char *host;
    const char *path;
    uint16_t port;
    uint8_t codec;
} RadioStation;

/* Direct, non-TLS streams are intentional: the F746 can spend its RAM and CPU
   on audio instead of a TLS session.  All four endpoints were verified live. */
static const RadioStation radioStations[] =
{
    {"Groove Salad", "ice2.somafm.com", "/groovesalad-128-mp3", 80U, INTERNET_RADIO_CODEC_MP3},
    {"Radyo 45lik", "stream.radyo45lik.com", "/stream", 4545U, INTERNET_RADIO_CODEC_AAC},
    {"Pal Nostalji", "shoutcast.radyogrup.com", "/", 1010U, INTERNET_RADIO_CODEC_AAC},
    {"Pal Station Pop", "shoutcast.radyogrup.com", "/", 1020U, INTERNET_RADIO_CODEC_AAC}
};
#define RADIO_STATION_COUNT ((uint8_t)(sizeof(radioStations) / sizeof(radioStations[0])))
static char radioRequest[320];
static uint32_t enterCritical(void);
static void leaveCritical(uint32_t primask);

/* The global lwIP window is intentionally large for the diagnostic speed
   test.  Radio uses a 64 KiB application FIFO, so cap only its PCB to prevent
   an already-advertised WAN window from overrunning that FIFO. */
static void limitRadioReceiveWindow(struct tcp_pcb *pcb)
{
    if (pcb == NULL) return;
    if (pcb->rcv_wnd > RADIO_TCP_WINDOW_BYTES)
        pcb->rcv_wnd = RADIO_TCP_WINDOW_BYTES;
    pcb->rcv_ann_wnd = pcb->rcv_wnd;
    pcb->rcv_ann_right_edge = pcb->rcv_nxt + pcb->rcv_ann_wnd;
}

static const RadioStation *currentStation(void)
{
    uint8_t index = radioStationIndex;
    if (index >= RADIO_STATION_COUNT) index = 0U;
    return &radioStations[index];
}

static void publishStation(void)
{
    const RadioStation *station = currentStation();
    uint32_t primask = enterCritical();
    radioSnapshot.stationIndex = radioStationIndex;
    radioSnapshot.stationCount = RADIO_STATION_COUNT;
    radioSnapshot.codec = station->codec;
    strncpy(radioSnapshot.stationName, station->name, sizeof(radioSnapshot.stationName) - 1U);
    radioSnapshot.stationName[sizeof(radioSnapshot.stationName) - 1U] = '\0';
    leaveCritical(primask);
}

static uint32_t enterCritical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void leaveCritical(uint32_t primask)
{
    if (primask == 0U) __enable_irq();
}

static void setRadioState(InternetRadioState state, const char *status)
{
    uint32_t primask = enterCritical();
    radioSnapshot.state = (uint8_t)state;
    radioSnapshot.bufferedBytes = radioWriteCount - radioReadCount;
    if (status != NULL)
    {
        strncpy(radioSnapshot.status, status, sizeof(radioSnapshot.status) - 1U);
        radioSnapshot.status[sizeof(radioSnapshot.status) - 1U] = '\0';
    }
    radioSnapshot.revision++;
    leaveCritical(primask);
}

static void resetRing(void)
{
    uint32_t primask = enterCritical();
    radioReadCount = 0U;
    radioWriteCount = 0U;
    radioSnapshot.bufferedBytes = 0U;
    radioWithheldCredit = 0U;
    leaveCritical(primask);
}

/* Must only run on lwIP's TCP/IP thread. */
static void releaseReceiveCredit(struct tcp_pcb *pcb)
{
    while (pcb != NULL && radioWithheldCredit != 0U)
    {
        u16_t credit = radioWithheldCredit > 0xFFFFU ? 0xFFFFU : (u16_t)radioWithheldCredit;
        tcp_recved(pcb, credit);
        limitRadioReceiveWindow(pcb);
        radioWithheldCredit -= credit;
    }
    if (pcb != NULL) (void)tcp_output(pcb);
}

static uint8_t pushRadioByte(uint8_t value)
{
    uint32_t write = radioWriteCount;
    if ((write - radioReadCount) >= RADIO_RING_BYTES) return 0U;
    radioRing[write & RADIO_RING_MASK] = value;
    __DMB();
    radioWriteCount = write + 1U;
    return 1U;
}

static void closeRadioPcb(void)
{
    if (radioPcb != NULL)
    {
        tcp_arg(radioPcb, NULL);
        tcp_recv(radioPcb, NULL);
        tcp_sent(radioPcb, NULL);
        tcp_poll(radioPcb, NULL, 0U);
        tcp_err(radioPcb, NULL);
        tcp_abort(radioPcb);
        radioPcb = NULL;
    }
}

static void failRadio(const char *status)
{
    closeRadioPcb();
    radioLastAttemptTick = osKernelGetTickCount();
    setRadioState(INTERNET_RADIO_ERROR, status);
}

static err_t radioReceive(void *argument, struct tcp_pcb *pcb, struct pbuf *packet, err_t error)
{
    struct pbuf *part;
    uint8_t overflow = 0U;
    (void)argument;

    if (packet == NULL)
    {
        if (pcb == radioPcb) radioPcb = NULL;
        (void)tcp_close(pcb);
        radioLastAttemptTick = osKernelGetTickCount();
        setRadioState(INTERNET_RADIO_ERROR, "Radio stream closed");
        return ERR_OK;
    }
    if (error != ERR_OK)
    {
        pbuf_free(packet);
        return error;
    }

    for (part = packet; part != NULL; part = part->next)
    {
        const uint8_t *source = (const uint8_t *)part->payload;
        uint16_t i;
        for (i = 0U; i < part->len; i++)
        {
            uint8_t value = source[i];
            if (radioHeaderComplete == 0U)
            {
                if (radioHeaderLength >= RADIO_HEADER_BYTES - 1U)
                {
                    pbuf_free(packet);
                    failRadio("Radio HTTP header too large");
                    return ERR_ABRT;
                }
                radioHeader[radioHeaderLength++] = (char)value;
                radioHeader[radioHeaderLength] = '\0';
                if (radioHeaderLength >= 4U &&
                    radioHeader[radioHeaderLength - 4U] == '\r' &&
                    radioHeader[radioHeaderLength - 3U] == '\n' &&
                    radioHeader[radioHeaderLength - 2U] == '\r' &&
                    radioHeader[radioHeaderLength - 1U] == '\n')
                {
                    if ((strncmp(radioHeader, "HTTP/1.", 7U) != 0 ||
                         strstr(radioHeader, " 200 ") == NULL) &&
                        strncmp(radioHeader, "ICY 200", 7U) != 0)
                    {
                        pbuf_free(packet);
                        failRadio("Radio HTTP response rejected");
                        return ERR_ABRT;
                    }
                    radioHeaderComplete = 1U;
                    setRadioState(INTERNET_RADIO_BUFFERING, "Radio buffering...");
                }
            }
            else if (pushRadioByte(value) == 0U)
            {
                overflow = 1U;
            }
        }
    }

    {
        uint32_t primask = enterCritical();
        radioSnapshot.receivedBytes += packet->tot_len;
        radioSnapshot.bufferedBytes = radioWriteCount - radioReadCount;
        radioSnapshot.revision++;
        leaveCritical(primask);
    }
    /* Do not continuously advertise an open receive window while the server
       is sending its large startup burst.  lwIP retains only a small window
       in flight, so stopping credit at 48 KiB cannot overrun the 64 KiB FIFO. */
    if (InternetRadio_Available() >= RADIO_FLOW_HIGH_BYTES)
        radioWithheldCredit += packet->tot_len;
    else
    {
        radioWithheldCredit += packet->tot_len;
        releaseReceiveCredit(pcb);
    }
    radioLastDataTick = osKernelGetTickCount();
    pbuf_free(packet);

    if (overflow != 0U)
    {
        failRadio("Radio buffer overflow");
        return ERR_ABRT;
    }
    else if (radioHeaderComplete != 0U && InternetRadio_Available() >= RADIO_PREBUFFER_BYTES &&
             radioSnapshot.state != INTERNET_RADIO_STREAMING)
    {
        setRadioState(INTERNET_RADIO_STREAMING, "Groove Salad 128k MP3");
    }
    return ERR_OK;
}

static void radioError(void *argument, err_t error)
{
    (void)argument;
    (void)error;
    radioPcb = NULL;
    radioLastAttemptTick = osKernelGetTickCount();
    setRadioState(INTERNET_RADIO_ERROR, "Radio TCP error");
}

static err_t radioPoll(void *argument, struct tcp_pcb *pcb)
{
    (void)argument;
    if (radioWithheldCredit != 0U && InternetRadio_Available() <= RADIO_FLOW_LOW_BYTES)
        releaseReceiveCredit(pcb);
    if (radioDesired != 0U && radioHeaderComplete != 0U &&
        radioWithheldCredit == 0U &&
        (uint32_t)(osKernelGetTickCount() - radioLastDataTick) >= RADIO_STALL_MS)
    {
        failRadio("Radio stream stalled");
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t radioConnected(void *argument, struct tcp_pcb *pcb, err_t error)
{
    err_t result;
    int requestLength;
    const RadioStation *station = currentStation();
    (void)argument;
    if (error != ERR_OK || radioDesired == 0U) return error;

    limitRadioReceiveWindow(pcb);
    radioHeaderLength = 0U;
    radioHeaderComplete = 0U;
    memset(radioHeader, 0, sizeof(radioHeader));
    tcp_recv(pcb, radioReceive);
    tcp_err(pcb, radioError);
    /* 500 ms flow-control cadence keeps at least two seconds of 128 kbps
       compressed audio buffered even during scheduler/network jitter. */
    tcp_poll(pcb, radioPoll, 1U);
    requestLength = snprintf(radioRequest, sizeof(radioRequest),
                             "GET %s HTTP/1.0\r\nHost: %s:%u\r\n"
                             "User-Agent: CleanMP3Player/1.0\r\n"
                             "Accept: audio/mpeg, audio/aac, audio/aacp\r\n"
                             "Connection: close\r\n\r\n",
                             station->path, station->host, (unsigned int)station->port);
    if (requestLength <= 0 || requestLength >= (int)sizeof(radioRequest))
    {
        failRadio("Radio request too large");
        return ERR_ABRT;
    }
    result = tcp_write(pcb, radioRequest, (u16_t)requestLength, TCP_WRITE_FLAG_COPY);
    if (result == ERR_OK) result = tcp_output(pcb);
    if (result != ERR_OK)
    {
        failRadio("Radio HTTP request failed");
        return ERR_ABRT;
    }
    radioLastDataTick = osKernelGetTickCount();
    setRadioState(INTERNET_RADIO_HEADERS, "Radio requesting stream...");
    return ERR_OK;
}

static void connectRadio(const ip_addr_t *address)
{
    const RadioStation *station = currentStation();
    if (radioDesired == 0U || address == NULL) return;
    closeRadioPcb();
    resetRing();
    radioPcb = tcp_new();
    if (radioPcb == NULL)
    {
        radioLastAttemptTick = osKernelGetTickCount();
        setRadioState(INTERNET_RADIO_ERROR, "No TCP PCB for radio");
        return;
    }
    tcp_arg(radioPcb, NULL);
    tcp_err(radioPcb, radioError);
    setRadioState(INTERNET_RADIO_CONNECTING, "Radio connecting...");
    if (tcp_connect(radioPcb, address, station->port, radioConnected) != ERR_OK)
    {
        failRadio("Radio connect failed");
    }
}

static void radioDnsResult(const char *name, const ip_addr_t *address, void *argument)
{
    (void)name;
    (void)argument;
    if (radioDesired == 0U) return;
    if (address == NULL)
    {
        radioLastAttemptTick = osKernelGetTickCount();
        setRadioState(INTERNET_RADIO_ERROR, "Radio DNS failed");
        return;
    }
    connectRadio(address);
}

static void beginRadioOnTcpip(void *argument)
{
    ip_addr_t address;
    err_t result;
    (void)argument;
    radioStartQueued = 0U;
    if (radioDesired == 0U) return;
    setRadioState(INTERNET_RADIO_RESOLVING, "Resolving radio server...");
    result = dns_gethostbyname(currentStation()->host, &address, radioDnsResult, NULL);
    if (result == ERR_OK) connectRadio(&address);
    else if (result != ERR_INPROGRESS)
    {
        radioLastAttemptTick = osKernelGetTickCount();
        setRadioState(INTERNET_RADIO_ERROR, "Radio DNS request failed");
    }
}

static void stopRadioOnTcpip(void *argument)
{
    (void)argument;
    closeRadioPcb();
}

void InternetRadio_Init(void)
{
    memset(&radioSnapshot, 0, sizeof(radioSnapshot));
    strcpy(radioSnapshot.status, "Radio stopped");
    radioStationIndex = 0U;
    publishStation();
    resetRing();
}

void InternetRadio_Start(void)
{
    radioDesired = 1U;
    radioHeaderComplete = 0U;
    radioHeaderLength = 0U;
    resetRing();
    publishStation();
    setRadioState(INTERNET_RADIO_WAIT_NETWORK, "Radio waiting for network");
    radioLastAttemptTick = 0U;
}

void InternetRadio_Select(uint8_t index)
{
    if (index >= RADIO_STATION_COUNT || index == radioStationIndex) return;
    InternetRadio_Stop();
    radioStationIndex = index;
    InternetRadio_Start();
}

void InternetRadio_Next(void)
{
    InternetRadio_Select((uint8_t)((radioStationIndex + 1U) % RADIO_STATION_COUNT));
}

void InternetRadio_Previous(void)
{
    InternetRadio_Select(radioStationIndex == 0U ? RADIO_STATION_COUNT - 1U : radioStationIndex - 1U);
}

void InternetRadio_Stop(void)
{
    radioDesired = 0U;
    radioStartQueued = 0U;
    resetRing();
    (void)tcpip_callback(stopRadioOnTcpip, NULL);
    setRadioState(INTERNET_RADIO_STOPPED, "Radio stopped");
}

void InternetRadio_Service(uint8_t linkUp, uint8_t hasAddress)
{
    uint32_t now = osKernelGetTickCount();
    if (radioDesired == 0U) return;
    if (linkUp == 0U || hasAddress == 0U)
    {
        setRadioState(INTERNET_RADIO_WAIT_NETWORK, "Radio waiting for network");
        return;
    }
    if ((radioSnapshot.state == INTERNET_RADIO_WAIT_NETWORK ||
         radioSnapshot.state == INTERNET_RADIO_ERROR ||
         radioSnapshot.state == INTERNET_RADIO_STOPPED) &&
        radioStartQueued == 0U &&
        (radioLastAttemptTick == 0U || (uint32_t)(now - radioLastAttemptTick) >= RADIO_RECONNECT_MS))
    {
        radioStartQueued = 1U;
        radioLastAttemptTick = now;
        if (radioSnapshot.state == INTERNET_RADIO_ERROR)
        {
            uint32_t primask = enterCritical();
            radioSnapshot.reconnectCount++;
            leaveCritical(primask);
        }
        if (tcpip_callback(beginRadioOnTcpip, NULL) != ERR_OK)
        {
            radioStartQueued = 0U;
            setRadioState(INTERNET_RADIO_ERROR, "Radio callback queue full");
        }
    }
}

uint32_t InternetRadio_Available(void)
{
    return radioWriteCount - radioReadCount;
}

uint32_t InternetRadio_Read(uint8_t *destination, uint32_t capacity)
{
    uint32_t read = radioReadCount;
    uint32_t available = radioWriteCount - read;
    uint32_t count = capacity < available ? capacity : available;
    uint32_t first;
    if (destination == NULL || count == 0U) return 0U;

    first = RADIO_RING_BYTES - (read & RADIO_RING_MASK);
    if (first > count) first = count;
    memcpy(destination, &radioRing[read & RADIO_RING_MASK], first);
    if (count > first) memcpy(&destination[first], radioRing, count - first);
    __DMB();
    radioReadCount = read + count;
    return count;
}

void InternetRadio_GetSnapshot(InternetRadioSnapshot *snapshot)
{
    uint32_t primask;
    if (snapshot == NULL) return;
    primask = enterCritical();
    radioSnapshot.bufferedBytes = radioWriteCount - radioReadCount;
    *snapshot = radioSnapshot;
    leaveCritical(primask);
}
