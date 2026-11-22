// zeroos-desktop-view: the Desktop app's remote desktop connection, as its own
// process so a protocol problem can never take ZeroShell down with it.
//
//   zeroos-desktop-view vnc|rdp ADDRESS
//
// stdin, one line each (the app writes these):
//   user NAME | pass PASSWORD | domain NAME | size WxH   before "go"
//   go                       connect
//   k KEYSYM 1|0             key down / up (X keysyms, hex)
//   p X Y BUTTONS            pointer (VNC button mask: 1 left, 2 middle,
//                            4 right, 8 wheel up, 16 wheel down)
//   q                        disconnect and exit
// stdout:
//   status TEXT              what it is doing
//   frame PATH W H           the screen is now in PATH (apps/desktop/frame.h)
//   connected
//   error auth TEXT          needs (another) password
//   error TEXT               could not connect, or the connection ended
// It exits when stdin closes.
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rfb/rfbclient.h>

#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <winpr/synch.h>

#include "apps/desktop/frame.h"
#include "apps/desktop/hosts.h"

using namespace zs::desktop;

namespace {

// ---- Talking to the app --------------------------------------------------

// The app's end of the socket. Not fd 1: FreeRDP and libvncclient log to
// stdout, which goes to the log file with stderr instead.
int g_out = 1;

void say(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > int(sizeof buf) - 2) n = int(sizeof buf) - 2;
    for (int i = 0; i < n; ++i)
        if (buf[i] == '\n' || buf[i] == '\r') buf[i] = ' ';
    buf[n++] = '\n';
    ssize_t w = write(g_out, buf, size_t(n));
    (void)w;
}

struct Options {
    std::string user, pass, domain;
    int width = 960, height = 450;
};

// Lines from stdin, without blocking. False once stdin has closed.
std::string g_in;
bool read_lines(std::string* out_lines) {
    char buf[1024];
    while (true) {
        ssize_t n = read(0, buf, sizeof buf);
        if (n > 0) {
            g_in.append(buf, size_t(n));
            continue;
        }
        if (n == 0) return false;
        if (errno == EINTR) continue;
        break;   // EAGAIN
    }
    size_t nl = g_in.rfind('\n');
    if (nl != std::string::npos) {
        *out_lines = g_in.substr(0, nl + 1);
        g_in.erase(0, nl + 1);
    }
    return true;
}

template <typename F>
void each_line(const std::string& lines, F f) {
    size_t start = 0;
    while (start < lines.size()) {
        size_t nl = lines.find('\n', start);
        if (nl == std::string::npos) nl = lines.size();
        if (nl > start) f(lines.substr(start, nl - start));
        start = nl + 1;
    }
}

// ---- The frame ------------------------------------------------------------

struct Frame {
    FrameHeader* hdr = nullptr;
    size_t bytes = 0;
    int serial = 0;
    std::string path;

    uint16_t* pixels() { return frame_pixels(hdr); }

    // A new file for a new size; the app maps it, then the old one goes.
    bool make(uint32_t w, uint32_t h) {
        char p[128];
        snprintf(p, sizeof p, "/dev/shm/zeroos-desktop-%d-%d", int(getpid()), ++serial);
        size_t n = size_t(frame_bytes(w, h));
        int fd = open(p, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0) return false;
        if (ftruncate(fd, off_t(n)) != 0) {
            close(fd);
            unlink(p);
            return false;
        }
        void* m = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (m == MAP_FAILED) {
            unlink(p);
            return false;
        }
        drop();
        hdr = static_cast<FrameHeader*>(m);
        bytes = n;
        path = p;
        hdr->width = w;
        hdr->height = h;
        hdr->stride = w * 2;
        hdr->seq = 0;
        __atomic_store_n(&hdr->magic, kFrameMagic, __ATOMIC_RELEASE);
        say("frame %s %u %u", p, w, h);
        return true;
    }
    void bump() {
        if (hdr) __atomic_add_fetch(&hdr->seq, 1, __ATOMIC_RELEASE);
    }
    void drop() {
        if (!hdr) return;
        munmap(hdr, bytes);
        unlink(path.c_str());   // the app has normally done this already
        hdr = nullptr;
    }
};
Frame g_frame;

// ---- VNC (libvncclient) ----------------------------------------------------

Options* g_opt = nullptr;
std::string g_last_log;

void vnc_log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fputs(buf, stderr);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    if (!s.empty()) g_last_log = s;
}

