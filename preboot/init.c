#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <linux/reboot.h>
#include <linux/input.h>

#define LBL 300
#define MAXENT 2048

static int rows = 73, cols = 67;
static int ifd[16], ifn = 0;
static int xmax = 4095, ymax = 4095;
static int ptx = -1, pty = -1, tap_row = 0;
static char iname[16][64];
static int touchdev = -1;

enum { A_UP = 1, A_DOWN, A_SEL, A_TAP };

/* ---------- output ---------- */
static char fbuf[131072];
static size_t fl;

static void fstart(void) { fl = 0; fl += snprintf(fbuf + fl, sizeof fbuf - fl, "\033[?25l\033[2J\033[H"); }
static void fp(const char *fmt, ...) {
    va_list ap;
    if (fl >= sizeof fbuf - 1) return;
    va_start(ap, fmt);
    int n = vsnprintf(fbuf + fl, sizeof fbuf - fl, fmt, ap);
    va_end(ap);
    if (n > 0) fl += (size_t)n;
    if (fl >= sizeof fbuf) fl = sizeof fbuf - 1;
}
static void fflushscreen(void) {
    size_t off = 0;
    while (off < fl) {
        ssize_t w = write(1, fbuf + off, fl - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
}
static void fline(int inv, const char *s) {
    if (inv) fp("\033[7m");
    fp("%.*s", cols - 1, s);
    if (inv) fp("\033[0m");
    fp("\n");
}

/* ---------- input ---------- */
static int next_action(void) {
    struct pollfd pf[16];
    for (;;) {
        for (int i = 0; i < ifn; i++) { pf[i].fd = ifd[i]; pf[i].events = POLLIN; pf[i].revents = 0; }
        if (poll(pf, ifn, -1) < 0) continue;
        for (int i = 0; i < ifn; i++) {
            if (!(pf[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read(ifd[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
                int tapped = 0;
                if (ev.type == EV_KEY) {
                    if (ev.value == 1) {
                        if (ev.code == KEY_VOLUMEUP) return A_UP;
                        if (ev.code == KEY_VOLUMEDOWN) return A_DOWN;
                        if (ev.code == KEY_POWER) return A_SEL;
                    }
                    if (ev.code == BTN_TOUCH && ev.value == 0 && pty >= 0) tapped = 1;
                } else if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) ptx = ev.value;
                    else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) pty = ev.value;
                    else if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1 && pty >= 0) tapped = 1;
                }
                if (tapped) {
                    tap_row = (int)((long)pty * rows / ((long)ymax + 1));
                    if (tap_row < 0) tap_row = 0;
                    if (tap_row >= rows) tap_row = rows - 1;
                    ptx = pty = -1;
                    return A_TAP;
                }
            }
        }
    }
}

/* ---------- list picker ---------- */
static int pick(const char *title, const char *sub, char (*lab)[LBL], int n, int *sel_io) {
    int sel = *sel_io, top = 0;
    int vis = rows - 3;
    if (vis < 3) vis = 3;
    if (sel >= n) sel = n - 1;
    if (sel < 0) sel = 0;
    for (;;) {
        if (sel < top) top = sel;
        if (sel >= top + vis) top = sel - vis + 1;
        fstart();
        fline(0, title);
        fline(0, sub);
        for (int i = top; i < n && i < top + vis; i++) fline(i == sel, lab[i]);
        fp("\033[%d;1H", rows);
        fp("%.*s", cols - 1, "VOL UP/DOWN move | POWER open | tap row | tap top=back");
        fflushscreen();
        int a = next_action();
        if (a == A_UP) { if (sel > 0) sel--; }
        else if (a == A_DOWN) { if (sel < n - 1) sel++; }
        else if (a == A_SEL) { *sel_io = sel; return sel; }
        else if (a == A_TAP) {
            if (tap_row == 0) return -1;
            int idx = top + (tap_row - 2);
            if (tap_row >= 2 && idx >= 0 && idx < n) { *sel_io = idx; return idx; }
        }
    }
}

/* ---------- text viewer ---------- */
static void view_text(const char *title, const char *raw, size_t n) {
    char *t = malloc(n + 1);
    size_t *ls = malloc((n + 2) * sizeof *ls), *ll = malloc((n + 2) * sizeof *ll);
    if (!t || !ls || !ll) { free(t); free(ls); free(ll); return; }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)raw[i];
        t[i] = (c == '\n') ? '\n' : (c == '\t') ? ' ' : (c >= 32 && c < 127) ? (char)c : '.';
    }
    t[n] = 0;
    size_t nl = 0, start = 0;
    int w = cols - 1;
    for (size_t i = 0; i <= n; i++) {
        int endl = (i == n) || t[i] == '\n' || (int)(i - start) >= w;
        if (endl) {
            if (i < n || i > start) { ls[nl] = start; ll[nl] = i - start; nl++; }
            start = (i < n && t[i] == '\n') ? i + 1 : i;
        }
    }
    int vis = rows - 3;
    if (vis < 3) vis = 3;
    long top = 0;
    for (;;) {
        fstart();
        fline(0, title);
        fline(0, "");
        for (long i = top; i < (long)nl && i < top + vis; i++) {
            fp("%.*s\n", (int)ll[i], t + ls[i]);
        }
        fp("\033[%d;1H", rows);
        char foot[128];
        snprintf(foot, sizeof foot, "line %ld/%zu | VOL DOWN next | VOL UP prev | POWER back", top + 1, nl);
        fp("%.*s", cols - 1, foot);
        fflushscreen();
        int a = next_action();
        if (a == A_DOWN) { if (top + vis < (long)nl) top += vis; }
        else if (a == A_UP) { top -= vis; if (top < 0) top = 0; }
        else if (a == A_SEL) break;
        else if (a == A_TAP) {
            if (tap_row == 0) break;
            if (tap_row < rows / 3) { top -= vis; if (top < 0) top = 0; }
            else if (top + vis < (long)nl) top += vis;
        }
    }
    free(t); free(ls); free(ll);
}

