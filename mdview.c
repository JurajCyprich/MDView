/* MD View - jednoduchy standalone Markdown prehliadac / editor (Win32, C).
   Nahlad kresli RichEdit (Msftedit.dll, sucast Windows) z RTF generovaneho z Markdownu. */
#define UNICODE
#define _UNICODE
#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <richedit.h>
#include <ole2.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <ctype.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>

#define APP L"MD View"
#define REGKEY L"Software\\MDView"

enum { ID_OPEN = 100, ID_SAVE, ID_SAVEAS, ID_EDIT, ID_THEME, ID_ZOOMOUT, ID_ZOOMIN, ID_ZOOM0,
       ID_FIND, ID_NEXT, ID_PREV, ID_ESC, ID_NEW, ID_RELOAD, ID_HELP, ID_CLEAR, ID_UPDATE, ID_UPDCHECK,
       ID_HOME, ID_LINKS, ID_STAR, ID_OPENURL,
       IDC_EDITOR = 200, IDC_VIEW, IDC_SEARCH, IDC_COUNT,
       IDM_COPY = 300, IDM_SELALL, IDM_CUT, IDM_PASTE, IDM_UNDO };
enum { T_RENDER = 1, T_RESIZE, T_FIND, T_IMG };

/* sentinely vkladane do RTF, po nacitani sa z dokumentu odstrania */
#define S_LINK_A 0xE000
#define S_LINK_B 0xE001
#define S_HEAD   0xE002
#define S_BREAK  0xE003
#define S_BLOCK  0xE004   /* zaciatok bloku; nesie zdrojovy riadok pre synchronizaciu */
#define S_IMG    0xE005   /* miesto obrazka; po nacitani sa nahradi obrazkom (1 znak) */

typedef struct { COLORREF bg, text, link, codebg, border, thbg, muted, codefg,
                 bar, barText, btn, btnDown, accent, editBg; } Theme;
static const Theme LIGHT = { RGB(255,255,255), RGB(31,35,40), RGB(9,105,218), RGB(239,241,243), RGB(208,215,222),
    RGB(243,245,247), RGB(89,99,110), RGB(163,31,52),
    RGB(243,244,246), RGB(36,41,47), RGB(255,255,255), RGB(216,219,224), RGB(9,105,218), RGB(255,255,255) };
static const Theme DARK = { RGB(21,23,28), RGB(215,218,224), RGB(88,166,255), RGB(38,42,51), RGB(61,67,77),
    RGB(31,35,43), RGB(145,152,161), RGB(255,123,114),
    RGB(30,33,40), RGB(215,218,224), RGB(42,46,55), RGB(58,63,74), RGB(31,111,235), RGB(15,17,21) };
static Theme T;

typedef struct { int a, b; wchar_t *url; } Link;
typedef struct { int pos; wchar_t slug[128]; } Head;
typedef struct { int line, cp; } Blk;   /* blok nahladu: zdrojovy riadok -> pozicia v nahlade */
static Blk *g_blk; static int g_nblk, g_capblk;
static int *g_lineStart, g_nLines, g_viewLen;   /* zaciatky riadkov v editore (cp) */
static const wchar_t *g_srcBase;                /* zdrojovy text pocas vykreslovania */
static int g_syncing, g_lastSy[2];              /* synchronizacia posuvania editor <-> nahlad */
static void sync_mark(void);

static HWND hMain, hEdit, hView, hSearch, hCount, hBtn[14];
static RECT g_searchRc;  /* ramcek okolo hladacieho pola */
static int g_help = 0;   /* 1 = v nahlade je zobrazeny vstavany navod */
static HWND g_lastPane;
static WNDPROC g_reProc, g_edProc;
static HFONT hUiFont;
static HBRUSH hbrBar, hbrEdit;
static int g_dpi = 96, g_dark = 0, g_edit = 0, g_modified = 0, g_loading = 0, g_fs = 22;
static int g_crlf = 1, g_hasTable = 0, g_availTw = 9000, g_lastViewW = 0, g_dragSplit = 0, g_inLink = 0;
static double g_split = 0.5;
static wchar_t g_path[1024], g_find[256];
static Link *g_links; static int g_nlinks, g_caplinks;
static Head *g_heads; static int g_nheads, g_capheads;
static POINT g_downPt;

#define S(x) MulDiv((x), g_dpi, 96)
#define FS(x) ((x) * g_fs / 22)

static const wchar_t HOME_KEYS[] =
    L"## Klávesové skratky\n\n"
    L"| Skratka | Akcia |\n| :-- | :-- |\n"
    L"| `Ctrl+O` | Otvoriť súbor |\n"
    L"| `Ctrl+L` | Otvoriť webovú adresu alebo cestu |\n"
    L"| `Ctrl+B` | Uložiť / odobrať odkaz na otvorený dokument |\n"
    L"| `Ctrl+W` | Zavrieť dokument a vrátiť sa sem |\n"
    L"| `Ctrl+S` / `Ctrl+Shift+S` | Uložiť / Uložiť ako |\n"
    L"| `Ctrl+E` | Zapnúť / skryť editor |\n"
    L"| `Ctrl+D` | Svetlý / tmavý režim |\n"
    L"| `Ctrl+F` | Hľadať — `Enter` / `F3` ďalší, `Shift+F3` predošlý, `Esc` zrušiť |\n"
    L"| `Ctrl +` / `Ctrl −` / `Ctrl+0` | Veľkosť písma |\n"
    L"| `F5` | Znovu načítať dokument |\n"
    L"| `F1` | Návod |\n";

/* ---------- buffre ---------- */
typedef struct { char *p; size_t n, cap; } SB;
static void sb_need(SB *b, size_t k) {
    if (b->n + k + 1 > b->cap) { b->cap = (b->n + k + 1) * 2 + 256; b->p = (char *)realloc(b->p, b->cap); }
}
static void sb_s(SB *b, const char *s) {
    size_t k = strlen(s); sb_need(b, k); memcpy(b->p + b->n, s, k); b->n += k; b->p[b->n] = 0;
}
static void sb_f(SB *b, const char *fmt, ...) {
    char t[1024]; va_list ap; va_start(ap, fmt); _vsnprintf(t, sizeof t - 1, fmt, ap); va_end(ap);
    t[sizeof t - 1] = 0; sb_s(b, t);
}
static void sb_wc(SB *b, wchar_t c) {
    if (c == '\\' || c == '{' || c == '}') { char t[3] = { '\\', (char)c, 0 }; sb_s(b, t); }
    else if (c == '\t') sb_s(b, "\\tab ");
    else if (c < 32) { }
    else if (c < 128) { sb_need(b, 1); b->p[b->n++] = (char)c; b->p[b->n] = 0; }
    else sb_f(b, "\\u%d?", (int)(short)c);
}
typedef struct { wchar_t *p; int n, cap; } WB;
static void wb_add(WB *w, const wchar_t *s, int k) {
    if (w->n + k + 1 > w->cap) { w->cap = (w->n + k + 1) * 2 + 64; w->p = (wchar_t *)realloc(w->p, w->cap * sizeof(wchar_t)); }
    memcpy(w->p + w->n, s, k * sizeof(wchar_t)); w->n += k; w->p[w->n] = 0;
}
static void wb_ch(WB *w, wchar_t c) { wb_add(w, &c, 1); }

/* ---------- pomocne ---------- */
static int is_sp(wchar_t c) { return c == ' ' || c == '\t'; }
static int is_alnum_w(wchar_t c) { return c && IsCharAlphaNumericW(c); }
static int is_punct_a(wchar_t c) { return c > 32 && c < 127 && !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')); }
static int starts_i(const wchar_t *s, int n, const wchar_t *pre) {
    int k = (int)wcslen(pre); return n >= k && _wcsnicmp(s, pre, k) == 0;
}
static int is_br(const wchar_t *s, int i, int n) {
    if (starts_i(s + i, n - i, L"<br>")) return 4;
    if (starts_i(s + i, n - i, L"<br/>")) return 5;
    if (starts_i(s + i, n - i, L"<br />")) return 6;
    return 0;
}

static wchar_t *get_text(HWND h, int crlf, int *outn) {
    GETTEXTLENGTHEX gl; GETTEXTEX gt; int n, got; wchar_t *w;
    gl.flags = (crlf ? GTL_USECRLF : GTL_DEFAULT) | GTL_NUMCHARS | GTL_PRECISE; gl.codepage = 1200;
    n = (int)SendMessageW(h, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0); if (n < 0) n = 0;
    w = (wchar_t *)malloc((n + 2) * sizeof(wchar_t));
    ZeroMemory(&gt, sizeof gt);
    gt.cb = (DWORD)((n + 1) * sizeof(wchar_t)); gt.flags = crlf ? GT_USECRLF : GT_DEFAULT; gt.codepage = 1200;
    got = (int)SendMessageW(h, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)w);
    if (got < 0) got = 0; if (got > n) got = n;
    w[got] = 0; if (outn) *outn = got; return w;
}

/* ---------- adresy: lokalne cesty aj webove adresy ---------- */
static int is_url(const wchar_t *s) { int n = (int)wcslen(s); return starts_i(s, n, L"http://") || starts_i(s, n, L"https://"); }
static int g_online;   /* 1 = otvoreny dokument je z internetu (g_path je URL) */

/* %XX -> znaky; kodovanie je UTF-8 */
static void url_decode(const wchar_t *in, wchar_t *out, int cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, NULL, NULL), i, k = 0;
    char *a = (char *)malloc(n + 1), *b = (char *)malloc(n + 1);
    WideCharToMultiByte(CP_UTF8, 0, in, -1, a, n, NULL, NULL);
    for (i = 0; a[i]; i++) {
        if (a[i] == '%' && isxdigit((unsigned char)a[i + 1]) && isxdigit((unsigned char)a[i + 2])) {
            char hx[3]; hx[0] = a[i + 1]; hx[1] = a[i + 2]; hx[2] = 0; b[k++] = (char)strtol(hx, NULL, 16); i += 2;
        } else b[k++] = a[i];
    }
    b[k] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, b, -1, out, cap)) lstrcpynW(out, in, cap);
    free(a); free(b);
}
/* medzery, diakritika a pod. -> %XX (uz zakodovane %XX ostanu); '\' -> '/' */
static void url_encode(const wchar_t *in, wchar_t *out, int cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, NULL, NULL), i, k = 0; char *a = (char *)malloc(n + 1);
    WideCharToMultiByte(CP_UTF8, 0, in, -1, a, n, NULL, NULL);
    for (i = 0; a[i] && k < cap - 4; i++) {
        unsigned char ch = (unsigned char)a[i];
        if (ch == '\\') out[k++] = '/';
        else if (ch <= 0x20 || ch >= 0x7F || strchr("\"<>^`{|}", ch)) { _snwprintf(out + k, 4, L"%%%02X", ch); k += 3; }
        else out[k++] = ch;
    }
    out[k] = 0; free(a);
}
/* odkaz na webovu stranku so suborom -> odkaz priamo na obsah suboru */
static void normalize_url(const wchar_t *in, wchar_t *out, int cap) {
    const wchar_t *p; wchar_t t[2200]; int n = (int)wcslen(in);
    lstrcpynW(out, in, cap);
    if (starts_i(in, n, L"https://github.com/") && (p = wcsstr(in + 19, L"/blob/")) != NULL) {
        _snwprintf(t, 2199, L"https://raw.githubusercontent.com/%.*s/%s", (int)(p - (in + 19)), in + 19, p + 6); t[2199] = 0;
        lstrcpynW(out, t, cap);
    } else if ((p = wcsstr(in, L"/-/blob/")) != NULL) {                       /* GitLab */
        _snwprintf(t, 2199, L"%.*s/-/raw/%s", (int)(p - in), in, p + 8); t[2199] = 0; lstrcpynW(out, t, cap);
    } else if (starts_i(in, n, L"https://drive.google.com/file/d/")) {          /* Google Disk */
        const wchar_t *id = in + 32; int k = 0; while (id[k] && id[k] != '/' && id[k] != '?') k++;
        _snwprintf(t, 2199, L"https://drive.google.com/uc?export=download&id=%.*s", k, id); t[2199] = 0; lstrcpynW(out, t, cap);
    } else if (wcsstr(in, L"dropbox.com/")) {                                   /* Dropbox */
        wchar_t *d; lstrcpynW(t, in, 2100);
        if ((d = wcsstr(t, L"dl=0")) != NULL) d[3] = '1';
        else if (!wcsstr(t, L"dl=1") && !wcsstr(t, L"raw=1")) wcscat(t, wcschr(t, '?') ? L"&dl=1" : L"?dl=1");
        lstrcpynW(out, t, cap);
    }
}
/* odkaz z dokumentu -> uplna cesta (1) alebo webova adresa (2); 0 = neda sa urcit.
   Relativne odkazy sa beru od priecinka DOKUMENTU, nie od priecinka aplikacie. */
static int resolve_ref(const wchar_t *ref, int rn, wchar_t *out, int cap) {
    wchar_t r[1100], d[1100], *sl; int a = 0;
    if (rn > 1099) rn = 1099;
    while (a < rn && is_sp(ref[a])) a++;
    while (rn > a && is_sp(ref[rn - 1])) rn--;
    memcpy(r, ref + a, (rn - a) * sizeof(wchar_t)); r[rn - a] = 0;
    if (!r[0]) return 0;
    if (is_url(r)) { wchar_t t[2200]; normalize_url(r, t, 2200); url_encode(t, out, cap); return 2; }
    if (starts_i(r, (int)wcslen(r), L"file:")) {
        const wchar_t *p = r + 5; while (*p == '/') p++;
        url_decode(p, out, cap); for (sl = out; *sl; sl++) if (*sl == '/') *sl = '\\';
        return 1;
    }
    if (g_online) {
        wchar_t e[2200]; DWORD len = cap;
        url_encode(r, e, 2200);
        return UrlCombineW(g_path, e, out, &len, 0) == S_OK ? 2 : 0;
    }
    url_decode(r, d, 1100);
    for (sl = d; *sl; sl++) if (*sl == '/') *sl = '\\';
    if ((d[0] && d[1] == ':') || (d[0] == '\\' && d[1] == '\\')) { lstrcpynW(out, d, cap); return 1; }
    if (!g_path[0]) return 0;
    wcscpy(r, g_path); sl = wcsrchr(r, '\\'); if (!sl) return 0; sl[1] = 0;
    sl = d; while (*sl == '\\') sl++;
    if (wcslen(r) + wcslen(sl) >= 1099) return 0;
    wcscat(r, sl);
    if (!PathCanonicalizeW(d, r)) lstrcpynW(d, r, 1100);
    lstrcpynW(out, d, cap);
    return 1;
}
/* nazov suboru z cesty alebo adresy */
static void loc_name(const wchar_t *loc, wchar_t *out, int cap) {
    wchar_t t[1100], *q, *s;
    lstrcpynW(t, loc, 1100);
    if (is_url(t)) {
        if ((q = wcspbrk(t, L"?#")) != NULL) *q = 0;
        while ((s = wcsrchr(t, '/')) != NULL && !s[1] && s > t) *s = 0;
        s = wcsrchr(t, '/'); url_decode(s ? s + 1 : t, out, cap);
    } else { s = wcsrchr(t, '\\'); lstrcpynW(out, s ? s + 1 : t, cap); }
    if (!out[0]) lstrcpynW(out, loc, cap);
}
static int has_md_ext(const wchar_t *loc) {
    wchar_t t[1100], *q, *e; lstrcpynW(t, loc, 1100);
    if (is_url(t) && (q = wcspbrk(t, L"?#")) != NULL) *q = 0;
    e = wcsrchr(t, '.'); if (!e || wcschr(e, '/') || wcschr(e, '\\')) return 0;
    return !_wcsicmp(e, L".md") || !_wcsicmp(e, L".markdown") || !_wcsicmp(e, L".txt");
}

/* ---------- siet ---------- */
static BYTE *http_get_url(const wchar_t *url, DWORD maxLen, DWORD *outLen, DWORD *status) {
    URL_COMPONENTS uc; wchar_t host[256], path[2200], extra[2200];
    HINTERNET s = NULL, c = NULL, r = NULL; BYTE *buf = NULL; DWORD n = 0, cap = 0, st = 0, sl = sizeof st; int ok = 0;
    *outLen = 0; if (status) *status = 0;
    ZeroMemory(&uc, sizeof uc); uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2100;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2100;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return NULL;
    { wchar_t *h = wcschr(extra, '#'); if (h) *h = 0; }
    if (!path[0]) wcscpy(path, L"/");
    if (wcslen(path) + wcslen(extra) < 2190) wcscat(path, extra);
    s = WinHttpOpen(L"MDView", 4 /*AUTOMATIC_PROXY*/, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0); if (!s) goto end;
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    c = WinHttpConnect(s, host, uc.nPort, 0); if (!c) goto end;
    r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                           uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!r) goto end;
    if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) goto end;
    if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX)) goto end;
    if (status) *status = st;
    if (st != 200) goto end;
    for (;;) {
        DWORD got = 0;
        if (n + 65536 + 2 > cap) { cap = cap * 2 + 262144; buf = (BYTE *)realloc(buf, cap); }
        if (!WinHttpReadData(r, buf + n, 65536, &got)) goto end;
        if (!got) { ok = 1; break; }
        n += got; if (n > maxLen) goto end;
    }
end:
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); if (s) WinHttpCloseHandle(s);
    if (!ok) { free(buf); return NULL; }
    buf[n] = buf[n + 1] = 0; *outLen = n; return buf;
}
/* stahovanie na pozadi: vysledok pride ako sprava msg s ukazovatelom na NetJob */
typedef struct { wchar_t url[1100]; UINT msg; BYTE *data; DWORD n, status; } NetJob;
#define WM_APP_DOC (WM_APP + 4)
#define WM_APP_IMG (WM_APP + 5)
static DWORD WINAPI net_thread(LPVOID p) {
    NetJob *j = (NetJob *)p;
    j->data = http_get_url(j->url, 48u << 20, &j->n, &j->status);
    PostMessageW(hMain, j->msg, 0, (LPARAM)j);
    return 0;
}
static void start_net(const wchar_t *url, UINT msg) {
    NetJob *j = (NetJob *)calloc(1, sizeof(NetJob)); HANDLE t;
    lstrcpynW(j->url, url, 1100); j->msg = msg;
    t = CreateThread(NULL, 0, net_thread, j, 0, NULL);
    if (t) CloseHandle(t); else PostMessageW(hMain, msg, 0, (LPARAM)j);
}
static BYTE *read_all(const wchar_t *path, DWORD maxLen, DWORD *outLen) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz; BYTE *buf; DWORD rd = 0;
    *outLen = 0;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > maxLen) { CloseHandle(h); return NULL; }
    buf = (BYTE *)malloc((size_t)sz.QuadPart + 4);
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &rd, NULL)) rd = 0;
    CloseHandle(h);
    buf[rd] = buf[rd + 1] = 0; *outLen = rd; return buf;
}

/* ---------- obrazky ----------
   Obrazok sa nacita raz (lokalne hned, z webu na pozadi) a drzi sa v pamati.
   Do nahladu ho vlozi RichEdit cez EM_INSERTIMAGE, ktory sam dekoduje PNG, JPEG, GIF, BMP... */
typedef struct { wchar_t key[1100]; BYTE *data; DWORD n; int w, h, state; } Img;   /* state: 0 caka, 1 ok, 2 chyba */
static Img *g_img; static int g_nimg, g_capimg;
typedef struct { int idx; LONG w, h; } ImgUse;                                     /* rozmery v HIMETRIC */
static ImgUse *g_use; static int g_nuse, g_capuse;
static int g_cellTw, g_curLi;   /* sirka bunky tabulky / odsadenie odseku (twips) pri vkladani obrazka */
static IWICImagingFactory *g_wic;
static const GUID MY_CLSID_WIC = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID MY_IID_WICF  = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };

static int img_size(const BYTE *data, DWORD n, int *w, int *h) {
    IStream *st; IWICBitmapDecoder *dec = NULL; IWICBitmapFrameDecode *fr = NULL; UINT W = 0, H = 0; int ok = 0;
    if (!g_wic && FAILED(CoCreateInstance(&MY_CLSID_WIC, NULL, CLSCTX_INPROC_SERVER, &MY_IID_WICF, (void **)&g_wic))) return 0;
    if (!data || !n || !(st = SHCreateMemStream(data, n))) return 0;
    if (SUCCEEDED(g_wic->lpVtbl->CreateDecoderFromStream(g_wic, st, NULL, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->lpVtbl->GetFrame(dec, 0, &fr)) && SUCCEEDED(fr->lpVtbl->GetSize(fr, &W, &H)) && W && H) { *w = (int)W; *h = (int)H; ok = 1; }
    if (fr) fr->lpVtbl->Release(fr);
    if (dec) dec->lpVtbl->Release(dec);
    st->lpVtbl->Release(st);
    return ok;
}
static void img_clear(void) { int i; for (i = 0; i < g_nimg; i++) free(g_img[i].data); g_nimg = 0; }
static int img_get(const wchar_t *key, int kind) {
    int i; Img *m;
    for (i = 0; i < g_nimg; i++) if (_wcsicmp(g_img[i].key, key) == 0) return i;
    if (g_nimg == g_capimg) { g_capimg = g_capimg * 2 + 16; g_img = (Img *)realloc(g_img, g_capimg * sizeof(Img)); }
    m = &g_img[g_nimg]; ZeroMemory(m, sizeof *m); lstrcpynW(m->key, key, 1100);
    if (kind == 1) {
        m->data = read_all(key, 64u << 20, &m->n);
        m->state = (m->data && img_size(m->data, m->n, &m->w, &m->h)) ? 1 : 2;
    } else start_net(key, WM_APP_IMG);
    return g_nimg++;
}
/* obrazok z webu dorazil (hlavne vlakno) */
static void img_arrived(NetJob *j) {
    int i;
    for (i = 0; i < g_nimg; i++) if (_wcsicmp(g_img[i].key, j->url) == 0 && g_img[i].state == 0) {
        Img *m = &g_img[i];
        if (j->data && img_size(j->data, j->n, &m->w, &m->h)) { m->data = j->data; m->n = j->n; m->state = 1; j->data = NULL; }
        else m->state = 2;
        break;
    }
    free(j->data); free(j);
}
/* nahradi znak na pozicii cp obrazkom (dlzka textu sa nemeni) */
static void insert_image(int cp, const ImgUse *u) {
    RICHEDIT_IMAGE_PARAMETERS ip; IStream *st = NULL; CHARRANGE cr; HRESULT hr = E_FAIL; Img *m = &g_img[u->idx];
    cr.cpMin = cp; cr.cpMax = cp + 1;
    SendMessageW(hView, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(hView, EM_REPLACESEL, FALSE, (LPARAM)L"");
    if (m->state == 1 && (st = SHCreateMemStream(m->data, m->n)) != NULL) {
        ZeroMemory(&ip, sizeof ip);
        ip.xWidth = u->w; ip.yHeight = u->h; ip.Ascent = u->h; ip.Type = TA_BASELINE;
        ip.pwszAlternateText = L"obrázok"; ip.pIStream = st;
        hr = (HRESULT)SendMessageW(hView, EM_INSERTIMAGE, 0, (LPARAM)&ip);
        st->lpVtbl->Release(st);
    }
    if (FAILED(hr)) SendMessageW(hView, EM_REPLACESEL, FALSE, (LPARAM)L"\x25A1");
}

/* ---------- drag & drop suborov a odkazov (vlastny OLE drop target) ----------
   RichEdit si registruje vlastny drop target, ktory subory neprijima; nahradzame ho tymto.
   Prijima subory z Prieskumnika aj webove odkazy pretiahnute z prehliadaca. */
#define WM_APP_DROP (WM_APP + 1)
static wchar_t g_dropPath[1100];
static int g_dropOk;
static FORMATETC g_fmtDrop = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
static int drop_url(IDataObject *d, wchar_t *out, int cap) {
    static UINT cfUrl; FORMATETC f = { 0, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }; STGMEDIUM m; wchar_t t[1100], *p; int ok = 0;
    if (!cfUrl) cfUrl = RegisterClipboardFormatW(L"UniformResourceLocatorW");
    f.cfFormat = (CLIPFORMAT)cfUrl;
    if (FAILED(d->lpVtbl->GetData(d, &f, &m))) { f.cfFormat = CF_UNICODETEXT; if (FAILED(d->lpVtbl->GetData(d, &f, &m))) return 0; }
    if ((p = (wchar_t *)GlobalLock(m.hGlobal)) != NULL) {
        while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;
        lstrcpynW(t, p, 1100); GlobalUnlock(m.hGlobal);
        if ((p = wcspbrk(t, L"\r\n \t")) != NULL) *p = 0;
        ok = is_url(t);
        if (ok && out) lstrcpynW(out, t, cap);
    }
    ReleaseStgMedium(&m);
    return ok;
}
static HRESULT STDMETHODCALLTYPE DT_QI(IDropTarget *t, REFIID r, void **o) {
    if (IsEqualIID(r, &IID_IUnknown) || IsEqualIID(r, &IID_IDropTarget)) { *o = t; return S_OK; }
    *o = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DT_AddRef(IDropTarget *t) { (void)t; return 2; }
static ULONG STDMETHODCALLTYPE DT_Release(IDropTarget *t) { (void)t; return 1; }
static HRESULT STDMETHODCALLTYPE DT_Enter(IDropTarget *t, IDataObject *d, DWORD ks, POINTL pt, DWORD *eff) {
    (void)t; (void)ks; (void)pt;
    g_dropOk = d && (d->lpVtbl->QueryGetData(d, &g_fmtDrop) == S_OK || drop_url(d, NULL, 0));
    *eff = g_dropOk ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE DT_Over(IDropTarget *t, DWORD ks, POINTL pt, DWORD *eff) {
    (void)t; (void)ks; (void)pt;
    *eff = g_dropOk ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE DT_Leave(IDropTarget *t) { (void)t; return S_OK; }
static HRESULT STDMETHODCALLTYPE DT_Drop(IDropTarget *t, IDataObject *d, DWORD ks, POINTL pt, DWORD *eff) {
    STGMEDIUM m;
    (void)t; (void)ks; (void)pt;
    *eff = DROPEFFECT_NONE;
    if (d && SUCCEEDED(d->lpVtbl->GetData(d, &g_fmtDrop, &m))) {
        HDROP h = (HDROP)GlobalLock(m.hGlobal);
        if (h) {
            if (DragQueryFileW(h, 0, g_dropPath, 1024)) { PostMessageW(hMain, WM_APP_DROP, 0, 0); *eff = DROPEFFECT_COPY; }
            GlobalUnlock(m.hGlobal);
        }
        ReleaseStgMedium(&m);
    } else if (d && drop_url(d, g_dropPath, 1100)) { PostMessageW(hMain, WM_APP_DROP, 0, 0); *eff = DROPEFFECT_COPY; }
    return S_OK;
}
static IDropTargetVtbl g_dtVtbl = { DT_QI, DT_AddRef, DT_Release, DT_Enter, DT_Over, DT_Leave, DT_Drop };
static IDropTarget g_dt = { &g_dtVtbl };
static HRESULT g_hookHr;
static void hook_drop(HWND h) { RevokeDragDrop(h); g_hookHr = RegisterDragDrop(h, &g_dt); }

/* ---------- inline Markdown -> RTF ---------- */
static int skip_code(const wchar_t *s, int i, int n) {
    int k = 0, j;
    while (i + k < n && s[i + k] == '`') k++;
    j = i + k;
    while (j < n) {
        if (s[j] == '`') { int r = 0; while (j + r < n && s[j + r] == '`') r++; if (r == k) return j + r; j += r; }
        else j++;
    }
    return -1;
}
static int find_close(const wchar_t *s, int from, int n, wchar_t c, int d) {
    int j = from;
    while (j < n) {
        wchar_t x = s[j];
        if (x == '\\' && j + 1 < n) { j += 2; continue; }
        if (x == '`') { int e = skip_code(s, j, n); if (e > 0) { j = e; continue; } while (j < n && s[j] == '`') j++; continue; }
        if (x == c) {
            int r = 0; while (j + r < n && s[j + r] == c) r++;
            if (r == d && j > from && !is_sp(s[j - 1]) && (c != '_' || j + r >= n || !is_alnum_w(s[j + r]))) return j;
            j += r; continue;
        }
        j++;
    }
    return -1;
}
static int parse_link(const wchar_t *s, int i, int n, int *tEnd, int *us, int *ue, int *next) {
    int depth = 0, j = i, k, pd = 1;
    for (; j < n; j++) {
        if (s[j] == '\\') { j++; continue; }
        if (s[j] == '`') { int e = skip_code(s, j, n); if (e > 0) { j = e - 1; continue; } }
        if (s[j] == '[') depth++;
        else if (s[j] == ']') { if (--depth == 0) break; }
    }
    if (j + 1 >= n || s[j + 1] != '(') return 0;
    for (k = j + 2; k < n; k++) {
        if (s[k] == '\\') { k++; continue; }
        if (s[k] == '(') pd++;
        else if (s[k] == ')') { if (--pd == 0) break; }
    }
    if (k >= n) return 0;
    *tEnd = j; *us = j + 2; *ue = k; *next = k + 1; return 1;
}
static void inl(SB *b, const wchar_t *s, int n);

static void emit_link(SB *b, const wchar_t *text, int tn, const wchar_t *url, int un) {
    wchar_t *u;
    while (un > 0 && is_sp(url[0])) { url++; un--; }
    while (un > 0 && is_sp(url[un - 1])) un--;
    if (un > 0 && url[0] == '<') { int k = 1; while (k < un && url[k] != '>') k++; url++; un = k - 1; }
    else { int k = 0; while (k < un && !is_sp(url[k])) k++; un = k; }
    if (un < 0) un = 0;
    if (g_inLink) { inl(b, text, tn); return; }
    u = (wchar_t *)malloc((un + 1) * sizeof(wchar_t)); memcpy(u, url, un * sizeof(wchar_t)); u[un] = 0;
    if (g_nlinks == g_caplinks) { g_caplinks = g_caplinks * 2 + 32; g_links = (Link *)realloc(g_links, g_caplinks * sizeof(Link)); }
    g_links[g_nlinks].a = g_links[g_nlinks].b = -1; g_links[g_nlinks].url = u; g_nlinks++;
    sb_f(b, "{\\cf2\\ul \\u%d?", (int)(short)S_LINK_A);
    g_inLink = 1; inl(b, text, tn); g_inLink = 0;
    sb_f(b, "\\u%d?}", (int)(short)S_LINK_B);
}

static void sb_ws(SB *b, const wchar_t *s, int n) { int i; if (n < 0) n = (int)wcslen(s); for (i = 0; i < n; i++) sb_wc(b, s[i]); }
static void img_note(SB *b, const wchar_t *msg, const wchar_t *what, int wn) {
    sb_s(b, "{\\i\\cf6 ["); sb_ws(b, msg, -1);
    if (what && wn > 0) { sb_s(b, ": "); sb_ws(b, what, wn); }
    sb_s(b, "]}");
}
/* wantPx: >0 sirka v px, <0 percento sirky, 0 prirodzena velkost */
static void emit_image(SB *b, const wchar_t *src, int sn, const wchar_t *alt, int an, int wantPx) {
    wchar_t loc[1100]; int kind, k; Img *m; double tw, th, mx;
    while (sn > 0 && is_sp(*src)) { src++; sn--; }
    while (sn > 0 && is_sp(src[sn - 1])) sn--;
    if (sn > 0 && src[0] == '<') { k = 1; while (k < sn && src[k] != '>') k++; src++; sn = k - 1; }
    else { k = 0; while (k < sn && !is_sp(src[k])) k++; sn = k; }          /* bez "titulku" */
    if (sn <= 0) { img_note(b, L"obrázok", alt, an); return; }
    kind = resolve_ref(src, sn, loc, 1100);
    if (!kind) { img_note(b, L"obrázok sa zobrazí po uložení dokumentu", src, sn); return; }
    k = img_get(loc, kind); m = &g_img[k];
    if (m->state == 0) { img_note(b, L"načítavam obrázok\x2026", NULL, 0); return; }
    if (m->state == 2) { img_note(b, kind == 1 ? L"obrázok sa nenašiel" : L"obrázok sa nepodarilo stiahnuť", src, sn); return; }
    mx = (g_cellTw > 0 ? g_cellTw : g_availTw - g_curLi) - 60; if (mx < 600) mx = 600;
    tw = wantPx > 0 ? wantPx * 15.0 : wantPx < 0 ? mx * -wantPx / 100.0 : m->w * 15.0;   /* 1 px = 15 twips */
    th = tw * m->h / m->w;
    if (tw > mx) { th = th * mx / tw; tw = mx; }
    if (g_nuse == g_capuse) { g_capuse = g_capuse * 2 + 16; g_use = (ImgUse *)realloc(g_use, g_capuse * sizeof(ImgUse)); }
    g_use[g_nuse].idx = k; g_use[g_nuse].w = (LONG)(tw * 2540 / 1440); g_use[g_nuse].h = (LONG)(th * 2540 / 1440); g_nuse++;
    sb_f(b, "\\u%d?", (int)(short)S_IMG);
}

/* ---------- HTML znacky v texte ---------- */
static int is_tag_ch(wchar_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
/* s[i] == '<': rozpozna znacku alebo komentar; nm = nazov ("!--" pre komentar), cl = uzatvaracia, end = index za '>' */
static int tag_parse(const wchar_t *s, int i, int n, wchar_t *nm, int cap, int *cl, int *end) {
    int j = i + 1, k = 0; wchar_t q = 0;
    *cl = 0; nm[0] = 0;
    if (j + 2 < n && s[j] == '!' && s[j + 1] == '-' && s[j + 2] == '-') {
        for (j += 3; j + 2 < n; j++) if (s[j] == '-' && s[j + 1] == '-' && s[j + 2] == '>') { lstrcpynW(nm, L"!--", cap); *end = j + 3; return 1; }
        return 0;
    }
    if (j < n && s[j] == '/') { *cl = 1; j++; }
    if (j >= n || !((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) return 0;
    while (j < n && is_tag_ch(s[j]) && k < cap - 1) nm[k++] = s[j++];
    nm[k] = 0;
    if (j < n && !is_sp(s[j]) && s[j] != '>' && s[j] != '/') return 0;
    for (; j < n; j++) {
        if (q) { if (s[j] == q) q = 0; }
        else if (s[j] == '"' || s[j] == '\'') q = s[j];
        else if (s[j] == '>') { *end = j + 1; return 1; }
        else if (s[j] == '<') return 0;
    }
    return 0;
}
static int tag_attr(const wchar_t *s, int a, int e, const wchar_t *name, wchar_t *out, int cap) {
    int nl = (int)wcslen(name), j;
    for (j = a + 1; j + nl < e; j++) {
        if (is_sp(s[j - 1]) && _wcsnicmp(s + j, name, nl) == 0) {
            int p = j + nl, k = 0; wchar_t q = 0;
            while (p < e && is_sp(s[p])) p++;
            if (p >= e || s[p] != '=') continue;
            p++; while (p < e && is_sp(s[p])) p++;
            if (p < e && (s[p] == '"' || s[p] == '\'')) q = s[p++];
            while (p < e && k < cap - 1 && (q ? s[p] != q : (!is_sp(s[p]) && s[p] != '>'))) out[k++] = s[p++];
            if (!q && k > 0 && out[k - 1] == '/') k--;
            out[k] = 0; return 1;
        }
    }
    return 0;
}
static const struct { const wchar_t *t; const char *on, *off; } TAG_FMT[] = {
    { L"b", "\\b ", "\\b0 " }, { L"strong", "\\b ", "\\b0 " }, { L"i", "\\i ", "\\i0 " }, { L"em", "\\i ", "\\i0 " },
    { L"u", "\\ul ", "\\ulnone " }, { L"ins", "\\ul ", "\\ulnone " }, { L"s", "\\strike ", "\\strike0 " },
    { L"del", "\\strike ", "\\strike0 " }, { L"strike", "\\strike ", "\\strike0 " },
    { L"sup", "\\super ", "\\nosupersub " }, { L"sub", "\\sub ", "\\nosupersub " }, { NULL, NULL, NULL } };
/* znacky, ktore sa len vynechaju (obsah medzi nimi sa zobrazi) */
static const wchar_t *const TAG_SKIP[] = { L"span", L"div", L"p", L"center", L"small", L"big", L"mark", L"details", L"summary",
    L"font", L"section", L"article", L"kbd", L"code", L"tt", L"abbr", L"cite", L"figure", L"figcaption", L"picture", L"source",
    L"hr", L"nav", L"header", L"footer", L"main", L"aside", L"dl", L"dt", L"dd", L"var", L"samp", L"q", NULL };
static int tag_known(const wchar_t *nm) {
    int i;
    if (!wcscmp(nm, L"!--") || !_wcsicmp(nm, L"a") || !_wcsicmp(nm, L"img") || !_wcsicmp(nm, L"br")) return 1;
    for (i = 0; TAG_FMT[i].t; i++) if (!_wcsicmp(nm, TAG_FMT[i].t)) return 1;
    for (i = 0; TAG_SKIP[i]; i++) if (!_wcsicmp(nm, TAG_SKIP[i])) return 1;
    return 0;
}
static int find_ci(const wchar_t *s, int from, int n, const wchar_t *pat) {
    int k = (int)wcslen(pat), j;
    for (j = from; j + k <= n; j++) if (_wcsnicmp(s + j, pat, k) == 0) return j;
    return -1;
}
static void add_head(const wchar_t *slug) {
    if (g_nheads == g_capheads) { g_capheads = g_capheads * 2 + 32; g_heads = (Head *)realloc(g_heads, g_capheads * sizeof(Head)); }
    g_heads[g_nheads].pos = -1; lstrcpynW(g_heads[g_nheads].slug, slug, 128); g_nheads++;
}
/* spracuje znamu HTML znacku na s[i]; vrati index za nou, alebo -1 ak to nie je znama znacka */
static int html_tag(SB *b, const wchar_t *s, int i, int n) {
    wchar_t nm[16], v[1100]; int cl, e, k;
    if (!tag_parse(s, i, n, nm, 16, &cl, &e) || !tag_known(nm)) return -1;
    if (!wcscmp(nm, L"!--")) return e;
    if (!_wcsicmp(nm, L"a")) {
        if (cl) return e;
        if (tag_attr(s, i, e, L"href", v, 1100)) {        /* <a href="...">text</a> */
            int c2 = find_ci(s, e, n, L"</a>");
            if (c2 >= 0) {
                wchar_t u[1104];
                if (wcschr(v, ' ')) _snwprintf(u, 1103, L"<%s>", v); else lstrcpynW(u, v, 1104);
                u[1103] = 0;
                emit_link(b, s + e, c2 - e, u, (int)wcslen(u));
                return c2 + 4;
            }
            return e;
        }
        /* <a id="..."> / <a name="...">: neviditelna kotva, na ktoru vedu odkazy #... */
        if ((tag_attr(s, i, e, L"id", v, 128) || tag_attr(s, i, e, L"name", v, 128)) && v[0]) {
            add_head(v); sb_f(b, "\\u%d?", (int)(short)S_HEAD);
        }
        return e;
    }
    if (!_wcsicmp(nm, L"img")) {
        wchar_t alt[256] = L"", wv[32]; int w = 0;
        if (cl) return e;
        tag_attr(s, i, e, L"alt", alt, 256);
        if (tag_attr(s, i, e, L"width", wv, 32)) { w = _wtoi(wv); if (wcschr(wv, '%')) w = -w; }
        if (tag_attr(s, i, e, L"src", v, 1100)) emit_image(b, v, (int)wcslen(v), alt, (int)wcslen(alt), w);
        return e;
    }
    if (!_wcsicmp(nm, L"br")) { sb_s(b, "\\line "); return e; }
    for (k = 0; TAG_FMT[k].t; k++) if (!_wcsicmp(nm, TAG_FMT[k].t)) { sb_s(b, cl ? TAG_FMT[k].off : TAG_FMT[k].on); return e; }
    return e;   /* TAG_SKIP */
}
/* odsek obsahuje len neviditelne znacky (kotvy, komentare, <div>...) */
static int only_tags(const wchar_t *s, int n) {
    int i = 0, cl, e; wchar_t nm[16], v[8];
    while (i < n) {
        if (is_sp(s[i]) || s[i] == S_BREAK) { i++; continue; }
        if (s[i] == '<' && tag_parse(s, i, n, nm, 16, &cl, &e) && tag_known(nm) && _wcsicmp(nm, L"img") && _wcsicmp(nm, L"br")
            && !(!_wcsicmp(nm, L"a") && !cl && tag_attr(s, i, e, L"href", v, 8))) { i = e; continue; }
        return 0;
    }
    return 1;
}

static void inl(SB *b, const wchar_t *s, int n) {
    int i = 0;
    while (i < n) {
        wchar_t c = s[i];
        if (c == '\\' && i + 1 < n && is_punct_a(s[i + 1])) { sb_wc(b, s[i + 1]); i += 2; continue; }
        if (c == S_BREAK) { sb_s(b, "\\line "); i++; continue; }
        if (c == '`') {
            int k = 0, e, a, z;
            while (i + k < n && s[i + k] == '`') k++;
            e = skip_code(s, i, n);
            if (e < 0) { int q; for (q = 0; q < k; q++) sb_wc(b, '`'); i += k; continue; }
            a = i + k; z = e - k;
            if (z - a >= 2 && s[a] == ' ' && s[z - 1] == ' ') { a++; z--; }
            sb_f(b, "{\\f1\\fs%d\\cf7\\highlight3 ", g_fs - 2);
            for (; a < z; a++) sb_wc(b, s[a] == S_BREAK ? ' ' : s[a]);
            sb_s(b, "}"); i = e; continue;
        }
        if ((c == '!' && i + 1 < n && s[i + 1] == '[') || c == '[') {
            int img = (c == '!'), st = i + img, tEnd, us, ue, next;
            if (parse_link(s, st, n, &tEnd, &us, &ue, &next)) {
                if (img) emit_image(b, s + us, ue - us, s + st + 1, tEnd - st - 1, 0);
                else emit_link(b, s + st + 1, tEnd - st - 1, s + us, ue - us);
                i = next; continue;
            }
            sb_wc(b, c); i++; continue;
        }
        if (c == '<') {
            int k = is_br(s, i, n), j;
            if (k) { sb_s(b, "\\line "); i += k; continue; }
            if (starts_i(s + i + 1, n - i - 1, L"http://") || starts_i(s + i + 1, n - i - 1, L"https://") || starts_i(s + i + 1, n - i - 1, L"mailto:")) {
                for (j = i + 1; j < n && s[j] != '>' && !is_sp(s[j]); j++) { }
                if (j < n && s[j] == '>') { emit_link(b, s + i + 1, j - i - 1, s + i + 1, j - i - 1); i = j + 1; continue; }
            }
            if ((j = html_tag(b, s, i, n)) > i) { i = j; continue; }
            sb_wc(b, c); i++; continue;
        }
        if (c == 'h' && !g_inLink && (i == 0 || !is_alnum_w(s[i - 1])) &&
            (starts_i(s + i, n - i, L"http://") || starts_i(s + i, n - i, L"https://"))) {
            int j = i;
            while (j < n && !is_sp(s[j]) && s[j] != '<' && s[j] != '>' && s[j] != '"' && s[j] != S_BREAK && s[j] != '|') j++;
            while (j > i && wcschr(L".,;:!?)*'_", s[j - 1])) j--;
            if (j - i > 8) {
                int q, save;
                /* text URL vypiseme doslovne, nie cez inl() */
                wchar_t *u = (wchar_t *)malloc((j - i + 1) * sizeof(wchar_t)); memcpy(u, s + i, (j - i) * sizeof(wchar_t)); u[j - i] = 0;
                if (g_nlinks == g_caplinks) { g_caplinks = g_caplinks * 2 + 32; g_links = (Link *)realloc(g_links, g_caplinks * sizeof(Link)); }
                g_links[g_nlinks].a = g_links[g_nlinks].b = -1; g_links[g_nlinks].url = u; g_nlinks++;
                sb_f(b, "{\\cf2\\ul \\u%d?", (int)(short)S_LINK_A);
                save = j; for (q = i; q < save; q++) sb_wc(b, s[q]);
                sb_f(b, "\\u%d?}", (int)(short)S_LINK_B);
                i = j; continue;
            }
        }
        if (c == '&') {
            static const struct { const wchar_t *e; wchar_t c; } ent[] = {
                { L"&amp;", '&' }, { L"&lt;", '<' }, { L"&gt;", '>' }, { L"&quot;", '"' }, { L"&nbsp;", 0xA0 } };
            int q, hit = 0;
            for (q = 0; q < 5; q++) if (starts_i(s + i, n - i, ent[q].e)) { sb_wc(b, ent[q].c); i += (int)wcslen(ent[q].e); hit = 1; break; }
            if (hit) continue;
            sb_wc(b, c); i++; continue;
        }
        if (c == '*' || c == '_') {
            int k = 0, d, done = 0; wchar_t prev, next;
            while (i + k < n && s[i + k] == c) k++;
            prev = i > 0 ? s[i - 1] : ' '; next = i + k < n ? s[i + k] : ' ';
            if (!is_sp(next) && next != S_BREAK && (c == '*' || !is_alnum_w(prev))) {
                for (d = k > 3 ? 3 : k; d >= 1 && !done; d--) {
                    int j = find_close(s, i + d, n, c, d);
                    if (j >= 0) {
                        int q; for (q = 0; q < k - d; q++) sb_wc(b, c);
                        sb_s(b, d == 3 ? "{\\b\\i " : d == 2 ? "{\\b " : "{\\i ");
                        inl(b, s + i + k, j - (i + k)); sb_s(b, "}");
                        i = j + d; done = 1;
                    }
                }
            }
            if (!done) { int q; for (q = 0; q < k; q++) sb_wc(b, c); i += k; }
            continue;
        }
        if (c == '~' && i + 1 < n && s[i + 1] == '~') {
            int j = find_close(s, i + 2, n, '~', 2);
            if (j >= 0) { sb_s(b, "{\\strike "); inl(b, s + i + 2, j - i - 2); sb_s(b, "}"); i = j + 2; continue; }
            sb_s(b, "~~"); i += 2; continue;
        }
        sb_wc(b, c); i++;
    }
}

/* ---------- blokovy parser ---------- */
typedef struct { const wchar_t *s; int len; } Line;
typedef struct { int indent, ordered, num, off; } LI;

static int ln_indent(Line l, int *off) {
    int i = 0, col = 0;
    while (i < l.len && is_sp(l.s[i])) { col += l.s[i] == '\t' ? 4 - (col % 4) : 1; i++; }
    if (off) *off = i; return col;
}
static int ln_blank(Line l) { int o; ln_indent(l, &o); return o >= l.len; }
static int ln_fence(Line l, wchar_t *ch, int *cnt, int *off) {
    int o, k = 0, i; wchar_t c;
    ln_indent(l, &o);
    if (o >= l.len || (l.s[o] != '`' && l.s[o] != '~')) return 0;
    c = l.s[o]; while (o + k < l.len && l.s[o + k] == c) k++;
    if (k < 3) return 0;
    if (c == '`') for (i = o + k; i < l.len; i++) if (l.s[i] == '`') return 0;
    *ch = c; *cnt = k; if (off) *off = o; return 1;
}
static int ln_fence_close(Line l, wchar_t c, int k) {
    int o, r = 0, i;
    ln_indent(l, &o);
    while (o + r < l.len && l.s[o + r] == c) r++;
    if (r < k) return 0;
    for (i = o + r; i < l.len; i++) if (!is_sp(l.s[i])) return 0;
    return 1;
}
static int ln_heading(Line l, int *off) {
    int o, k = 0;
    if (ln_indent(l, &o) > 3) return 0;
    while (o + k < l.len && l.s[o + k] == '#') k++;
    if (k < 1 || k > 6) return 0;
    if (o + k < l.len && !is_sp(l.s[o + k])) return 0;
    *off = o + k; return k;
}
static int ln_hr(Line l) {
    int o, k = 0, i; wchar_t c;
    if (ln_indent(l, &o) > 3 || o >= l.len) return 0;
    c = l.s[o]; if (c != '-' && c != '*' && c != '_') return 0;
    for (i = o; i < l.len; i++) { if (l.s[i] == c) k++; else if (!is_sp(l.s[i])) return 0; }
    return k >= 3;
}
static int ln_quote(Line l, int *off) {
    int o;
    if (ln_indent(l, &o) > 3) return 0;
    if (o < l.len && l.s[o] == '>') { o++; if (o < l.len && l.s[o] == ' ') o++; *off = o; return 1; }
    return 0;
}
static int ln_list(Line l, LI *it) {
    int o, p, ind = ln_indent(l, &o); wchar_t c;
    if (o >= l.len) return 0;
    c = l.s[o]; p = o; it->num = 0;
    if (c == '-' || c == '*' || c == '+') { p++; it->ordered = 0; }
    else if (c >= '0' && c <= '9') {
        int v = 0, k = 0;
        while (p < l.len && l.s[p] >= '0' && l.s[p] <= '9' && k < 9) { v = v * 10 + (l.s[p] - '0'); p++; k++; }
        if (p >= l.len || (l.s[p] != '.' && l.s[p] != ')')) return 0;
        p++; it->ordered = 1; it->num = v;
    } else return 0;
    if (p < l.len && !is_sp(l.s[p])) return 0;
    while (p < l.len && is_sp(l.s[p])) p++;
    it->indent = ind; it->off = p; return 1;
}
static int ln_haspipe(Line l) { int i; for (i = 0; i < l.len; i++) if (l.s[i] == '|') return 1; return 0; }
static int ln_tdelim(Line l) {
    int i, pipes = 0, dash = 0;
    for (i = 0; i < l.len; i++) {
        wchar_t c = l.s[i];
        if (c == '|') pipes++; else if (c == '-') dash++; else if (c != ':' && !is_sp(c)) return 0;
    }
    return pipes >= 1 && dash >= 1;
}
static int starts_block(Line l) {
    wchar_t c; int k, o; LI it;
    if (ln_fence(l, &c, &k, NULL) || ln_heading(l, &o) || ln_hr(l) || ln_quote(l, &o)) return 1;
    if (ln_list(l, &it) && it.off < l.len && (!it.ordered || it.num == 1)) return 1;
    return 0;
}
static int table_at(Line *L, int i, int n) {
    return i + 1 < n && ln_haspipe(L[i]) && ln_tdelim(L[i + 1]);
}
/* prida riadok do odstavca; dve medzery / backslash na konci = tvrdy zlom */
static void para_add(WB *w, const wchar_t *s, int len) {
    int a = 0, e = len, hard = 0;
    while (a < e && is_sp(s[a])) a++;
    while (e > a && is_sp(s[e - 1])) e--;
    if (len - e >= 2) hard = 1;
    else if (e > a && s[e - 1] == '\\') { hard = 1; e--; }
    wb_add(w, s + a, e - a);
    wb_ch(w, hard ? S_BREAK : ' ');
}
static void para_trim(WB *w) { while (w->n > 0 && (w->p[w->n - 1] == ' ' || w->p[w->n - 1] == S_BREAK)) w->n--; }

static void emit_rule(SB *b, int li) {
    int right = g_availTw; if (right < li + 600) right = li + 600;
    sb_f(b, "\\trowd\\trgaph0\\trrh-20\\trleft%d\\clcbpat4\\cellx%d\\pard\\intbl\\plain\\f0\\fs2\\cf1\\cell\\row\\pard\\plain\\f0\\fs8\\par\n", li, right);
}

static int src_line(const wchar_t *p) {
    int off = (int)(p - g_srcBase), lo = 0, hi = g_nLines - 1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (g_lineStart[mid] <= off) lo = mid; else hi = mid - 1; }
    return lo;
}
static void emit_blk(SB *b, const wchar_t *p) {
    if (!g_srcBase || g_nLines <= 0) return;
    if (g_nblk == g_capblk) { g_capblk = g_capblk * 2 + 64; g_blk = (Blk *)realloc(g_blk, g_capblk * sizeof(Blk)); }
    g_blk[g_nblk].line = src_line(p); g_blk[g_nblk].cp = -1; g_nblk++;
    sb_f(b, "\\u%d?", (int)(short)S_BLOCK);
}

#define MAXC 48
static int split_cells(const wchar_t *s, int n, int *st, int *en, int max) {
    int a = 0, b = n, c = 0, cs, i;
    while (a < b && is_sp(s[a])) a++;
    while (b > a && is_sp(s[b - 1])) b--;
    if (a < b && s[a] == '|') a++;
    if (b > a && s[b - 1] == '|' && !(b - 2 >= a && s[b - 2] == '\\')) b--;
    cs = a;
    for (i = a; i <= b; i++) {
        if (i + 1 < b && s[i] == '\\') { i++; continue; }
        if (i < b && s[i] == '`') { int e = skip_code(s, i, b); if (e > 0) { i = e - 1; continue; } }
        if (i == b || s[i] == '|') {
            if (c < max) {
                int x = cs, y = i;
                while (x < y && is_sp(s[x])) x++;
                while (y > x && is_sp(s[y - 1])) y--;
                st[c] = x; en[c] = y; c++;
            }
            cs = i + 1;
        }
    }
    return c;
}
/* odhad viditelnej dlzky bunky (najdlhsi riadok) a najdlhsieho slova */
static void cell_metrics(const wchar_t *s, int a, int b, int *line, int *word) {
    int best = 0, cur = 0, wbest = 0, wcur = 0, i;
    for (i = a; i < b; i++) {
        wchar_t c = s[i]; int k;
        if (c == '<' && (k = is_br(s, i, b)) != 0) { if (cur > best) best = cur; if (wcur > wbest) wbest = wcur; cur = wcur = 0; i += k - 1; continue; }
        if (c == '*' || c == '`') continue;
        if (c == ']' && i + 1 < b && s[i + 1] == '(') { while (i < b && s[i] != ')') i++; continue; }
        cur++;
        if (is_sp(c)) { if (wcur > wbest) wbest = wcur; wcur = 0; } else wcur++;
    }
    if (cur > best) best = cur; if (wcur > wbest) wbest = wcur;
    *line = best; *word = wbest;
}

static void emit_table(SB *b, Line *L, int i0, int i1, int li) {
    int st[MAXC], en[MAXC], al[MAXC], nat[MAXC], minw[MAXC], wd[MAXC];
    int nc, c, r, k, avail, pad = FS(300), x;
    double cw = g_fs * 5.5;
    long sum = 0, summin = 0;
    char cellb[] = "\\clvertalc\\clbrdrt\\brdrs\\brdrw10\\brdrcf4\\clbrdrl\\brdrs\\brdrw10\\brdrcf4\\clbrdrb\\brdrs\\brdrw10\\brdrcf4\\clbrdrr\\brdrs\\brdrw10\\brdrcf4";
    Line h = L[i0], d = L[i0 + 1];

    nc = split_cells(h.s, h.len, st, en, MAXC);
    if (nc < 1) return;
    for (c = 0; c < nc; c++) {
        int ln, wl; cell_metrics(h.s, st[c], en[c], &ln, &wl);
        nat[c] = (int)(ln * cw * 1.12) + pad; minw[c] = (int)(wl * cw * 1.12) + pad; al[c] = 0;
    }
    { int ds[MAXC], de[MAXC]; k = split_cells(d.s, d.len, ds, de, MAXC);
      for (c = 0; c < k && c < nc; c++) {
          int l_ = de[c] > ds[c] && d.s[ds[c]] == ':', r_ = de[c] > ds[c] && d.s[de[c] - 1] == ':';
          al[c] = (l_ && r_) ? 1 : r_ ? 2 : 0;
      } }
    for (r = i0 + 2; r < i1; r++) {
        int cs[MAXC], ce[MAXC]; k = split_cells(L[r].s, L[r].len, cs, ce, MAXC);
        for (c = 0; c < k && c < nc; c++) {
            int ln, wl, v, m; cell_metrics(L[r].s, cs[c], ce[c], &ln, &wl);
            if (wl > 22) wl = 22;
            v = (int)(ln * cw) + pad; m = (int)(wl * cw) + pad;
            if (v > nat[c]) nat[c] = v; if (m > minw[c]) minw[c] = m;
        }
    }
    avail = g_availTw - li; if (avail < 1500) avail = 1500;
    for (c = 0; c < nc; c++) { if (minw[c] > nat[c]) minw[c] = nat[c]; sum += nat[c]; summin += minw[c]; }
    if (sum <= avail) { for (c = 0; c < nc; c++) wd[c] = nat[c]; }
    else if (summin >= avail) { for (c = 0; c < nc; c++) wd[c] = (int)((double)minw[c] * avail / summin); }
    else {
        int lo = 0, hi = 0, it;
        for (c = 0; c < nc; c++) if (nat[c] > hi) hi = nat[c];
        for (it = 0; it < 24; it++) {
            int mid = (lo + hi) / 2; long sm = 0;
            for (c = 0; c < nc; c++) { int v = nat[c] < mid ? nat[c] : mid; if (v < minw[c]) v = minw[c]; sm += v; }
            if (sm <= avail) lo = mid; else hi = mid;
        }
        for (c = 0; c < nc; c++) { int v = nat[c] < lo ? nat[c] : lo; if (v < minw[c]) v = minw[c]; wd[c] = v; }
    }
    for (r = i0; r < i1; r++) {
        int cs[MAXC], ce[MAXC], hdr = (r == i0);
        if (r == i0 + 1) continue;
        k = split_cells(L[r].s, L[r].len, cs, ce, MAXC);
        sb_f(b, "\\trowd\\trgaph%d\\trleft%d\\trpaddl%d\\trpaddr%d\\trpaddt%d\\trpaddb%d\\trpaddfl3\\trpaddfr3\\trpaddft3\\trpaddfb3",
             FS(110), li, FS(110), FS(110), FS(50), FS(50));
        x = li;
        for (c = 0; c < nc; c++) { x += wd[c]; sb_s(b, cellb); if (hdr) sb_s(b, "\\clcbpat5"); sb_f(b, "\\cellx%d", x); }
        for (c = 0; c < nc; c++) {
            sb_f(b, "\\pard\\intbl\\%s\\sl276\\slmult1{\\plain\\f0\\fs%d\\cf1 %s", al[c] == 1 ? "qc" : al[c] == 2 ? "qr" : "ql", g_fs, hdr ? "\\b " : "");
            if (c == 0) emit_blk(b, L[r].s);
            g_cellTw = wd[c] - 2 * FS(110); g_curLi = li;
            if (c < k) inl(b, L[r].s + cs[c], ce[c] - cs[c]);
            g_cellTw = 0;
            sb_s(b, "}\\cell ");
        }
        sb_s(b, "\\row\n");
    }
    sb_s(b, "\\pard\\plain\\f0\\fs10\\par\n");
    g_hasTable = 1;
}

static void make_slug(const wchar_t *s, int n, wchar_t *out, int cap) {
    int i, k = 0;
    for (i = 0; i < n && k < cap - 1; i++) {
        wchar_t c = s[i];
        if (is_alnum_w(c) || c == '_') out[k++] = c;
        else if (c == ' ' || c == '-') out[k++] = '-';
    }
    out[k] = 0; CharLowerBuffW(out, k);
}

static void blocks(SB *b, Line *L, int n, int li, int quote) {
    static const int hs[6] = { 40, 32, 27, 24, 22, 20 };
    int stk[16], depth = 0, i = 0, cf = quote ? 6 : 1;
    WB w = { 0 };
    while (i < n) {
        Line l = L[i]; int off, h, fk, foff; wchar_t fc; LI it;
        if (ln_blank(l)) { i++; continue; }
        {   /* HTML komentar cez viac riadkov <!-- ... --> sa nezobrazuje */
            int o; ln_indent(l, &o);
            if (l.len - o >= 4 && !wcsncmp(l.s + o, L"<!--", 4)) {
                int j = i, f = -1;
                for (; j < n && f < 0; j++) f = find_ci(L[j].s, j == i ? o + 4 : 0, L[j].len, L"-->");
                if (f >= 0 && j - 1 > i) { i = j; continue; }
            }
        }
        if (ln_fence(l, &fc, &fk, &foff)) {
            int j, first = 1, right = g_availTw; if (right < li + 1200) right = li + 1200;
            depth = 0;
            sb_f(b, "\\trowd\\trgaph%d\\trleft%d\\trpaddl%d\\trpaddr%d\\trpaddt%d\\trpaddb%d\\trpaddfl3\\trpaddfr3\\trpaddft3\\trpaddfb3\\clbrdrt\\brdrs\\brdrw10\\brdrcf4\\clbrdrl\\brdrs\\brdrw10\\brdrcf4\\clbrdrb\\brdrs\\brdrw10\\brdrcf4\\clbrdrr\\brdrs\\brdrw10\\brdrcf4\\clcbpat3\\cellx%d\\pard\\intbl\\plain\\f1\\fs%d\\cf1\\sl290\\slmult1 ",
                 FS(150), li, FS(150), FS(150), FS(90), FS(90), right, g_fs - 2);
            for (j = i + 1; j < n; j++) {
                int q = 0, strip = foff;
                if (ln_fence_close(L[j], fc, fk)) break;
                if (!first) sb_s(b, "\\par "); first = 0;
                emit_blk(b, L[j].s);
                while (q < L[j].len && strip > 0 && is_sp(L[j].s[q])) { q++; strip--; }
                for (; q < L[j].len; q++) { if (L[j].s[q] == '\t') sb_s(b, "    "); else sb_wc(b, L[j].s[q]); }
            }
            sb_s(b, "\\cell\\row\\pard\\plain\\f0\\fs10\\par\n");
            i = j < n ? j + 1 : j; continue;
        }
        h = ln_heading(l, &off);
        if (h) {
            int a = off, e = l.len;
            depth = 0;
            while (a < e && is_sp(l.s[a])) a++;
            while (e > a && is_sp(l.s[e - 1])) e--;
            { int e2 = e; while (e2 > a && l.s[e2 - 1] == '#') e2--; if (e2 < e && (e2 == a || is_sp(l.s[e2 - 1]))) { e = e2; while (e > a && is_sp(l.s[e - 1])) e--; } }
            if (g_nheads == g_capheads) { g_capheads = g_capheads * 2 + 32; g_heads = (Head *)realloc(g_heads, g_capheads * sizeof(Head)); }
            g_heads[g_nheads].pos = -1; make_slug(l.s + a, e - a, g_heads[g_nheads].slug, 128); g_nheads++;
            sb_f(b, "\\pard\\plain\\f0\\b\\fs%d\\cf%d\\li%d\\sb%d\\sa%d\\keepn \\u%d?", FS(hs[h - 1]), cf, li,
                 h <= 2 ? FS(280) : FS(220), h <= 2 ? FS(60) : FS(110), (int)(short)S_HEAD);
            emit_blk(b, l.s);
            g_curLi = li;
            inl(b, l.s + a, e - a);
            sb_s(b, "\\par\n");
            if (h <= 2) emit_rule(b, li);
            i++; continue;
        }
        if (ln_hr(l)) { depth = 0; sb_s(b, "\\pard\\plain\\f0\\fs10\\par\n"); emit_rule(b, li); i++; continue; }
        if (ln_quote(l, &off)) {
            int j = i, cnt = 0, o; Line *sub = (Line *)malloc(sizeof(Line) * (n - i));
            depth = 0;
            while (j < n) {
                if (ln_quote(L[j], &o)) { sub[cnt].s = L[j].s + o; sub[cnt].len = L[j].len - o; cnt++; j++; }
                else if (!ln_blank(L[j]) && !starts_block(L[j])) { sub[cnt++] = L[j]; j++; }
                else break;
            }
            blocks(b, sub, cnt, li + FS(400), 1); free(sub); i = j; continue;
        }
        if (ln_list(l, &it)) {
            int level, j, k, hang, lii, nextList, a = 0; wchar_t mk[16]; LI t2;
            while (depth > 0 && it.indent < stk[depth - 1]) depth--;
            if (depth == 0 || it.indent >= stk[depth - 1] + 2) { if (depth < 16) stk[depth++] = it.indent; }
            level = depth - 1;
            w.n = 0; para_add(&w, l.s + it.off, l.len - it.off);
            for (j = i + 1; j < n && !ln_blank(L[j]) && !starts_block(L[j]) && !ln_list(L[j], &t2) && !table_at(L, j, n); j++) para_add(&w, L[j].s, L[j].len);
            para_trim(&w);
            if (it.ordered) _snwprintf(mk, 16, L"%d.", it.num);
            else wcscpy(mk, level == 0 ? L"\x2022" : level == 1 ? L"\x25E6" : L"\x25AA");
            if (w.n >= 3 && w.p[0] == '[' && w.p[2] == ']' && (w.n == 3 || w.p[3] == ' ') && (w.p[1] == ' ' || w.p[1] == 'x' || w.p[1] == 'X')) {
                wcscpy(mk, w.p[1] == ' ' ? L"\x2610" : L"\x2611"); a = w.n > 3 ? 4 : 3;
            }
            for (k = j; k < n && ln_blank(L[k]); k++) { }
            nextList = k < n && ln_list(L[k], &t2) && !ln_hr(L[k]);
            hang = it.ordered ? FS(460) : FS(320);
            lii = li + FS(160) + level * FS(380) + hang;
            sb_f(b, "\\pard\\plain\\f0\\fs%d\\cf%d\\sl276\\slmult1\\fi-%d\\li%d\\tx%d\\sa%d ", g_fs, cf, hang, lii, lii, nextList ? FS(50) : FS(160));
            emit_blk(b, l.s);
            for (k = 0; mk[k]; k++) sb_wc(b, mk[k]);
            sb_s(b, "\\tab ");
            g_curLi = lii;
            inl(b, w.p + a, w.n - a);
            sb_s(b, "\\par\n");
            i = j; continue;
        }
        depth = 0;
        if (table_at(L, i, n)) {
            int j = i + 2;
            while (j < n && !ln_blank(L[j]) && ln_haspipe(L[j])) j++;
            emit_table(b, L, i, j, li); i = j; continue;
        }
        {   int j = i;
            w.n = 0;
            do { para_add(&w, L[j].s, L[j].len); j++; }
            while (j < n && !ln_blank(L[j]) && !starts_block(L[j]) && !table_at(L, j, n));
            para_trim(&w);
            {   SB t = { 0 }; int nu = g_nuse;
                g_curLi = li;
                emit_blk(&t, L[i].s);
                inl(&t, w.p, w.n);
                if (only_tags(w.p, w.n))   /* napr. samotna kotva <a id="..."></a>: bez medzery v texte */
                    sb_f(b, "\\pard\\plain\\f0\\fs2\\li%d\\sa0\\sb0 ", li);
                else   /* s obrazkom jednoduche riadkovanie, inak by pod nim ostala velka medzera */
                    sb_f(b, "\\pard\\plain\\f0\\fs%d\\cf%d%s\\li%d\\sa%d ", g_fs, cf, g_nuse > nu ? "" : "\\sl276\\slmult1", li, FS(160));
                if (t.p) sb_s(b, t.p);
                free(t.p);
            }
            sb_s(b, "\\par\n");
            i = j;
        }
    }
    free(w.p);
}

/* ---------- ulozene odkazy a naposledy otvorene ----------
   Ukladaju sa do %APPDATA%\MDView\odkazy.txt (UTF-8, riadok = typ<TAB>nazov<TAB>umiestnenie). */
typedef struct { wchar_t name[200], loc[1100]; } Ref;
#define MAXBM 200
#define MAXRC 12
static Ref g_bm[MAXBM], g_rc[MAXRC]; static int g_nbm, g_nrc;
static int refs_file(wchar_t *out) {
    wchar_t d[MAX_PATH];
#ifdef MDV_TEST
    if (_wgetenv(L"MDV_TEST_REFS")) { lstrcpynW(out, _wgetenv(L"MDV_TEST_REFS"), 1100); return 1; }
#endif
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, d))) return 0;
    _snwprintf(out, 1099, L"%s\\MDView", d); out[1099] = 0;
    CreateDirectoryW(out, NULL);
    wcscat(out, L"\\odkazy.txt");
    return 1;
}
static void refs_load(void) {
    wchar_t f[1100], *w, *p; DWORD n; BYTE *buf; int wn;
    g_nbm = g_nrc = 0;
    if (!refs_file(f) || !(buf = read_all(f, 4u << 20, &n))) return;
    wn = MultiByteToWideChar(CP_UTF8, 0, (char *)buf, n, NULL, 0);
    w = (wchar_t *)malloc((wn + 1) * sizeof(wchar_t)); MultiByteToWideChar(CP_UTF8, 0, (char *)buf, n, w, wn); w[wn] = 0; free(buf);
    for (p = w; *p; ) {
        wchar_t *e = p, *t1, *t2;
        while (*e && *e != '\n') e++;
        if (*e) *e++ = 0;
        { size_t L = wcslen(p); if (L && p[L - 1] == '\r') p[L - 1] = 0; }
        if (p[0] == 0xFEFF) p++;
        t1 = wcschr(p, '\t'); t2 = t1 ? wcschr(t1 + 1, '\t') : NULL;
        if (t1 && t2 && t2[1]) {
            Ref *r = NULL;
            if (p[0] == 'B' && g_nbm < MAXBM) r = &g_bm[g_nbm++];
            else if (p[0] == 'R' && g_nrc < MAXRC) r = &g_rc[g_nrc++];
            if (r) { *t2 = 0; lstrcpynW(r->name, t1 + 1, 200); lstrcpynW(r->loc, t2 + 1, 1100); }
        }
        p = e;
    }
    free(w);
}
static void refs_save(void) {
    wchar_t f[1100]; WB w = { 0 }; int i, bn; char *u; HANDLE h; DWORD wr;
    if (!refs_file(f)) return;
    wb_add(&w, L"", 0);
    { const wchar_t *hd = L"# MD View: B = uložený odkaz, R = naposledy otvorený (typ<TAB>názov<TAB>umiestnenie)\r\n"; wb_add(&w, hd, (int)wcslen(hd)); }
    for (i = 0; i < g_nbm + g_nrc; i++) {
        Ref *r = i < g_nbm ? &g_bm[i] : &g_rc[i - g_nbm];
        wb_add(&w, i < g_nbm ? L"B\t" : L"R\t", 2);
        wb_add(&w, r->name, (int)wcslen(r->name)); wb_ch(&w, '\t');
        wb_add(&w, r->loc, (int)wcslen(r->loc)); wb_add(&w, L"\r\n", 2);
    }
    bn = WideCharToMultiByte(CP_UTF8, 0, w.p, w.n, NULL, 0, NULL, NULL);
    u = (char *)malloc(bn + 1); WideCharToMultiByte(CP_UTF8, 0, w.p, w.n, u, bn, NULL, NULL); free(w.p);
    h = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, u, bn, &wr, NULL); CloseHandle(h); }
    free(u);
}
static void ref_set(Ref *r, const wchar_t *name, const wchar_t *loc) {
    wchar_t *p;
    lstrcpynW(r->name, name && name[0] ? name : loc, 200); lstrcpynW(r->loc, loc, 1100);
    for (p = r->name; *p; p++) if (*p == '\t' || *p == '\r' || *p == '\n') *p = ' ';
}
static int bm_find(const wchar_t *loc) {
    int i; if (!loc || !loc[0]) return -1;
    for (i = 0; i < g_nbm; i++) if (_wcsicmp(g_bm[i].loc, loc) == 0) return i;
    return -1;
}
static void bm_add(const wchar_t *name, const wchar_t *loc) {
    int i = bm_find(loc);
    if (i < 0) { if (g_nbm >= MAXBM) return; i = g_nbm++; }
    ref_set(&g_bm[i], name, loc); refs_save();
}
static void bm_del(int i) {
    if (i < 0 || i >= g_nbm) return;
    memmove(&g_bm[i], &g_bm[i + 1], (g_nbm - i - 1) * sizeof(Ref)); g_nbm--; refs_save();
}
static void recent_add(const wchar_t *name, const wchar_t *loc) {
    int i;
    for (i = 0; i < g_nrc; i++) if (_wcsicmp(g_rc[i].loc, loc) == 0) break;
    if (i == g_nrc) i = g_nrc < MAXRC ? g_nrc++ : MAXRC - 1;
    memmove(&g_rc[1], &g_rc[0], i * sizeof(Ref));
    ref_set(&g_rc[0], name, loc); refs_save();
}
/* nazov dokumentu = prvy nadpis "# ..." */
static void doc_title(const wchar_t *w, wchar_t *out, int cap) {
    const wchar_t *p = w; int lines = 0;
    out[0] = 0;
    while (p && *p && lines < 400) {
        const wchar_t *e = p, *q = p;
        while (*e && *e != '\r' && *e != '\n') e++;
        while (q < e && *q == ' ') q++;
        if (e - q > 2 && q[0] == '#' && q[1] == ' ') {
            int k = 0;
            for (q += 2; q < e && k < cap - 1; q++) if (*q != '*' && *q != '_' && *q != '`') out[k++] = *q;
            while (k > 0 && out[k - 1] == ' ') k--;
            out[k] = 0; if (k) return;
        }
        p = e; if (*p == '\r') p++; if (*p == '\n') p++; lines++;
    }
}
static void wb_s(WB *w, const wchar_t *s) { wb_add(w, s, (int)wcslen(s)); }
static void wb_md(WB *w, const wchar_t *s) {   /* text bez markdown formatovania */
    for (; *s; s++) { if (wcschr(L"\\`*_[]<>|#~!", *s)) wb_ch(w, '\\'); wb_ch(w, *s); }
}
static void wb_code(WB *w, const wchar_t *s) {
    wb_ch(w, '`'); for (; *s; s++) wb_ch(w, *s == '`' ? '\'' : *s); wb_ch(w, '`');
}
/* uvodna stranka (ked nie je otvoreny ziadny dokument) */
static wchar_t *build_home(int *outn) {
    WB w = { 0 }; int i; wchar_t t[32];
    wb_s(&w, L"# MD View\n\n");
    wb_s(&w, L"[Otvoriť súbor](mdview:open) \x00B7 [Otvoriť webovú adresu](mdview:url) \x00B7 [Pridať odkaz](mdview:add) \x00B7 [Návod](mdview:help)\n\n");
    wb_s(&w, L"Dokument môžete aj **pretiahnuť do okna** \x2014 súbor z Prieskumníka alebo odkaz z prehliadača.\n\n");
    wb_s(&w, L"## Uložené odkazy\n\n");
    if (!g_nbm) wb_s(&w, L"*Zatiaľ nemáte uložené žiadne odkazy.* Otvorte dokument a kliknite na **\x2606** v hornom paneli, alebo použite [Pridať odkaz](mdview:add).\n\n");
    else {
        wb_s(&w, L"| | Názov | Umiestnenie | |\n| :-: | :-- | :-- | :-: |\n");
        for (i = 0; i < g_nbm; i++) {
            wb_s(&w, is_url(g_bm[i].loc) ? L"| web | [" : L"| PC | [");
            wb_md(&w, g_bm[i].name);
            _snwprintf(t, 31, L"](mdview:bm?%d) | ", i); t[31] = 0; wb_s(&w, t);
            wb_code(&w, g_bm[i].loc);
            _snwprintf(t, 31, L" | [odstrániť](mdview:del?%d) |\n", i); t[31] = 0; wb_s(&w, t);
        }
        wb_s(&w, L"\n");
    }
    if (g_nrc) {
        wb_s(&w, L"## Naposledy otvorené\n\n");
        for (i = 0; i < g_nrc; i++) {
            wb_s(&w, L"- [");
            wb_md(&w, g_rc[i].name);
            _snwprintf(t, 31, L"](mdview:rc?%d) \x2014 ", i); t[31] = 0; wb_s(&w, t);
            wb_code(&w, g_rc[i].loc); wb_s(&w, L"\n");
        }
        wb_s(&w, L"\n");
    }
    wb_s(&w, HOME_KEYS);
    *outn = w.n; return w.p;
}

/* ---------- vykreslenie ---------- */
static void render(int resetScroll);
static void rtf_color(SB *b, COLORREF c) { sb_f(b, "\\red%d\\green%d\\blue%d;", GetRValue(c), GetGValue(c), GetBValue(c)); }

typedef struct { int pos; wchar_t c; } SPos;
static int spos_cmp(const void *a, const void *b) { return ((const SPos *)a)->pos - ((const SPos *)b)->pos; }
static void post_sentinels(void) {
    /* pozicie hladame cez EM_FINDTEXTEX, aby sedeli s indexmi RichEditu aj v tabulkach */
    static const wchar_t sent[5] = { S_LINK_A, S_LINK_B, S_HEAD, S_BLOCK, S_IMG };
    int i, k, removed = 0, li_ = 0, hi_ = 0, bi_ = 0, np = 0, cap = 0, nimg = 0, ui; SPos *pos = NULL;
    for (k = 0; k < 5; k++) {
        wchar_t needle[2]; FINDTEXTEXW ft; int from = 0;
        needle[0] = sent[k]; needle[1] = 0;
        for (;;) {
            ft.chrg.cpMin = from; ft.chrg.cpMax = -1; ft.lpstrText = needle;
            if (SendMessageW(hView, EM_FINDTEXTEXW, FR_DOWN | FR_MATCHCASE, (LPARAM)&ft) < 0) break;
            if (np == cap) { cap = cap * 2 + 64; pos = (SPos *)realloc(pos, cap * sizeof(SPos)); }
            pos[np].pos = ft.chrgText.cpMin; pos[np].c = needle[0]; np++;
            from = ft.chrgText.cpMin + 1;
        }
    }
    if (np) qsort(pos, np, sizeof(SPos), spos_cmp);
    for (i = 0; i < np; i++) {
        int p = pos[i].pos - removed; wchar_t c = pos[i].c;
        if (c == S_IMG) { nimg++; continue; }   /* obrazok zaberie rovnako 1 znak, posun nevznika */
        if (c == S_LINK_A) { if (li_ < g_nlinks) g_links[li_].a = p; }
        else if (c == S_LINK_B) { if (li_ < g_nlinks) g_links[li_++].b = p; }
        else if (c == S_BLOCK) { if (bi_ < g_nblk) g_blk[bi_++].cp = p; }
        else if (hi_ < g_nheads) g_heads[hi_++].pos = p;
        removed++;
    }
    ui = nimg - 1;
    for (i = np - 1; i >= 0; i--) {
        CHARRANGE cr; cr.cpMin = pos[i].pos; cr.cpMax = pos[i].pos + 1;
        if (pos[i].c == S_IMG) {
            if (ui >= 0 && ui < g_nuse) insert_image(pos[i].pos, &g_use[ui]);
            else { SendMessageW(hView, EM_EXSETSEL, 0, (LPARAM)&cr); SendMessageW(hView, EM_REPLACESEL, FALSE, (LPARAM)L"\x25A1"); }
            ui--; continue;
        }
        SendMessageW(hView, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(hView, EM_REPLACESEL, FALSE, (LPARAM)L"");
    }
    free(pos);
}

/* ---------- hladanie bez ohladu na velkost pismen a diakritiku ---------- */
/* Znak sa prevedie na male pismeno bez diakritiky; dlzka textu sa nemeni, takze indexy sedia. */
static wchar_t fold_ch(wchar_t c) {
    static wchar_t tab[0x250]; static int init = 0;
    if (!init) {
        int i;
        for (i = 0; i < 0x250; i++) {
            wchar_t in = (wchar_t)i, out[8], b = (wchar_t)i;
            if (i >= 0x80) {
                int k = FoldStringW(MAP_COMPOSITE, &in, 1, out, 8);
                if (k >= 1 && out[0] && !(out[0] >= 0x300 && out[0] < 0x370)) b = out[0];
            }
            if (i == 0xA0) b = ' ';
            if (i >= 32) CharLowerBuffW(&b, 1);
            tab[i] = b;
        }
        init = 1;
    }
    if (c < 0x250) return tab[c];
    CharLowerBuffW(&c, 1); return c;
}
/* text v indexoch RichEditu (1 znak = 1 pozicia, aj v tabulkach) */
static wchar_t *get_cp_text(HWND h, int *outn) {
    GETTEXTLENGTHEX gl; GETTEXTEX gt; int n, cap, got; wchar_t *w;
    gl.flags = GTL_NUMCHARS | GTL_PRECISE; gl.codepage = 1200;
    n = (int)SendMessageW(h, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0); if (n < 0) n = 0;
    cap = n * 2 + 65536; w = (wchar_t *)malloc(cap * sizeof(wchar_t));
    /* GT_RAWTEXT (4): text presne ako v pamati vratane znakov struktury tabuliek */
    ZeroMemory(&gt, sizeof gt); gt.cb = (DWORD)((cap - 1) * sizeof(wchar_t)); gt.flags = 4; gt.codepage = 1200;
    got = (int)SendMessageW(h, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)w);
    if (got < 0) got = 0; if (got > cap - 1) got = cap - 1;
    w[got] = 0; *outn = got; return w;
}
/* vrati pole zaciatkov vsetkych vyskytov g_find v okne t; dlzka zhody je *mlen */
static int *find_all(HWND t, int *cnt, int *mlen) {
    wchar_t nd[256], *s; int m = 0, n, i, cap = 0, c = 0, *r = NULL;
    for (; g_find[m] && m < 255; m++) nd[m] = fold_ch(g_find[m]);
    *mlen = m; *cnt = 0;
    if (!m) return NULL;
    s = get_cp_text(t, &n);
    for (i = 0; i < n; i++) s[i] = fold_ch(s[i]);
    for (i = 0; i + m <= n && c < 20000; i++) {
        if (s[i] == nd[0] && memcmp(s + i, nd, m * sizeof(wchar_t)) == 0) {
            if (c == cap) { cap = cap * 2 + 64; r = (int *)realloc(r, cap * sizeof(int)); }
            r[c++] = i; i += m - 1;
        }
    }
    free(s); *cnt = c; return r;
}

static void apply_hl(void) {
    CHARFORMAT2W cf; int cnt, m, i, *hits;
    if (!g_find[0]) return;
    hits = find_all(hView, &cnt, &m);
    ZeroMemory(&cf, sizeof cf); cf.cbSize = sizeof cf; cf.dwMask = CFM_BACKCOLOR | CFM_COLOR;
    cf.crBackColor = RGB(255, 214, 10); cf.crTextColor = RGB(0, 0, 0);
    for (i = 0; i < cnt && i < 3000; i++) {
        CHARRANGE cr; cr.cpMin = hits[i]; cr.cpMax = hits[i] + m;
        SendMessageW(hView, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(hView, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    free(hits);
}

/* navod je vlozeny v exe ako RCDATA 1 (subor NAVOD.md, UTF-8) */
static wchar_t *load_help(int *outn) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(1), (LPCWSTR)RT_RCDATA); HGLOBAL g; const char *p; int len, wn; wchar_t *w;
    if (!r || !(g = LoadResource(NULL, r))) return NULL;
    p = (const char *)LockResource(g); len = (int)SizeofResource(NULL, r);
    if (!p || len <= 0) return NULL;
    if (len >= 3 && (BYTE)p[0] == 0xEF && (BYTE)p[1] == 0xBB && (BYTE)p[2] == 0xBF) { p += 3; len -= 3; }
    wn = MultiByteToWideChar(CP_UTF8, 0, p, len, NULL, 0);
    w = (wchar_t *)malloc((wn + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, p, len, w, wn); w[wn] = 0;
    *outn = wn; return w;
}
static void set_help(int on) {
    if (g_help == on) return;
    g_help = on; render(1);
    if (hBtn[8]) InvalidateRect(hBtn[8], NULL, TRUE);
}

static void render(int resetScroll) {
    int n, i, nl = 0, cap = 0; wchar_t *src; Line *L = NULL; SB b = { 0 };
    POINT sp = { 0, 0 }; SETTEXTEX st; CHARRANGE z = { 0, 0 }; CHARFORMAT2W cf;
    COLORREF cols[7]; int fromEd;

    g_syncing++;
    src = g_help ? load_help(&n) : NULL;
    fromEd = (src == NULL);
    if (!src) src = get_text(hEdit, 0, &n);
    if (!g_help && n == 0 && !g_path[0] && !g_modified) { free(src); src = build_home(&n); fromEd = 0; }
    for (i = 0; i < n; i++) if (src[i] >= S_LINK_A && src[i] <= S_IMG) src[i] = 0xFFFD;
    for (i = 0; i <= n; ) {
        int s0 = i;
        while (i < n && src[i] != '\r' && src[i] != '\n') i++;
        if (nl == cap) { cap = cap * 2 + 256; L = (Line *)realloc(L, cap * sizeof(Line)); }
        L[nl].s = src + s0; L[nl].len = i - s0; nl++;
        if (i >= n) break;
        if (src[i] == '\r' && i + 1 < n && src[i + 1] == '\n') i++;
        i++;
    }
    for (i = 0; i < g_nlinks; i++) free(g_links[i].url);
    g_nlinks = 0; g_nheads = 0; g_hasTable = 0; g_inLink = 0; g_nblk = 0; g_nuse = 0; g_cellTw = 0; g_curLi = 0;
    /* editor ma oddelovac riadkov '\r' (1 znak), takze posun v src = pozicia v editore */
    g_nLines = 0;
    if (fromEd) {
        g_lineStart = (int *)realloc(g_lineStart, (nl + 1) * sizeof(int));
        for (i = 0; i < nl; i++) g_lineStart[i] = (int)(L[i].s - src);
        g_nLines = nl;
    }
    g_srcBase = fromEd ? src : NULL;

    sb_s(&b, "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Segoe UI;}{\\f1\\fmodern Consolas;}}{\\colortbl ;");
    cols[0] = T.text; cols[1] = T.link; cols[2] = T.codebg; cols[3] = T.border; cols[4] = T.thbg; cols[5] = T.muted; cols[6] = T.codefg;
    for (i = 0; i < 7; i++) rtf_color(&b, cols[i]);
    sb_s(&b, "}\\uc1 ");
    blocks(&b, L, nl, 0, 0);
    g_srcBase = NULL;
    sb_s(&b, "}");

    SendMessageW(hView, EM_GETSCROLLPOS, 0, (LPARAM)&sp);
    SendMessageW(hView, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hView, EM_SETREADONLY, FALSE, 0);
    SendMessageW(hView, EM_SETBKGNDCOLOR, 0, T.bg);
    ZeroMemory(&cf, sizeof cf); cf.cbSize = sizeof cf; cf.dwMask = CFM_COLOR; cf.crTextColor = T.text;
    SendMessageW(hView, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    st.flags = ST_DEFAULT; st.codepage = CP_ACP;
    SendMessageW(hView, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)b.p);
    post_sentinels();
    { GETTEXTLENGTHEX gl; gl.flags = GTL_NUMCHARS | GTL_PRECISE; gl.codepage = 1200;
      g_viewLen = (int)SendMessageW(hView, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0); }
    apply_hl();
    SendMessageW(hView, EM_EXSETSEL, 0, (LPARAM)&z);
    SendMessageW(hView, EM_SETREADONLY, TRUE, 0);
    hook_drop(hView);   /* prepnutie read-only meni registraciu drop targetu RichEditu */
    if (!resetScroll) SendMessageW(hView, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
    SendMessageW(hView, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hView, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
    free(b.p); free(L); free(src);
    g_syncing--;
    sync_mark();
}

/* ---------- subor ---------- */
static void set_title(void) {
    wchar_t t[1200], nm[300];
    if (g_path[0]) loc_name(g_path, nm, 300);
    else if (g_modified || GetWindowTextLengthW(hEdit) > 0) wcscpy(nm, L"Bez názvu");
    else { SetWindowTextW(hMain, APP); return; }
    _snwprintf(t, 1199, L"%s%s%s \x2014 %s", nm, g_online ? L" (web)" : L"", g_modified ? L" *" : L"", APP); t[1199] = 0;
    SetWindowTextW(hMain, t);
}
static void star_update(void) {
    if (!hBtn[13]) return;
    SetWindowTextW(hBtn[13], bm_find(g_path) >= 0 ? L"\x2605" : L"\x2606");
    EnableWindow(hBtn[13], g_path[0] != 0);
    InvalidateRect(hBtn[13], NULL, TRUE);
}
static void set_modified(int m) { if (g_modified != m) { g_modified = m; set_title(); } }

static void set_editor_text(const wchar_t *w) {
    SETTEXTEX st; st.flags = ST_DEFAULT; st.codepage = 1200;
    g_loading = 1;
    SendMessageW(hEdit, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)w);
    SendMessageW(hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    g_loading = 0;
}

/* bajty suboru -> text (UTF-8, UTF-16 alebo stredoeuropske kodovanie) */
static wchar_t *decode_text(const BYTE *buf, int n) {
    wchar_t *w = NULL; int wn = 0;
    if (n >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
        wn = (n - 2) / 2; w = (wchar_t *)malloc((wn + 1) * sizeof(wchar_t)); memcpy(w, buf + 2, wn * 2);
    } else if (n >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) {
        int i; wn = (n - 2) / 2; w = (wchar_t *)malloc((wn + 1) * sizeof(wchar_t));
        for (i = 0; i < wn; i++) w[i] = (wchar_t)((buf[2 + i * 2] << 8) | buf[3 + i * 2]);
    } else {
        const char *p = (const char *)buf; int len = n; UINT cp = CP_UTF8;
        if (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) { p += 3; len -= 3; }
        if (len > 0) {
            wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, len, NULL, 0);
            if (wn == 0) { cp = CP_ACP; wn = MultiByteToWideChar(cp, 0, p, len, NULL, 0); }
        }
        w = (wchar_t *)malloc((wn + 1) * sizeof(wchar_t));
        if (wn > 0) MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, p, len, w, wn);
    }
    w[wn] = 0;
    return w;
}
/* zobrazi nacitany dokument; loc = cesta alebo webova adresa */
static void show_doc(wchar_t *w, const wchar_t *loc, int online) {
    wchar_t title[200];
    g_crlf = wcsstr(w, L"\r\n") != NULL || wcschr(w, '\n') == NULL;
    lstrcpynW(g_path, loc, 1024); g_online = online;
    set_editor_text(w);
    g_modified = 0; set_title();
    g_help = 0; if (hBtn[8]) InvalidateRect(hBtn[8], NULL, TRUE);
    render(1);
    doc_title(w, title, 200); if (!title[0]) loc_name(loc, title, 200);
    recent_add(title, loc);
    star_update();
}
static int load_file(const wchar_t *path) {
    DWORD n; BYTE *buf; wchar_t full[1024], *w;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        wchar_t m[1200]; _snwprintf(m, 1199, L"Súbor sa nenašiel:\n%s", path); m[1199] = 0;
        MessageBoxW(hMain, m, APP, MB_ICONWARNING); return 0;
    }
    if (!(buf = read_all(path, 64u << 20, &n))) { MessageBoxW(hMain, L"Súbor sa nepodarilo otvoriť (alebo je väčší ako 64 MB).", APP, MB_ICONWARNING); return 0; }
    w = decode_text(buf, (int)n); free(buf);
    if (!GetFullPathNameW(path, 1024, full, NULL)) lstrcpynW(full, path, 1024);
    show_doc(w, full, 0); free(w);
    return 1;
}
static int confirm_discard(void);
static void goto_anchor(const wchar_t *a);
static wchar_t g_pendFrag[256];
static int g_docBusy;
/* otvori subor alebo webovu adresu; frag = kotva, na ktoru sa ma posunut */
static void open_location(const wchar_t *loc0, const wchar_t *frag) {
    wchar_t loc[1100];
    if (!loc0 || !loc0[0] || !confirm_discard()) return;
    if (is_url(loc0)) {
        wchar_t t[2200];
        if (g_docBusy) return;
        normalize_url(loc0, t, 2200); url_encode(t, loc, 1100);
        g_docBusy = 1; lstrcpynW(g_pendFrag, frag ? frag : L"", 256);
        SetWindowTextW(hMain, L"Načítavam\x2026 \x2014 " APP);
        start_net(loc, WM_APP_DOC);
    } else {
        lstrcpynW(loc, loc0, 1100);
        if (load_file(loc) && frag && frag[0]) goto_anchor(frag);
    }
}
static int looks_html(const BYTE *b, DWORD n) {
    DWORD i = 0; char t[16]; int k = 0;
    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i = 3;
    while (i < n && (b[i] == ' ' || b[i] == '\r' || b[i] == '\n' || b[i] == '\t')) i++;
    while (i < n && k < 14) t[k++] = (char)tolower(b[i++]);
    t[k] = 0;
    return !strncmp(t, "<!doctype html", 14) || !strncmp(t, "<html", 5);
}
/* webovy dokument dorazil (hlavne vlakno) */
static void doc_arrived(NetJob *j) {
    g_docBusy = 0;
    if (!j->data) {
        wchar_t m[1400];
        if (j->status) _snwprintf(m, 1399, L"Dokument sa nepodarilo stiahnuť (server vrátil chybu %lu).\n\n%s", (unsigned long)j->status, j->url);
        else _snwprintf(m, 1399, L"Dokument sa nepodarilo stiahnuť. Skontrolujte adresu a pripojenie na internet.\n\n%s", j->url);
        m[1399] = 0; set_title(); MessageBoxW(hMain, m, APP, MB_ICONWARNING);
    } else if (looks_html(j->data, j->n)) {
        set_title();
        MessageBoxW(hMain, L"Na tejto adrese je webová stránka, nie súbor Markdown.\n\nPoužite odkaz priamo na súbor \x2014 napríklad na GitHube tlačidlo „Raw“.", APP, MB_ICONWARNING);
    } else {
        wchar_t *w = decode_text(j->data, (int)j->n);
        show_doc(w, j->url, 1); free(w);
        if (g_pendFrag[0]) goto_anchor(g_pendFrag);
    }
    free(j->data); free(j);
}

static int save_to(const wchar_t *path) {
    int n, i, k = 0, bn; wchar_t *w = get_text(hEdit, 1, &n); char *u; HANDLE h; DWORD wr = 0; BOOL ok;
    if (!g_crlf) { for (i = 0; i < n; i++) if (w[i] != '\r') w[k++] = w[i]; n = k; w[n] = 0; }
    bn = n ? WideCharToMultiByte(CP_UTF8, 0, w, n, NULL, 0, NULL, NULL) : 0;
    u = (char *)malloc(bn + 1);
    if (bn) WideCharToMultiByte(CP_UTF8, 0, w, n, u, bn, NULL, NULL);
    free(w);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { free(u); MessageBoxW(hMain, L"Súbor sa nepodarilo uložiť.", APP, MB_ICONWARNING); return 0; }
    ok = WriteFile(h, u, bn, &wr, NULL) && wr == (DWORD)bn; CloseHandle(h); free(u);
    if (!ok) { MessageBoxW(hMain, L"Zápis súboru zlyhal.", APP, MB_ICONWARNING); return 0; }
    return 1;
}
static const wchar_t FILTER[] = L"Markdown (*.md;*.markdown)\0*.md;*.markdown\0Text (*.txt)\0*.txt\0Všetky súbory (*.*)\0*.*\0";
static int do_save(int saveAs) {
    wchar_t path[1024], *p;
    wcscpy(path, g_path);
    if (g_online) {   /* dokument z webu sa uklada ako novy subor v pocitaci */
        loc_name(g_path, path, 1024);
        for (p = path; *p; p++) if (wcschr(L"\\/:*?\"<>|", *p)) *p = '_';
    }
    if (saveAs || !path[0] || g_online) {
        OPENFILENAMEW of; ZeroMemory(&of, sizeof of);
        of.lStructSize = sizeof of; of.hwndOwner = hMain; of.lpstrFilter = FILTER; of.lpstrFile = path; of.nMaxFile = 1024;
        of.lpstrDefExt = L"md"; of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        if (!GetSaveFileNameW(&of)) return 0;
    }
    if (!save_to(path)) return 0;
    { int wasOnline = g_online; wcscpy(g_path, path); g_online = 0; g_modified = 0; set_title();
      if (wasOnline || saveAs) { wchar_t nm[200]; loc_name(path, nm, 200); recent_add(nm, path); }
      star_update(); }
    return 1;
}
static int confirm_discard(void) {
    int r;
    if (!g_modified) return 1;
    r = MessageBoxW(hMain, L"Dokument obsahuje neuložené zmeny. Uložiť ich?", APP, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) return do_save(0);
    return 1;
}
static void do_open(void) {
    wchar_t path[1024] = L""; OPENFILENAMEW of; ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = hMain; of.lpstrFilter = FILTER; of.lpstrFile = path; of.nMaxFile = 1024;
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!confirm_discard()) return;
    if (GetOpenFileNameW(&of)) load_file(path);
}
static void set_edit_mode(int on);
static void set_help(int on);
/* zavrie dokument (nie aplikaciu) a ukaze uvodnu stranku s odkazmi */
static void go_home(void) {
    if (!confirm_discard()) return;
    g_path[0] = 0; g_online = 0; g_modified = 0; g_help = 0;
    set_editor_text(L"");
    if (hBtn[8]) InvalidateRect(hBtn[8], NULL, TRUE);
    set_title(); star_update();
    if (g_edit) set_edit_mode(0);
    render(1);
}

/* ---------- odkazy ---------- */
static void scroll_to_cp(int cp, int topPad) {
    POINTL p; POINT sp;
    SendMessageW(hView, EM_POSFROMCHAR, (WPARAM)&p, cp);
    SendMessageW(hView, EM_GETSCROLLPOS, 0, (LPARAM)&sp);
    sp.y += p.y - topPad; if (sp.y < 0) sp.y = 0;
    SendMessageW(hView, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
}
/* kotva bez diakritiky, pomlciek a velkosti pismen (zachrana, ak sa presny tvar lisi) */
static void anchor_norm(const wchar_t *s, wchar_t *o, int cap) {
    int k = 0;
    for (; *s && k < cap - 1; s++) { wchar_t c = fold_ch(*s); if (is_alnum_w(c)) o[k++] = c; }
    o[k] = 0;
}
static void goto_anchor(const wchar_t *a0) {
    wchar_t a[256], na[256], nh[256]; int i;
    url_decode(a0, a, 256);
    for (i = 0; i < g_nheads; i++)
        if (g_heads[i].pos >= 0 && _wcsicmp(g_heads[i].slug, a) == 0) { scroll_to_cp(g_heads[i].pos, S(8)); return; }
    anchor_norm(a, na, 256);
    if (!na[0]) return;
    for (i = 0; i < g_nheads; i++) {
        anchor_norm(g_heads[i].slug, nh, 256);
        if (g_heads[i].pos >= 0 && !wcscmp(na, nh)) { scroll_to_cp(g_heads[i].pos, S(8)); return; }
    }
}
static int link_at(POINT pt) {
    POINTL pl, p1, p2; int cp, i;
    if (!g_nlinks) return -1;
    pl.x = pt.x; pl.y = pt.y;
    cp = (int)SendMessageW(hView, EM_CHARFROMPOS, 0, (LPARAM)&pl);
    for (i = 0; i < g_nlinks; i++) {
        if (g_links[i].a <= cp && cp < g_links[i].b) {
            SendMessageW(hView, EM_POSFROMCHAR, (WPARAM)&p1, cp);
            SendMessageW(hView, EM_POSFROMCHAR, (WPARAM)&p2, cp + 1);
            if (pt.x < p1.x - S(8) || pt.y < p1.y - S(2)) return -1;
            if (p2.y == p1.y && pt.x > p2.x + S(8)) return -1;
            return i;
        }
        if (g_links[i].a > cp) break;
    }
    return -1;
}
/* lokalne subory inych typov sa otvoria v ich programe, ale len bezpecne typy (nie .exe, .bat...) */
static int safe_ext(const wchar_t *p) {
    static const wchar_t *const ok[] = { L".pdf", L".png", L".jpg", L".jpeg", L".gif", L".webp", L".bmp", L".svg", L".txt", L".csv",
        L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".odt", L".ods", L".rtf", L".mp3", L".mp4", L".wav", L".mov", L".srt", NULL };
    const wchar_t *e = wcsrchr(p, '.'); int i;
    if (GetFileAttributesW(p) & FILE_ATTRIBUTE_DIRECTORY) return 1;
    if (!e) return 0;
    for (i = 0; ok[i]; i++) if (!_wcsicmp(e, ok[i])) return 1;
    return 0;
}
static void home_cmd(const wchar_t *cmd);
static void open_link(const wchar_t *url) {
    int n = (int)wcslen(url), kind; wchar_t u[1100], frag[256] = L"", loc[1100], *h;
    if (starts_i(url, n, L"mdview:")) { home_cmd(url + 7); return; }
    if (starts_i(url, n, L"mailto:")) { ShellExecuteW(hMain, L"open", url, NULL, NULL, SW_SHOWNORMAL); return; }
    if (url[0] == '#') { goto_anchor(url + 1); return; }
    lstrcpynW(u, url, 1100);
    if ((h = wcschr(u, '#')) != NULL) { lstrcpynW(frag, h + 1, 256); *h = 0; }
    kind = resolve_ref(u, (int)wcslen(u), loc, 1100);
    if (kind == 2) {                                   /* webova adresa */
        if (has_md_ext(loc)) open_location(loc, frag);  /* dalsi Markdown dokument -> priamo v aplikacii */
        else {
            if (frag[0] && wcslen(loc) + wcslen(frag) < 1098) { wcscat(loc, L"#"); wcscat(loc, frag); }
            ShellExecuteW(hMain, L"open", loc, NULL, NULL, SW_SHOWNORMAL);
        }
    } else if (kind == 1) {                            /* subor v pocitaci */
        if (GetFileAttributesW(loc) == INVALID_FILE_ATTRIBUTES) {
            wchar_t m[1200]; _snwprintf(m, 1199, L"Súbor sa nenašiel:\n%s", loc); m[1199] = 0;
            MessageBoxW(hMain, m, APP, MB_ICONWARNING);
        } else if (has_md_ext(loc)) open_location(loc, frag);
        else if (safe_ext(loc)) ShellExecuteW(hMain, L"open", loc, NULL, NULL, SW_SHOWNORMAL);
    }
}

/* ---------- hladanie ---------- */
static HWND find_target(void) { return (g_edit && g_lastPane == hEdit) ? hEdit : hView; }

static void do_find(int dir, int fromStart) {
    HWND t = find_target(); CHARRANGE sel; int cnt, m, i, idx = -1, *hits; wchar_t s[32];
    if (!g_find[0]) { SetWindowTextW(hCount, L""); return; }
    hits = find_all(t, &cnt, &m);
    SendMessageW(t, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (cnt) {
        if (dir > 0) {
            int start = fromStart ? sel.cpMin : sel.cpMax;
            for (i = 0; i < cnt; i++) if (hits[i] >= start) { idx = i; break; }
            if (idx < 0) idx = 0;
        } else {
            for (i = cnt - 1; i >= 0; i--) if (hits[i] < sel.cpMin) { idx = i; break; }
            if (idx < 0) idx = cnt - 1;
        }
    }
    if (idx >= 0) {
        POINTL p; RECT rc; CHARRANGE cr;
        cr.cpMin = hits[idx]; cr.cpMax = hits[idx] + m;
        SendMessageW(t, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(t, EM_POSFROMCHAR, (WPARAM)&p, cr.cpMin);
        GetClientRect(t, &rc);
        if (p.y < S(4) || p.y > rc.bottom - S(48)) {
            POINT sp; SendMessageW(t, EM_GETSCROLLPOS, 0, (LPARAM)&sp);
            sp.y += p.y - rc.bottom / 3; if (sp.y < 0) sp.y = 0;
            SendMessageW(t, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
        }
    }
    _snwprintf(s, 31, L"%d / %d", idx + 1, cnt); s[31] = 0;
    SetWindowTextW(hCount, s);
    free(hits);
}
static void find_changed(void) {
    CHARRANGE sel;
    GetWindowTextW(hSearch, g_find, 255);
    SendMessageW(hView, EM_EXGETSEL, 0, (LPARAM)&sel);
    render(0);                       /* prekresli zvyraznenia */
    sel.cpMax = sel.cpMin; SendMessageW(hView, EM_EXSETSEL, 0, (LPARAM)&sel);
    do_find(1, 1);
}

/* ---------- synchronizacia editor <-> nahlad ----------
   Pri vykresleni sa na zaciatok kazdeho bloku vlozi znacka so zdrojovym riadkom (g_blk).
   Medzi blokmi sa poloha interpoluje podla zvislej pozicie, takze posuvanie je plynule.
   Volania idu priamo do povodnej procedury RichEditu, aby ich nezachytil nas subclass. */
static int sc_top(HWND h) { RECT r; CallWindowProcW(g_reProc, h, EM_GETRECT, 0, (LPARAM)&r); return r.top; }
static int sc_get(HWND h) { POINT sp = { 0, 0 }; CallWindowProcW(g_reProc, h, EM_GETSCROLLPOS, 0, (LPARAM)&sp); return sp.y; }
static void sc_set(HWND h, double y) {
    POINT sp; CallWindowProcW(g_reProc, h, EM_GETSCROLLPOS, 0, (LPARAM)&sp);
    sp.y = y < 0 ? 0 : (int)(y + 0.5);
    CallWindowProcW(g_reProc, h, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
}
/* zvisla poloha znaku v dokumente (0 = zaciatok obsahu) */
static double sc_y(HWND h, int cp, int sy, int top) {
    POINTL p; p.x = p.y = 0; CallWindowProcW(g_reProc, h, EM_POSFROMCHAR, (WPARAM)&p, cp);
    return (double)p.y + sy - top;
}
static double clamp01(double f) { return f < 0 ? 0 : f > 1 ? 1 : f; }
static double ed_line_y(double line, int sy, int top) {
    int i; double y0, y1;
    if (g_nLines <= 0) return 0;
    if (line < 0) line = 0;
    i = (int)line;
    if (i >= g_nLines - 1) return sc_y(hEdit, g_lineStart[g_nLines - 1], sy, top);
    y0 = sc_y(hEdit, g_lineStart[i], sy, top); y1 = sc_y(hEdit, g_lineStart[i + 1], sy, top);
    return y0 + (line - i) * (y1 - y0);
}
/* zdrojovy riadok (s desatinnou castou) na hornom okraji editora */
static double ed_top_line(int sy, int top) {
    int lo = 0, hi = g_nLines - 1; double y0, y1;
    if (g_nLines <= 0) return 0;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (sc_y(hEdit, g_lineStart[mid], sy, top) <= sy) lo = mid; else hi = mid - 1; }
    if (lo >= g_nLines - 1) return lo;
    y0 = sc_y(hEdit, g_lineStart[lo], sy, top); y1 = sc_y(hEdit, g_lineStart[lo + 1], sy, top);
    return lo + (y1 > y0 ? clamp01((sy - y0) / (y1 - y0)) : 0);
}
/* bod mapovania k: -1 = zaciatok dokumentu, g_nblk = koniec */
static void blk_pt(int k, int sy, int top, double *line, double *y) {
    if (k < 0) { *line = 0; *y = 0; return; }
    if (k >= g_nblk) { *line = g_nLines; *y = sc_y(hView, g_viewLen > 0 ? g_viewLen - 1 : 0, sy, top); return; }
    *line = g_blk[k].line; *y = sc_y(hView, g_blk[k].cp, sy, top);
}
static void sync_e2v(void) {
    int se = sc_get(hEdit), te = sc_top(hEdit), sv = sc_get(hView), tv = sc_top(hView), lo = -1, hi = g_nblk - 1;
    double L = ed_top_line(se, te), l0, y0, l1, y1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (g_blk[mid].line <= L) lo = mid; else hi = mid - 1; }
    blk_pt(lo, sv, tv, &l0, &y0); blk_pt(lo + 1, sv, tv, &l1, &y1);
    sc_set(hView, l1 > l0 ? y0 + clamp01((L - l0) / (l1 - l0)) * (y1 - y0) : y0);
}
static void sync_v2e(void) {
    int sv = sc_get(hView), tv = sc_top(hView), se = sc_get(hEdit), te = sc_top(hEdit), lo = -1, hi = g_nblk - 1;
    double l0, y0, l1, y1, L;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (sc_y(hView, g_blk[mid].cp, sv, tv) <= sv) lo = mid; else hi = mid - 1; }
    blk_pt(lo, sv, tv, &l0, &y0); blk_pt(lo + 1, sv, tv, &l1, &y1);
    L = y1 > y0 ? l0 + clamp01((sv - y0) / (y1 - y0)) * (l1 - l0) : l0;
    sc_set(hEdit, ed_line_y(L, se, te));
}
static int sync_ok(void) { return g_edit && !g_help && g_nLines > 0 && hEdit && hView; }
static void sync_mark(void) { if (hEdit && hView) { g_lastSy[0] = sc_get(hEdit); g_lastSy[1] = sc_get(hView); } }
static void sync_from(HWND src) {
    if (!sync_ok()) return;
    g_syncing++;
    if (src == hEdit) sync_e2v(); else sync_v2e();
    g_syncing--;
    sync_mark();
}
/* dvojklik v nahlade: oznaci prislusny riadok v editore na rovnakej vyske obrazovky */
static void jump_to_source(int x, int y) {
    POINTL p; CHARRANGE cr; int cp, sv = sc_get(hView), tv = sc_top(hView), se, te, lo = -1, hi = g_nblk - 1, line;
    double l0, y0, l1, y1, L, yc;
    p.x = x; p.y = y; cp = (int)CallWindowProcW(g_reProc, hView, EM_CHARFROMPOS, 0, (LPARAM)&p);
    yc = sc_y(hView, cp, sv, tv);
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (g_blk[mid].cp <= cp) lo = mid; else hi = mid - 1; }
    blk_pt(lo, sv, tv, &l0, &y0); blk_pt(lo + 1, sv, tv, &l1, &y1);
    L = (y1 > y0 && l1 > l0) ? l0 + clamp01((yc - y0) / (y1 - y0)) * (l1 - l0) : l0;
    line = (int)L;
    if (l1 > l0 && line >= (int)l1) line = (int)l1 - 1;
    if (line > g_nLines - 1) line = g_nLines - 1; if (line < 0) line = 0;
    g_syncing++;
    se = sc_get(hEdit); te = sc_top(hEdit);
    sc_set(hEdit, ed_line_y(line, se, te) + te - (yc - sv + tv));
    cr.cpMin = g_lineStart[line]; cr.cpMax = line + 1 < g_nLines ? g_lineStart[line + 1] - 1 : -1;
    CallWindowProcW(g_reProc, hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    g_syncing--;
    sync_mark();
    g_lastPane = hEdit; SetFocus(hEdit);
}
static int view_top_cp(void) {
    RECT r; POINTL p;
    CallWindowProcW(g_reProc, hView, EM_GETRECT, 0, (LPARAM)&r);
    p.x = r.left + 1; p.y = r.top + 1;
    return (int)CallWindowProcW(g_reProc, hView, EM_CHARFROMPOS, 0, (LPARAM)&p);
}

/* ---------- vzhlad ---------- */
static void editor_format(void) {
    CHARFORMAT2W cf; ZeroMemory(&cf, sizeof cf); cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    cf.yHeight = g_fs * 10; cf.crTextColor = T.text; wcscpy(cf.szFaceName, L"Consolas");
    g_loading = 1;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, T.bg);
    g_loading = 0;
}
static void layout(void) {
    RECT rc, r; int W, H, tb = S(44), bh = S(28), by, x, i, right, sw;
    /* poradie tlacidiel zlava: index v hBtn, sirka, medzera za nim */
    static const struct { int i, w, gap; } BL[] = {
        { 11, 66, 6 }, { 0, 70, 6 }, { 12, 84, 6 }, { 13, 32, 14 },   /* Domov, Otvoriť, Odkazy ▾, ☆ */
        { 1, 64, 6 }, { 2, 70, 14 },                                 /* Uložiť, Upraviť */
        { 3, 64, 6 }, { 4, 36, 6 }, { 5, 36, 14 },                   /* Tmavý, A−, A+ */
        { 8, 62, 6 } };                                              /* Návod */
    GetClientRect(hMain, &rc); W = rc.right; H = rc.bottom; by = (tb - bh) / 2; x = S(8);
    for (i = 0; i < (int)(sizeof BL / sizeof BL[0]); i++) { MoveWindow(hBtn[BL[i].i], x, by, S(BL[i].w), bh, TRUE); x += S(BL[i].w) + S(BL[i].gap); }
    MoveWindow(hBtn[10], x, by, S(104), bh, TRUE);
    if (GetWindowLongW(hBtn[10], GWL_STYLE) & WS_VISIBLE) x += S(104) + S(6);
    right = W - S(8) - S(30); MoveWindow(hBtn[7], right, by, S(30), bh, TRUE);
    right -= S(30) + S(4); MoveWindow(hBtn[6], right, by, S(30), bh, TRUE);
    right -= S(56) + S(4); MoveWindow(hCount, right, by + S(6), S(56), bh - S(10), TRUE);
    sw = right - S(6) - (x + S(6)); if (sw > S(240)) sw = S(240); if (sw < S(120)) sw = S(120);
    right -= sw + S(6);
    SetRect(&g_searchRc, right, by, right + sw, by + bh);
    MoveWindow(hSearch, right + S(5), by + S(6), sw - S(10) - S(24), bh - S(12), TRUE);
    MoveWindow(hBtn[9], right + sw - S(26), by + S(3), S(23), bh - S(6), TRUE);
    ShowWindow(hBtn[9], GetWindowTextLengthW(hSearch) ? SW_SHOW : SW_HIDE);
    if (g_edit) {
        int sw = S(6), ew = (int)((W - sw) * g_split);
        MoveWindow(hEdit, 0, tb, ew, H - tb, TRUE); MoveWindow(hView, ew + sw, tb, W - ew - sw, H - tb, TRUE);
        ShowWindow(hEdit, SW_SHOW);
        GetClientRect(hEdit, &r); r.left += S(12); r.right -= S(8); r.top += S(8);
        SendMessageW(hEdit, EM_SETRECT, 0, (LPARAM)&r);
    } else {
        ShowWindow(hEdit, SW_HIDE);
        MoveWindow(hView, 0, tb, W, H - tb, TRUE);
    }
    GetClientRect(hView, &r); r.left += S(30); r.right -= S(24); r.top += S(10);
    if (r.right < r.left + S(60)) r.right = r.left + S(60);
    SendMessageW(hView, EM_SETRECT, 0, (LPARAM)&r);
    g_availTw = MulDiv(r.right - r.left, 1440, g_dpi) - 90;
}
static void apply_theme(void) {
    BOOL dk = g_dark;
    T = g_dark ? DARK : LIGHT;
    if (hbrBar) DeleteObject(hbrBar); if (hbrEdit) DeleteObject(hbrEdit);
    hbrBar = CreateSolidBrush(T.bar); hbrEdit = CreateSolidBrush(T.editBg);
    DwmSetWindowAttribute(hMain, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dk, sizeof dk);
    SetWindowTheme(hView, g_dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    SetWindowTheme(hEdit, g_dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    SetWindowTextW(hBtn[3], g_dark ? L"Svetlý" : L"Tmavý");
    editor_format();
    render(0);
    SetWindowPos(hMain, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    RedrawWindow(hMain, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}
static void set_edit_mode(int on) {
    int cp = view_top_cp();          /* co sa prave cita v nahlade */
    g_edit = on; layout();
    render(0);
    g_syncing++;                     /* po zmene sirky nahladu ostane na vrchu to iste miesto */
    sc_set(hView, sc_y(hView, cp, sc_get(hView), sc_top(hView)));
    g_syncing--;
    if (on) sync_from(hView); else sync_mark();   /* editor otvori presne to, co sa citalo */
    g_lastPane = on ? hEdit : hView; SetFocus(g_lastPane);
    g_lastViewW = 0;
    InvalidateRect(hBtn[2], NULL, TRUE); InvalidateRect(hMain, NULL, TRUE);
}
static void set_zoom(int fs) {
    if (fs < 14) fs = 14; if (fs > 44) fs = 44;
    g_fs = fs; editor_format(); render(0); sync_from(hEdit);
}
static void reg_load(void) {
    DWORD v, cb = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &cb) == ERROR_SUCCESS) g_dark = (v == 0);
    cb = sizeof v; if (RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"Dark", RRF_RT_REG_DWORD, NULL, &v, &cb) == ERROR_SUCCESS) g_dark = v != 0;
    cb = sizeof v; if (RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"FontSize", RRF_RT_REG_DWORD, NULL, &v, &cb) == ERROR_SUCCESS && v >= 14 && v <= 44) g_fs = (int)v;
}
static void reg_save(void) {
    HKEY k; DWORD v;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    v = g_dark; RegSetValueExW(k, L"Dark", 0, REG_DWORD, (BYTE *)&v, sizeof v);
    v = g_fs; RegSetValueExW(k, L"FontSize", 0, REG_DWORD, (BYTE *)&v, sizeof v);
    RegCloseKey(k);
}

/* ---------- aktualizacia z GitHub Releases ----------
   Najnovsie vydanie: tag "vX.Y" a prilozeny subor s nazvom presne MDView.exe.
   Kontrola bezi na pozadi; stiahnuty subor sa vymeni za beziace exe a aplikacia sa restartuje. */
#ifndef APP_VERSION
#define APP_VERSION "1.3"
#endif
#define UPD_API_HOST L"api.github.com"
#define UPD_API_PATH L"/repos/JurajCyprich/MDView/releases/latest"
#define UPD_DL_HOST  L"github.com"
#define UPD_DL_PATH  L"/JurajCyprich/MDView/releases/latest/download/MDView.exe"
#define UPD_PAGE     L"https://github.com/JurajCyprich/MDView/releases/latest"
#define WM_APP_UPDCHECK (WM_APP + 2)   /* wParam: 1 = rucna kontrola, lParam: 1 novsia, 0 aktualna, -1 chyba */
#define WM_APP_UPDDONE  (WM_APP + 3)   /* lParam: 1 = stiahnute */
static char g_ver[32] = APP_VERSION, g_newVer[32];
static int g_updBusy, g_updTest;
static wchar_t g_exe[1024];

static BYTE *http_get(const wchar_t *host, const wchar_t *path, DWORD maxLen, DWORD *outLen) {
    HINTERNET s = NULL, c = NULL, r = NULL; BYTE *buf = NULL; DWORD n = 0, cap = 0, st = 0, sl = sizeof st; int ok = 0;
    s = WinHttpOpen(L"MDView-updater", 4 /*WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY*/, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) goto end;
    WinHttpSetTimeouts(s, 8000, 8000, 10000, 30000);
    c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0); if (!c) goto end;
    r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE); if (!r) goto end;
    if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) goto end;
    if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX) || st != 200) goto end;
    for (;;) {
        DWORD got = 0;
        if (n + 65536 + 1 > cap) { cap = cap * 2 + 262144; buf = (BYTE *)realloc(buf, cap); }
        if (!WinHttpReadData(r, buf + n, 65536, &got)) goto end;
        if (!got) { ok = 1; break; }
        n += got; if (n > maxLen) goto end;
    }