rfbBool vnc_alloc(rfbClient* cl) {
    if (!g_frame.make(uint32_t(cl->width), uint32_t(cl->height))) {
        say("error Not enough memory for a %dx%d desktop", cl->width, cl->height);
        return FALSE;
    }
    cl->frameBuffer = reinterpret_cast<uint8_t*>(g_frame.pixels());
    // 16-bit RGB565, which is what we draw.
    cl->format.bitsPerPixel = 16;
    cl->format.depth = 16;
    cl->format.trueColour = TRUE;
    cl->format.bigEndian = FALSE;
    cl->format.redMax = 31;
    cl->format.greenMax = 63;
    cl->format.blueMax = 31;
    cl->format.redShift = 11;
    cl->format.greenShift = 5;
    cl->format.blueShift = 0;
    return TRUE;
}

void vnc_update(rfbClient*, int, int, int, int) { g_frame.bump(); }

bool g_need_auth = false;

char* vnc_password(rfbClient*) {
    if (g_opt->pass.empty()) {
        g_need_auth = true;
        return nullptr;
    }
    return strdup(g_opt->pass.c_str());
}

rfbCredential* vnc_credential(rfbClient*, int type) {
    if (type != rfbCredentialTypeUser) return nullptr;   // no client certificates
    if (g_opt->pass.empty() || g_opt->user.empty()) {
        g_need_auth = true;
        return nullptr;
    }
    auto* c = static_cast<rfbCredential*>(calloc(1, sizeof(rfbCredential)));
    c->userCredential.username = strdup(g_opt->user.c_str());
    c->userCredential.password = strdup(g_opt->pass.c_str());
    return c;
}

bool is_auth_message(const std::string& s) {
    for (const char* w : {"uthentication", "password", "Password", "credential"})
        if (s.find(w) != std::string::npos) return true;
    return false;
}

int run_vnc(const std::string& host, int port, Options& opt) {
    g_opt = &opt;
    rfbClientLog = vnc_log;
    rfbClientErr = vnc_log;
    rfbClient* cl = rfbGetClient(5, 3, 2);
    cl->MallocFrameBuffer = vnc_alloc;
    cl->GotFrameBufferUpdate = vnc_update;
    cl->GetPassword = vnc_password;
    cl->GetCredential = vnc_credential;
    cl->canHandleNewFBSize = TRUE;
    cl->listenSpecified = FALSE;
    cl->appData.useRemoteCursor = FALSE;   // the server draws its pointer
    cl->appData.encodingsString = "tight zrle ultra copyrect hextile zlib corre rre raw";
    cl->appData.compressLevel = 6;
    cl->appData.qualityLevel = 5;
    cl->serverHost = strdup(host.c_str());
    cl->serverPort = port;
    // The pixel format has to be set before the handshake asks for it.
    cl->format.bitsPerPixel = 16;
    cl->format.depth = 16;
    cl->format.redMax = 31;
    cl->format.greenMax = 63;
    cl->format.blueMax = 31;
    cl->format.redShift = 11;
    cl->format.greenShift = 5;
    cl->format.blueShift = 0;

    say("status Connecting to %s...", host.c_str());
    if (!rfbInitClient(cl, nullptr, nullptr)) {   // frees cl on failure
        if (g_need_auth) say("error auth Password needed");
        else if (is_auth_message(g_last_log)) say("error auth %s", g_last_log.c_str());
        else say("error %s", g_last_log.empty() ? "Couldn't connect" : g_last_log.c_str());
        return 1;
    }
    say("connected");

    int buttons = 0;
    std::string lines;
    while (true) {
        int n = WaitForMessage(cl, 10000);
        if (n < 0 || (n > 0 && !HandleRFBServerMessage(cl))) {
            say("error Connection lost%s%s", g_last_log.empty() ? "" : ": ", g_last_log.c_str());
            break;
        }
        lines.clear();
        if (!read_lines(&lines)) break;
        bool quit = false;
        each_line(lines, [&](const std::string& l) {
            if (l == "q") {
                quit = true;
            } else if (l[0] == 'k') {
                unsigned sym = 0;
                int down = 0;
                if (sscanf(l.c_str(), "k %x %d", &sym, &down) == 2) SendKeyEvent(cl, sym, down ? TRUE : FALSE);
            } else if (l[0] == 'p') {
                int x = 0, y = 0;
                if (sscanf(l.c_str(), "p %d %d %d", &x, &y, &buttons) == 3) {
                    SendPointerEvent(cl, x, y, buttons);
                    if (buttons & (8 | 16)) {   // a wheel step is a press and a release
                        buttons &= ~(8 | 16);
                        SendPointerEvent(cl, x, y, buttons);
                    }
                }
            }
        });
        if (quit) break;
    }
    rfbClientCleanup(cl);
    return 0;
}

