/* ============================================================
 * etb_gen.c - 生成 C / Python 可运行代码 (含积木逻辑)
 * 版权: (c) ET 2024-2026
 * ============================================================ */
#include "etb.h"
#include <stdarg.h>

/* ---------- 增长型 wchar 缓冲 ---------- */
typedef struct { wchar_t *p; size_t len, cap; } WBuf;
static void wb_init(WBuf *b){ b->p=NULL; b->len=0; b->cap=0; }
static void wb_add(WBuf *b, const wchar_t *s){
    size_t n = wcslen(s);
    if (b->len + n + 1 > b->cap){ b->cap = (b->len + n + 1)*2 + 256; b->p = realloc(b->p, b->cap*sizeof(wchar_t)); }
    memcpy(b->p + b->len, s, n*sizeof(wchar_t));
    b->len += n; b->p[b->len]=0;
}
static void wb_addn(WBuf *b, const wchar_t *s, size_t n){
    if (b->len + n + 1 > b->cap){ b->cap = (b->len + n + 1)*2 + 256; b->p = realloc(b->p, b->cap*sizeof(wchar_t)); }
    memcpy(b->p + b->len, s, n*sizeof(wchar_t));
    b->len += n; b->p[b->len]=0;
}
static void wb_fmt(WBuf *b, const wchar_t *fmt, ...){
    wchar_t tmp[4096]; va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(tmp, 4095, fmt, ap);
    va_end(ap);
    tmp[4095]=0;
    wb_add(b, tmp);
}
static void wb_ind(WBuf *b, int depth){
    while (depth-- > 0) wb_add(b, L"    ");
}

/* 将用户文本转义为可嵌入 L"..." 字面量 */
static void esc_lit(const wchar_t *in, wchar_t *out, size_t cap){
    size_t pos=0;
    if (!in) in = L"";
    while (*in && pos+1 < cap){
        if (*in==L'\\'){ if (pos+2<cap){ out[pos++]=L'\\'; out[pos++]=L'\\'; } }
        else if (*in==L'"'){ if (pos+2<cap){ out[pos++]=L'\\'; out[pos++]=L'"'; } }
        else if (*in==L'\n'){ if (pos+2<cap){ out[pos++]=L'\\'; out[pos++]=L'n'; } }
        else if (*in==L'\r'){ /* 跳过 */ }
        else out[pos++] = *in;
        in++;
    }
    out[pos]=0;
}

/* ---------- 变量表 ---------- */
typedef struct { wchar_t name[128]; } VarEntry;
static VarEntry g_vars[64]; static int g_nvars;
static int var_index(const wchar_t *name){
    int i; for (i=0;i<g_nvars;i++) if (wcscmp(g_vars[i].name, name)==0) return i;
    if (g_nvars >= 64) return 0;
    wcscpy(g_vars[g_nvars].name, name); return g_nvars++;
}
static void collect_vars(Project *p){
    int i; g_nvars = 0;
    for (i=0;i<p->nblocks;i++){
        Block *b = &p->blocks[i];
        if (b->type==20 || b->type==21 || b->type==30) var_index(b->params[0]);
    }
}

/* ---------- 组件名解析 ---------- */
static int comp_idx_by_name(Project *p, const wchar_t *name){
    int i;
    if (!name || !name[0]) return -1;
    for (i=0;i<p->ncomps;i++) if (wcscmp(p->comps[i].text, name)==0) return i;
    {
        const wchar_t *prefixes[] = {L"按钮",L"标签",L"输入框",L"复选框",L"单选钮",L"下拉列表",L"进度条",L"图片"};
        int types[] = {CT_BUTTON,CT_LABEL,CT_ENTRY,CT_CHECK,CT_RADIO,CT_COMBO,CT_PROGRESS,CT_IMAGE};
        for (i=0;i<8;i++){
            size_t plen = wcslen(prefixes[i]);
            if (wcsncmp(name, prefixes[i], plen)==0){
                int id = _wtoi(name+plen);
                int j;
                if (id >= 0) for (j=0;j<p->ncomps;j++)
                    if (p->comps[j].id==id && p->comps[j].type==types[i]) return j;
            }
        }
    }
    for (i=0;i<p->ncomps;i++) if (p->comps[i].type==CT_BUTTON) return i;
    return -1;
}
static int comp_idx_by_name_any(Project *p, const wchar_t *name){
    int i;
    if (!name || !name[0]) return -1;
    for (i=0;i<p->ncomps;i++) if (wcscmp(p->comps[i].text, name)==0) return i;
    {
        const wchar_t *prefixes[] = {L"按钮",L"标签",L"输入框",L"复选框",L"单选钮",L"下拉列表",L"进度条",L"图片"};
        for (i=0;i<8;i++){
            size_t plen = wcslen(prefixes[i]);
            if (wcsncmp(name, prefixes[i], plen)==0){
                int id = _wtoi(name+plen);
                int j;
                if (id >= 0) for (j=0;j<p->ncomps;j++) if (p->comps[j].id==id) return j;
            }
        }
    }
    for (i=0;i<p->ncomps;i++) if (p->comps[i].type==CT_LABEL) return i;
    return -1;
}

