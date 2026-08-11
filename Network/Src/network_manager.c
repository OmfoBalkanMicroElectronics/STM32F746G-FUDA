#include "network_manager.h"
#include "ethernetif.h"
#include "internet_radio.h"

#include "cmsis_os.h"
#include "cmsis_os2.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"
#include "lwip/raw.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include <stdio.h>
#include <string.h>

#define GOOGLE_PING_A             8U
#define GOOGLE_PING_B             8U
#define GOOGLE_PING_C             8U
#define GOOGLE_PING_D             8U
#define PING_INTERVAL_MS          5000U
#define PING_TIMEOUT_MS           1500U
#define PING_PAYLOAD_BYTES        24U
#define PING_IDENTIFIER           0x0746U
#define SPEED_HOST                "speedtest.tele2.net"
#define SPEED_DOWNLOAD_PATH       "/100MB.zip"
#define SPEED_TEST_MS             4000U
#define SPEED_UPLOAD_BYTES        (16U * 1024U * 1024U)
#define SPEED_HEADER_BYTES        512U
#define SPEED_UPLOAD_CHUNK        1400U

static struct netif ethernetNetif;
static struct raw_pcb *pingPcb;
static ip_addr_t pingTarget;
static osMutexId_t networkMutex;
static NetworkSnapshot sharedNetwork;
static volatile uint16_t pingSequence;
static volatile uint16_t waitingSequence;
static volatile uint32_t pingSentTick;
static volatile uint8_t pingOutstanding;
static volatile uint8_t speedStartRequested;
static struct tcp_pcb *speedPcb;
static ip_addr_t speedAddress;
static uint32_t speedStartTick;
static uint32_t speedBytes;
static uint32_t speedUploadQueued;
static uint8_t speedHeaderComplete;
static uint16_t speedHeaderLength;
static char speedHeader[SPEED_HEADER_BYTES];
static uint8_t speedUploadData[SPEED_UPLOAD_CHUNK];
volatile uint32_t ethDiagRawIcmp;
volatile uint32_t ethDiagRawEchoReply;
volatile uint32_t ethDiagRawFirstWord;
volatile uint32_t ethDiagRawPayloadAddr;
volatile uint32_t ethDiagRawLen;

static uint32_t speedCentiMbps(uint32_t bytes, uint32_t elapsedMs)
{
    if (elapsedMs == 0U) return 0U;
    return (uint32_t)(((uint64_t)bytes * 8ULL) / ((uint64_t)elapsedMs * 10ULL));
}

static void publishSpeed(NetworkSpeedState state, uint32_t down, uint32_t up,
                         const char *diagnostic)
{
    uint32_t limit;
    osMutexAcquire(networkMutex, osWaitForever);
    sharedNetwork.speedState = (uint8_t)state;
    sharedNetwork.downCentiMbps = down;
    sharedNetwork.upCentiMbps = up;
    limit = (uint32_t)sharedNetwork.linkMbps * 100U;
    sharedNetwork.limitWarning = limit != 0U && (down > limit || up > limit);
    if (diagnostic != NULL)
    {
        strncpy(sharedNetwork.diagnostic, diagnostic, sizeof(sharedNetwork.diagnostic) - 1U);
        sharedNetwork.diagnostic[sizeof(sharedNetwork.diagnostic) - 1U] = '\0';
    }
    sharedNetwork.revision++;
    osMutexRelease(networkMutex);
}

static void speedDetachAndAbort(struct tcp_pcb *pcb)
{
    if (pcb == NULL) return;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_poll(pcb, NULL, 0U);
    tcp_err(pcb, NULL);
    tcp_abort(pcb);
    if (speedPcb == pcb) speedPcb = NULL;
}

static void speedFail(const char *message)
{
    struct tcp_pcb *pcb = speedPcb;
    speedPcb = NULL;
    speedDetachAndAbort(pcb);
    publishSpeed(NETWORK_SPEED_ERROR, sharedNetwork.downCentiMbps,
                 sharedNetwork.upCentiMbps, message);
}