// ---- RDP (FreeRDP 3) -------------------------------------------------------

struct RdpCtx {
    rdpContext base;   // first: FreeRDP allocates ContextSize and casts
};

bool g_rdp_need_auth = false;
std::string g_rdp_host;

void rdp_copy(rdpGdi* gdi, int x, int y, int w, int h) {
    if (!g_frame.hdr) return;
    int fw = int(g_frame.hdr->width), fh = int(g_frame.hdr->height);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fw) w = fw - x;
    if (y + h > fh) h = fh - y;
    if (w <= 0 || h <= 0) return;
    uint16_t* dst = g_frame.pixels();
    for (int r = y; r < y + h; ++r) {
        const uint32_t* s = reinterpret_cast<const uint32_t*>(gdi->primary_buffer + size_t(r) * gdi->stride) + x;
        uint16_t* d = dst + size_t(r) * size_t(fw) + x;
        for (int i = 0; i < w; ++i) {
            uint32_t p = s[i];   // BGRX32: 0xXXRRGGBB
            d[i] = uint16_t(((p >> 8) & 0xf800) | ((p >> 5) & 0x07e0) | ((p >> 3) & 0x001f));
        }
    }
    g_frame.bump();
}

BOOL rdp_begin_paint(rdpContext* ctx) {
    ctx->gdi->primary->hdc->hwnd->invalid->null = TRUE;
    return TRUE;
}

BOOL rdp_end_paint(rdpContext* ctx) {
    rdpGdi* gdi = ctx->gdi;
    HGDI_WND hwnd = gdi->primary->hdc->hwnd;
    if (hwnd->invalid->null) return TRUE;
    rdp_copy(gdi, hwnd->invalid->x, hwnd->invalid->y, hwnd->invalid->w, hwnd->invalid->h);
    return TRUE;
}

BOOL rdp_resize(rdpContext* ctx) {
    rdpSettings* s = ctx->settings;
    UINT32 w = freerdp_settings_get_uint32(s, FreeRDP_DesktopWidth);
    UINT32 h = freerdp_settings_get_uint32(s, FreeRDP_DesktopHeight);
    if (!gdi_resize(ctx->gdi, w, h)) return FALSE;
    if (!g_frame.make(w, h)) return FALSE;
    rdp_copy(ctx->gdi, 0, 0, int(w), int(h));
    return TRUE;
}

BOOL rdp_pre_connect(freerdp*) { return TRUE; }

BOOL rdp_post_connect(freerdp* inst) {
    if (!gdi_init(inst, PIXEL_FORMAT_BGRX32)) return FALSE;
    rdpContext* ctx = inst->context;
    rdpGdi* gdi = ctx->gdi;
    if (!g_frame.make(gdi->width, gdi->height)) return FALSE;
    ctx->update->BeginPaint = rdp_begin_paint;
    ctx->update->EndPaint = rdp_end_paint;
    ctx->update->DesktopResize = rdp_resize;
    return TRUE;
}

void rdp_post_disconnect(freerdp* inst) { gdi_free(inst); }

BOOL rdp_authenticate(freerdp*, char** user, char** pass, char**, rdp_auth_reason reason) {
    // Without NLA the server shows its own sign-in screen, which works too.
    if (reason != AUTH_NLA) return TRUE;
    if (!user || !*user || !**user || !pass || !*pass || !**pass) {
        g_rdp_need_auth = true;
        return FALSE;
    }
    return TRUE;
}

// Trust on first use, like the monitor's FreeRDP (/cert:tofu): FreeRDP keeps
// the certificate and only calls the second function if it ever changes.
DWORD rdp_verify(freerdp*, const char* host, UINT16, const char*, const char*, const char*,
                 const char*, DWORD) {
    say("status Trusting %s's certificate (first connection)", host ? host : "the computer");
    return 1;
}

DWORD rdp_verify_changed(freerdp*, const char* host, UINT16, const char*, const char*, const char*,
                         const char*, const char*, const char*, const char*, DWORD) {
    say("error %s has a different certificate than last time. If that's expected, remove "
        "~/.config/freerdp/server/ files for it",
        host ? host : "The computer");
    return 0;
}

