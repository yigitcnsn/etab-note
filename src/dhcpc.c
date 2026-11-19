#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC       0x63825363u

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5

typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
} DhcpPacket;

static volatile sig_atomic_t g_quit = 0;
static volatile sig_atomic_t g_renew = 0;

static void on_term(int sig) { (void)sig; g_quit = 1; }
static void on_usr1(int sig) { (void)sig; g_renew = 1; }

static bool iface_get_flags(int s, const char *ifname, short *flags_out) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
    if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) return false;
    *flags_out = ifr.ifr_flags;
    return true;
}

static bool iface_get_mac(int s, const char *ifname, uint8_t mac[6]) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
    if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) return false;
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return true;
}

static uint32_t iface_get_ipv4(int s, const char *ifname) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
    ifr.ifr_addr.sa_family = AF_INET;
    if (ioctl(s, SIOCGIFADDR, &ifr) < 0) return 0;
    return ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
}

static bool iface_configure(const char *ifname, uint32_t ip, uint32_t mask, uint32_t gw) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);

    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = ip;
    if (ioctl(s, SIOCSIFADDR, &ifr) < 0) {
        perror("SIOCSIFADDR");
        close(s);
        return false;
    }

    if (mask == 0) mask = htonl(0xFFFFFF00u);
    sin->sin_addr.s_addr = mask;
    if (ioctl(s, SIOCSIFNETMASK, &ifr) < 0) {
        perror("SIOCSIFNETMASK");
    }

    if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
        ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
        ioctl(s, SIOCSIFFLAGS, &ifr);
    }

    if (gw != 0) {
        struct rtentry rt;
        memset(&rt, 0, sizeof(rt));
        ((struct sockaddr_in *)&rt.rt_dst)->sin_family = AF_INET;
        ((struct sockaddr_in *)&rt.rt_genmask)->sin_family = AF_INET;
        ((struct sockaddr_in *)&rt.rt_gateway)->sin_family = AF_INET;
        ((struct sockaddr_in *)&rt.rt_gateway)->sin_addr.s_addr = gw;
        rt.rt_flags = RTF_UP | RTF_GATEWAY;
        rt.rt_dev = (char *)ifname;
        /* remove stale default route first (ignore error if none) */
        ioctl(s, SIOCDELRT, &rt);
        if (ioctl(s, SIOCADDRT, &rt) < 0 && errno != EEXIST) {
            perror("SIOCADDRT");
        }
    }

    close(s);
    return true;
}

static int parse_options(const DhcpPacket *pkt, size_t pkt_len,
                         uint8_t *msg_type, uint32_t *server_id,
                         uint32_t *mask, uint32_t *gw, uint32_t *lease) {
    if (pkt_len < 240 || ntohl(pkt->magic) != DHCP_MAGIC) return -1;
    size_t opt_len = pkt_len - 240;
    size_t i = 0;
    *msg_type = 0;
    while (i < opt_len) {
        uint8_t code = pkt->options[i++];
        if (code == 0) continue;
        if (code == 255) break;
        if (i >= opt_len) break;
        uint8_t len = pkt->options[i++];
        if (i + len > opt_len) break;
        const uint8_t *val = &pkt->options[i];
        if (code == 53 && len >= 1) *msg_type = val[0];
        else if (code == 54 && len >= 4) memcpy(server_id, val, 4);
        else if (code == 1 && len >= 4) memcpy(mask, val, 4);
        else if (code == 3 && len >= 4) memcpy(gw, val, 4);
        else if (code == 51 && len >= 4) {
            uint32_t lt;
            memcpy(&lt, val, 4);
            *lease = ntohl(lt);
        }
        i += len;
    }
    return 0;
}