end:
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); if (s) WinHttpCloseHandle(s);
    if (!ok) { free(buf); return NULL; }
    buf[n] = 0; *outLen = n; return buf;
}
/* porovna verzie "1.10.2" / "v1.2"; >0 ak a je novsia */
static int ver_cmp(const char *a, const char *b) {
    int i;
    if (*a == 'v' || *a == 'V') a++; if (*b == 'v' || *b == 'V') b++;
    for (i = 0; i < 4; i++) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x > y ? 1 : -1;
        if (*a == '.') a++; if (*b == '.') b++;
    }
    return 0;
}
static DWORD WINAPI upd_check_thread(LPVOID p) {
    DWORD n; int res = -1; BYTE *b = http_get(UPD_API_HOST, UPD_API_PATH, 1 << 20, &n);
    if (b) {
        char *t = strstr((char *)b, "\"tag_name\""), *e;
        if (t && (t = strchr(t + 10, ':')) != NULL && (t = strchr(t, '"')) != NULL && (e = strchr(++t, '"')) != NULL && e - t < 31) {
            char v[32]; memcpy(v, t, e - t); v[e - t] = 0;
            res = ver_cmp(v, g_ver) > 0;
            if (res) lstrcpynA(g_newVer, (v[0] == 'v' || v[0] == 'V') ? v + 1 : v, 32);
        }
        free(b);
    }
    PostMessageW(hMain, WM_APP_UPDCHECK, (WPARAM)p, res);
    return 0;
}
static DWORD WINAPI upd_download_thread(LPVOID p) {
    DWORD n = 0, wr = 0; int ok = 0; BYTE *b = http_get(UPD_DL_HOST, UPD_DL_PATH, 64u << 20, &n);
    (void)p;
    if (b && n > 50000 && b[0] == 'M' && b[1] == 'Z') {
        wchar_t tmp[1100]; HANDLE h;
        _snwprintf(tmp, 1099, L"%s.new", g_exe); tmp[1099] = 0;
        h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) { ok = WriteFile(h, b, n, &wr, NULL) && wr == n; CloseHandle(h); if (!ok) DeleteFileW(tmp); }
    }
    free(b);
    PostMessageW(hMain, WM_APP_UPDDONE, 0, ok);
    return 0;
}
static void upd_check(int manual) {
    HANDLE t;
    if (g_updBusy) return;
    g_updBusy = 1;
    t = CreateThread(NULL, 0, upd_check_thread, (LPVOID)(INT_PTR)manual, 0, NULL);
    if (t) CloseHandle(t); else g_updBusy = 0;
}
static void upd_button(const wchar_t *text, int enabled) {
    SetWindowTextW(hBtn[10], text); EnableWindow(hBtn[10], enabled);
    ShowWindow(hBtn[10], SW_SHOW); layout(); InvalidateRect(hMain, NULL, TRUE); InvalidateRect(hBtn[10], NULL, TRUE);
}
static void upd_ask(void) {
    wchar_t m[300]; HANDLE t;
    if (g_updBusy || !g_newVer[0]) return;
    _snwprintf(m, 299, L"Je dostupná nová verzia %S (máte %S).\n\nStiahnuť a nainštalovať ju teraz? Aplikácia sa potom sama reštartuje.", g_newVer, g_ver); m[299] = 0;
    if (!g_updTest && MessageBoxW(hMain, m, APP, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g_updBusy = 1; upd_button(L"Sťahujem\x2026", FALSE);
    t = CreateThread(NULL, 0, upd_download_thread, NULL, 0, NULL);
    if (t) CloseHandle(t); else { g_updBusy = 0; upd_button(L"Aktualizovať", TRUE); }
}
/* vymeni beziace exe za stiahnute a spusti ho; vrati 0 pri chybe */
static int upd_apply(void) {
    wchar_t nw[1100], old[1100], cmd[2400]; STARTUPINFOW si; PROCESS_INFORMATION pi;
    _snwprintf(nw, 1099, L"%s.new", g_exe); nw[1099] = 0;
    _snwprintf(old, 1099, L"%s.old", g_exe); old[1099] = 0;
    if (!confirm_discard()) { DeleteFileW(nw); return -1; }
    DeleteFileW(old);
    if (!MoveFileExW(g_exe, old, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(nw); return 0; }
    if (!MoveFileExW(nw, g_exe, MOVEFILE_REPLACE_EXISTING)) { MoveFileExW(old, g_exe, MOVEFILE_REPLACE_EXISTING); DeleteFileW(nw); return 0; }
    if (g_updTest) return 1;
    if (g_path[0]) _snwprintf(cmd, 2399, L"\"%s\" \"%s\"", g_exe, g_path); else _snwprintf(cmd, 2399, L"\"%s\"", g_exe);
    cmd[2399] = 0;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    if (CreateProcessW(g_exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    g_modified = 0; DestroyWindow(hMain);
    return 1;
}
static void upd_startup(void) {
    wchar_t old[1100];
    GetModuleFileNameW(NULL, g_exe, 1024);
    _snwprintf(old, 1099, L"%s.old", g_exe); old[1099] = 0;
    DeleteFileW(old);              /* zvysok po predoslej aktualizacii */
    upd_check(0);
}

/* ---------- subclassy ---------- */
static void ctx_menu(HWND h, int x, int y) {
    HMENU m = CreatePopupMenu(); int cmd, ed = (h == hEdit); CHARRANGE sel;
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (ed) { AppendMenuW(m, MF_STRING, IDM_UNDO, L"Späť\tCtrl+Z"); AppendMenuW(m, MF_SEPARATOR, 0, NULL);
              AppendMenuW(m, MF_STRING | (sel.cpMax > sel.cpMin ? 0 : MF_GRAYED), IDM_CUT, L"Vystrihnúť\tCtrl+X"); }
    AppendMenuW(m, MF_STRING | (sel.cpMax > sel.cpMin ? 0 : MF_GRAYED), IDM_COPY, L"Kopírovať\tCtrl+C");
    if (ed) AppendMenuW(m, MF_STRING, IDM_PASTE, L"Vložiť\tCtrl+V");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_SELALL, L"Vybrať všetko\tCtrl+A");
    if (x == -1 && y == -1) { POINT p = { S(20), S(20) }; ClientToScreen(h, &p); x = p.x; y = p.y; }
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hMain, NULL);
    DestroyMenu(m);
    if (cmd == IDM_COPY) SendMessageW(h, WM_COPY, 0, 0);
    else if (cmd == IDM_CUT) SendMessageW(h, WM_CUT, 0, 0);
    else if (cmd == IDM_PASTE) SendMessageW(h, WM_PASTE, 0, 0);
    else if (cmd == IDM_UNDO) SendMessageW(h, EM_UNDO, 0, 0);
    else if (cmd == IDM_SELALL) { CHARRANGE all = { 0, -1 }; SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&all); }
}
static LRESULT ReProc0(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_LBUTTONDBLCLK:
        if (h == hView && sync_ok()) { jump_to_source(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0; }
        break;
    case WM_DROPFILES: return SendMessageW(hMain, WM_DROPFILES, wp, lp);
    case WM_SETFOCUS: g_lastPane = h; break;
    case WM_CONTEXTMENU: ctx_menu(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_SETCURSOR:
        if (h == hView && LOWORD(lp) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            if (link_at(pt) >= 0) { SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE; }
        }
        break;
    case WM_LBUTTONDOWN: g_downPt.x = GET_X_LPARAM(lp); g_downPt.y = GET_Y_LPARAM(lp); break;
    case WM_LBUTTONUP:
        if (h == hView) {
            POINT pt; CHARRANGE sel; int li; LRESULT r = CallWindowProcW(g_reProc, h, m, wp, lp);
            pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
            SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&sel);
            if (sel.cpMin == sel.cpMax && abs(pt.x - g_downPt.x) < 4 && abs(pt.y - g_downPt.y) < 4 && (li = link_at(pt)) >= 0) {
                wchar_t *u = _wcsdup(g_links[li].url); open_link(u); free(u);
            }
            return r;
        }
        break;
    }
    return CallWindowProcW(g_reProc, h, m, wp, lp);
}
/* po kazdej sprave zisti, ci sa panel posunul (koliesko, posuvnik, klavesy, plynule
   posuvanie, pisanie...), a ak ano, posunie druhy panel na zodpovedajuce miesto */
static LRESULT CALLBACK ReProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    LRESULT r = ReProc0(h, m, wp, lp);
    if (!g_syncing && m != WM_DESTROY && m != WM_NCDESTROY && (h == hEdit || h == hView) && sync_ok()) {
        int i = (h == hView), y = sc_get(h);
        if (y != g_lastSy[i]) { g_lastSy[i] = y; sync_from(h); }
    }
    return r;
}
static LRESULT CALLBACK SearchProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        KillTimer(hMain, T_FIND);
        { wchar_t cur[256]; GetWindowTextW(h, cur, 255); if (wcscmp(cur, g_find)) find_changed(); else do_find(GetKeyState(VK_SHIFT) < 0 ? -1 : 1, 0); }
        if (find_target() == hEdit) SetFocus(hEdit);
        return 0;
    }
    if (m == WM_CHAR && (wp == '\r' || wp == '\n')) return 0;
    return CallWindowProcW(g_edProc, h, m, wp, lp);
}

