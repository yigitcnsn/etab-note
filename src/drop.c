#define _GNU_SOURCE
#include "drop.h"
#include "storage.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_DROP_BYTES (12 * 1024 * 1024) /* 12 MB max image */

static uint32_t g_img_seq = 1;

static const char INDEX_HTML[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>E-Tab Drop</title><style>"
    "body{font-family:-apple-system,sans-serif;max-width:520px;margin:24px auto;padding:0 16px;"
    "background:#0d1117;color:#e6edf3}"
    "h1{font-size:22px;color:#58a6ff;margin-bottom:6px}"
    "p{color:#8b949e;font-size:14px}"
    ".box{border:2px dashed #30363d;border-radius:12px;padding:20px;text-align:center;margin:14px 0;background:#161b22}"
    "textarea{width:100%;height:90px;box-sizing:border-box;background:#0d1117;color:#e6edf3;"
    "border:1px solid #30363d;border-radius:8px;padding:10px;font-size:15px}"
    "button,input[type=file]{margin-top:10px;font-size:15px}"
    "button{background:#58a6ff;color:#0d1117;border:0;border-radius:8px;padding:10px 18px;font-weight:600;cursor:pointer}"
    "#st{margin-top:12px;font-size:14px;color:#3fb950;min-height:20px}"
    "</style></head><body>"
    "<h1>E-Tab 5 Drop</h1>"
    "<p>Paste an image (Cmd/Ctrl+V), pick a photo, or send text straight to your current note.</p>"
    "<div class=box>"
    "<input id=f type=file accept='image/*'><br>"
    "<button onclick='sendFile()'>Send Image</button>"
    "</div>"
    "<div class=box>"
    "<textarea id=t placeholder='Paste or type text here...'></textarea><br>"
    "<button onclick='sendText()'>Send Text</button>"
    "</div>"
    "<div id=st></div>"
    "<script>"
    "const st=document.getElementById('st');"
    "async function postBlob(b,ct){st.textContent='Sending...';"
    "const r=await fetch('/drop',{method:'POST',headers:{'Content-Type':ct},body:b});"
    "st.textContent=r.ok?'Dropped onto tablet!':'Error: '+r.status;}"
    "function sendFile(){const f=document.getElementById('f').files[0];if(f)postBlob(f,f.type||'application/octet-stream');}"
    "function sendText(){const v=document.getElementById('t').value.trim();if(v)postBlob(v,'text/plain');}"
    "window.addEventListener('paste',e=>{for(const it of e.clipboardData.items){"
    "if(it.type.startsWith('image/')){e.preventDefault();postBlob(it.getAsFile(),it.type);return;}}});"
    "</script></body></html>";

int drop_open(DropServer *ds) {
    memset(ds, 0, sizeof(*ds));
    ds->listen_fd = -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DROP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ds->listen_fd = fd;
    return 0;
}

void drop_close(DropServer *ds) {
    if (ds->listen_fd >= 0) close(ds->listen_fd);
    ds->listen_fd = -1;
}

static void send_http(int fd, int code, const char *status, const char *ct,
                      const void *body, size_t len) {
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "Access-Control-Allow-Origin: *\r\n\r\n",
                     code, status, ct, len);
    if (n > 0) (void)write(fd, hdr, (size_t)n);
    if (body && len > 0) (void)write(fd, body, len);
}

static const uint8_t *find_bytes(const uint8_t *hay, size_t hlen,
                                 const uint8_t *ndl, size_t nlen) {
    if (nlen == 0 || nlen > hlen) return NULL;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hay + i, ndl, nlen) == 0) return hay + i;
    }
    return NULL;
}

