#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__


#include "lwip/err.h"
#include "lwip/netif.h"
#include "cmsis_os.h"

/* Exported types ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
err_t ethernetif_init(struct netif *netif);
void ethernet_link_thread(void *argument);
uint32_t EthernetIf_GetRxBytes(void);
uint32_t EthernetIf_GetTxBytes(void);
uint8_t EthernetIf_GetLinkMbps(void);
uint8_t EthernetIf_IsFullDuplex(void);
#endif