static HWND make_rich(HWND parent, int id, DWORD extra) {
    HWND h = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL | ES_DISABLENOSCROLL | extra, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, EM_EXLIMITTEXT, 0, 0x7FFFFFF0);
    hook_drop(h);
    g_reProc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)ReProc);
    return h;
}

/* ---------- jednoduche okno na zadanie textu ---------- */
static int g_ibDone, g_ibOk;
static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { g_ibOk = 1; g_ibDone = 1; return 0; }
        if (LOWORD(wp) == IDCANCEL) { g_ibDone = 1; return 0; }
        break;
    case WM_CLOSE: g_ibDone = 1; return 0;
    case WM_ERASEBKGND: { RECT rc; GetClientRect(h, &rc); FillRect((HDC)wp, &rc, hbrBar); return 1; }
    case WM_CTLCOLORSTATIC: SetTextColor((HDC)wp, T.barText); SetBkColor((HDC)wp, T.bar); return (LRESULT)hbrBar;
    case WM_CTLCOLOREDIT: SetTextColor((HDC)wp, T.text); SetBkColor((HDC)wp, T.editBg); return (LRESULT)hbrEdit;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static int input_box(const wchar_t *title, const wchar_t *prompt, wchar_t *buf, int cap) {
    static int reg = 0; HINSTANCE hi = GetModuleHandleW(NULL); HWND dlg, ed, ok, cancel, st; RECT pr, cr;
    int W = S(600), H = S(176), cw, ch; MSG msg; BOOL dk = g_dark; const wchar_t *thm = g_dark ? L"DarkMode_Explorer" : L"Explorer";
    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof wc);
        wc.lpfnWndProc = InputProc; wc.hInstance = hi; wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.lpszClassName = L"MDViewInput";
        RegisterClassW(&wc); reg = 1;
    }
    GetWindowRect(hMain, &pr);
    dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MDViewInput", title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                          pr.left + (pr.right - pr.left - W) / 2, pr.top + S(110), W, H, hMain, NULL, hi, NULL);
    if (!dlg) return 0;
    DwmSetWindowAttribute(dlg, 20, &dk, sizeof dk);
    GetClientRect(dlg, &cr); cw = cr.right; ch = cr.bottom;
    st = CreateWindowExW(0, L"STATIC", prompt, WS_CHILD | WS_VISIBLE, S(16), S(14), cw - S(32), S(20), dlg, NULL, hi, NULL);
    ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, S(16), S(40), cw - S(32), S(28), dlg, (HMENU)100, hi, NULL);
    ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, cw - S(208), ch - S(46), S(92), S(30), dlg, (HMENU)IDOK, hi, NULL);
    cancel = CreateWindowExW(0, L"BUTTON", L"Zrušiť", WS_CHILD | WS_VISIBLE | WS_TABSTOP, cw - S(108), ch - S(46), S(92), S(30), dlg, (HMENU)IDCANCEL, hi, NULL);
    SendMessageW(st, WM_SETFONT, (WPARAM)hUiFont, 0); SendMessageW(ed, WM_SETFONT, (WPARAM)hUiFont, 0);
    SendMessageW(ok, WM_SETFONT, (WPARAM)hUiFont, 0); SendMessageW(cancel, WM_SETFONT, (WPARAM)hUiFont, 0);
    SetWindowTheme(ok, thm, NULL); SetWindowTheme(cancel, thm, NULL); SetWindowTheme(ed, thm, NULL);
    SendMessageW(ed, EM_LIMITTEXT, cap - 1, 0); SendMessageW(ed, EM_SETSEL, 0, -1);
    EnableWindow(hMain, FALSE);
    ShowWindow(dlg, SW_SHOW); SetFocus(ed);
    g_ibDone = g_ibOk = 0;
    while (!g_ibDone) {
        int r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) { PostQuitMessage(0); break; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) { if (GetFocus() == cancel) g_ibDone = 1; else { g_ibOk = 1; g_ibDone = 1; } continue; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { g_ibDone = 1; continue; }
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (g_ibOk) GetWindowTextW(ed, buf, cap);
    EnableWindow(hMain, TRUE); DestroyWindow(dlg); SetForegroundWindow(hMain);
    return g_ibOk;
}
static void trim_ws(wchar_t *s) {   /* medzery a uvodzovky okolo (napr. z "Kopirovat ako cestu") */
    int n = (int)wcslen(s), a = 0;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '"' || s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\t')) n--;
    while (a < n && (s[a] == ' ' || s[a] == '"' || s[a] == '\t')) a++;
    memmove(s, s + a, (n - a) * sizeof(wchar_t)); s[n - a] = 0;
}
/* adresa zo schranky, ak tam je odkaz alebo cesta (predvyplni okno) */
static void clip_loc(wchar_t *out, int cap) {
    HANDLE h; wchar_t *p, t[1100];
    out[0] = 0;
    if (!OpenClipboard(hMain)) return;
    if ((h = GetClipboardData(CF_UNICODETEXT)) != NULL && (p = (wchar_t *)GlobalLock(h)) != NULL) {
        lstrcpynW(t, p, 1100); GlobalUnlock(h);
        if ((p = wcspbrk(t, L"\r\n")) != NULL) *p = 0;
        trim_ws(t);
        if (is_url(t) || (t[0] && t[1] == ':' && t[2] == '\\') || (t[0] == '\\' && t[1] == '\\')) lstrcpynW(out, t, cap);
    }
    CloseClipboard();
}
static int ask_location(wchar_t *v, int cap, const wchar_t *title) {
    clip_loc(v, cap);
    if (!input_box(title, L"Webová adresa (https://\x2026) alebo cesta k súboru v počítači:", v, cap)) return 0;
    trim_ws(v);
    return v[0] != 0;
}
/* Ctrl+L: otvori webovu adresu alebo cestu */
static void open_url_dialog(void) {
    wchar_t v[1100], *h;
    if (!ask_location(v, 1100, L"Otvoriť adresu")) return;
    if (is_url(v) && (h = wcschr(v, '#')) != NULL) { *h = 0; open_location(v, h + 1); }
    else open_location(v, NULL);
}
/* normalizovany tvar adresy, v akom sa uklada a porovnava */
static void loc_key(const wchar_t *v, wchar_t *out, int cap) {
    if (is_url(v)) { wchar_t t[2200]; normalize_url(v, t, 2200); url_encode(t, out, cap); }
    else if (!GetFullPathNameW(v, cap, out, NULL)) lstrcpynW(out, v, cap);
}
/* ulozi odkaz bez otvorenia dokumentu (z uvodnej stranky) */
static void add_ref_dialog(void) {
    wchar_t v[1100], key[1100], name[200];
    if (!ask_location(v, 1100, L"Pridať odkaz")) return;
    loc_key(v, key, 1100);
    loc_name(key, name, 200);
    if (!input_box(L"Pridať odkaz", L"Názov odkazu:", name, 200)) return;
    trim_ws(name);
    bm_add(name, key);
    if (!g_path[0]) render(0);
}
/* ☆: ulozi otvoreny dokument medzi odkazy, alebo ho z nich odoberie */
static void star_toggle(void) {
    int i; wchar_t name[200], m[400];
    if (!g_path[0]) { MessageBoxW(hMain, L"Najprv otvorte dokument (alebo ho uložte). Odkaz bez otvorenia pridáte na úvodnej stránke cez „Pridať odkaz“.", APP, MB_ICONINFORMATION); return; }
    i = bm_find(g_path);
    if (i >= 0) {
        _snwprintf(m, 399, L"Odobrať „%s“ z uložených odkazov?", g_bm[i].name); m[399] = 0;
        if (MessageBoxW(hMain, m, APP, MB_YESNO | MB_ICONQUESTION) == IDYES) bm_del(i);
    } else {
        wchar_t *w = get_text(hEdit, 0, NULL);
        doc_title(w, name, 200); free(w);
        if (!name[0]) loc_name(g_path, name, 200);
        if (input_box(L"Uložiť odkaz", L"Názov odkazu (zobrazí sa na úvodnej stránke a v menu Odkazy):", name, 200)) { trim_ws(name); bm_add(name, g_path); }
    }
    star_update();
}
/* odkazy "mdview:..." na uvodnej stranke */
static void home_cmd(const wchar_t *c) {
    const wchar_t *q = wcschr(c, '?'); int k = q ? _wtoi(q + 1) : -1; wchar_t loc[1100];
    if (!wcsncmp(c, L"open", 4)) do_open();
    else if (!wcsncmp(c, L"url", 3)) open_url_dialog();
    else if (!wcsncmp(c, L"add", 3)) add_ref_dialog();
    else if (!wcsncmp(c, L"help", 4)) set_help(1);
    else if (!wcsncmp(c, L"bm", 2) && k >= 0 && k < g_nbm) { lstrcpynW(loc, g_bm[k].loc, 1100); open_location(loc, NULL); }
    else if (!wcsncmp(c, L"rc", 2) && k >= 0 && k < g_nrc) { lstrcpynW(loc, g_rc[k].loc, 1100); open_location(loc, NULL); }
    else if (!wcsncmp(c, L"del", 3) && k >= 0 && k < g_nbm) {
        wchar_t m[400]; _snwprintf(m, 399, L"Odstrániť odkaz „%s“?\n\n(Súbor ani stránka sa nemažú, len odkaz v MD View.)", g_bm[k].name); m[399] = 0;
        if (MessageBoxW(hMain, m, APP, MB_YESNO | MB_ICONQUESTION) == IDYES) { bm_del(k); render(0); }
    }
}
static void menu_text(const wchar_t *name, const wchar_t *right, wchar_t *out, int cap) {
    int k = 0; const wchar_t *p;
    for (p = name; *p && k < cap - 40; p++) { if (*p == '&') out[k++] = '&'; out[k++] = *p; }
    out[k] = 0;
    if (right) { wcscat(out, L"\t"); wcscat(out, right); }
}
/* tlacidlo "Odkazy ▾": ulozene a nedavne dokumenty bez zatvarania aktualneho */
static void links_menu(void) {
    HMENU m = CreatePopupMenu(), r = CreatePopupMenu(); RECT rc; int cmd, i; wchar_t t[300], loc[1100];
    for (i = 0; i < g_nbm && i < 80; i++) { menu_text(g_bm[i].name, is_url(g_bm[i].loc) ? L"web" : L"PC", t, 300); AppendMenuW(m, MF_STRING | (!_wcsicmp(g_bm[i].loc, g_path) ? MF_CHECKED : 0), 1000 + i, t); }
    if (!g_nbm) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"(zatiaľ žiadne uložené odkazy)");
    for (i = 0; i < g_nrc; i++) { menu_text(g_rc[i].name, is_url(g_rc[i].loc) ? L"web" : L"PC", t, 300); AppendMenuW(r, MF_STRING, 2000 + i, t); }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_POPUP | (g_nrc ? 0 : MF_GRAYED), (UINT_PTR)r, L"Naposledy otvorené");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_OPENURL, L"Otvoriť webovú adresu alebo cestu\x2026\tCtrl+L");
    AppendMenuW(m, MF_STRING | (g_path[0] ? 0 : MF_GRAYED), ID_STAR, bm_find(g_path) >= 0 ? L"Odobrať z uložených odkazov\tCtrl+B" : L"Uložiť odkaz na tento dokument\tCtrl+B");
    AppendMenuW(m, MF_STRING, ID_HOME, L"Domov \x2014 zavrieť dokument\tCtrl+W");
    GetWindowRect(hBtn[12], &rc);
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom + S(2), 0, hMain, NULL);
    DestroyMenu(m);
    if (cmd >= 2000 && cmd - 2000 < g_nrc) { lstrcpynW(loc, g_rc[cmd - 2000].loc, 1100); open_location(loc, NULL); }
    else if (cmd >= 1000 && cmd - 1000 < g_nbm) { lstrcpynW(loc, g_bm[cmd - 1000].loc, 1100); open_location(loc, NULL); }
    else if (cmd) PostMessageW(hMain, WM_COMMAND, cmd, 0);
}
static void add_tip(HWND tip, HWND ctl, const wchar_t *text) {
    TOOLINFOW ti; ZeroMemory(&ti, sizeof ti);
    ti.cbSize = sizeof ti; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = hMain; ti.uId = (UINT_PTR)ctl; ti.lpszText = (LPWSTR)text;
    SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

/* ---------- hlavne okno ---------- */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        static const struct { int id; const wchar_t *t; } bt[14] = {
            { ID_OPEN, L"Otvoriť" }, { ID_SAVE, L"Uložiť" }, { ID_EDIT, L"Upraviť" }, { ID_THEME, L"Tmavý" },
            { ID_ZOOMOUT, L"A\x2212" }, { ID_ZOOMIN, L"A+" }, { ID_PREV, L"\x2039" }, { ID_NEXT, L"\x203A" }, { ID_HELP, L"Návod" }, { ID_CLEAR, L"\x2715" }, { ID_UPDATE, L"Aktualizovať" },
            { ID_HOME, L"Domov" }, { ID_LINKS, L"Odkazy \x25BE" }, { ID_STAR, L"\x2606" } };
        int i; HINSTANCE hi = GetModuleHandleW(NULL); HWND tip;
        hMain = h;
        hUiFont = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        for (i = 0; i < 14; i++) {
            hBtn[i] = CreateWindowExW(0, L"BUTTON", bt[i].t, WS_CHILD | (i == 10 ? 0 : WS_VISIBLE) | BS_OWNERDRAW, 0, 0, 10, 10, h, (HMENU)(INT_PTR)bt[i].id, hi, NULL);
            SendMessageW(hBtn[i], WM_SETFONT, (WPARAM)hUiFont, 0);
        }
        hSearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)IDC_SEARCH, hi, NULL);
        SendMessageW(hSearch, WM_SETFONT, (WPARAM)hUiFont, 0);
        SendMessageW(hSearch, EM_SETCUEBANNER, FALSE, (LPARAM)L"Hľadať  (Ctrl+F)");
        SendMessageW(hSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(3), S(3)));
        SendMessageW(hSearch, EM_LIMITTEXT, 250, 0);
        g_edProc = (WNDPROC)SetWindowLongPtrW(hSearch, GWLP_WNDPROC, (LONG_PTR)SearchProc);
        hCount = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 10, 10, h, (HMENU)IDC_COUNT, hi, NULL);
        SendMessageW(hCount, WM_SETFONT, (WPARAM)hUiFont, 0);
        hEdit = make_rich(h, IDC_EDITOR, 0);
        SendMessageW(hEdit, EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO, 0);
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE);
        hView = make_rich(h, IDC_VIEW, ES_READONLY);
        SendMessageW(hView, EM_SETUNDOLIMIT, 0, 0);
        g_lastPane = hView;
        tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, h, NULL, hi, NULL);
        add_tip(tip, hBtn[11], L"Zavrieť dokument a ukázať uložené odkazy (Ctrl+W)");
        add_tip(tip, hBtn[12], L"Uložené a naposledy otvorené dokumenty, webová adresa (Ctrl+L)");
        add_tip(tip, hBtn[13], L"Uložiť odkaz na tento dokument / odobrať ho (Ctrl+B)");
        add_tip(tip, hBtn[0], L"Otvoriť súbor z počítača (Ctrl+O)");
        add_tip(tip, hBtn[6], L"Predošlý výskyt (Shift+F3)");
        add_tip(tip, hBtn[7], L"Ďalší výskyt (F3)");
        add_tip(tip, hBtn[9], L"Zrušiť hľadanie (Esc)");
        hook_drop(h);
        layout();
        apply_theme();
        set_title(); star_update();
        return 0; }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            RECT r; layout(); GetClientRect(hView, &r);
            if (g_hasTable && r.right != g_lastViewW) SetTimer(h, T_RESIZE, 120, NULL);
            g_lastViewW = r.right;
        }
        return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = S(980); mm->ptMinTrackSize.y = S(300); return 0; }
    case WM_ERASEBKGND: {
        RECT rc; HDC dc = (HDC)wp; HPEN pen; HGDIOBJ ob, op; int foc = (GetFocus() == hSearch);
        GetClientRect(h, &rc); FillRect(dc, &rc, hbrBar);
        pen = CreatePen(PS_SOLID, foc ? S(2) : 1, foc ? T.accent : T.border);
        ob = SelectObject(dc, hbrEdit); op = SelectObject(dc, pen);
        RoundRect(dc, g_searchRc.left, g_searchRc.top, g_searchRc.right, g_searchRc.bottom, S(7), S(7));
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(pen);
        return 1; }
    case WM_SETFOCUS: if (g_lastPane) SetFocus(g_lastPane); return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, T.text); SetBkColor((HDC)wp, T.editBg); return (LRESULT)hbrEdit;
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, T.muted); SetBkColor((HDC)wp, T.bar); return (LRESULT)hbrBar;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp; wchar_t t[32]; int on = (d->CtlID == ID_EDIT && g_edit) || (d->CtlID == ID_HELP && g_help) || d->CtlID == ID_UPDATE;
        int flat = (d->CtlID == ID_CLEAR);   /* krizik lezi vnutri hladacieho pola */
        COLORREF bg = (d->itemState & ODS_SELECTED) ? T.btnDown : on ? T.accent : flat ? T.editBg : T.btn;
        HBRUSH br = CreateSolidBrush(bg); HPEN pen = CreatePen(PS_SOLID, 1, on ? T.accent : flat ? bg : T.border);
        HGDIOBJ ob, op;
        FillRect(d->hDC, &d->rcItem, flat ? hbrEdit : hbrBar);
        ob = SelectObject(d->hDC, br); op = SelectObject(d->hDC, pen);
        RoundRect(d->hDC, d->rcItem.left, d->rcItem.top, d->rcItem.right, d->rcItem.bottom, S(7), S(7));
        SelectObject(d->hDC, ob); SelectObject(d->hDC, op); DeleteObject(br); DeleteObject(pen);
        GetWindowTextW(d->hwndItem, t, 31);
        SetBkMode(d->hDC, TRANSPARENT); SetTextColor(d->hDC, on ? RGB(255, 255, 255) : (d->itemState & ODS_DISABLED) ? T.border : T.barText);
        SelectObject(d->hDC, hUiFont);
        DrawTextW(d->hDC, t, -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE; }
    case WM_SETCURSOR:
        if ((HWND)wp == h && LOWORD(lp) == HTCLIENT && g_edit) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            if (pt.y > S(44)) { SetCursor(LoadCursorW(NULL, IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_LBUTTONDOWN:
        if (g_edit && GET_Y_LPARAM(lp) > S(44)) { g_dragSplit = 1; SetCapture(h); }
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragSplit) {
            RECT rc; GetClientRect(h, &rc);
            g_split = (double)GET_X_LPARAM(lp) / (rc.right > 1 ? rc.right : 1);
            if (g_split < 0.15) g_split = 0.15; if (g_split > 0.85) g_split = 0.85;
            layout();
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragSplit) { g_dragSplit = 0; ReleaseCapture(); if (g_hasTable) render(0); sync_from(hEdit); }
        return 0;
    case WM_TIMER:
        KillTimer(h, wp);
        if (wp == T_IMG) {   /* doplnili sa obrazky z webu: na vrchu ostane to iste miesto */
            int cp = view_top_cp();
            render(0);
            g_syncing++; sc_set(hView, sc_y(hView, cp, sc_get(hView), sc_top(hView))); g_syncing--;
            sync_mark();
        }
        else if (wp == T_RENDER || wp == T_RESIZE) { render(0); sync_from(hEdit); }
        else if (wp == T_FIND) find_changed();
        return 0;
    case WM_APP_DOC: doc_arrived((NetJob *)lp); return 0;
    case WM_APP_IMG: img_arrived((NetJob *)lp); SetTimer(h, T_IMG, 150, NULL); return 0;
    case WM_APP_UPDCHECK:
        g_updBusy = 0;
        if (lp == 1) { upd_button(L"Aktualizovať", TRUE); if (wp || g_updTest) upd_ask(); }
        else if (wp) {
            wchar_t m[200];
            if (lp == 0) { _snwprintf(m, 199, L"Máte najnovšiu verziu (%S).", g_ver); m[199] = 0; MessageBoxW(h, m, APP, MB_ICONINFORMATION); }
            else MessageBoxW(h, L"Nepodarilo sa overiť novú verziu. Skontrolujte pripojenie na internet.", APP, MB_ICONWARNING);
        }
        return 0;
    case WM_APP_UPDDONE: {
        int r = lp ? upd_apply() : 0;
        g_updBusy = 0;
        if (r == 1) return 0;
        upd_button(L"Aktualizovať", TRUE);
        if (r == 0 && !g_updTest && MessageBoxW(h, L"Aktualizáciu sa nepodarilo stiahnuť alebo nainštalovať.\n(Priečinok s aplikáciou musí byť zapisovateľný.)\n\nOtvoriť stránku na ručné stiahnutie?", APP, MB_YESNO | MB_ICONWARNING) == IDYES)
            ShellExecuteW(h, L"open", UPD_PAGE, NULL, NULL, SW_SHOWNORMAL);
        return 0; }
    case WM_APP_DROP:
        SetForegroundWindow(h);
        { wchar_t p[1100], *hs; lstrcpynW(p, g_dropPath, 1100);
          if (is_url(p) && (hs = wcschr(p, '#')) != NULL) { *hs = 0; open_location(p, hs + 1); } else open_location(p, NULL); }
        return 0;
    case WM_DROPFILES: {
        wchar_t path[1024]; HDROP d = (HDROP)wp;
        if (DragQueryFileW(d, 0, path, 1024)) { DragFinish(d); SetForegroundWindow(h); open_location(path, NULL); }
        else DragFinish(d);
        return 0; }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDC_EDITOR) { if (code == EN_CHANGE && !g_loading) { set_modified(1); SetTimer(h, T_RENDER, g_nuse ? 600 : 250, NULL); } return 0; }
        if (id == IDC_SEARCH) {
            if (code == EN_CHANGE) { SetTimer(h, T_FIND, 200, NULL); ShowWindow(hBtn[9], GetWindowTextLengthW(hSearch) ? SW_SHOW : SW_HIDE); }
            else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) InvalidateRect(h, &g_searchRc, TRUE);
            return 0;
        }
        if (id == IDC_VIEW || id == IDC_COUNT) return 0;
        switch (id) {
        case ID_OPEN: do_open(); break;
        case ID_SAVE: do_save(0); break;
        case ID_SAVEAS: do_save(1); break;
        case ID_HELP: set_help(!g_help); break;
        case ID_UPDATE: upd_ask(); break;
        case ID_UPDCHECK: upd_check(1); break;
        case ID_NEW: if (confirm_discard()) { g_help = 0; InvalidateRect(hBtn[8], NULL, TRUE); g_path[0] = 0; g_online = 0; set_editor_text(L""); g_modified = 0; set_title(); star_update(); render(1); if (!g_edit) set_edit_mode(1); } return 0;
        case ID_RELOAD: if (g_path[0]) { wchar_t p[1024]; wcscpy(p, g_path); img_clear(); open_location(p, NULL); } else render(0); break;
        case ID_HOME: go_home(); break;
        case ID_LINKS: links_menu(); break;
        case ID_STAR: star_toggle(); break;
        case ID_OPENURL: open_url_dialog(); break;
        case ID_EDIT: set_edit_mode(!g_edit); return 0;
        case ID_THEME: g_dark = !g_dark; apply_theme(); break;
        case ID_ZOOMIN: set_zoom(g_fs + 2); break;
        case ID_ZOOMOUT: set_zoom(g_fs - 2); break;
        case ID_ZOOM0: set_zoom(22); break;
        case ID_FIND: SetFocus(hSearch); SendMessageW(hSearch, EM_SETSEL, 0, -1); return 0;
        case ID_NEXT: case ID_PREV:
            KillTimer(h, T_FIND);
            { wchar_t cur[256]; GetWindowTextW(hSearch, cur, 255); if (wcscmp(cur, g_find)) find_changed(); else do_find(id == ID_NEXT ? 1 : -1, 0); }
            if (GetFocus() == hSearch) return 0;
            break;
        case ID_ESC: case ID_CLEAR:
            if (id == ID_CLEAR || g_find[0] || GetWindowTextLengthW(hSearch) || GetFocus() == hSearch) {
                KillTimer(h, T_FIND); SetWindowTextW(hSearch, L""); KillTimer(h, T_FIND);
                if (g_find[0]) { g_find[0] = 0; render(0); }
                SetWindowTextW(hCount, L"");
            } else if (g_help) set_help(0);
            break;
        default: return 0;
        }
        if (lp || GetFocus() == hSearch || !GetFocus()) SetFocus(g_edit && g_lastPane == hEdit ? hEdit : hView);
        return 0; }
    case WM_CLOSE: if (confirm_discard()) DestroyWindow(h); return 0;
    case WM_DESTROY: reg_save(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show) {
    static ACCEL acc[] = {
        { FCONTROL | FVIRTKEY, 'O', ID_OPEN }, { FCONTROL | FVIRTKEY, 'S', ID_SAVE }, { FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_SAVEAS },
        { FCONTROL | FVIRTKEY, 'E', ID_EDIT }, { FCONTROL | FVIRTKEY, 'D', ID_THEME }, { FCONTROL | FVIRTKEY, 'F', ID_FIND },
        { FCONTROL | FVIRTKEY, 'N', ID_NEW }, { FVIRTKEY, VK_F5, ID_RELOAD }, { FVIRTKEY, VK_F1, ID_HELP }, { FCONTROL | FVIRTKEY, 'U', ID_UPDCHECK },
        { FVIRTKEY, VK_F3, ID_NEXT }, { FSHIFT | FVIRTKEY, VK_F3, ID_PREV }, { FVIRTKEY, VK_ESCAPE, ID_ESC },
        { FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_ZOOMIN }, { FCONTROL | FVIRTKEY, VK_ADD, ID_ZOOMIN },
        { FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_ZOOMOUT }, { FCONTROL | FVIRTKEY, VK_SUBTRACT, ID_ZOOMOUT },
        { FCONTROL | FVIRTKEY, '0', ID_ZOOM0 },
        { FCONTROL | FVIRTKEY, 'W', ID_HOME }, { FCONTROL | FVIRTKEY, 'L', ID_OPENURL }, { FCONTROL | FVIRTKEY, 'B', ID_STAR } };
    WNDCLASSEXW wc; MSG msg; HACCEL ha; HDC dc; int argc = 0; wchar_t **argv;
    (void)hp; (void)cmd;

    OleInitialize(NULL);
    InitCommonControls();
    if (!LoadLibraryW(L"Msftedit.dll")) { MessageBoxW(NULL, L"Chýba Msftedit.dll.", APP, MB_ICONERROR); return 1; }
    dc = GetDC(NULL); g_dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc);
    reg_load();
    refs_load();
    T = g_dark ? DARK : LIGHT; hbrBar = CreateSolidBrush(T.bar); hbrEdit = CreateSolidBrush(T.editBg);

    ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"MDViewMain";
    RegisterClassExW(&wc);
    CreateWindowExW(0, L"MDViewMain", APP, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, S(1100), S(780), NULL, NULL, hi, NULL);
    if (!hMain) return 1;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) open_location(argv[1], NULL);