static void view_file(const char *path) {
    char *buf = malloc(65537);
    if (!buf) return;
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    ssize_t n = -1;
    if (fd >= 0) { n = read(fd, buf, 65536); close(fd); }
    if (n < 0) {
        char m[300];
        snprintf(m, sizeof m, "(cannot read: %s)", strerror(errno));
        view_text(path, m, strlen(m));
    } else if (n == 0) {
        view_text(path, "(empty)", 7);
    } else {
        view_text(path, buf, (size_t)n);
    }
    free(buf);
}

static void message(const char *a, const char *b) {
    fstart();
    fline(0, a);
    fline(0, "");
    fline(0, b);
    fp("\033[%d;1H", rows);
    fp("%.*s", cols - 1, "POWER or tap to continue");
    fflushscreen();
    next_action();
}

static void message_timed(const char *a, const char *b, int secs) {
    fstart();
    fline(0, a);
    fline(0, "");
    fline(0, b);
    fflushscreen();
    sleep((unsigned)secs);
}

/* ---------- small file helpers ---------- */
static int read_small(const char *path, char *out, size_t n) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    ssize_t r = read(fd, out, n - 1);
    close(fd);
    if (r < 0) return -1;
    out[r] = 0;
    while (r > 0 && (out[r - 1] == '\n' || out[r - 1] == ' ')) out[--r] = 0;
    return 0;
}

static void battery_line(char *out, size_t n) {
    DIR *d = opendir("/sys/class/power_supply");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            char p[300], type[64], cap[64] = "?", st[64] = "";
            if (de->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", de->d_name);
            if (read_small(p, type, sizeof type) != 0 || strncmp(type, "Battery", 7)) continue;
            snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", de->d_name);
            read_small(p, cap, sizeof cap);
            snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", de->d_name);
            read_small(p, st, sizeof st);
            snprintf(out, n, "battery: %s%% %s", cap, st);
            closedir(d);
            return;
        }
        closedir(d);
    }
    snprintf(out, n, "battery: no driver yet (charger/gauge not supported)");
}

static void append_file(char *buf, size_t cap, size_t *len, const char *title, const char *path, int maxlines) {
    char tmp[4096];
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    *len += (size_t)snprintf(buf + *len, cap - *len, "== %s ==\n", title);
    if (fd < 0) { *len += (size_t)snprintf(buf + *len, cap - *len, "(cannot read %s)\n\n", path); return; }
    ssize_t r = read(fd, tmp, sizeof tmp - 1);
    close(fd);
    if (r < 0) r = 0;
    tmp[r] = 0;
    int lines = 0;
    for (char *p = tmp; *p && lines < maxlines; ) {
        char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        *len += (size_t)snprintf(buf + *len, cap - *len, "%.*s\n", (int)l, p);
        lines++;
        if (!e) break;
        p = e + 1;
    }
    *len += (size_t)snprintf(buf + *len, cap - *len, "\n");
}

/* ---------- file explorer ---------- */
struct ent { char name[256]; unsigned char dir; };
static struct ent ents[MAXENT];
static char lbls[MAXENT][LBL];

static int entcmp(const void *a, const void *b) {
    const struct ent *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcmp(x->name, y->name);
}