/* ---------- 积木 -> C 语句 ---------- */
static void blocks_to_c(Project *p, WBuf *b, int from, int to){
    int i, depth=0;
    wchar_t t[256];
    for (i=from;i<to;i++){
        Block *bl = &p->blocks[i];
        if (etb_block_is_event(bl->type)) continue;
        switch (bl->type){
        case 10:
            esc_lit(bl->params[0], t, 256);
            wb_ind(b,depth); wb_fmt(b, L"MessageBoxW(hwnd_main, L\"%ls\", L\"提示\", MB_OK);\n", t); break;
        case 11: {
            int ci = comp_idx_by_name_any(p, bl->params[0]);
            if (ci>=0){ esc_lit(bl->params[1], t, 256); wb_ind(b,depth); wb_fmt(b, L"SetWindowTextW(g_h[%d], L\"%ls\");\n", ci, t); }
            break; }
        case 12: case 13: {
            esc_lit(bl->params[0], t, 256);
            wb_ind(b,depth); wb_fmt(b, L"ShellExecuteW(NULL, L\"open\", L\"%ls\", NULL, NULL, SW_SHOWNORMAL);\n", t); break; }
        case 14:
            wb_ind(b,depth); wb_add(b, L"PostQuitMessage(0);\n"); break;
        case 15: {
            int ci = comp_idx_by_name_any(p, bl->params[0]);
            if (ci>=0){ wb_ind(b,depth); wb_fmt(b, L"ShowWindow(g_h[%d], SW_HIDE);\n", ci); }
            break; }
        case 16: {
            int ci = comp_idx_by_name_any(p, bl->params[0]);
            if (ci>=0){ wb_ind(b,depth); wb_fmt(b, L"ShowWindow(g_h[%d], SW_SHOW);\n", ci); }
            break; }
        case 17:
            wb_ind(b,depth); wb_add(b, L"MessageBeep(MB_OK);\n"); break;
        case 18:
            wb_ind(b,depth); wb_fmt(b, L"Sleep(%ls);\n", bl->params[0]); break;
        case 20: {
            int vi = var_index(bl->params[0]);
            esc_lit(bl->params[1], t, 256);
            wb_ind(b,depth); wb_fmt(b, L"wcscpy(var_%d, L\"%ls\");\n", vi, t); break; }
        case 21: {
            int vi = var_index(bl->params[0]);
            esc_lit(bl->params[1], t, 256);
            wb_ind(b,depth); wb_fmt(b, L"{ wchar_t *_ep; long _v = wcstol(var_%d,&_ep,10); _v += wcstol(L\"%ls\",NULL,10); swprintf(var_%d,512,L\"%%ld\",_v); }\n", vi, t, vi);
            break; }
        case 30: {
            int vi = var_index(bl->params[0]);
            esc_lit(bl->params[1], t, 256);
            wb_ind(b,depth); wb_fmt(b, L"if (wcscmp(var_%d, L\"%ls\")==0) {\n", vi, t);
            depth++; break; }
        case 31:
            if (depth>0) depth--;
            wb_ind(b,depth); wb_add(b, L"}\n"); break;
        case 40:
            wb_ind(b,depth); wb_fmt(b, L"for (int _k%d=0;_k%d<%ls;_k%d++){\n", depth, depth, bl->params[0], depth);
            depth++; break;
        case 41:
            if (depth>0) depth--;
            wb_ind(b,depth); wb_add(b, L"}\n"); break;
        }
    }
    while (depth>0){ depth--; wb_ind(b,depth); wb_add(b, L"}\n"); }
}