void rdp_key(rdpInput* in, uint32_t sym, bool down, bool shift_held) {
    bool needs_shift = false;
    uint32_t sc = rdp_scancode(sym, &needs_shift);
    if (!sc) {
        uint32_t cp = sym >= 0x01000000 ? sym & 0xffffff : sym;
        if (cp >= 0xa0 && cp < 0x10000)
            (void)freerdp_input_send_unicode_keyboard_event(in, down ? 0 : KBD_FLAGS_RELEASE, UINT16(cp));
        return;
    }
    bool wrap = needs_shift && !shift_held;
    if (wrap && down) (void)freerdp_input_send_keyboard_event_ex(in, TRUE, FALSE, 0x2a);
    (void)freerdp_input_send_keyboard_event_ex(in, down ? TRUE : FALSE, FALSE, sc);
    if (wrap && !down) (void)freerdp_input_send_keyboard_event_ex(in, FALSE, FALSE, 0x2a);
}

void rdp_pointer(rdpInput* in, int x, int y, int buttons, int& last) {
    UINT16 ux = UINT16(x < 0 ? 0 : x), uy = UINT16(y < 0 ? 0 : y);
    (void)freerdp_input_send_mouse_event(in, PTR_FLAGS_MOVE, ux, uy);
    struct B { int bit; UINT16 flag; };
    for (B b : {B{1, PTR_FLAGS_BUTTON1}, B{4, PTR_FLAGS_BUTTON2}, B{2, PTR_FLAGS_BUTTON3}}) {
        if ((buttons ^ last) & b.bit)
            (void)freerdp_input_send_mouse_event(in, UINT16(b.flag | ((buttons & b.bit) ? PTR_FLAGS_DOWN : 0)), ux, uy);
    }
    if (buttons & 8) (void)freerdp_input_send_mouse_event(in, PTR_FLAGS_WHEEL | 0x0078, ux, uy);
    if (buttons & 16)
        (void)freerdp_input_send_mouse_event(in, PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 0x0088, ux, uy);
    last = buttons & ~(8 | 16);
}

bool rdp_auth_error(UINT32 e) {
    return e == FREERDP_ERROR_AUTHENTICATION_FAILED || e == FREERDP_ERROR_CONNECT_LOGON_FAILURE ||
           e == FREERDP_ERROR_CONNECT_WRONG_PASSWORD || e == FREERDP_ERROR_CONNECT_NO_OR_MISSING_CREDENTIALS ||
           e == FREERDP_ERROR_CONNECT_ACCESS_DENIED || e == FREERDP_ERROR_CONNECT_ACCOUNT_RESTRICTION ||
           e == FREERDP_ERROR_CONNECT_PASSWORD_EXPIRED || e == FREERDP_ERROR_CONNECT_PASSWORD_MUST_CHANGE;
}