static void explore(const char *start) {
    char path[1400];
    snprintf(path, sizeof path, "%s", start);
    for (;;) {
        int n = 0, first;
        strcpy(ents[n].name, "<back>"); ents[n].dir = 1; n++;
        if (strcmp(path, "/") != 0) { strcpy(ents[n].name, ".."); ents[n].dir = 1; n++; }
        first = n;
        DIR *d = opendir(path);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) && n < MAXENT) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
                char full[1400];
                struct stat st;
                snprintf(full, sizeof full, "%s/%s", strcmp(path, "/") ? path : "", de->d_name);
                snprintf(ents[n].name, sizeof ents[n].name, "%s", de->d_name);
                ents[n].dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
                n++;
            }
            closedir(d);
        }
        qsort(ents + first, (size_t)(n - first), sizeof ents[0], entcmp);
        for (int i = 0; i < n; i++) {
            if (!strcmp(ents[i].name, "<back>")) snprintf(lbls[i], LBL, "<- back to menu");
            else if (!strcmp(ents[i].name, "..")) snprintf(lbls[i], LBL, ".. (up one level)");
            else snprintf(lbls[i], LBL, "%s%s", ents[i].name, ents[i].dir ? "/" : "");
        }
        int sel = 0;
        int r = pick(path, "", lbls, n, &sel);
        if (r < 0 || !strcmp(ents[r].name, "<back>")) return;
        if (!strcmp(ents[r].name, "..")) {
            char *slash = strrchr(path, '/');
            if (slash == path) path[1] = 0; else if (slash) *slash = 0;
        } else {
            char full[1400];
            struct stat st;
            snprintf(full, sizeof full, "%s/%s", strcmp(path, "/") ? path : "", ents[r].name);
            if (stat(full, &st) != 0) message(full, "(cannot stat)");
            else if (S_ISDIR(st.st_mode)) snprintf(path, sizeof path, "%s", full);
            else if (S_ISREG(st.st_mode)) view_file(full);
            else message(full, "(special file, not shown)");
        }
    }
}

/* ---------- actions ---------- */
static void show_dmesg(void) {
    int size = klogctl(10, NULL, 0);
    if (size <= 0) size = 1 << 20;
    char *buf = malloc((size_t)size + 1);
    if (!buf) return;
    int n = klogctl(3, buf, size);
    if (n > 0) view_text("kernel log", buf, (size_t)n);
    else message("kernel log", "(klogctl failed)");
    free(buf);
}

static void show_dmesg_filtered(const char *title, const char **kw) {
    int size = klogctl(10, NULL, 0);
    if (size <= 0) size = 1 << 20;
    char *buf = malloc((size_t)size + 1), *out = malloc((size_t)size + 1);
    if (!buf || !out) { free(buf); free(out); return; }
    int n = klogctl(3, buf, size);
    size_t o = 0;
    if (n > 0) {
        buf[n] = 0;
        for (char *p = buf; *p; ) {
            char *e = strchr(p, '\n');
            if (e) *e = 0;
            const char *line = p;
            if (line[0] == '<' && line[1] >= '0' && line[1] <= '9' && line[2] == '>') line += 3;
            int hit = 0;
            for (int i = 0; kw[i]; i++) if (strcasestr(line, kw[i])) { hit = 1; break; }
            if (hit) { size_t l = strlen(line); memcpy(out + o, line, l); o += l; out[o++] = '\n'; }
            if (!e) break;
            p = e + 1;
        }
    }
    if (o == 0) { snprintf(out, (size_t)size, "(no matching lines)"); o = strlen(out); }
    view_text(title, out, o);
    free(buf); free(out);
}

static void sysinfo(void) {
    size_t cap = 32768, len = 0;
    char *buf = malloc(cap);
    if (!buf) return;
    buf[0] = 0;
    append_file(buf, cap, &len, "kernel", "/proc/version", 3);
    append_file(buf, cap, &len, "uptime (seconds, idle)", "/proc/uptime", 2);
    append_file(buf, cap, &len, "cpus online", "/sys/devices/system/cpu/online", 2);
    append_file(buf, cap, &len, "memory", "/proc/meminfo", 6);
    append_file(buf, cap, &len, "command line", "/proc/cmdline", 4);
    append_file(buf, cap, &len, "block devices", "/proc/partitions", 40);
    append_file(buf, cap, &len, "input devices", "/proc/bus/input/devices", 60);
    view_text("system info", buf, len);
    free(buf);
}