/* ---------- 积木 -> Python 语句 ---------- */
static void blocks_to_py(Project *p, WBuf *b, int from, int to){
    int i, depth=0;
    for (i=from;i<to;i++){
        Block *bl = &p->blocks[i];
        if (etb_block_is_event(bl->type)) continue;
        switch (bl->type){
        case 10: wb_ind(b,depth); wb_fmt(b, L"messagebox.showinfo(\"提示\", \"%ls\")\n", bl->params[0]); break;
        case 11: {
            int ci = comp_idx_by_name_any(p, bl->params[0]);
            if (ci>=0){
                wb_ind(b,depth); wb_fmt(b, L"c = self.components.get(\"c%d\")\n", ci);
                wb_ind(b,depth); wb_add(b, L"if isinstance(c, tk.Entry): c.delete(0, tk.END); c.insert(0, ");
                wb_fmt(b, L"\"%ls\")\n", bl->params[1]);
                wb_ind(b,depth); wb_fmt(b, L"else: c.config(text=\"%ls\")\n", bl->params[1]);
            }
            break; }
        case 12: wb_ind(b,depth); wb_fmt(b, L"os.startfile(r\"%ls\")\n", bl->params[0]); break;
        case 13: wb_ind(b,depth); wb_fmt(b, L"webbrowser.open(\"%ls\")\n", bl->params[0]); break;
        case 14: wb_ind(b,depth); wb_add(b, L"self.root.destroy()\n"); break;
        case 15: {
            int ci = comp_idx_by_name_any(p, bl->params[0]);
            if (ci>=0){ wb_ind(b,depth); wb_fmt(b, L"self.components.get(\"c%d\").place_forget()\n", ci); }
            break; }
        case 16: {
            int ci = comp_idx_by_name_any(p, bl->params[0]);
            if (ci>=0){
                wb_ind(b,depth); wb_fmt(b, L"p = self._pos.get(\"c%d\")\n", ci);
                wb_ind(b,depth); wb_fmt(b, L"if p: self.components.get(\"c%d\").place(x=p[0], y=p[1], width=p[2], height=p[3])\n", ci);
            }
            break; }
        case 17: wb_ind(b,depth); wb_add(b, L"self.root.bell()\n"); break;
        case 18: wb_ind(b,depth); wb_fmt(b, L"time.sleep(%ls/1000.0)\n", bl->params[0]); break;
        case 20: wb_ind(b,depth); wb_fmt(b, L"self.vars[\"%ls\"] = \"%ls\"\n", bl->params[0], bl->params[1]); break;
        case 21:
            wb_ind(b,depth); wb_fmt(b, L"try: self.vars[\"%ls\"] = str(int(self.vars.get(\"%ls\", \"0\")) + int(\"%ls\"))\n", bl->params[0], bl->params[0], bl->params[1]);
            wb_ind(b,depth); wb_fmt(b, L"except: self.vars[\"%ls\"] = \"%ls\"\n", bl->params[0], bl->params[1]);
            break;
        case 30:
            wb_ind(b,depth); wb_fmt(b, L"if self.vars.get(\"%ls\") == \"%ls\":\n", bl->params[0], bl->params[1]);
            depth++; break;
        case 31:
            if (depth>0) depth--;
            wb_ind(b,depth); wb_add(b, L"pass\n");
            break;
        case 40:
            wb_ind(b,depth); wb_fmt(b, L"for _k in range(int(\"%ls\")):\n", bl->params[0]);
            depth++; break;
        case 41:
            if (depth>0) depth--;
            wb_ind(b,depth); wb_add(b, L"pass\n");
            break;
        }
    }
    while (depth>0){ depth--; wb_ind(b,depth); wb_add(b, L"pass\n"); }
}

/* 事件分段结构 */
#define SEC_MAX 64
typedef struct { int kind; int comp_idx; int from, to; } Sec;
static int build_secs(Project *p, Sec *secs, int cap){
    int nsecs=0, cur=-1, i;
    for (i=0;i<p->nblocks;i++){
        Block *bl = &p->blocks[i];
        if (etb_block_is_event(bl->type)){
            if (nsecs < cap){
                secs[nsecs].kind = bl->type;
                secs[nsecs].from = i+1; secs[nsecs].to = i+1;
                secs[nsecs].comp_idx = (bl->type==1) ? comp_idx_by_name(p, bl->params[0]) : -1;
                cur = nsecs; nsecs++;
            }
        } else if (cur >= 0){
            secs[cur].to = i+1;
        }
    }
    return nsecs;
}

