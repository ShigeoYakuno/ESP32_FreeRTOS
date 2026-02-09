#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ENET_USE_STATIC_IP 1
#define ENET_IP      "192.168.1.50"
#define ENET_GATEWAY "192.168.1.1"
#define ENET_NETMASK "255.255.255.0"
#define ENET_DNS     "8.8.8.8"

void start_enet_task(void);
bool enet_set_ip(uint8_t host_id);

#ifdef __cplusplus
}
#endif
