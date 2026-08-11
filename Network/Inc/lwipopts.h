#ifndef CLEAN_MP3_LWIPOPTS_H
#define CLEAN_MP3_LWIPOPTS_H

#define NO_SYS                          0
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (16U * 1024U)
#define LWIP_RAM_HEAP_POINTER           ((void *)0x20048000U)

#define MEMP_NUM_PBUF                   128
#define MEMP_NUM_RAW_PCB                3
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_PCB                2
#define MEMP_NUM_TCP_PCB_LISTEN         0
#define MEMP_NUM_TCP_SEG                128
#define MEMP_NUM_TCPIP_MSG_INPKT        16
#define MEMP_NUM_SYS_TIMEOUT            10
#define PBUF_POOL_SIZE                  10
#define PBUF_POOL_BUFSIZE               1524

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_DHCP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DNS                        1
#define LWIP_IGMP                       0
#define LWIP_AUTOIP                     0
#define LWIP_STATS                      0
#define LWIP_NETIF_LINK_CALLBACK        1

/* Internet radio only needs a small window, but the diagnostic screen also
   measures a real WAN path.  The test server is tens of milliseconds away,
   so window scaling is required to avoid measuring TCP's window ceiling
   instead of the Ethernet/Internet throughput.  Received data is consumed
   immediately by raw callbacks; TCP_WND is flow-control credit, not a RAM
   allocation in this configuration. */
#define TCP_MSS                         1460
#define LWIP_WND_SCALE                  1
#define TCP_RCV_SCALE                   4
#define TCP_WND                         (512U * TCP_MSS)
#define TCP_SND_BUF                     (112U * TCP_MSS)
#define TCP_SND_QUEUELEN                128
#define LWIP_DISABLE_TCP_SANITY_CHECKS  1
#define TCP_QUEUE_OOSEQ                 0
#define TCP_LISTEN_BACKLOG              0
#define TCP_DEFAULT_LISTEN_BACKLOG      0
#define DNS_TABLE_SIZE                  2
#define DNS_MAX_SERVERS                 2
/* The player does not expose a DNS service and uses one fixed station host.
   Avoid pulling a platform PRNG dependency into this small baseline. */
#define LWIP_DNS_SECURE                 0

#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              0
#define CHECKSUM_CHECK_ICMP             1

#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_NETIF_API                  1

#define TCPIP_THREAD_NAME               "TCP/IP"
#define TCPIP_THREAD_STACKSIZE          1024
#define TCPIP_MBOX_SIZE                 16
#define DEFAULT_UDP_RECVMBOX_SIZE       6
#define DEFAULT_RAW_RECVMBOX_SIZE       6
#define DEFAULT_THREAD_STACKSIZE        512
#define TCPIP_THREAD_PRIO               osPriorityNormal

#endif