#ifdef MDV_TEST
    if (_wgetenv(L"MDV_TEST_VER")) { WideCharToMultiByte(CP_ACP, 0, _wgetenv(L"MDV_TEST_VER"), -1, g_ver, 31, NULL, NULL); g_updTest = 1; }
#endif
    upd_startup();
#ifdef MDV_TEST
    {   /* samotest: indexy hladania a drop target (len v testovacom builde) */
        wchar_t *out = _wgetenv(L"MDV_TEST_OUT"), *dropf = _wgetenv(L"MDV_TEST_DROP");
        if (out) {
            FILE *f = _wfopen(out, L"w"); int cnt, m, from = 0, fc = 0, bad = 0, k, *hh; FINDTEXTEXW ft; HRESULT hv, hm;
            wcscpy(g_find, L"sa"); hh = find_all(hView, &cnt, &m);
            for (;;) {
                int found = 0;
                ft.chrg.cpMin = from; ft.chrg.cpMax = -1; ft.lpstrText = L"sa";
                if (SendMessageW(hView, EM_FINDTEXTEXW, FR_DOWN, (LPARAM)&ft) < 0) break;
                fc++; for (k = 0; k < cnt; k++) if (hh[k] == ft.chrgText.cpMin) { found = 1; break; }
                if (!found) bad++;
                from = ft.chrgText.cpMax;
            }
            fprintf(f, "search: mine=%d richedit=%d missing=%d\n", cnt, fc, bad);
            wcscpy(g_find, L"citanie"); free(hh); hh = find_all(hView, &cnt, &m); fprintf(f, "fold 'citanie' hits=%d\n", cnt); free(hh);
            g_find[0] = 0;
            hook_drop(hView); hv = g_hookHr; hook_drop(hMain); hm = g_hookHr;
            fprintf(f, "register view=0x%lx main=0x%lx\n", (unsigned long)hv, (unsigned long)hm);
            if (dropf) {
                IShellItem *it; IDataObject *d; POINTL p = { 0, 0 }; DWORD e = DROPEFFECT_COPY;
                if (SUCCEEDED(SHCreateItemFromParsingName(dropf, NULL, &IID_IShellItem, (void **)&it)) &&
                    SUCCEEDED(it->lpVtbl->BindToHandler(it, NULL, &BHID_DataObject, &IID_IDataObject, (void **)&d))) {
                    g_dt.lpVtbl->DragEnter(&g_dt, d, 0, p, &e); fprintf(f, "enter effect=%lu\n", (unsigned long)e);
                    e = DROPEFFECT_COPY; g_dt.lpVtbl->Drop(&g_dt, d, 0, p, &e);
                    fprintf(f, "drop effect=%lu path=%ls\n", (unsigned long)e, g_dropPath);
                } else fprintf(f, "data object failed\n");
            }
            fclose(f);
        }
        if (_wgetenv(L"MDV_TEST_LINK")) open_link(_wgetenv(L"MDV_TEST_LINK"));   /* ako klik na odkaz */
        if (_wgetenv(L"MDV_TEST_FIND")) {   /* predvyplni hladanie a skoci na 2. vyskyt */
            SetWindowTextW(hSearch, _wgetenv(L"MDV_TEST_FIND")); KillTimer(hMain, T_FIND);
            ShowWindow(hBtn[9], SW_SHOW); find_changed(); do_find(1, 0);
        }
        /* MDV_TEST_SYNC=n: editor na riadok n; MDV_TEST_SYNCV=y: nahlad na y px; MDV_TEST_DBL=x,y: dvojklik */
        if (_wgetenv(L"MDV_TEST_SYNC") || _wgetenv(L"MDV_TEST_SYNCV")) {
            POINT sp = { 0, 0 };
            set_edit_mode(1);
            if (_wgetenv(L"MDV_TEST_SYNC")) {
                sp.y = (int)ed_line_y(_wtoi(_wgetenv(L"MDV_TEST_SYNC")), sc_get(hEdit), sc_top(hEdit));
                SendMessageW(hEdit, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
            } else {
                sp.y = _wtoi(_wgetenv(L"MDV_TEST_SYNCV"));
                SendMessageW(hView, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
            }
            if (_wgetenv(L"MDV_TEST_DBL")) {
                int x = 0, y = 0; swscanf(_wgetenv(L"MDV_TEST_DBL"), L"%d,%d", &x, &y);
                SendMessageW(hView, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(x, y));
            }
        }
    }
#endif
    ShowWindow(hMain, show); UpdateWindow(hMain);
    ha = CreateAcceleratorTableW(acc, sizeof acc / sizeof acc[0]);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (TranslateAcceleratorW(hMain, ha, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    OleUninitialize();
    return 0;
}