int run_rdp(const std::string& host, int port, Options& opt) {
    g_rdp_host = host;
    freerdp* inst = freerdp_new();
    if (!inst) {
        say("error Out of memory");
        return 1;
    }
    inst->ContextSize = sizeof(RdpCtx);
    inst->PreConnect = rdp_pre_connect;
    inst->PostConnect = rdp_post_connect;
    inst->PostDisconnect = rdp_post_disconnect;
    inst->AuthenticateEx = rdp_authenticate;
    inst->VerifyCertificateEx = rdp_verify;
    inst->VerifyChangedCertificateEx = rdp_verify_changed;
    if (!freerdp_context_new(inst)) {
        say("error Couldn't start FreeRDP");
        freerdp_free(inst);
        return 1;
    }
    rdpContext* ctx = inst->context;
    rdpSettings* s = ctx->settings;
    bool ok = freerdp_settings_set_string(s, FreeRDP_ServerHostname, host.c_str()) &&
              freerdp_settings_set_uint32(s, FreeRDP_ServerPort, UINT32(port)) &&
              freerdp_settings_set_uint32(s, FreeRDP_DesktopWidth, UINT32(opt.width)) &&
              freerdp_settings_set_uint32(s, FreeRDP_DesktopHeight, UINT32(opt.height)) &&
              freerdp_settings_set_uint32(s, FreeRDP_ColorDepth, 16) &&
              freerdp_settings_set_uint32(s, FreeRDP_KeyboardLayout, 0x409) &&
              freerdp_settings_set_bool(s, FreeRDP_SoftwareGdi, TRUE) &&
              freerdp_settings_set_bool(s, FreeRDP_AutoReconnectionEnabled, FALSE);
    if (!opt.user.empty()) ok = ok && freerdp_settings_set_string(s, FreeRDP_Username, opt.user.c_str());
    if (!opt.pass.empty()) ok = ok && freerdp_settings_set_string(s, FreeRDP_Password, opt.pass.c_str());
    if (!opt.domain.empty()) ok = ok && freerdp_settings_set_string(s, FreeRDP_Domain, opt.domain.c_str());
    if (!ok) {
        say("error Bad connection settings");
        freerdp_context_free(inst);
        freerdp_free(inst);
        return 1;
    }

    say("status Connecting to %s...", host.c_str());
    if (!freerdp_connect(inst)) {
        UINT32 e = freerdp_get_last_error(ctx);
        if (g_rdp_need_auth) say("error auth User name and password needed");
        else if (rdp_auth_error(e)) say("error auth %s", freerdp_get_last_error_string(e));
        else say("error %s", e ? freerdp_get_last_error_string(e) : "Couldn't connect");
        freerdp_context_free(inst);
        freerdp_free(inst);
        return 1;
    }
    say("connected");

    int last_buttons = 0;
    bool shift_held = false;
    std::string lines;
    while (!freerdp_shall_disconnect_context(ctx)) {
        HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
        DWORD n = freerdp_get_event_handles(ctx, handles, ARRAYSIZE(handles));
        if (n == 0) {
            say("error Connection lost");
            break;
        }
        if (WaitForMultipleObjects(n, handles, FALSE, 10) == WAIT_FAILED) {
            say("error Connection lost");
            break;
        }
        if (!freerdp_check_event_handles(ctx)) {
            UINT32 e = freerdp_get_last_error(ctx);
            say("error %s", e ? freerdp_get_last_error_string(e) : "Disconnected");
            break;
        }
        lines.clear();
        if (!read_lines(&lines)) break;
        bool quit = false;
        rdpInput* in = ctx->input;
        each_line(lines, [&](const std::string& l) {
            if (l == "q") {
                quit = true;
            } else if (l[0] == 'k') {
                unsigned sym = 0;
                int down = 0;
                if (sscanf(l.c_str(), "k %x %d", &sym, &down) == 2) {
                    if (sym == ks::ShiftL) shift_held = down != 0;
                    rdp_key(in, sym, down != 0, shift_held);
                }
            } else if (l[0] == 'p') {
                int x = 0, y = 0, b = 0;
                if (sscanf(l.c_str(), "p %d %d %d", &x, &y, &b) == 3) rdp_pointer(in, x, y, b, last_buttons);
            }
        });
        if (quit) break;
    }
    freerdp_disconnect(inst);
    freerdp_context_free(inst);
    freerdp_free(inst);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || (strcmp(argv[1], "vnc") != 0 && strcmp(argv[1], "rdp") != 0)) {
        fprintf(stderr, "usage: zeroos-desktop-view vnc|rdp ADDRESS  (protocol on stdin/stdout)\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    g_out = fcntl(1, F_DUPFD_CLOEXEC, 3);
    if (g_out < 0) return 1;
    dup2(2, 1);
    Proto proto = strcmp(argv[1], "vnc") == 0 ? Proto::Vnc : Proto::Rdp;
    std::string host;
    int port = 0;
    if (!split_address(argv[2], default_port(proto), host, port)) {
        say("error Not an address: %s", argv[2]);
        return 2;
    }
    // A VNC "display" (pc:1) is port 5901, as every VNC viewer reads it.
    if (proto == Proto::Vnc && port > 0 && port < 100) port += 5900;

    // Settings until "go" (blocking: nothing else to do yet).
    Options opt;
    std::string line;
    char ch;
    bool go = false;
    while (!go && read(0, &ch, 1) == 1) {
        if (ch != '\n') {
            line += ch;
            continue;
        }
        if (line == "go") go = true;
        else if (line.rfind("user ", 0) == 0) opt.user = line.substr(5);
        else if (line.rfind("pass ", 0) == 0) opt.pass = line.substr(5);
        else if (line.rfind("domain ", 0) == 0) opt.domain = line.substr(7);
        else if (line.rfind("size ", 0) == 0) sscanf(line.c_str(), "size %dx%d", &opt.width, &opt.height);
        line.clear();
    }
    if (!go) return 0;
    if (opt.width < 200 || opt.width > 4096) opt.width = 960;
    if (opt.height < 150 || opt.height > 4096) opt.height = 450;
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    int rc = proto == Proto::Vnc ? run_vnc(host, port, opt) : run_rdp(host, port, opt);
    g_frame.drop();
    say("bye");
    return rc;
}