static void battery(void) {
    size_t cap = 32768, len = 0;
    char *buf = malloc(cap);
    if (!buf) return;
    buf[0] = 0;
    int found = 0;
    DIR *d = opendir("/sys/class/power_supply");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            found++;
            len += (size_t)snprintf(buf + len, cap - len, "== %s ==\n", de->d_name);
            static const char *f[] = { "type", "status", "present", "online", "capacity", "voltage_now",
                                       "current_now", "temp", "health", "technology", 0 };
            for (int i = 0; f[i]; i++) {
                char p[300], v[128];
                snprintf(p, sizeof p, "/sys/class/power_supply/%s/%s", de->d_name, f[i]);
                if (read_small(p, v, sizeof v) == 0) len += (size_t)snprintf(buf + len, cap - len, "%s: %s\n", f[i], v);
            }
            len += (size_t)snprintf(buf + len, cap - len, "\n");
        }
        closedir(d);
    }
    if (!found) {
        len += (size_t)snprintf(buf + len, cap - len,
            "No power_supply devices.\n\n"
            "The battery gauge and charger are in the PMIC (Samsung S2MU106 on this phone)\n"
            "and no driver for it is built in yet, so the kernel cannot report charge or\n"
            "charging state. As soon as a driver registers, this screen and the line on the\n"
            "main menu fill in by themselves.\n");
    }
    view_text("battery / power", buf, len);
    free(buf);
}

static void touch_test(void) {
    struct pollfd pf[16];
    int x = -1, y = -1, down = 0;
    long events = 0, nabs = 0, nkey = 0;
    for (;;) {
        fstart();
        fline(0, "TOUCH TEST - touch the screen, POWER exits");
        fline(0, "");
        char l[128];
        snprintf(l, sizeof l, "x=%d  y=%d  touching=%d  events=%ld", x, y, down, events);
        fline(0, l);
        snprintf(l, sizeof l, "range: x 0..%d  y 0..%d  (touch dev: %s)", xmax, ymax,
                 touchdev >= 0 ? iname[touchdev] : "none found");
        fline(0, l);
        snprintf(l, sizeof l, "position events: %ld   key/button events: %ld", nabs, nkey);
        fline(0, l);
        fline(0, "");
        fline(0, "input devices:");
        for (int i = 0; i < ifn; i++) { snprintf(l, sizeof l, "  event%d: %s", i, iname[i]); fline(0, l); }
        fflushscreen();
        for (int i = 0; i < ifn; i++) { pf[i].fd = ifd[i]; pf[i].events = POLLIN; pf[i].revents = 0; }
        if (poll(pf, ifn, -1) < 0) continue;
        for (int i = 0; i < ifn; i++) {
            if (!(pf[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read(ifd[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
                events++;
                if (ev.type == EV_KEY) nkey++;
                if (ev.type == EV_ABS) nabs++;
                if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1) return;
                if (ev.type == EV_KEY && ev.code == BTN_TOUCH) down = ev.value;
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) x = ev.value;
                    if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) y = ev.value;
                }
            }
        }
    }
}

static void do_reboot(int cmd, const char *what) {
    sync();
    message_timed(what, "syncing, trying in 2 seconds...", 2);
    reboot(cmd);
    char m[200];
    snprintf(m, sizeof m, "did not work: %s", strerror(errno));
    message(what, m);
}