static void speedTcpError(void *argument, err_t error)
{
    uint32_t down;
    uint32_t up;
    (void)argument;
    (void)error;

    /* lwIP has already released the PCB before this callback is invoked. */
    speedPcb = NULL;
    osMutexAcquire(networkMutex, osWaitForever);
    down = sharedNetwork.downCentiMbps;
    up = sharedNetwork.upCentiMbps;
    osMutexRelease(networkMutex);
    publishSpeed(NETWORK_SPEED_ERROR, down, up, "Speed-test TCP error");
}

static void speedPumpUpload(void);
static void speedBeginUpload(void);

static err_t speedUploadReceive(void *argument, struct tcp_pcb *pcb,
                                struct pbuf *packet, err_t error)
{
    (void)argument;
    (void)pcb;
    if (packet != NULL) pbuf_free(packet);
    if (error != ERR_OK) speedFail("Upload TCP error");
    return ERR_OK;
}

static void speedFinishUpload(struct tcp_pcb *pcb)
{
    uint32_t elapsed = osKernelGetTickCount() - speedStartTick;
    uint32_t up = speedCentiMbps(speedBytes, elapsed);
    uint32_t down = sharedNetwork.downCentiMbps;
    speedDetachAndAbort(pcb);
    publishSpeed(NETWORK_SPEED_DONE, down, up, "Test complete");
}

static err_t speedUploadSent(void *argument, struct tcp_pcb *pcb, u16_t length)
{
    (void)argument;
    speedBytes += length;
    if ((uint32_t)(osKernelGetTickCount() - speedStartTick) >= SPEED_TEST_MS ||
        speedBytes >= SPEED_UPLOAD_BYTES)
    {
        speedFinishUpload(pcb);
        return ERR_ABRT;
    }
    speedPumpUpload();
    return ERR_OK;
}

static err_t speedUploadPoll(void *argument, struct tcp_pcb *pcb)
{
    (void)argument;
    if ((uint32_t)(osKernelGetTickCount() - speedStartTick) >= SPEED_TEST_MS)
    {
        speedFinishUpload(pcb);
        return ERR_ABRT;
    }
    speedPumpUpload();
    return ERR_OK;
}

static void speedPumpUpload(void)
{
    struct tcp_pcb *pcb = speedPcb;
    while (pcb != NULL && speedUploadQueued < SPEED_UPLOAD_BYTES)
    {
        uint32_t remaining = SPEED_UPLOAD_BYTES - speedUploadQueued;
        u16_t chunk = remaining > SPEED_UPLOAD_CHUNK ? SPEED_UPLOAD_CHUNK : (u16_t)remaining;
        if (tcp_sndbuf(pcb) < chunk) break;
        /* The test payload is immutable zero data.  Referencing it directly
           lets TCP keep a WAN-sized flight without consuming the 16 KB lwIP
           heap for duplicate payload copies. */
        if (tcp_write(pcb, speedUploadData, chunk, 0U) != ERR_OK) break;
        speedUploadQueued += chunk;
    }
    if (pcb != NULL) (void)tcp_output(pcb);
}

static err_t speedUploadConnected(void *argument, struct tcp_pcb *pcb, err_t error)
{
    char request[240];
    int length;
    (void)argument;
    if (error != ERR_OK) { speedFail("Upload connect failed"); return error; }
    length = snprintf(request, sizeof(request),
                      "POST /upload.php HTTP/1.0\r\nHost: %s\r\n"
                      "Content-Type: application/octet-stream\r\n"
                      "Content-Length: %lu\r\nConnection: close\r\n\r\n",
                      SPEED_HOST, (unsigned long)SPEED_UPLOAD_BYTES);
    if (length <= 0 || length >= (int)sizeof(request) ||
        tcp_write(pcb, request, (u16_t)length, TCP_WRITE_FLAG_COPY) != ERR_OK)
    {
        speedFail("Upload request failed");
        return ERR_ABRT;
    }
    tcp_recv(pcb, speedUploadReceive);
    tcp_sent(pcb, speedUploadSent);
    tcp_poll(pcb, speedUploadPoll, 1U);
    speedStartTick = osKernelGetTickCount();
    speedBytes = 0U;
    speedUploadQueued = 0U;
    publishSpeed(NETWORK_SPEED_UPLOADING, sharedNetwork.downCentiMbps, 0U,
                 "Testing upload...");
    speedPumpUpload();
    return ERR_OK;
}