static const char *detect_image_ext(const uint8_t *buf, size_t len) {
    if (len >= 8 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G')
        return "png";
    if (len >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)
        return "jpg";
    if (len >= 2 && buf[0] == 'B' && buf[1] == 'M')
        return "bmp";
    return NULL;
}

/* Case-insensitive header lookup */
static bool get_header_val(const char *hdrs, const char *key, char *out, size_t out_sz) {
    size_t klen = strlen(key);
    const char *p = hdrs;
    while (*p) {
        const char *eol = strstr(p, "\r\n");
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        if (llen > klen + 1 && strncasecmp(p, key, klen) == 0 && p[klen] == ':') {
            const char *v = p + klen + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t vlen = (size_t)(p + llen - v);
            if (vlen >= out_sz) vlen = out_sz - 1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return true;
        }
        if (!eol) break;
        p = eol + 2;
    }
    return false;
}

/* Extract first payload from multipart/form-data if present */
static bool unwrap_multipart(const char *ct, const uint8_t *body, size_t body_len,
                             const uint8_t **part_out, size_t *part_len_out) {
    const char *bp = strstr(ct, "boundary=");
    if (!bp) return false;
    bp += 9;
    char boundary[128];
    size_t blen = 0;
    if (*bp == '"') bp++;
    while (*bp && *bp != '"' && *bp != ';' && *bp != ' ' && blen + 3 < sizeof(boundary)) {
        boundary[blen++] = *bp++;
    }
    boundary[blen] = '\0';
    if (blen == 0) return false;

    char delim[136];
    snprintf(delim, sizeof(delim), "--%s", boundary);
    size_t dlen = strlen(delim);

    const uint8_t *start = find_bytes(body, body_len, (const uint8_t *)delim, dlen);
    if (!start) return false;
    start += dlen;
    const uint8_t *hdr_end = find_bytes(start, (size_t)(body + body_len - start),
                                        (const uint8_t *)"\r\n\r\n", 4);
    if (!hdr_end) return false;
    const uint8_t *data_start = hdr_end + 4;
    size_t rem = (size_t)(body + body_len - data_start);
    const uint8_t *data_end = find_bytes(data_start, rem, (const uint8_t *)delim, dlen);
    if (!data_end) return false;
    /* Strip trailing \r\n before boundary */
    if (data_end >= data_start + 2 && data_end[-2] == '\r' && data_end[-1] == '\n') {
        data_end -= 2;
    }
    *part_out = data_start;
    *part_len_out = (size_t)(data_end - data_start);
    return true;
}

static bool save_dropped_image(const uint8_t *data, size_t len, const char *ext,
                               char *fname_out, size_t fname_sz) {
    storage_init(IMAGES_DIR);
    snprintf(fname_out, fname_sz, "img_%lu_%u.%s",
             (unsigned long)time(NULL), g_img_seq++, ext);
    char full[512];
    snprintf(full, sizeof(full), "%s/%s", IMAGES_DIR, fname_out);
    FILE *f = fopen(full, "wb");
    if (!f) return false;
    bool ok = (fwrite(data, 1, len, f) == len);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    if (!ok) unlink(full);
    return ok;
}

bool drop_accept_and_handle(DropServer *ds, DropEvent *out_ev) {
    memset(out_ev, 0, sizeof(*out_ev));
    if (ds->listen_fd < 0) return false;

    int cfd = accept(ds->listen_fd, NULL, NULL);
    if (cfd < 0) return false;

    struct timeval tv = { .tv_sec = 4, .tv_usec = 0 };
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Read HTTP headers */
    size_t cap = 16384;
    uint8_t *buf = malloc(cap);
    if (!buf) { close(cfd); return false; }

    size_t total = 0;
    const uint8_t *hdr_end = NULL;
    while (total < 16384) {
        ssize_t n = read(cfd, buf + total, cap - total);
        if (n <= 0) break;
        total += (size_t)n;
        hdr_end = find_bytes(buf, total, (const uint8_t *)"\r\n\r\n", 4);
        if (hdr_end) break;
    }

    if (!hdr_end) {
        free(buf);
        close(cfd);
        return false;
    }

    size_t hdr_len = (size_t)(hdr_end - buf);
    char *hdr_str = malloc(hdr_len + 1);
    if (!hdr_str) { free(buf); close(cfd); return false; }
    memcpy(hdr_str, buf, hdr_len);
    hdr_str[hdr_len] = '\0';

    /* Handle GET / -> serve built-in mobile web drop UI */
    if (strncmp(hdr_str, "GET ", 4) == 0) {
        send_http(cfd, 200, "OK", "text/html; charset=utf-8", INDEX_HTML, sizeof(INDEX_HTML) - 1);
        free(hdr_str);
        free(buf);
        close(cfd);
        return false;
    }

    if (strncmp(hdr_str, "OPTIONS ", 8) == 0) {
        send_http(cfd, 200, "OK", "text/plain", "", 0);
        free(hdr_str);
        free(buf);
        close(cfd);
        return false;
    }

    if (strncmp(hdr_str, "POST ", 5) != 0) {
        send_http(cfd, 405, "Method Not Allowed", "text/plain", "Use POST /drop\n", 15);
        free(hdr_str);
        free(buf);
        close(cfd);
        return false;
    }

    char cl_str[32] = {0}, ct_str[256] = {0};
    size_t content_len = 0;
    if (get_header_val(hdr_str, "Content-Length", cl_str, sizeof(cl_str))) {
        content_len = (size_t)strtoul(cl_str, NULL, 10);
    }
    get_header_val(hdr_str, "Content-Type", ct_str, sizeof(ct_str));

    if (content_len == 0 || content_len > MAX_DROP_BYTES) {
        send_http(cfd, 413, "Payload Too Large", "text/plain", "Bad size\n", 9);
        free(hdr_str);
        free(buf);
        close(cfd);
        return false;
    }

    size_t body_off = hdr_len + 4;
    size_t need_total = body_off + content_len;
    if (need_total > cap) {
        uint8_t *nb = realloc(buf, need_total);
        if (!nb) {
            send_http(cfd, 500, "Error", "text/plain", "OOM\n", 4);
            free(hdr_str);
            free(buf);
            close(cfd);
            return false;
        }
        buf = nb;
        cap = need_total;
    }

    while (total < need_total) {
        ssize_t n = read(cfd, buf + total, need_total - total);
        if (n <= 0) break;
        total += (size_t)n;
    }

    const uint8_t *payload = buf + body_off;
    size_t payload_len = (total > body_off) ? (total - body_off) : 0;

    if (strncasecmp(ct_str, "multipart/form-data", 19) == 0) {
        const uint8_t *part = NULL;
        size_t part_len = 0;
        if (unwrap_multipart(ct_str, payload, payload_len, &part, &part_len)) {
            payload = part;
            payload_len = part_len;
        }
    }

    bool handled = false;
    const char *ext = detect_image_ext(payload, payload_len);
    if (ext) {
        if (save_dropped_image(payload, payload_len, ext,
                               out_ev->image_filename, sizeof(out_ev->image_filename))) {
            out_ev->kind = DROP_IMAGE;
            handled = true;
            send_http(cfd, 200, "OK", "application/json", "{\"ok\":true,\"kind\":\"image\"}\n", 28);
        } else {
            send_http(cfd, 500, "Error", "application/json", "{\"ok\":false}\n", 13);
        }
    } else if (payload_len > 0) {
        size_t copy = payload_len < sizeof(out_ev->text) - 1 ? payload_len : sizeof(out_ev->text) - 1;
        size_t j = 0;
        for (size_t i = 0; i < copy; i++) {
            unsigned char c = payload[i];
            if (c == '\r') continue;
            if (c == '\n' || (c >= 32 && c < 127)) out_ev->text[j++] = (char)c;
        }
        while (j > 0 && (out_ev->text[j - 1] == ' ' || out_ev->text[j - 1] == '\n')) j--;
        out_ev->text[j] = '\0';
        if (j > 0) {
            out_ev->kind = DROP_TEXT;
            handled = true;
            send_http(cfd, 200, "OK", "application/json", "{\"ok\":true,\"kind\":\"text\"}\n", 27);
        } else {
            send_http(cfd, 400, "Bad Request", "application/json", "{\"ok\":false}\n", 13);
        }
    } else {
        send_http(cfd, 400, "Bad Request", "application/json", "{\"ok\":false}\n", 13);
    }

    free(hdr_str);
    free(buf);
    close(cfd);
    return handled;
}