/* ================= C 代码生成 ================= */
static char *buf_to_utf8(WBuf *b, size_t *outlen){
    /* 关键修复: cchWideChar 传显式长度(b->len)而非 -1。
       旧实现传 -1 使返回长度含结尾 NUL, WriteFile 把 NUL 写进文件,
       导致 main.py "cannot contain null bytes"、main.c 末尾空字符。 */
    int wlen = (int)(b->len);
    int n = WideCharToMultiByte(CP_UTF8, 0, b->p?b->p:L"", wlen, NULL, 0, NULL, NULL);
    if (n <= 0) n = 0;
    char *out = (char*)malloc(n+1);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, b->p?b->p:L"", wlen, out, n, NULL, NULL);
    out[n] = 0;
    if (outlen) *outlen = (size_t)n;   /* 长度不含结尾 NUL */
    free(b->p);
    return out;
}

char *etb_gen_c(Project *p, size_t *outlen){
    WBuf b; int i;
    Sec secs[SEC_MAX]; int nsecs;
    wchar_t t[256];
    wb_init(&b);
    collect_vars(p);
    nsecs = build_secs(p, secs, SEC_MAX);

    wb_add(&b, L"#include <windows.h>\n#include <commctrl.h>\n#include <shellapi.h>\n#include <gdiplus.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <wchar.h>\n\n");
    wb_add(&b, L"/* Generated by ETC WindowBuilder Professional v" VERSION_W L" - Copyright (c) ET */\n\n");
    wb_add(&b, L"static HWND hwnd_main;\n");
    wb_fmt(&b, L"static HWND g_h[%d] = {0};\n", p->ncomps>0?p->ncomps:1);
    wb_fmt(&b, L"static HBITMAP g_img[%d] = {0};\n", p->ncomps>0?p->ncomps:1);
    wb_fmt(&b, L"static HBRUSH g_br[%d] = {0};\n", p->ncomps>0?p->ncomps:1);
    wb_fmt(&b, L"static const wchar_t *g_col_bg[%d] = {", p->ncomps>0?p->ncomps:1);
    for (i=0;i<p->ncomps;i++) wb_fmt(&b, L"L\"%ls\"%s", p->comps[i].bg, i+1<p->ncomps?L",":L"");
    wb_add(&b, L"};\n");
    wb_fmt(&b, L"static const wchar_t *g_col_fg[%d] = {", p->ncomps>0?p->ncomps:1);
    for (i=0;i<p->ncomps;i++) wb_fmt(&b, L"L\"%ls\"%s", p->comps[i].fg, i+1<p->ncomps?L",":L"");
    wb_add(&b, L"};\n");
    wb_add(&b, L"static COLORREF et_col(const wchar_t *s){\n    unsigned v; if (!s || s[0]!=L'#') return RGB(43,43,43);\n    if (swscanf(s+1, L\"%x\", &v)!=1) return RGB(43,43,43);\n    return RGB((v>>16)&255,(v>>8)&255,v&255);\n}\n\n");
    for (i=0;i<g_nvars;i++) wb_fmt(&b, L"static wchar_t var_%d[512] = L\"\";\n", i);
    wb_add(&b, L"\n");

    wb_add(&b, L"static void on_init(void){\n");
    for (i=0;i<nsecs;i++) if (secs[i].kind==0) blocks_to_c(p, &b, secs[i].from, secs[i].to);
    wb_add(&b, L"}\n\n");
    wb_add(&b, L"static void on_close(void){\n");
    for (i=0;i<nsecs;i++) if (secs[i].kind==2) blocks_to_c(p, &b, secs[i].from, secs[i].to);
    wb_add(&b, L"}\n\n");
    for (i=0;i<nsecs;i++){
        if (secs[i].kind==1 && secs[i].comp_idx>=0){
            wb_fmt(&b, L"static void on_btn_%d(void){\n", secs[i].comp_idx);
            blocks_to_c(p, &b, secs[i].from, secs[i].to);
            wb_add(&b, L"}\n\n");
        }
    }

    wb_add(&b, L"LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){\n");
    wb_add(&b, L"    switch(msg){\n");
    wb_add(&b, L"    case WM_CREATE:{\n        int _i;\n");
    wb_fmt(&b, L"        for(_i=0;_i<%d;_i++) g_br[_i]=CreateSolidBrush(et_col(g_col_bg[_i]));\n", p->ncomps);
    wb_add(&b, L"        hwnd_main = hwnd;\n");
    for (i=0;i<p->ncomps;i++){
        Component *c = &p->comps[i];
        const wchar_t *cls, *sty;
        switch (c->type){
        case CT_BUTTON: cls=L"BUTTON"; sty=L""; break;
        case CT_LABEL:  cls=L"STATIC"; sty=L"|SS_CENTER"; break;
        case CT_ENTRY:  cls=L"EDIT";   sty=L"|WS_BORDER|ES_AUTOHSCROLL"; break;
        case CT_CHECK:  cls=L"BUTTON"; sty=L"|BS_AUTOCHECKBOX"; break;
        case CT_RADIO:  cls=L"BUTTON"; sty=L"|BS_AUTORADIOBUTTON"; break;
        case CT_COMBO:  cls=L"COMBOBOX"; sty=L"|CBS_DROPDOWNLIST"; break;
        default:        cls=L"msctls_progress32"; sty=L""; break;
        }
        esc_lit(c->text, t, 256);
        wb_fmt(&b, L"        g_h[%d] = CreateWindowW(L\"%ls\", L\"%ls\", WS_CHILD|WS_VISIBLE%ls, %d, %d, %d, %d, hwnd, (HMENU)(%d), ((LPCREATESTRUCT)lp)->hInstance, NULL);\n",
            i, cls, t, sty, c->x, c->y, c->w, c->h, 1000+i);
        wb_fmt(&b, L"        SendMessageW(g_h[%d], WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);\n", i);
        if (c->type==CT_COMBO){
            wchar_t vals[512];
            wchar_t *p, *st;
            wcscpy(vals, c->values);
            p = vals; st = vals;
            for (;;){
                if (*p==L',' || *p==0){
                    wchar_t save = *p;
                    *p = 0;
                    if (*st){ esc_lit(st, t, 256); wb_fmt(&b, L"        SendMessageW(g_h[%d], CB_ADDSTRING, 0, (LPARAM)L\"%ls\");\n", i, t); }
                    if (save==0) break;
                    p++; st = p;
                } else p++;
            }
        }
        if (c->type==CT_PROGRESS){
            wb_fmt(&b, L"        SendMessageW(g_h[%d], PBM_SETRANGE, 0, MAKELPARAM(0,100));\n", i);
            wb_fmt(&b, L"        SendMessageW(g_h[%d], PBM_SETPOS, 50, 0);\n", i);
        }
        if (c->type==CT_IMAGE && c->image_path[0]){
            esc_lit(c->image_path, t, 256);
            wb_fmt(&b, L"        { GpBitmap *_bm=NULL; if (GdipCreateBitmapFromFile(L\"%ls\", &_bm)==Ok){ HBITMAP _hb=NULL; if (GdipCreateHBITMAPFromBitmap(_bm,&_hb,0)==Ok) g_img[%d]=_hb; GdipDisposeImage((GpImage*)_bm); } }\n", t, i);
        }
    }
    wb_add(&b, L"        on_init();\n        return 0;\n    }\n");
    wb_add(&b, L"    case WM_COMMAND:{\n        switch(LOWORD(wp)){\n");
    for (i=0;i<nsecs;i++)
        if (secs[i].kind==1 && secs[i].comp_idx>=0)
            wb_fmt(&b, L"        case %d: on_btn_%d(); break;\n", 1000+secs[i].comp_idx, secs[i].comp_idx);
    wb_add(&b, L"        }\n        break;\n    }\n");
    wb_add(&b, L"    case WM_CTLCOLORBTN: case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT:{\n        int _i; for (_i=0;_i<");
    wb_fmt(&b, L"%d;_i++){ if (g_h[_i]==(HWND)lp){ SetTextColor((HDC)wp, et_col(g_col_fg[_i])); SetBkColor((HDC)wp, et_col(g_col_bg[_i])); return (LRESULT)g_br[_i]; } }\n        break;\n    }\n", p->ncomps);
    {
        int has_img = 0; for (i=0;i<p->ncomps;i++) if (p->comps[i].type==CT_IMAGE && p->comps[i].image_path[0]) has_img=1;
        if (has_img){
            wb_add(&b, L"    case WM_PAINT:{ PAINTSTRUCT _ps; HDC _hdc=BeginPaint(hwnd,&_ps);\n");
            for (i=0;i<p->ncomps;i++){
                Component *c = &p->comps[i];
                if (c->type==CT_IMAGE && c->image_path[0]){
                    wb_fmt(&b, L"        if (g_img[%d]){ HDC _m=CreateCompatibleDC(_hdc); HGDIOBJ _o=SelectObject(_m,g_img[%d]); BITMAP _bm; GetObject(g_img[%d],sizeof(_bm),&_bm); StretchBlt(_hdc,%d,%d,%d,%d,_m,0,0,_bm.bmWidth,_bm.bmHeight,SRCCOPY); SelectObject(_m,_o); DeleteDC(_m); }\n", i,i,i,c->x,c->y,c->w,c->h);
                }
            }
            wb_add(&b, L"        EndPaint(hwnd,&_ps); return 0; }\n");
        }
    }
    wb_add(&b, L"    case WM_CLOSE: on_close(); DestroyWindow(hwnd); return 0;\n");
    wb_add(&b, L"    case WM_DESTROY:{ int _i; for(_i=0;_i<");
    wb_fmt(&b, L"%d;_i++){ if(g_br[_i]) DeleteObject(g_br[_i]); if(g_img[_i]) DeleteObject(g_img[_i]); } PostQuitMessage(0); return 0; }\n", p->ncomps);
    wb_add(&b, L"    }\n    return DefWindowProcW(hwnd, msg, wp, lp);\n}\n\n");

    wb_add(&b, L"int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nCmdShow){\n");
    wb_add(&b, L"    INITCOMMONCONTROLSEX _icex; _icex.dwSize=sizeof(_icex); _icex.dwICC=ICC_WIN95_CLASSES; InitCommonControlsEx(&_icex);\n");
    wb_add(&b, L"    GdiplusStartupInput _gsi; ULONG_PTR _token; memset(&_gsi,0,sizeof(_gsi)); _gsi.GdiplusVersion=1; GdiplusStartup(&_token,&_gsi,NULL);\n");
    wb_add(&b, L"    WNDCLASSEXW _wc; memset(&_wc,0,sizeof(_wc)); _wc.cbSize=sizeof(_wc); _wc.lpfnWndProc=WndProc; _wc.hInstance=hInstance; _wc.hCursor=LoadCursor(NULL,IDC_ARROW);\n");
    esc_lit(p->bg, t, 256);
    wb_fmt(&b, L"    _wc.hbrBackground=CreateSolidBrush(et_col(L\"%ls\")); _wc.lpszClassName=L\"ETCGenApp\";\n", t);
    wb_add(&b, L"    RegisterClassExW(&_wc);\n");
    {
        int w=800,h=600; wchar_t s[64]; wchar_t *x;
        wcscpy(s, p->size);
        x = wcschr(s, L'x'); if (!x) x = wcschr(s, L'X');
        if (x){ *x=0; w=_wtoi(s); h=_wtoi(x+1); if (w<=0) w=800; if (h<=0) h=600; }
        esc_lit(p->title, t, 256);
        wb_fmt(&b, L"    CreateWindowExW(0, L\"ETCGenApp\", L\"%ls\", WS_OVERLAPPEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, 0, %d, %d, NULL, NULL, hInstance, NULL);\n", t, w, h);
    }
    wb_add(&b, L"    MSG _msg; while(GetMessageW(&_msg,NULL,0,0)){ TranslateMessage(&_msg); DispatchMessageW(&_msg); }\n");
    wb_add(&b, L"    GdiplusShutdown(_token);\n    return (int)_msg.wParam;\n}\n");

    return buf_to_utf8(&b, outlen);
}

