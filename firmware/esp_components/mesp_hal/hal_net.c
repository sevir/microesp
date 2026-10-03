/* MicroESP HAL — Wake-on-LAN magic packet broadcast over the Wi-Fi station interface. */
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "mesp_hal.h"

static const char *TAG = "mhal_net";

static esp_netif_t *sta_netif(void) { return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"); }

bool mhal_ip(char out[16])
{
    esp_netif_ip_info_t ip;
    esp_netif_t *n = sta_netif();
    if (!n || esp_netif_get_ip_info(n, &ip) != ESP_OK || ip.ip.addr == 0) return false;
    esp_ip4addr_ntoa(&ip.ip, out, 16);
    return true;
}

int mhal_wol_send(const uint8_t *pkt, size_t len)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        ESP_LOGE(TAG, "socket failed");
        return -1;
    }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    uint32_t dst[2] = {htonl(INADDR_BROADCAST), 0};
    esp_netif_ip_info_t ip;
    esp_netif_t *n = sta_netif();
    if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr) dst[1] = (ip.ip.addr & ip.netmask.addr) | ~ip.netmask.addr;
    int sent = 0;
    const uint16_t ports[2] = {9, 7};
    for (int d = 0; d < 2; d++) {
        if (!dst[d]) continue;
        for (int p = 0; p < 2; p++) {
            struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(ports[p]), .sin_addr.s_addr = dst[d]};
            if (sendto(s, pkt, len, 0, (struct sockaddr *)&to, sizeof(to)) == (int)len) sent++;
        }
    }
    close(s);
    ESP_LOGI(TAG, "WOL: %d datagrams sent", sent);
    return sent ? 0 : -1;
}
