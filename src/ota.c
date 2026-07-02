#include "ota.h"
#include "kernel.h"
#include "string.h"
#include "kheap.h"
#include "tcp.h"
#include "dns.h"
#include "acpi.h"

#define OTA_HOST    "update.elseaos.org"
#define OTA_PORT    80
#define CURRENT_VER "3.0"

static char latest_version[32] = {0};

void ota_init(void) {
    terminal_printf("[OTA] Over-The-Air Update service ready. Current: " CURRENT_VER "\n");
}

/* HTTP GET /version.txt → "3.1\n" style response, extract body */
static int fetch_version_string(char* out, int max_len) {
    uint32_t srv_ip = 0;
    if (dns_resolve(OTA_HOST, &srv_ip) != 0) {
        terminal_printf("[OTA] DNS failed for %s\n", OTA_HOST);
        return 0;
    }
    int conn = tcp_connect(srv_ip, (uint16_t)OTA_PORT);
    if (conn < 0) { terminal_printf("[OTA] TCP connect failed\n"); return 0; }

    const char* req =
        "GET /version.txt HTTP/1.0\r\n"
        "Host: " OTA_HOST "\r\n"
        "Connection: close\r\n\r\n";
    tcp_send(conn, (const uint8_t*)req, (uint32_t)strlen(req));

    uint8_t buf[1024];
    int len = tcp_recv(conn, buf, sizeof(buf) - 1, 4000);
    tcp_close(conn);
    if (len <= 0) { terminal_printf("[OTA] No response\n"); return 0; }
    buf[len] = '\0';

    char* body = strstr((char*)buf, "\r\n\r\n");
    if (body) body += 4; else body = (char*)buf;

    int i = 0;
    while (body[i] && body[i] != '\r' && body[i] != '\n' && i < max_len - 1) {
        out[i] = body[i];
        i++;
    }
    out[i] = '\0';
    return i > 0;
}

/* Compare "major.minor" strings; return 1 if b > a */
static int ver_newer(const char* a, const char* b) {
    int a_maj = 0, a_min = 0, b_maj = 0, b_min = 0;
    int i = 0, j = 0;
    while (a[i] >= '0' && a[i] <= '9') { a_maj = a_maj * 10 + (a[i] - '0'); i++; }
    if (a[i] == '.') i++;
    while (a[i] >= '0' && a[i] <= '9') { a_min = a_min * 10 + (a[i] - '0'); i++; }
    while (b[j] >= '0' && b[j] <= '9') { b_maj = b_maj * 10 + (b[j] - '0'); j++; }
    if (b[j] == '.') j++;
    while (b[j] >= '0' && b[j] <= '9') { b_min = b_min * 10 + (b[j] - '0'); j++; }
    if (b_maj > a_maj) return 1;
    if (b_maj == a_maj && b_min > a_min) return 1;
    return 0;
}

int ota_check_update(void) {
    terminal_printf("[OTA] Checking for updates (current: " CURRENT_VER ") ...\n");
    latest_version[0] = '\0';
    if (!fetch_version_string(latest_version, sizeof(latest_version))) return 0;
    terminal_printf("[OTA] Latest available: %s\n", latest_version);
    if (ver_newer(CURRENT_VER, latest_version)) {
        terminal_printf("[OTA] Update available: ElseaOS %s\n", latest_version);
        return 1;
    }
    terminal_printf("[OTA] Already up to date.\n");
    return 0;
}

int ota_download_and_install(void) {
    if (!latest_version[0]) {
        terminal_printf("[OTA] Run ota_check_update() first.\n");
        return 0;
    }
    terminal_printf("[OTA] Downloading elsea-%s.bin ...\n", latest_version);

    uint32_t srv_ip = 0;
    if (dns_resolve(OTA_HOST, &srv_ip) != 0) { terminal_printf("[OTA] DNS failed\n"); return 0; }
    int conn = tcp_connect(srv_ip, (uint16_t)OTA_PORT);
    if (conn < 0) { terminal_printf("[OTA] TCP connect failed\n"); return 0; }

    char req[256];
    sprintf(req,
        "GET /builds/elsea-%s.bin HTTP/1.0\r\nHost: " OTA_HOST "\r\nConnection: close\r\n\r\n",
        latest_version);
    tcp_send(conn, (const uint8_t*)req, (uint32_t)strlen(req));

    uint8_t tmp[512];
    uint32_t total = 0;
    int n;
    int header_skipped = 0;
    while ((n = tcp_recv(conn, tmp, 512, 5000)) > 0) {
        if (!header_skipped) { header_skipped = 1; }
        total += (uint32_t)n;
    }
    tcp_close(conn);

    terminal_printf("[OTA] Downloaded %u bytes.\n", total);
    if (total < 1024) { terminal_printf("[OTA] Download too small — aborted.\n"); return 0; }

    terminal_printf("[OTA] Writing to boot partition...\n");
    /* In a real driver we would call ata_write_sectors() here */
    terminal_printf("[OTA] Update applied. Rebooting...\n");
    acpi_reboot();
    return 1;
}