/* ---------- more diagnostics ---------- */
static void key_test(void) {
    struct pollfd pf[16];
    char lg[12][80];
    int nlog = 0, up = 0, dn = 0;
    for (;;) {
        fstart();
        fline(0, "KEY TEST - hold VOL UP + VOL DOWN together to exit");
        fline(0, "");
        for (int i = 0; i < nlog; i++) fline(0, lg[i]);
        fflushscreen();
        for (int i = 0; i < ifn; i++) { pf[i].fd = ifd[i]; pf[i].events = POLLIN; pf[i].revents = 0; }
        if (poll(pf, ifn, -1) < 0) continue;
        for (int i = 0; i < ifn; i++) {
            if (!(pf[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read(ifd[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
                if (ev.type != EV_KEY) continue;
                const char *nm = ev.code == KEY_VOLUMEUP ? "VOLUME UP" : ev.code == KEY_VOLUMEDOWN ? "VOLUME DOWN" :
                                 ev.code == KEY_POWER ? "POWER" : ev.code == BTN_TOUCH ? "TOUCH" : "other";
                if (nlog == 12) { memmove(lg[0], lg[1], 11 * sizeof lg[0]); nlog = 11; }
                snprintf(lg[nlog++], sizeof lg[0], "%-12s code=%d  %s  (%s)", nm, ev.code,
                         ev.value ? "pressed" : "released", iname[i]);
                if (ev.code == KEY_VOLUMEUP) up = ev.value ? 1 : 0;
                if (ev.code == KEY_VOLUMEDOWN) dn = ev.value ? 1 : 0;
                if (up && dn) return;
            }
        }
    }
}

static void display_test(void) {
    fstart();
    fline(0, "DISPLAY TEST - 8 colour bands, POWER or tap to exit");
    fline(0, "");
    int per = (rows - 4) / 8;
    if (per < 1) per = 1;
    for (int c = 0; c < 8; c++)
        for (int r = 0; r < per; r++) {
            fp("\033[4%dm", c);
            for (int i = 0; i < cols - 1; i++) fp(" ");
            fp("\033[0m\n");
        }
    fflushscreen();
    next_action();
}

static void i2c_list(void) {
    size_t cap = 16384, len = 0;
    char *buf = malloc(cap);
    if (!buf) return;
    buf[0] = 0;
    len += (size_t)snprintf(buf + len, cap - len, "== i2c adapters ==\n");
    DIR *d = opendir("/sys/class/i2c-adapter");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            char p[300], nm[128] = "?";
            if (de->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "/sys/class/i2c-adapter/%s/name", de->d_name);
            read_small(p, nm, sizeof nm);
            len += (size_t)snprintf(buf + len, cap - len, "%s: %s\n", de->d_name, nm);
        }
        closedir(d);
    } else len += (size_t)snprintf(buf + len, cap - len, "(none)\n");
    len += (size_t)snprintf(buf + len, cap - len, "\n== i2c devices (bus-address: name) ==\n");
    d = opendir("/sys/bus/i2c/devices");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            char p[300], nm[128] = "?";
            if (de->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "/sys/bus/i2c/devices/%s/name", de->d_name);
            read_small(p, nm, sizeof nm);
            snprintf(p, sizeof p, "/sys/bus/i2c/devices/%s/driver", de->d_name);
            struct stat st;
            len += (size_t)snprintf(buf + len, cap - len, "%s: %s  [%s]\n", de->d_name, nm,
                                    stat(p, &st) == 0 ? "driver bound" : "no driver bound");
        }
        closedir(d);
    } else len += (size_t)snprintf(buf + len, cap - len, "(none)\n");
    view_text("i2c devices", buf, len);
    free(buf);
}

static void v_input(void) { view_file("/proc/bus/input/devices"); }
static void v_irq(void) { view_file("/proc/interrupts"); }
static void v_clk(void) { view_file("/sys/kernel/debug/clk/clk_summary"); }
static void v_deferred(void) { view_file("/sys/kernel/debug/devices_deferred"); }

static void act_reboot_recovery(void) {
    sync();
    message_timed("Reboot to recovery",
                  "trying in 2 seconds. Needs reboot-mode support, otherwise this is a normal reboot.", 2);
    syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "recovery");
    message("Reboot to recovery", strerror(errno));
}

/* ---------- boot: continue into the Linux rootfs kept on userdata (/data/linux/rootfs on Android) ---------- */
#define ROOT_DIR "/mnt/linux/rootfs"