/* ================= Python 代码生成 ================= */
char *etb_gen_py(Project *p, size_t *outlen){
    WBuf b; int i;
    Sec secs[SEC_MAX]; int nsecs;
    wb_init(&b);
    nsecs = build_secs(p, secs, SEC_MAX);

    wb_add(&b, L"# -*- coding: utf-8 -*-\n");
    wb_add(&b, L"# Generated by ETC WindowBuilder Professional v" VERSION_W L" - Copyright (c) ET\n");
    wb_add(&b, L"import tkinter as tk\nfrom tkinter import ttk, messagebox\nimport os, time, webbrowser, sys\n\n");
    wb_add(&b, L"class Program:\n    def __init__(self):\n        self.root = tk.Tk()\n");
    wb_fmt(&b, L"        self.root.title(\"%ls\")\n", p->title);
    wb_fmt(&b, L"        self.root.geometry(\"%ls\")\n", p->size);
    wb_fmt(&b, L"        self.root.configure(bg=\"%ls\")\n", p->bg);
    wb_add(&b, L"        self.vars = {}\n        self.components = {}\n        self._pos = {}\n        self.create_widgets()\n        self.on_init()\n\n");
    wb_add(&b, L"    def create_widgets(self):\n");
    if (p->ncomps==0) wb_add(&b, L"        pass  # 尚未添加组件\n");
    for (i=0;i<p->ncomps;i++){
        Component *c = &p->comps[i];
        wb_fmt(&b, L"        # 组件%d: %ls\n", c->id, c->text);
        switch (c->type){
        case CT_BUTTON:
            wb_fmt(&b, L"        b = tk.Button(self.root, text=\"%ls\", bg=\"%ls\", fg=\"%ls\")\n", c->text, c->bg, c->fg);
            wb_fmt(&b, L"        b.place(x=%d, y=%d, width=%d, height=%d)\n", c->x,c->y,c->w,c->h);
            wb_fmt(&b, L"        b.config(command=lambda i=%d: self.on_btn(i))\n", i);
            wb_fmt(&b, L"        self.components[\"c%d\"] = b; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n", i,i,c->x,c->y,c->w,c->h);
            break;
        case CT_LABEL:
            wb_fmt(&b, L"        l = tk.Label(self.root, text=\"%ls\", bg=\"%ls\", fg=\"%ls\")\n", c->text, c->bg, c->fg);
            wb_fmt(&b, L"        l.place(x=%d, y=%d, width=%d, height=%d)\n", c->x,c->y,c->w,c->h);
            wb_fmt(&b, L"        self.components[\"c%d\"] = l; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n", i,i,c->x,c->y,c->w,c->h);
            break;
        case CT_ENTRY:
            wb_fmt(&b, L"        e = tk.Entry(self.root, bg=\"%ls\", fg=\"%ls\", insertbackground=\"%ls\")\n", c->bg, c->fg, c->fg);
            wb_fmt(&b, L"        e.place(x=%d, y=%d, width=%d, height=%d)\n", c->x,c->y,c->w,c->h);
            wb_fmt(&b, L"        self.components[\"c%d\"] = e; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n", i,i,c->x,c->y,c->w,c->h);
            break;
        case CT_CHECK:
            wb_fmt(&b, L"        ch = tk.Checkbutton(self.root, text=\"%ls\", bg=\"%ls\", fg=\"%ls\", selectcolor=\"%ls\")\n", c->text, c->bg, c->fg, c->bg);
            wb_fmt(&b, L"        ch.place(x=%d, y=%d, width=%d, height=%d)\n", c->x,c->y,c->w,c->h);
            wb_fmt(&b, L"        self.components[\"c%d\"] = ch; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n", i,i,c->x,c->y,c->w,c->h);
            break;
        case CT_RADIO:
            wb_fmt(&b, L"        r = tk.Radiobutton(self.root, text=\"%ls\", bg=\"%ls\", fg=\"%ls\", selectcolor=\"%ls\")\n", c->text, c->bg, c->fg, c->bg);
            wb_fmt(&b, L"        r.place(x=%d, y=%d, width=%d, height=%d)\n", c->x,c->y,c->w,c->h);
            wb_fmt(&b, L"        self.components[\"c%d\"] = r; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n", i,i,c->x,c->y,c->w,c->h);
            break;
        case CT_COMBO: {
            wchar_t vals[512];
            wchar_t *p, *st;
            wcscpy(vals, c->values);
            wb_fmt(&b, L"        cb = ttk.Combobox(self.root, values=[");
            p = vals; st = vals;
            for (;;){
                if (*p==L',' || *p==0){
                    wchar_t save = *p;
                    *p = 0;
                    if (*st) wb_fmt(&b, L"\"%ls\",", st);
                    if (save==0) break;
                    p++; st = p;
                } else p++;
            }
            wb_add(&b, L"])\n");
            wb_fmt(&b, L"        cb.place(x=%d, y=%d, width=%d, height=%d)\n", c->x,c->y,c->w,c->h);
            wb_fmt(&b, L"        self.components[\"c%d\"] = cb; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n", i,i,c->x,c->y,c->w,c->h);
            break; }
        case CT_PROGRESS:
            wb_fmt(&b, L"        pr = ttk.Progressbar(self.root, length=%d, mode=\"determinate\")\n", c->w);
            wb_fmt(&b, L"        pr.place(x=%d, y=%d, width=%d, height=%d)\n", c->x,c->y,c->w,c->h);
            wb_fmt(&b, L"        self.components[\"c%d\"] = pr; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n", i,i,c->x,c->y,c->w,c->h);
            break;
        case CT_IMAGE:
            if (c->image_path[0]){
                wb_fmt(&b, L"        try:\n            from PIL import Image, ImageTk\n            im = Image.open(r\"%ls\").resize((%d,%d), Image.LANCZOS)\n            ph = ImageTk.PhotoImage(im)\n            il = tk.Label(self.root, image=ph, bg=\"%ls\")\n            il.image = ph\n            il.place(x=%d, y=%d, width=%d, height=%d)\n            self.components[\"c%d\"] = il; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n        except Exception:\n            il = tk.Label(self.root, text=\"图片加载失败\", bg=\"%ls\", fg=\"red\")\n            il.place(x=%d, y=%d, width=%d, height=%d)\n            self.components[\"c%d\"] = il; self._pos[\"c%d\"] = (%d,%d,%d,%d)\n",
                        c->image_path, c->w, c->h, c->bg, c->x,c->y,c->w,c->h, i,i,c->x,c->y,c->w,c->h, c->bg, c->x,c->y,c->w,c->h, i,i,c->x,c->y,c->w,c->h);
            }
            break;
        }
    }
    for (i=0;i<nsecs;i++){
        if (secs[i].kind==1 && secs[i].comp_idx>=0){
            wb_fmt(&b, L"    def on_btn(self, idx):\n        if idx == %d:\n", secs[i].comp_idx);
            {
                WBuf body; wchar_t *line, *nl;
                wb_init(&body);
                blocks_to_py(p, &body, secs[i].from, secs[i].to);
                if (!body.p || body.len==0){ wb_add(&b, L"            pass\n"); free(body.p); }
                else {
                line = body.p;
                while (line && *line){
                    nl = wcschr(line, L'\n');
                    if (nl){ wb_addn(&b, L"            ", 12); wb_addn(&b, line, (size_t)(nl-line)+1); line = nl+1; }
                    else { wb_add(&b, L"            "); wb_add(&b, line); break; }
                }
                free(body.p);
                }
            }
        }
    }
    {
        int has_btn = 0;
        for (i=0;i<nsecs;i++) if (secs[i].kind==1 && secs[i].comp_idx>=0) has_btn = 1;
        if (!has_btn) wb_add(&b, L"    def on_btn(self, idx):\n        pass\n");
    }
    wb_add(&b, L"    def on_init(self):\n");
    for (i=0;i<nsecs;i++) if (secs[i].kind==0){
        WBuf body; wchar_t *line, *nl;
        wb_init(&body);
        blocks_to_py(p, &body, secs[i].from, secs[i].to);
        line = body.p;
        while (line && *line){
            nl = wcschr(line, L'\n');
            if (nl){ wb_addn(&b, L"        ", 8); wb_addn(&b, line, (size_t)(nl-line)+1); line = nl+1; }
            else { wb_add(&b, L"        "); wb_add(&b, line); break; }
        }
        free(body.p);
    }
    wb_add(&b, L"        pass\n");
    wb_add(&b, L"    def run(self):\n        self.root.mainloop()\n\nif __name__ == \"__main__\":\n    Program().run()\n");

    return buf_to_utf8(&b, outlen);
}