static void speedBeginUpload(void)
{
    speedPcb = tcp_new();
    if (speedPcb == NULL) { speedFail("No TCP PCB for upload"); return; }
    tcp_arg(speedPcb, NULL);
    tcp_err(speedPcb, speedTcpError);
    if (tcp_connect(speedPcb, &speedAddress, 80U, speedUploadConnected) != ERR_OK)
        speedFail("Upload connect failed");
}

static void speedFinishDownload(struct tcp_pcb *pcb)
{
    uint32_t elapsed = osKernelGetTickCount() - speedStartTick;
    uint32_t down = speedCentiMbps(speedBytes, elapsed);
    speedDetachAndAbort(pcb);
    publishSpeed(NETWORK_SPEED_UPLOADING, down, 0U, "Connecting upload...");
    speedBeginUpload();
}

static err_t speedDownloadReceive(void *argument, struct tcp_pcb *pcb,
                                  struct pbuf *packet, err_t error)
{
    struct pbuf *part;
    (void)argument;
    if (packet == NULL) { speedFail("Download closed early"); return ERR_OK; }
    if (error != ERR_OK) { pbuf_free(packet); speedFail("Download TCP error"); return error; }
    for (part = packet; part != NULL; part = part->next)
    {
        const uint8_t *data = (const uint8_t *)part->payload;
        uint16_t i = 0U;

        /* Only the small HTTP header needs byte-wise parsing. Counting the
           response body per pbuf avoids millions of iterations at 100 Mbps. */
        if (speedHeaderComplete != 0U)
        {
            speedBytes += part->len;
            continue;
        }

        for (; i < part->len; i++)
        {
            if (speedHeaderLength >= SPEED_HEADER_BYTES - 1U)
            {
                pbuf_free(packet); speedFail("HTTP header too large"); return ERR_ABRT;
            }
            speedHeader[speedHeaderLength++] = (char)data[i];
            speedHeader[speedHeaderLength] = '\0';
            if (speedHeaderLength >= 4U &&
                memcmp(&speedHeader[speedHeaderLength - 4U], "\r\n\r\n", 4U) == 0)
            {
                if (strncmp(speedHeader, "HTTP/1.", 7U) != 0 ||
                    strstr(speedHeader, " 200 ") == NULL)
                {
                    pbuf_free(packet); speedFail("HTTP download rejected"); return ERR_ABRT;
                }
                speedHeaderComplete = 1U;
                speedStartTick = osKernelGetTickCount();
                speedBytes = (uint32_t)(part->len - i - 1U);
                publishSpeed(NETWORK_SPEED_DOWNLOADING, 0U, 0U,
                             "Testing download...");
                break;
            }
        }
    }
    tcp_recved(pcb, packet->tot_len);
    pbuf_free(packet);
    if (speedHeaderComplete != 0U &&
        (uint32_t)(osKernelGetTickCount() - speedStartTick) >= SPEED_TEST_MS)
    {
        speedFinishDownload(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t speedDownloadPoll(void *argument, struct tcp_pcb *pcb)
{
    (void)argument;
    if (speedHeaderComplete != 0U &&
        (uint32_t)(osKernelGetTickCount() - speedStartTick) >= SPEED_TEST_MS)
    {
        speedFinishDownload(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t speedDownloadConnected(void *argument, struct tcp_pcb *pcb, err_t error)
{
    char request[192];
    int length;
    (void)argument;
    if (error != ERR_OK) { speedFail("Download connect failed"); return error; }
    length = snprintf(request, sizeof(request),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: CleanMP3Player/1.0\r\n"
                      "Connection: close\r\n\r\n", SPEED_DOWNLOAD_PATH, SPEED_HOST);
    if (length <= 0 || length >= (int)sizeof(request) ||
        tcp_write(pcb, request, (u16_t)length, TCP_WRITE_FLAG_COPY) != ERR_OK)
    {
        speedFail("Download request failed");
        return ERR_ABRT;
    }
    speedHeaderLength = 0U;
    speedHeaderComplete = 0U;
    tcp_recv(pcb, speedDownloadReceive);
    tcp_poll(pcb, speedDownloadPoll, 1U);
    (void)tcp_output(pcb);
    return ERR_OK;
}

static void speedDnsResult(const char *name, const ip_addr_t *address, void *argument)
{
    (void)name;
    (void)argument;
    if (address == NULL) { speedFail("Speed-test DNS failed"); return; }
    speedAddress = *address;
    speedPcb = tcp_new();
    if (speedPcb == NULL) { speedFail("No TCP PCB for download"); return; }
    tcp_arg(speedPcb, NULL);
    tcp_err(speedPcb, speedTcpError);
    if (tcp_connect(speedPcb, &speedAddress, 80U, speedDownloadConnected) != ERR_OK)
        speedFail("Download connect failed");
}

static void speedStartOnTcpip(void *argument)
{
    err_t result;
    (void)argument;
    speedStartRequested = 0U;
    if (!netif_is_link_up(&ethernetNetif) ||
        ip4_addr_isany_val(*netif_ip4_addr(&ethernetNetif)))
    {
        publishSpeed(NETWORK_SPEED_NO_LINK, 0U, 0U, "Ethernet/IP unavailable");
        return;
    }
    if (speedPcb != NULL) speedDetachAndAbort(speedPcb);
    publishSpeed(NETWORK_SPEED_DOWNLOADING, 0U, 0U, "Resolving speed server...");
    result = dns_gethostbyname(SPEED_HOST, &speedAddress, speedDnsResult, NULL);
    if (result == ERR_OK) speedDnsResult(SPEED_HOST, &speedAddress, NULL);
    else if (result != ERR_INPROGRESS) speedFail("Speed-test DNS failed");
}

static void commitNetwork(uint8_t link, uint8_t hasAddress, const char *address,
                          NetworkPingState pingState, uint32_t pingMs)
{
    uint8_t changed;
    uint8_t linkMbps = link != 0U ? EthernetIf_GetLinkMbps() : 0U;
    uint8_t fullDuplex = link != 0U ? EthernetIf_IsFullDuplex() : 0U;
    osMutexAcquire(networkMutex, osWaitForever);
    changed = sharedNetwork.linkUp != link || sharedNetwork.hasAddress != hasAddress ||
              sharedNetwork.pingState != (uint8_t)pingState ||
              sharedNetwork.pingMilliseconds != pingMs ||
              sharedNetwork.linkMbps != linkMbps ||
              sharedNetwork.fullDuplex != fullDuplex ||
              strncmp(sharedNetwork.ipAddress, address, sizeof(sharedNetwork.ipAddress)) != 0;
    sharedNetwork.linkUp = link;
    sharedNetwork.hasAddress = hasAddress;
    sharedNetwork.pingState = (uint8_t)pingState;
    sharedNetwork.pingMilliseconds = pingMs;
    sharedNetwork.linkMbps = linkMbps;
    sharedNetwork.fullDuplex = fullDuplex;
    strncpy(sharedNetwork.ipAddress, address, sizeof(sharedNetwork.ipAddress) - 1U);
    sharedNetwork.ipAddress[sizeof(sharedNetwork.ipAddress) - 1U] = '\0';
    if (changed != 0U) sharedNetwork.revision++;
    osMutexRelease(networkMutex);
}

static uint8_t pingReceive(void *argument, struct raw_pcb *pcb, struct pbuf *packet,
                           const ip_addr_t *address)
{
    struct ip_hdr ipHeader;
    struct icmp_echo_hdr echo;
    uint16_t headerBytes;
    (void)argument;
    (void)pcb;
    (void)address;

    ethDiagRawIcmp++;
    if (packet != NULL)
    {
        uint8_t first[4] = {0U, 0U, 0U, 0U};
        ethDiagRawPayloadAddr = (uint32_t)(uintptr_t)packet->payload;
        ethDiagRawLen = packet->tot_len;
        if (pbuf_copy_partial(packet, first, sizeof(first), 0U) == sizeof(first))
        {
            ethDiagRawFirstWord = ((uint32_t)first[0] << 24) | ((uint32_t)first[1] << 16) |
                                  ((uint32_t)first[2] << 8) | (uint32_t)first[3];
        }
    }

    /* Raw callbacks share this pbuf with lwIP's normal ICMP input path. Do not
       move packet->payload with pbuf_header(): doing so can leave the packet at
       the wrong layer for icmp_input(). Copy only the bytes we need instead. */
    if (packet == NULL ||
        pbuf_copy_partial(packet, &ipHeader, sizeof(ipHeader), 0U) != sizeof(ipHeader))
    {
        return 0U;
    }

    headerBytes = (uint16_t)(IPH_HL(&ipHeader) * 4U);
    if (headerBytes < sizeof(struct ip_hdr) ||
        packet->tot_len < headerBytes + sizeof(struct icmp_echo_hdr) ||
        pbuf_copy_partial(packet, &echo, sizeof(echo), headerBytes) != sizeof(echo))
    {
        return 0U;
    }

    if (ICMPH_TYPE(&echo) == ICMP_ER && lwip_ntohs(echo.id) == PING_IDENTIFIER &&
        lwip_ntohs(echo.seqno) == waitingSequence)
    {
        ethDiagRawEchoReply++;
        uint32_t elapsed = osKernelGetTickCount() - pingSentTick;
        pingOutstanding = 0U;
        commitNetwork(1U, 1U, ip4addr_ntoa(netif_ip4_addr(&ethernetNetif)),
                      NETWORK_PING_OK, elapsed);
        pbuf_free(packet);
        return 1U;
    }

    /* Not our echo reply: leave the pbuf untouched for lwIP's ICMP handler. */
    return 0U;
}

static void pingInitialize(void *argument)
{
    (void)argument;
    IP_ADDR4(&pingTarget, GOOGLE_PING_A, GOOGLE_PING_B, GOOGLE_PING_C, GOOGLE_PING_D);
    pingPcb = raw_new(IP_PROTO_ICMP);
    if (pingPcb != NULL)
    {
        raw_recv(pingPcb, pingReceive, NULL);
        raw_bind(pingPcb, IP_ADDR_ANY);
    }
}

static void pingSend(void *argument)
{
    struct pbuf *packet;
    struct icmp_echo_hdr *echo;
    uint16_t sequence = waitingSequence;
    uint16_t i;
    (void)argument;

    if (pingPcb == NULL)
    {
        pingOutstanding = 0U;
        return;
    }

    packet = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + PING_PAYLOAD_BYTES, PBUF_RAM);
    if (packet == NULL)
    {
        pingOutstanding = 0U;
        return;
    }

    echo = (struct icmp_echo_hdr *)packet->payload;
    ICMPH_TYPE_SET(echo, ICMP_ECHO);
    ICMPH_CODE_SET(echo, 0U);
    echo->id = lwip_htons(PING_IDENTIFIER);
    echo->seqno = lwip_htons(sequence);
    for (i = 0U; i < PING_PAYLOAD_BYTES; i++)
    {
        ((uint8_t *)packet->payload)[sizeof(struct icmp_echo_hdr) + i] = (uint8_t)i;
    }
    echo->chksum = 0U;
    echo->chksum = inet_chksum(echo, packet->len);
    if (raw_sendto(pingPcb, packet, &pingTarget) != ERR_OK) pingOutstanding = 0U;
    pbuf_free(packet);
}

void NetworkManager_Init(void)
{
    memset(&sharedNetwork, 0, sizeof(sharedNetwork));
    strcpy(sharedNetwork.ipAddress, "0.0.0.0");
    strcpy(sharedNetwork.diagnostic, "Ready");
    sharedNetwork.pingState = NETWORK_PING_IDLE;
    sharedNetwork.speedState = NETWORK_SPEED_IDLE;
    networkMutex = osMutexNew(NULL);
    InternetRadio_Init();
}

void NetworkManager_Task(void *argument)
{
    ip_addr_t zeroAddress;
    uint8_t previousLink = 0U;
    uint8_t dhcpStarted = 0U;
    uint32_t lastPingTick = 0U;
    (void)argument;

    /* Let the high-priority media task finish its initial SD mount/scan before
       bringing up another DMA peripheral and its protocol threads. */
    osDelay(1500U);

    tcpip_init(NULL, NULL);
    ip_addr_set_zero_ip4(&zeroAddress);
    netif_add(&ethernetNetif, &zeroAddress, &zeroAddress, &zeroAddress,
              NULL, ethernetif_init, tcpip_input);
    netif_set_default(&ethernetNetif);

    const osThreadAttr_t ethLinkAttributes = {
        .name = "EthLink",
        .stack_size = configMINIMAL_STACK_SIZE * 8U,
        .priority = osPriorityLow
    };
    (void)osThreadNew(ethernet_link_thread, &ethernetNetif, &ethLinkAttributes);
    (void)tcpip_callback(pingInitialize, NULL);

    for (;;)
    {
        uint8_t link = netif_is_link_up(&ethernetNetif) ? 1U : 0U;
        uint8_t hasAddress = !ip4_addr_isany_val(*netif_ip4_addr(&ethernetNetif));
        const char *address = hasAddress != 0U ? ip4addr_ntoa(netif_ip4_addr(&ethernetNetif)) : "0.0.0.0";
        uint32_t now = osKernelGetTickCount();
        NetworkPingState pingState = (NetworkPingState)sharedNetwork.pingState;
        uint32_t pingMs = sharedNetwork.pingMilliseconds;

        if (link != 0U && dhcpStarted == 0U)
        {
            (void)netifapi_dhcp_start(&ethernetNetif);
            dhcpStarted = 1U;
            pingState = NETWORK_PING_IDLE;
        }
        else if (link == 0U && dhcpStarted != 0U)
        {
            (void)netifapi_dhcp_stop(&ethernetNetif);
            netifapi_netif_set_addr(&ethernetNetif, ip_2_ip4(&zeroAddress),
                                    ip_2_ip4(&zeroAddress), ip_2_ip4(&zeroAddress));
            dhcpStarted = 0U;
            pingOutstanding = 0U;
            pingState = NETWORK_PING_IDLE;
            pingMs = 0U;
        }

        if (link != 0U && hasAddress != 0U)
        {
            if (pingOutstanding != 0U && (uint32_t)(now - pingSentTick) >= PING_TIMEOUT_MS)
            {
                pingOutstanding = 0U;
                pingState = NETWORK_PING_TIMEOUT;
                pingMs = 0U;
                lastPingTick = now;
            }
            else if (pingOutstanding == 0U &&
                     (lastPingTick == 0U || (uint32_t)(now - lastPingTick) >= PING_INTERVAL_MS))
            {
                waitingSequence = ++pingSequence;
                pingSentTick = now;
                pingOutstanding = 1U;
                pingState = NETWORK_PING_WAITING;
                pingMs = 0U;
                lastPingTick = now;
                if (tcpip_callback(pingSend, NULL) != ERR_OK)
                {
                    pingOutstanding = 0U;
                    pingState = NETWORK_PING_ERROR;
                }
            }
        }

        if (previousLink != link && link == 0U) lastPingTick = 0U;
        previousLink = link;
        commitNetwork(link, hasAddress, address, pingState, pingMs);
        if (speedStartRequested == 1U)
        {
            speedStartRequested = 2U;
            if (tcpip_callback(speedStartOnTcpip, NULL) != ERR_OK)
            {
                speedStartRequested = 0U;
                publishSpeed(NETWORK_SPEED_ERROR, 0U, 0U, "TCP/IP callback full");
            }
        }
        InternetRadio_Service(link, hasAddress);
        osDelay(250U);
    }
}

void NetworkManager_StartSpeedTest(void)
{
    if (speedStartRequested == 0U) speedStartRequested = 1U;
}

void NetworkManager_GetSnapshot(NetworkSnapshot *snapshot)
{
    if (snapshot == NULL || networkMutex == NULL) return;
    osMutexAcquire(networkMutex, osWaitForever);
    *snapshot = sharedNetwork;
    osMutexRelease(networkMutex);
}