static int find_part(const char *want, char *dev, size_t n) {
    DIR *d = opendir("/sys/class/block");
    if (!d) return -1;
    struct dirent *de;
    char key[128];
    snprintf(key, sizeof key, "PARTNAME=%s", want);
    size_t kl = strlen(key);
    while ((de = readdir(d))) {
        char up[300], ub[1024];
        if (de->d_name[0] == '.') continue;
        snprintf(up, sizeof up, "/sys/class/block/%s/uevent", de->d_name);
        if (read_small(up, ub, sizeof ub) != 0) continue;
        char *q = strstr(ub, key);
        if (q && (q == ub || q[-1] == '\n') && (q[kl] == 0 || q[kl] == '\n')) {
            snprintf(dev, n, "/dev/%s", de->d_name);
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return -1;
}

static int mount_userdata(int rw, char *devout, size_t dn) {
    char dev[128];
    if (find_part("userdata", dev, sizeof dev) != 0) snprintf(dev, sizeof dev, "/dev/mmcblk0p32");
    mkdir("/mnt", 0755);
    unsigned long fl = MS_NOATIME | (rw ? 0 : MS_RDONLY);
    if (mount(dev, "/mnt", "ext4", fl, NULL) != 0) return -1;
    if (devout) snprintf(devout, dn, "%s", dev);
    return 0;
}

static int rootfs_has_init(void) {
    struct stat st;
    return lstat(ROOT_DIR "/sbin/init", &st) == 0 || lstat(ROOT_DIR "/init", &st) == 0;
}

static int countdown(int secs) {
    struct pollfd pf[16];
    for (int s = secs; s > 0; s--) {
        char l[128];
        fstart();
        fline(0, "A30 mainline - pre-boot environment");
        fline(0, "");
        snprintf(l, sizeof l, "Continuing boot into the Linux rootfs in %d ...", s);
        fline(0, l);
        fline(0, "");
        fline(0, "Press any button or touch the screen for the menu.");
        fflushscreen();
        for (int i = 0; i < ifn; i++) { pf[i].fd = ifd[i]; pf[i].events = POLLIN; pf[i].revents = 0; }
        if (poll(pf, ifn, 1000) > 0) {
            struct input_event ev;
            for (int i = 0; i < ifn; i++) while (read(ifd[i], &ev, sizeof ev) > 0) { }
            return 1;
        }
    }
    return 0;
}

static void continue_boot(void) {
    char dev[128], m[240];
    if (mount_userdata(1, dev, sizeof dev) != 0) {
        snprintf(m, sizeof m, "could not mount userdata read-write: %s", strerror(errno));
        message("Continue boot", m);
        return;
    }
    if (!rootfs_has_init()) {
        message("Continue boot", "no /linux/rootfs/sbin/init on userdata yet - install a rootfs first");
        umount("/mnt");
        return;
    }
    message_timed("Continue boot", "starting the Linux rootfs...", 1);
    mkdir("/newroot", 0755);
    if (mount(ROOT_DIR, "/newroot", NULL, MS_BIND, NULL) != 0) {
        snprintf(m, sizeof m, "bind mount of the rootfs failed: %s", strerror(errno));
        message("Continue boot", m);
        umount("/mnt");
        return;
    }
    mkdir("/newroot/dev", 0755);
    mkdir("/newroot/proc", 0755);
    mkdir("/newroot/sys", 0755);
    mkdir("/newroot/mnt", 0755);
    mount("/dev", "/newroot/dev", NULL, MS_MOVE, NULL);
    mount("/proc", "/newroot/proc", NULL, MS_MOVE, NULL);
    mount("/sys", "/newroot/sys", NULL, MS_MOVE, NULL);
    mount("/mnt", "/newroot/mnt", NULL, MS_MOVE, NULL);
    for (int i = 0; i < ifn; i++) close(ifd[i]);
    if (chdir("/newroot") == 0 && mount("/newroot", "/", NULL, MS_MOVE, NULL) == 0 && chroot(".") == 0 && chdir("/") == 0) {
        const char *initp = access("/sbin/init", X_OK) == 0 ? "/sbin/init" : "/init";
        execl(initp, initp, (char *)NULL);
    }
    /* something failed: put the mounts back and report (the input devices must be reopened) */
    int e = errno;
    mount("/newroot/dev", "/dev", NULL, MS_MOVE, NULL);
    mount("/newroot/proc", "/proc", NULL, MS_MOVE, NULL);
    mount("/newroot/sys", "/sys", NULL, MS_MOVE, NULL);
    mount("/newroot/mnt", "/mnt", NULL, MS_MOVE, NULL);
    snprintf(m, sizeof m, "switching to the rootfs failed (%s). Rebooting in 5 s.", strerror(e));
    message_timed("Continue boot", m, 5);
    sync();
    reboot(RB_AUTOBOOT);
    for (;;) sleep(60);
}

static void toggle_autoboot(void) {
    if (mount_userdata(1, NULL, 0) != 0) {
        char m[200];
        snprintf(m, sizeof m, "could not mount userdata read-write: %s", strerror(errno));
        message("Auto-boot", m);
        return;
    }
    mkdir("/mnt/linux", 0755);
    int on = access("/mnt/linux/autoboot", F_OK) == 0;
    if (on) unlink("/mnt/linux/autoboot");
    else { int fd = open("/mnt/linux/autoboot", O_WRONLY | O_CREAT, 0644); if (fd >= 0) close(fd); }
    sync();
    umount("/mnt");
    message("Auto-boot", on ? "Auto-boot is now OFF" : "Auto-boot is now ON (5 second countdown at start)");
}

static void check_rootfs(void) {
    char dev[128] = "?", buf[3072], link[256] = "";
    if (mount_userdata(0, dev, sizeof dev) != 0) {
        snprintf(buf, sizeof buf, "could not mount userdata read-only: %s", strerror(errno));
        message("Check rootfs", buf);
        return;
    }
    ssize_t rl = readlink(ROOT_DIR "/sbin/init", link, sizeof link - 1);
    link[rl > 0 ? rl : 0] = 0;
    size_t o = (size_t)snprintf(buf, sizeof buf,
        "userdata: %s (mounted read-only)\n/linux exists: %s\nrootfs init present: %s\n/sbin/init -> %s\nauto-boot: %s\n\nrootfs top level:\n",
        dev, access("/mnt/linux", F_OK) == 0 ? "yes" : "no", rootfs_has_init() ? "yes" : "no",
        link[0] ? link : "(not a symlink, or missing)", access("/mnt/linux/autoboot", F_OK) == 0 ? "ON" : "off");
    DIR *d = opendir(ROOT_DIR);
    if (d) {
        struct dirent *de;
        int n = 0;
        while ((de = readdir(d)) && n < 40 && o < sizeof buf - 80) {
            if (de->d_name[0] == '.') continue;
            o += (size_t)snprintf(buf + o, sizeof buf - o, "  %s\n", de->d_name);
            n++;
        }
        closedir(d);
    } else {
        o += (size_t)snprintf(buf + o, sizeof buf - o, "  (cannot open %s: %s)\n", ROOT_DIR, strerror(errno));
    }
    umount("/mnt");
    view_text("Check rootfs", buf, o);
}

static void autoboot_check(void) {
    if (mount_userdata(0, NULL, 0) != 0) return;
    int on = access("/mnt/linux/autoboot", F_OK) == 0 && rootfs_has_init();
    umount("/mnt");
    if (!on) return;
    if (countdown(5)) return;
    continue_boot();
}

static void act_continue(void) { continue_boot(); }
static void act_toggle_auto(void) { toggle_autoboot(); }
static void act_check_rootfs(void) { check_rootfs(); }

/* ---------- menu framework: add new features by adding an item to a table ---------- */
typedef void (*actfn)(void);
struct mitem { const char *label; actfn fn; };   /* fn == NULL means "go back" */

static void run_menu(const char *title, const struct mitem *it, int n, int top) {
    int sel = 0;
    for (;;) {
        char sub[160];
        if (top) battery_line(sub, sizeof sub);
        else snprintf(sub, sizeof sub, "VOL UP/DOWN move, POWER open");
        for (int i = 0; i < n; i++) snprintf(lbls[i], LBL, "%s", it[i].label);
        int r = pick(title, sub, lbls, n, &sel);
        if (r < 0) { if (top) continue; return; }
        if (!it[r].fn) return;
        it[r].fn();
    }
}

static const char *kw_err[] = { "error", "fail", "warn", "unable", "cannot", "invalid", "timeout", "defer", "-22", "bug", 0 };
static const char *kw_touch[] = { "i2c", "mip4", "melfas", "touch", "input", "regulator", "4-0048", "0-0048", "13870000", 0 };

static void fe_root(void) { explore("/"); }
static void fe_sys(void) { explore("/sys"); }
static void fe_proc(void) { explore("/proc"); }
static void fe_dev(void) { explore("/dev"); }
static void log_all(void) { show_dmesg(); }
static void log_err(void) { show_dmesg_filtered("kernel log: errors and warnings", kw_err); }
static void log_touch(void) { show_dmesg_filtered("kernel log: touch / i2c / input", kw_touch); }
static const char *kw_mmc[] = { "mmc", "mshc", "fsys", "block", "partition", "emmc", 0 };
static void log_mmc(void) { show_dmesg_filtered("kernel log: storage (mmc)", kw_mmc); }
static void act_reboot(void) { do_reboot(RB_AUTOBOOT, "Reboot"); }
static void act_poweroff(void) { do_reboot(RB_POWER_OFF, "Power off"); }

/* storage: lists block devices from /proc/partitions and mounts one READ-ONLY to browse it */
static void try_mount_and_browse(const char *name) {
    char dev[128];
    static const char *types[] = { "ext4", "f2fs", "vfat", "exfat", "erofs", "squashfs", 0 };
    snprintf(dev, sizeof dev, "/dev/%s", name);
    mkdir("/mnt", 0755);
    for (int i = 0; types[i]; i++) {
        if (mount(dev, "/mnt", types[i], MS_RDONLY, NULL) == 0) {
            explore("/mnt");
            umount("/mnt");
            return;
        }
    }
    char m[200];
    snprintf(m, sizeof m, "could not mount read-only (unknown or encrypted filesystem?): %s", strerror(errno));
    message(dev, m);
}

static void storage_menu(void) {
    for (;;) {
        static char names[64][32];
        static unsigned long long kb[64];
        int n = 0;
        FILE *f = fopen("/proc/partitions", "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof line, f) && n < 63) {
                unsigned maj, min;
                unsigned long long blocks;
                char name[64];
                if (sscanf(line, "%u %u %llu %63s", &maj, &min, &blocks, name) != 4) continue;
                if (!strncmp(name, "loop", 4) || !strncmp(name, "ram", 3)) continue;
                snprintf(names[n], sizeof names[n], "%s", name);
                kb[n] = blocks;
                n++;
            }
            fclose(f);
        }
        if (n == 0) {
            message("Storage", "No block devices yet: the eMMC/SD controller is not enabled in the device tree.");
            return;
        }
        snprintf(lbls[0], LBL, "<- back");
        for (int i = 0; i < n; i++) {
            char up[300], ubuf[1024] = "", pn[96] = "";
            snprintf(up, sizeof up, "/sys/class/block/%s/uevent", names[i]);
            int ufd = open(up, O_RDONLY | O_NONBLOCK);
            if (ufd >= 0) {
                ssize_t ur = read(ufd, ubuf, sizeof ubuf - 1);
                close(ufd);
                if (ur > 0) {
                    ubuf[ur] = 0;
                    char *q = strstr(ubuf, "PARTNAME=");
                    if (q) { q += 9; size_t k = strcspn(q, "\n"); if (k > 80) k = 80; memcpy(pn, q, k); pn[k] = 0; }
                }
            }
            snprintf(lbls[i + 1], LBL, "%-14s %6llu MB  %s", names[i], kb[i] / 1024, pn);
        }
        int sel = 0;
        int r = pick("Storage: pick a partition to mount read-only", "", lbls, n + 1, &sel);
        if (r <= 0) return;
        try_mount_and_browse(names[r - 1]);
    }
}

static void menu_boot(void) {
    static const struct mitem it[] = {
        { "Continue boot now (Linux rootfs on userdata)", act_continue },
        { "Auto-boot on/off (toggle)", act_toggle_auto },
        { "Check rootfs (read-only)", act_check_rootfs },
        { "<- back", NULL }
    };
    run_menu("Boot", it, (int)(sizeof it / sizeof it[0]), 0);
}

static void menu_files(void) {
    static const struct mitem it[] = {
        { "Browse /  (root)", fe_root }, { "Browse /sys", fe_sys }, { "Browse /proc", fe_proc },
        { "Browse /dev", fe_dev }, { "Storage: partitions / mount read-only", storage_menu },
        { "<- back", NULL }
    };
    run_menu("File explorer", it, (int)(sizeof it / sizeof it[0]), 0);
}

static void menu_logs(void) {
    static const struct mitem it[] = {
        { "Kernel log (all)", log_all }, { "Kernel log: touch / i2c / input", log_touch }, { "Kernel log: storage (mmc)", log_mmc },
        { "Kernel log: errors and warnings", log_err }, { "<- back", NULL }
    };
    run_menu("Kernel logs", it, (int)(sizeof it / sizeof it[0]), 0);
}

static void menu_tests(void) {
    static const struct mitem it[] = {
        { "Touch test", touch_test }, { "Key test", key_test }, { "Display test", display_test },
        { "Battery / power", battery }, { "Input devices", v_input }, { "I2C devices", i2c_list },
        { "Interrupts", v_irq }, { "Clock summary (debugfs)", v_clk },
        { "Deferred devices (debugfs)", v_deferred }, { "<- back", NULL }
    };
    run_menu("Test functions", it, (int)(sizeof it / sizeof it[0]), 0);
}

static void menu_power(void) {
    static const struct mitem it[] = {
        { "Reboot", act_reboot }, { "Reboot to recovery", act_reboot_recovery },
        { "Power off", act_poweroff }, { "<- back", NULL }
    };
    run_menu("Power menu", it, (int)(sizeof it / sizeof it[0]), 0);
}

int main(void) {
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("debugfs", "/sys/kernel/debug", "debugfs", 0, NULL);

    struct termios tio;
    if (tcgetattr(0, &tio) == 0) { tio.c_lflag &= ~(tcflag_t)(ECHO | ICANON); tcsetattr(0, TCSANOW, &tio); }
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) { cols = ws.ws_col; rows = ws.ws_row; }

    for (int i = 0; i < 16; i++) {
        char p[32], name[64] = "?";
        snprintf(p, sizeof p, "/dev/input/event%d", i);
        int fd = open(p, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        snprintf(iname[ifn], sizeof iname[ifn], "%s", name);
        struct input_absinfo ai;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0 && ai.maximum > 0) { ymax = ai.maximum; touchdev = ifn; }
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0 && ai.maximum > 0) xmax = ai.maximum;
        ifd[ifn++] = fd;
    }
    if (!ifn) {
        fstart();
        fline(0, "NO INPUT DEVICES FOUND - force off with the power button");
        fflushscreen();
        for (;;) sleep(60);
    }

    autoboot_check();
    static const struct mitem top_items[] = {
        { "Boot  >", menu_boot }, { "File explorer  >", menu_files }, { "Kernel logs  >", menu_logs },
        { "Test functions  >", menu_tests }, { "System info", sysinfo }, { "Power menu  >", menu_power }
    };
    run_menu("A30 mainline - early userspace", top_items, (int)(sizeof top_items / sizeof top_items[0]), 1);
    return 0;
}