static bool do_dhcp_lease(const char *ifname, uint32_t *lease_out) {
    int ctrl = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctrl < 0) return false;
    uint8_t mac[6];
    bool ok = iface_get_mac(ctrl, ifname, mac);
    close(ctrl);
    if (!ok) return false;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("socket");
        return false;
    }

    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, ifname, (socklen_t)strlen(ifname));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(DHCP_CLIENT_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("bind :68");
        close(sock);
        return false;
    }

    struct sockaddr_in bcast;
    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(DHCP_SERVER_PORT);
    bcast.sin_addr.s_addr = INADDR_BROADCAST;

    uint32_t xid = (uint32_t)time(NULL) ^ ((uint32_t)mac[4] << 8) ^ mac[5] ^ (uint32_t)getpid();

    /* 1. DHCPDISCOVER */
    DhcpPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = htonl(xid);
    pkt.flags = htons(0x8000u); /* Broadcast reply so we receive without IP */
    memcpy(pkt.chaddr, mac, 6);
    pkt.magic = htonl(DHCP_MAGIC);
    int o = 0;
    pkt.options[o++] = 53; pkt.options[o++] = 1; pkt.options[o++] = DHCPDISCOVER;
    pkt.options[o++] = 55; pkt.options[o++] = 4;
    pkt.options[o++] = 1;  pkt.options[o++] = 3; pkt.options[o++] = 6; pkt.options[o++] = 51;
    pkt.options[o++] = 255;

    if (sendto(sock, &pkt, 240 + (size_t)o, 0, (struct sockaddr *)&bcast, sizeof(bcast)) < 0) {
        perror("sendto DISCOVER");
        close(sock);
        return false;
    }

    /* Wait for DHCPOFFER */
    uint32_t offered_ip = 0, server_id = 0, mask = 0, gw = 0, lease = 3600;
    struct pollfd pfd = { .fd = sock, .events = POLLIN };
    time_t deadline = time(NULL) + 4;
    while (time(NULL) <= deadline) {
        if (poll(&pfd, 1, 500) <= 0) continue;
        DhcpPacket rx;
        ssize_t n = recvfrom(sock, &rx, sizeof(rx), 0, NULL, NULL);
        if (n < 240 || ntohl(rx.xid) != xid || rx.op != 2) continue;
        uint8_t mtype = 0;
        parse_options(&rx, (size_t)n, &mtype, &server_id, &mask, &gw, &lease);
        if (mtype == DHCPOFFER && rx.yiaddr != 0) {
            offered_ip = rx.yiaddr;
            break;
        }
    }
    if (offered_ip == 0) {
        close(sock);
        return false;
    }

    /* 2. DHCPREQUEST */
    memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = htonl(xid);
    pkt.flags = htons(0x8000u);
    memcpy(pkt.chaddr, mac, 6);
    pkt.magic = htonl(DHCP_MAGIC);
    o = 0;
    pkt.options[o++] = 53; pkt.options[o++] = 1; pkt.options[o++] = DHCPREQUEST;
    pkt.options[o++] = 50; pkt.options[o++] = 4; memcpy(&pkt.options[o], &offered_ip, 4); o += 4;
    if (server_id != 0) {
        pkt.options[o++] = 54; pkt.options[o++] = 4; memcpy(&pkt.options[o], &server_id, 4); o += 4;
    }
    pkt.options[o++] = 55; pkt.options[o++] = 4;
    pkt.options[o++] = 1;  pkt.options[o++] = 3; pkt.options[o++] = 6; pkt.options[o++] = 51;
    pkt.options[o++] = 255;

    if (sendto(sock, &pkt, 240 + (size_t)o, 0, (struct sockaddr *)&bcast, sizeof(bcast)) < 0) {
        perror("sendto REQUEST");
        close(sock);
        return false;
    }

    /* Wait for DHCPACK */
    bool got_ack = false;
    deadline = time(NULL) + 4;
    while (time(NULL) <= deadline) {
        if (poll(&pfd, 1, 500) <= 0) continue;
        DhcpPacket rx;
        ssize_t n = recvfrom(sock, &rx, sizeof(rx), 0, NULL, NULL);
        if (n < 240 || ntohl(rx.xid) != xid || rx.op != 2) continue;
        uint8_t mtype = 0;
        parse_options(&rx, (size_t)n, &mtype, &server_id, &mask, &gw, &lease);
        if (mtype == DHCPACK && rx.yiaddr != 0) {
            offered_ip = rx.yiaddr;
            got_ack = true;
            break;
        }
    }
    close(sock);

    if (!got_ack) return false;
    if (!iface_configure(ifname, offered_ip, mask, gw)) return false;

    struct in_addr ia = { .s_addr = offered_ip };
    struct in_addr ga = { .s_addr = gw };
    char ip_s[32], gw_s[32];
    snprintf(ip_s, sizeof(ip_s), "%s", inet_ntoa(ia));
    snprintf(gw_s, sizeof(gw_s), "%s", inet_ntoa(ga));
    fprintf(stderr, "noteos-dhcp: %s leased %s gw %s lease=%us\n", ifname, ip_s, gw_s, lease);
    if (lease_out) *lease_out = lease;
    return true;
}

int main(int argc, char **argv) {
    const char *ifname = (argc >= 2) ? argv[1] : "wlan0";
    bool once = (argc >= 3 && strcmp(argv[2], "--once") == 0);

    signal(SIGINT, on_term);
    signal(SIGTERM, on_term);
    signal(SIGUSR1, on_usr1);

    if (once) {
        for (int tries = 0; tries < 5; tries++) {
            uint32_t lease = 3600;
            if (do_dhcp_lease(ifname, &lease)) return 0;
            sleep(1);
        }
        return 1;
    }

    time_t next_renew = 0;
    while (!g_quit) {
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        short flags = 0;
        uint32_t cur_ip = 0;
        bool has_if = (s >= 0 && iface_get_flags(s, ifname, &flags));
        if (has_if) cur_ip = iface_get_ipv4(s, ifname);
        if (s >= 0) close(s);

        bool link_up = has_if && (flags & IFF_UP) && (flags & IFF_RUNNING);
        time_t now = time(NULL);

        if (link_up && (cur_ip == 0 || g_renew || (next_renew > 0 && now >= next_renew))) {
            g_renew = 0;
            uint32_t lease = 3600;
            if (do_dhcp_lease(ifname, &lease)) {
                if (lease < 120) lease = 120;
                next_renew = time(NULL) + (time_t)(lease / 2);
            } else {
                sleep(2);
                continue;
            }
        }
        sleep(2);
    }
    return 0;
}
