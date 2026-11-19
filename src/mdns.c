#define _GNU_SOURCE
#include "mdns.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define MDNS_GROUP "224.0.0.251"
#define MDNS_PORT  5353

static uint32_t get_wlan0_ipv4(void) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "wlan0");
    ifr.ifr_addr.sa_family = AF_INET;
    uint32_t ip = 0;
    if (ioctl(s, SIOCGIFADDR, &ifr) == 0) {
        ip = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
    }
    close(s);
    return ip;
}

static size_t build_a_response(uint8_t *buf, uint16_t txid, uint32_t ipv4_net) {
    /* DNS header (12 bytes): QR=1, AA=1, ANCOUNT=1 */
    memset(buf, 0, 12);
    buf[0] = (uint8_t)(txid >> 8);
    buf[1] = (uint8_t)(txid & 0xFF);
    buf[2] = 0x84; /* QR=1, AA=1 */
    buf[3] = 0x00;
    buf[7] = 0x01; /* ANCOUNT = 1 */

    size_t p = 12;
    /* Name: etab.local */
    static const uint8_t qname[] = {
        4, 'e', 't', 'a', 'b',
        5, 'l', 'o', 'c', 'a', 'l',
        0
    };
    memcpy(buf + p, qname, sizeof(qname));
    p += sizeof(qname);

    /* TYPE = A (1), CLASS = IN | cache-flush (0x8001) */
    buf[p++] = 0x00; buf[p++] = 0x01;
    buf[p++] = 0x80; buf[p++] = 0x01;
    /* TTL = 120 seconds */
    buf[p++] = 0x00; buf[p++] = 0x00; buf[p++] = 0x00; buf[p++] = 120;
    /* RDLENGTH = 4 */
    buf[p++] = 0x00; buf[p++] = 0x04;
    /* RDATA = IPv4 */
    memcpy(buf + p, &ipv4_net, 4);
    p += 4;
    return p;
}

static bool label_eq_ci(const uint8_t *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != (unsigned char)b[i]) return false;
    }
    return true;
}

/* Parse DNS name at offset pos (supporting compression pointers) and check if == "etab.local" */
static bool parse_and_match_etab_local(const uint8_t *pkt, size_t pkt_len, size_t *pos_io) {
    size_t pos = *pos_io;
    size_t end_pos = 0;
    bool jumped = false;
    int label_idx = 0;
    bool match = true;
    int guard = 0;

    while (pos < pkt_len && guard++ < 16) {
        uint8_t len = pkt[pos];
        if (len == 0) {
            pos++;
            if (!jumped) end_pos = pos;
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= pkt_len) return false;
            uint16_t off = (uint16_t)(((len & 0x3F) << 8) | pkt[pos + 1]);
            if (!jumped) end_pos = pos + 2;
            jumped = true;
            pos = off;
            continue;
        }
        pos++;
        if (pos + len > pkt_len) return false;
        if (label_idx == 0) {
            if (len != 4 || !label_eq_ci(pkt + pos, "etab", 4)) match = false;
        } else if (label_idx == 1) {
            if (len != 5 || !label_eq_ci(pkt + pos, "local", 5)) match = false;
        } else {
            match = false;
        }
        label_idx++;
        pos += len;
    }

    if (end_pos == 0) return false;
    *pos_io = end_pos;
    return match && (label_idx == 2);
}

int mdns_open(MdnsState *ms) {
    memset(ms, 0, sizeof(*ms));
    ms->fd = -1;

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MDNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    unsigned char ttl = 255;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    unsigned char loop = 0;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ms->fd = fd;
    mdns_announce(ms);
    return 0;
}

void mdns_close(MdnsState *ms) {
    if (ms->fd >= 0) close(ms->fd);
    ms->fd = -1;
}

void mdns_announce(MdnsState *ms) {
    if (ms->fd < 0) return;
    uint32_t ip = get_wlan0_ipv4();
    if (ip == 0) return;

    /* Ensure multicast membership is active once wlan0 has an IP */
    if (ip != ms->last_ip) {
        struct ip_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = inet_addr(MDNS_GROUP);
        mreq.imr_interface.s_addr = ip;
        setsockopt(ms->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
        setsockopt(ms->fd, IPPROTO_IP, IP_MULTICAST_IF, &ip, sizeof(ip));
        ms->last_ip = ip;
    }

    uint8_t resp[64];
    size_t rlen = build_a_response(resp, 0, ip);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(MDNS_PORT);
    dst.sin_addr.s_addr = inet_addr(MDNS_GROUP);
    sendto(ms->fd, resp, rlen, 0, (struct sockaddr *)&dst, sizeof(dst));
}

void mdns_handle_readable(MdnsState *ms) {
    if (ms->fd < 0) return;
    uint8_t pkt[1500];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);

    for (;;) {
        ssize_t n = recvfrom(ms->fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&src, &srclen);
        if (n < 12) break;

        uint16_t txid    = (uint16_t)((pkt[0] << 8) | pkt[1]);
        uint16_t flags   = (uint16_t)((pkt[2] << 8) | pkt[3]);
        uint16_t qdcount = (uint16_t)((pkt[4] << 8) | pkt[5]);
        if ((flags & 0x8000u) != 0 || qdcount == 0) continue; /* ignore responses */

        size_t pos = 12;
        bool want_reply = false;
        bool unicast = (ntohs(src.sin_port) != MDNS_PORT);

        for (uint16_t q = 0; q < qdcount && pos < (size_t)n; q++) {
            bool matched = parse_and_match_etab_local(pkt, (size_t)n, &pos);
            if (pos + 4 > (size_t)n) break;
            uint16_t qtype  = (uint16_t)((pkt[pos] << 8) | pkt[pos + 1]);
            uint16_t qclass = (uint16_t)((pkt[pos + 2] << 8) | pkt[pos + 3]);
            pos += 4;
            if (matched && (qtype == 1 || qtype == 255)) {
                want_reply = true;
                if (qclass & 0x8000u) unicast = true;
            }
        }

        if (!want_reply) continue;
        uint32_t ip = get_wlan0_ipv4();
        if (ip == 0) continue;

        uint8_t resp[64];
        size_t rlen = build_a_response(resp, unicast ? txid : 0, ip);

        struct sockaddr_in mcast;
        memset(&mcast, 0, sizeof(mcast));
        mcast.sin_family = AF_INET;
        mcast.sin_port = htons(MDNS_PORT);
        mcast.sin_addr.s_addr = inet_addr(MDNS_GROUP);
        sendto(ms->fd, resp, rlen, 0, (struct sockaddr *)&mcast, sizeof(mcast));

        if (unicast) {
            sendto(ms->fd, resp, rlen, 0, (struct sockaddr *)&src, srclen);
        }
    }
}
