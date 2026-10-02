/* ============================================================
 * etb_ui.c - 主界面布局 / 设计画布 / 属性面板 / 日志
 * 版权: (c) ET 2024-2026
 * ============================================================ */
#include "etb_internal.h"

/* ---------- 图片缓存 ---------- */
typedef struct { wchar_t path[MAX_PATH]; HBITMAP hb; int w, h; } ImgEnt;
static ImgEnt g_imgs[48]; static int g_nimgs;
HBITMAP etb_load_image(const wchar_t *path, int *w, int *h){
    int i;
    if (w) *w=0; if (h) *h=0;
    if (!path || !path[0]) return NULL;
    for (i=0;i<g_nimgs;i++) if (wcscmp(g_imgs[i].path, path)==0){ if(w)*w=g_imgs[i].w; if(h)*h=g_imgs[i].h; return g_imgs[i].hb; }
    if (g_nimgs >= 48) return NULL;
    {
        /* 修复: PNG/JPEG 解码得到的是 GpBitmap, 必须用 GdipCreateBitmapFromFile,
           旧代码把 GdipLoadImageFromFile 的 GpImage* 直接传给
           GdipCreateHBITMAPFromBitmap(需要 GpBitmap*), 导致永远加载失败。 */
        GpBitmap *bm = NULL;
        HBITMAP hb = NULL; UINT iw=0, ih=0;
        if (GdipCreateBitmapFromFile(path, &bm) != Ok || !bm) return NULL;
        GdipGetImageWidth((GpImage*)bm, &iw); GdipGetImageHeight((GpImage*)bm, &ih);
        if (GdipCreateHBITMAPFromBitmap(bm, &hb, 0) == Ok && hb){
            g_imgs[g_nimgs].hb = hb;
            wcsncpy(g_imgs[g_nimgs].path, path, MAX_PATH-1);
            g_imgs[g_nimgs].path[MAX_PATH-1]=0;
            g_imgs[g_nimgs].w = (int)iw; g_imgs[g_nimgs].h = (int)ih;
            if (w) *w = (int)iw; if (h) *h = (int)ih;
            g_nimgs++;
        }
        GdipDisposeImage((GpImage*)bm);
        return hb;
    }
}

/* ---------- 日志 ---------- */
void etb_loga(const char *fmt, ...){
    char buf[1024]; va_list ap; wchar_t wbuf[2048];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 2048);
    etb_log(L"%ls", wbuf);
}
void etb_log(const wchar_t *fmt, ...){
    wchar_t tmp[2048]; va_list ap;
    SYSTEMTIME st;
    va_start(ap, fmt); _vsnwprintf(tmp, 2047, fmt, ap); va_end(ap); tmp[2047]=0;
    GetLocalTime(&st);
    {
        wchar_t line[2304];
        swprintf(line, 2304, L"[%02d:%02d:%02d] %ls\r\n", st.wHour, st.wMinute, st.wSecond, tmp);
        if (g_app.hLog){
            int len = GetWindowTextLengthW(g_app.hLog);
            SendMessageW(g_app.hLog, EM_SETSEL, len, len);
            SendMessageW(g_app.hLog, EM_REPLACESEL, 0, (LPARAM)line);
            SendMessageW(g_app.hLog, EM_SCROLLCARET, 0, 0);
        }
    }
}

/* ---------- 组件辅助 ---------- */
const wchar_t *comp_type_name(int type){
    switch (type){
    case CT_BUTTON: return L"按钮";
    case CT_LABEL: return L"标签";
    case CT_ENTRY: return L"输入框";
    case CT_CHECK: return L"复选框";
    case CT_RADIO: return L"单选钮";
    case CT_COMBO: return L"下拉列表";
    case CT_PROGRESS: return L"进度条";
    case CT_IMAGE: return L"图片";
    }
    return L"组件";
}
void comp_default_name(Component *c){
    if (c->text[0]) return;
    swprintf(c->text, 256, L"%ls%d", comp_type_name(c->type), c->id);
}
int comp_index_by_id(int id){
    int i;
    for (i=0;i<g_app.proj.ncomps;i++) if (g_app.proj.comps[i].id==id) return i;
    return -1;
}
static int comp_rect(Component *c, RECT *rc){
    int z = g_app.zoom;
    rc->left = c->x * z / 100; rc->top = c->y * z / 100;
    rc->right = rc->left + c->w * z / 100; rc->bottom = rc->top + c->h * z / 100;
    return 1;
}

/* ---------- 绘制组件形状 ---------- */
static COLORREF parse_color(const wchar_t *s){
    unsigned v;
    if (!s || s[0]!=L'#') return RGB(43,43,43);
    if (swscanf(s+1, L"%x", &v)!=1) return RGB(43,43,43);
    return RGB((v>>16)&255,(v>>8)&255,v&255);
}
static void draw_star(HDC hdc, int cx, int cy, int r, COLORREF col){
    POINT pts[10]; int i; double a;
    HBRUSH br = CreateSolidBrush(col); HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN pn = CreatePen(PS_SOLID,1,RGB(74,74,74)); HPEN op = (HPEN)SelectObject(hdc, pn);
    for (i=0;i<10;i++){
        a = -3.1415926/2 + i*3.1415926/5;
        pts[i].x = cx + (int)((i%2==0? r : r*0.42) * cos(a));
        pts[i].y = cy + (int)((i%2==0? r : r*0.42) * sin(a));
    }
    Polygon(hdc, pts, 10);
    SelectObject(hdc, ob); SelectObject(hdc, op); DeleteObject(br); DeleteObject(pn);
}
static void draw_shape(HDC hdc, Component *c, int x, int y, int w, int h){
    COLORREF bg = parse_color(c->bg);
    HBRUSH br = CreateSolidBrush(bg); HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN pn = CreatePen(PS_SOLID,1,RGB(90,90,90)); HPEN op = (HPEN)SelectObject(hdc, pn);
    if (wcscmp(c->shape, L"star")==0){
        draw_star(hdc, x+w/2, y+h/2, (w<h?w:h)/2, bg);
        SelectObject(hdc, ob); SelectObject(hdc, op); DeleteObject(br); DeleteObject(pn);
        return;
    }
    if (wcscmp(c->shape, L"diamond")==0){
        POINT pts[4] = {{x+w/2,y},{x+w,y+h/2},{x+w/2,y+h},{x,y+h/2}};
        Polygon(hdc, pts, 4);
        SelectObject(hdc, ob); SelectObject(hdc, op); DeleteObject(br); DeleteObject(pn);
        return;
    }
    if (wcscmp(c->shape, L"circle")==0){
        Ellipse(hdc, x, y, x+w, y+h);
        SelectObject(hdc, ob); SelectObject(hdc, op); DeleteObject(br); DeleteObject(pn);
        return;
    }
    {
        HRGN rgn = CreateRoundRectRgn(x, y, x+w+1, y+h+1, 14, 14);
        FillRgn(hdc, rgn, br);
        FrameRgn(hdc, rgn, (HBRUSH)GetStockObject(NULL_BRUSH), 0, 0);
        {
            HBRUSH fbr = CreateSolidBrush(RGB(90,90,90));
            FrameRgn(hdc, rgn, fbr, 1, 1);
            DeleteObject(fbr);
        }
        DeleteObject(rgn);
    }
    SelectObject(hdc, ob); SelectObject(hdc, op); DeleteObject(br); DeleteObject(pn);
}

static void draw_text(HDC hdc, Component *c, RECT *rc){
    HFONT f = CreateFontW(-(c->font_size>0?c->font_size:14)*g_app.zoom/100, 0,0,0, FW_NORMAL,0,0,0,
                          DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, L"Microsoft YaHei UI");
    HFONT of = (HFONT)SelectObject(hdc, f);
    SetTextColor(hdc, parse_color(c->fg));
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, c->text, -1, rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(hdc, of); DeleteObject(f);
}

/* ---------- 画布 ---------- */
void etb_refresh_canvas(void){
    if (g_app.hCanvas) InvalidateRect(g_app.hCanvas, NULL, TRUE);
}
void etb_paint_canvas(HDC hdc, RECT *rc){
    int i;
    /* 背景 */
    {
        HBRUSH br = CreateSolidBrush(parse_color(g_app.proj.bg));
        FillRect(hdc, rc, br); DeleteObject(br);
        if (g_app.proj.bg_image[0]){
            int iw=0, ih=0;
            HBITMAP hb = etb_load_image(g_app.proj.bg_image, &iw, &ih);
            if (hb){
                HDC mem = CreateCompatibleDC(hdc);
                HGDIOBJ old = SelectObject(mem, hb);
                int cw = rc->right - rc->left, ch = rc->bottom - rc->top;
                if (iw>0 && ih>0){
                    double s = (double)cw/iw < (double)ch/ih ? (double)cw/iw : (double)ch/ih;
                    int dw = (int)(iw*s), dh = (int)(ih*s);
                    StretchBlt(hdc, (cw-dw)/2, (ch-dh)/2, dw, dh, mem, 0,0,iw,ih, SRCCOPY);
                }
                SelectObject(mem, old); DeleteDC(mem);
            }
        }
    }
    for (i=0;i<g_app.proj.ncomps;i++){
        Component *c = &g_app.proj.comps[i];
        RECT r;
        if (!c->visible) continue;
        comp_rect(c, &r);
        if (c->type==CT_IMAGE && c->image_path[0]){
            int iw=0, ih=0;
            HBITMAP hb = etb_load_image(c->image_path, &iw, &ih);
            if (hb){
                HDC mem = CreateCompatibleDC(hdc);
                HGDIOBJ old = SelectObject(mem, hb);
                StretchBlt(hdc, r.left, r.top, r.right-r.left, r.bottom-r.top, mem, 0,0,iw,ih, SRCCOPY);
                SelectObject(mem, old); DeleteDC(mem);
            } else {
                HBRUSH br = CreateSolidBrush(RGB(60,60,60));
                FillRect(hdc, &r, br); DeleteObject(br);
            }
        } else {
            draw_shape(hdc, c, r.left, r.top, r.right-r.left, r.bottom-r.top);
            draw_text(hdc, c, &r);
        }
    }
    /* 选中框 */
    if (g_app.sel >= 0){
        int idx = comp_index_by_id(g_app.sel);
        if (idx >= 0){
            Component *c = &g_app.proj.comps[idx];
            RECT r; comp_rect(c, &r);
            HPEN pn = CreatePen(PS_DASH, 2, RGB(0,180,255));
            HGDIOBJ op = SelectObject(hdc, pn);
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, r.left-2, r.top-2, r.right+2, r.bottom+2);
            SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(pn);
            /* 角点把手 */
            {
                HBRUSH hb = CreateSolidBrush(RGB(0,180,255));
                RECT hs[4] = {
                    {r.left-4, r.top-4, r.left+2, r.top+2},
                    {r.right-2, r.top-4, r.right+4, r.top+2},
                    {r.left-4, r.bottom-2, r.left+2, r.bottom+4},
                    {r.right-2, r.bottom-2, r.right+4, r.bottom+4}
                };
                int k; for (k=0;k<4;k++) FillRect(hdc, &hs[k], hb);
                DeleteObject(hb);
            }
        }
    }
}
static int canvas_hit(POINT pt){
    int i;
    for (i=g_app.proj.ncomps-1;i>=0;i--){
        Component *c = &g_app.proj.comps[i];
        RECT r; comp_rect(c, &r);
        if (pt.x>=r.left && pt.x<=r.right && pt.y>=r.top && pt.y<=r.bottom) return i;
    }
    return -1;
}

void etb_select_component(int id){
    g_app.sel = id;
    etb_refresh_canvas();
    etb_refresh_props();
}

void etb_add_component(int type){
    Project *p = &g_app.proj;
    if (p->ncomps >= MAX_COMPS){ etb_log(L"组件数量已达上限"); return; }
    {
        Component *c = &p->comps[p->ncomps];
        int n = p->ncomps;
        memset(c, 0, sizeof(Component));
        c->id = p->next_id++; c->type = type;
        c->x = 60 + (n%8)*18; c->y = 50 + (n%8)*14;
        c->w = 120; c->h = 36; c->visible = 1;
        c->font_size = 14;
        wcscpy(c->bg, L"#0078d4"); wcscpy(c->fg, L"#ffffff");
        wcscpy(c->shape, L"rectangle");
        switch (type){
        case CT_BUTTON: break;
        case CT_LABEL: c->w=150; c->h=32; wcscpy(c->bg, L"#2b2b2b"); break;
        case CT_ENTRY: c->w=180; c->h=30; wcscpy(c->bg, L"#2b2b2b"); break;
        case CT_CHECK: c->w=110; wcscpy(c->bg, L"#2b2b2b"); break;
        case CT_RADIO: c->w=110; wcscpy(c->bg, L"#2b2b2b"); break;
        case CT_COMBO: c->w=160; wcscpy(c->values, L"选项1,选项2,选项3"); wcscpy(c->bg, L"#2b2b2b"); break;
        case CT_PROGRESS: c->w=220; c->h=20; wcscpy(c->bg, L"#2b2b2b"); break;
        case CT_IMAGE: c->w=120; c->h=120; wcscpy(c->bg, L"#2b2b2b"); break;
        }
        comp_default_name(c);
        if (type==CT_IMAGE) wcscpy(c->text, L"选择图片");
        p->ncomps++;
        etb_select_component(c->id);
        etb_refresh_codes();
        etb_log(L"已添加组件: %ls (ID:%d)", c->text, c->id);
    }
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg){
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        etb_paint_canvas(hdc, &rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN:{
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int idx = canvas_hit(pt);
        if (idx >= 0){
            etb_select_component(g_app.proj.comps[idx].id);
            g_app.dragging = 1; g_app.dragStart = pt; g_app.dragComp = idx;
            SetCapture(hwnd);
        } else etb_select_component(-1);
        return 0;
    }
    case WM_MOUSEMOVE:{
        if (g_app.dragging && g_app.dragComp>=0){
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            Component *c = &g_app.proj.comps[g_app.dragComp];
            int dx = (pt.x - g_app.dragStart.x) * 100 / g_app.zoom;
            int dy = (pt.y - g_app.dragStart.y) * 100 / g_app.zoom;
            if (c->x + dx >= 0) c->x += dx;
            if (c->y + dy >= 0) c->y += dy;
            g_app.dragStart = pt;
            etb_refresh_canvas();
            etb_refresh_codes();
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_app.dragging){ g_app.dragging=0; ReleaseCapture(); }
        return 0;
    case WM_LBUTTONDBLCLK:{
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int idx = canvas_hit(pt);
        if (idx >= 0){
            Component *c = &g_app.proj.comps[idx];
            wchar_t buf[256];
            wcscpy(buf, c->text);
            if (etb_prompt(L"编辑文本", L"输入新文本:", buf, 256, 0)){
                wcscpy(c->text, buf);
                if (c->text[0]==0) comp_default_name(c);
                etb_refresh_canvas(); etb_refresh_props(); etb_refresh_codes();
            }
        }
        return 0;
    }
    case WM_RBUTTONUP:{
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int idx = canvas_hit(pt);
        if (idx >= 0){
            etb_select_component(g_app.proj.comps[idx].id);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"编辑文本...");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, 2, L"复制");
            AppendMenuW(m, MF_STRING, 3, L"删除");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, 4, L"置顶");
            AppendMenuW(m, MF_STRING, 5, L"置底");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            HMENU sm = CreatePopupMenu();
            AppendMenuW(sm, MF_STRING, 6, L"矩形");
            AppendMenuW(sm, MF_STRING, 7, L"星形");
            AppendMenuW(sm, MF_STRING, 8, L"菱形");
            AppendMenuW(sm, MF_STRING, 9, L"圆形");
            AppendMenuW(m, MF_POPUP, (UINT_PTR)sm, L"形状");
            POINT sp; GetCursorPos(&sp);
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD|TPM_RIGHTBUTTON, sp.x, sp.y, 0, hwnd, NULL);
            DestroyMenu(m);
            if (cmd==1){
                Component *c = &g_app.proj.comps[idx];
                wchar_t buf[256]; wcscpy(buf, c->text);
                if (etb_prompt(L"编辑文本", L"输入新文本:", buf, 256, 0)){
                    wcscpy(c->text, buf);
                    if (c->text[0]==0) comp_default_name(c);
                    etb_refresh_canvas(); etb_refresh_props(); etb_refresh_codes();
                }
            } else if (cmd==2){ g_app.sel = g_app.proj.comps[idx].id; etb_duplicate_selected(); }
            else if (cmd==3){ g_app.sel = g_app.proj.comps[idx].id; etb_delete_selected(); }
            else if (cmd==4 || cmd==5){
                Project *p = &g_app.proj;
                int ci = idx;
                if (cmd==4 && ci < p->ncomps-1){
                    Component tmp = p->comps[ci]; memmove(&p->comps[ci], &p->comps[ci+1], (p->ncomps-ci-1)*sizeof(Component)); p->comps[p->ncomps-1]=tmp;
                } else if (cmd==5 && ci > 0){
                    Component tmp = p->comps[ci]; memmove(&p->comps[1], &p->comps[0], ci*sizeof(Component)); p->comps[0]=tmp;
                }
                etb_refresh_canvas(); etb_refresh_codes();
            }
            else if (cmd>=6 && cmd<=9){
                const wchar_t *shapes[] = {L"rectangle", L"star", L"diamond", L"circle"};
                wcscpy(g_app.proj.comps[idx].shape, shapes[cmd-6]);
                etb_refresh_canvas(); etb_refresh_props(); etb_refresh_codes();
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------- 属性面板 ---------- */
typedef struct { int kind; int id; int comp_id; int prop; HWND h; } PropRow;
static PropRow g_props[40]; static int g_nprops;

static void props_destroy_children(HWND hwnd){
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child){ HWND next = GetWindow(child, GW_HWNDNEXT); DestroyWindow(child); child = next; }
    g_nprops = 0;
}

static HWND etb_add_label(HWND parent, const wchar_t *text, int x, int y, int w, int h, int bold){
    DWORD style = WS_CHILD|WS_VISIBLE|SS_LEFT;
    HWND hw = CreateWindowW(L"STATIC", text, style, x, y, w, h, parent, NULL, g_app.hInst, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)(bold?g_app.hFontBig:g_app.hFontUI), TRUE);
    return hw;
}
static HWND props_add_edit(HWND parent, int id, int x, int y, int w, int h){
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                              x, y, w, h, parent, (HMENU)(INT_PTR)id, g_app.hInst, NULL);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    return hw;
}
static HWND props_add_btn(HWND parent, int id, const wchar_t *text, int x, int y, int w, int h, COLORREF bg){
    HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, g_app.hInst, NULL);
    SetWindowLongPtrW(b, GWLP_USERDATA, (LONG_PTR)bg);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    return b;
}
static void props_add_textrow(HWND parent, int y, const wchar_t *label, const wchar_t *value, int comp_id, int prop){
    HWND l = etb_add_label(parent, label, 12, y, 78, 26, 0);
    HWND e = props_add_edit(parent, IDM_PROP_BASE+g_nprops, 94, y, 200, 26);
    SetWindowTextW(e, value);
    if (g_nprops < 40){ g_props[g_nprops].kind=0; g_props[g_nprops].id=IDM_PROP_BASE+g_nprops; g_props[g_nprops].comp_id=comp_id; g_props[g_nprops].prop=prop; g_props[g_nprops].h=e; g_nprops++; }
    (void)l;
}

static void props_apply_row(int row){
    PropRow *r = &g_props[row];
    Component *c = NULL; int ci;
    wchar_t buf[256];
    if (r->comp_id >= 0){ ci = comp_index_by_id(r->comp_id); if (ci<0) return; c = &g_app.proj.comps[ci]; }
    GetWindowTextW(r->h, buf, 256);
    if (c){
        switch (r->prop){
        case 0: wcscpy(c->text, buf); break;
        case 1: c->x = _wtoi(buf); break;
        case 2: c->y = _wtoi(buf); break;
        case 3: c->w = _wtoi(buf); break;
        case 4: c->h = _wtoi(buf); break;
        case 5: c->font_size = _wtoi(buf); break;
        case 6: wcscpy(c->values, buf); break;
        }
    }
    etb_refresh_canvas();
    etb_refresh_codes();
}

void etb_refresh_props(void){
    HWND hwnd = g_app.hProps;
    int y = 8, i;
    if (!hwnd) return;
    props_destroy_children(hwnd);
    if (g_app.sel < 0){
        etb_add_label(hwnd, L"未选择组件", 20, 40, 200, 30, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }
    {
        int ci = comp_index_by_id(g_app.sel);
        if (ci < 0){ etb_add_label(hwnd, L"未选择组件", 20, 40, 200, 30, 0); InvalidateRect(hwnd, NULL, TRUE); return; }
        Component *c = &g_app.proj.comps[ci];
        wchar_t hdr[128];
        swprintf(hdr, 128, L"%ls  (ID:%d)", comp_type_name(c->type), c->id);
        etb_add_label(hwnd, hdr, 12, y, 260, 28, 1);
        y += 36;
        props_add_textrow(hwnd, y, L"文本:", c->text, c->id, 0); y += 30;
        {
            wchar_t num[32];
            swprintf(num,32,L"%d",c->x); props_add_textrow(hwnd, y, L"X 位置:", num, c->id, 1); y += 30;
            swprintf(num,32,L"%d",c->y); props_add_textrow(hwnd, y, L"Y 位置:", num, c->id, 2); y += 30;
            swprintf(num,32,L"%d",c->w); props_add_textrow(hwnd, y, L"宽度:", num, c->id, 3); y += 30;
            swprintf(num,32,L"%d",c->h); props_add_textrow(hwnd, y, L"高度:", num, c->id, 4); y += 30;
            swprintf(num,32,L"%d",c->font_size); props_add_textrow(hwnd, y, L"字号:", num, c->id, 5); y += 30;
        }
        /* 背景色 / 前景色 */
        {
            etb_add_label(hwnd, L"背景色:", 12, y, 78, 26, 0);
            props_add_btn(hwnd, IDM_PROP_COLOR, L"选择颜色", 94, y, 96, 26, parse_color(c->bg));
            y += 30;
            etb_add_label(hwnd, L"前景色:", 12, y, 78, 26, 0);
            props_add_btn(hwnd, IDM_PROP_COLOR+1, L"选择颜色", 94, y, 96, 26, parse_color(c->fg));
            y += 30;
        }
        if (c->type==CT_BUTTON){
            etb_add_label(hwnd, L"形状:", 12, y, 78, 26, 0);
            HWND cb = CreateWindowW(L"COMBOBOX", L"", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
                                    94, y, 140, 120, hwnd, (HMENU)IDM_PROP_SHAPE, g_app.hInst, NULL);
            SendMessageW(cb, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"矩形");
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"星形");
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"菱形");
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"圆形");
            {
                int s = wcscmp(c->shape,L"star")==0?1:wcscmp(c->shape,L"diamond")==0?2:wcscmp(c->shape,L"circle")==0?3:0;
                SendMessageW(cb, CB_SETCURSEL, s, 0);
            }
            y += 30;
        }
        if (c->type==CT_COMBO){
            props_add_textrow(hwnd, y, L"选项:", c->values, c->id, 6); y += 30;
        }
        if (c->type==CT_IMAGE){
            etb_add_label(hwnd, L"图片文件:", 12, y, 200, 24, 0); y += 24;
            {
                wchar_t disp[300];
                if (c->image_path[0]) swprintf(disp,300,L"%ls",c->image_path); else wcscpy(disp, L"(未选择)");
                HWND l = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", disp, WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,
                                         12, y, 282, 24, hwnd, NULL, g_app.hInst, NULL);
                SendMessageW(l, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
            }
            y += 32;
            props_add_btn(hwnd, IDM_PROP_IMG, L"选择图片...", 94, y, 120, 28, C_ACCENT); y += 34;
        }
        if (c->type==CT_BUTTON){
            props_add_btn(hwnd, IDM_PROP_BIND, L"绑定文件...", 12, y, 130, 28, C_HOVER);
            y += 34;
        }
        /* 操作按钮 */
        props_add_btn(hwnd, IDM_PROP_DUP, L"复制", 12, y, 130, 30, C_PANEL);
        props_add_btn(hwnd, IDM_PROP_DEL, L"删除", 158, y, 130, 30, RGB(120,40,40));
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

LRESULT CALLBACK PropsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg){
    case WM_DRAWITEM:
        etb_draw_owndraw((LPDRAWITEMSTRUCT)lp);
        return TRUE;
    case WM_COMMAND:
        if (HIWORD(wp)==EN_CHANGE && LOWORD(wp)>=IDM_PROP_BASE && LOWORD(wp)<IDM_PROP_BASE+40){
            int row = LOWORD(wp)-IDM_PROP_BASE;
            if (row>=0 && row<g_nprops && g_props[row].kind==0) props_apply_row(row);
            return 0;
        }
        if (LOWORD(wp)==IDM_PROP_SHAPE && HIWORD(wp)==CBN_SELCHANGE){
            if (g_app.sel>=0){
                int ci = comp_index_by_id(g_app.sel);
                if (ci>=0){
                    int s = (int)SendMessageW((HWND)lp, CB_GETCURSEL, 0, 0);
                    const wchar_t *shapes[] = {L"rectangle", L"star", L"diamond", L"circle"};
                    if (s>=0 && s<=3){ wcscpy(g_app.proj.comps[ci].shape, shapes[s]); etb_refresh_canvas(); etb_refresh_codes(); }
                }
            }
            return 0;
        }
        if (LOWORD(wp)>=IDM_PROP_COLOR && LOWORD(wp)<=IDM_PROP_COLOR+1){
            int fg = (LOWORD(wp)==IDM_PROP_COLOR+1);
            if (g_app.sel>=0){
                int ci = comp_index_by_id(g_app.sel);
                if (ci>=0){
                    Component *c = &g_app.proj.comps[ci];
                    CHOOSECOLORW cc; COLORREF custom[16]={0};
                    memset(&cc,0,sizeof(cc)); cc.lStructSize=sizeof(cc); cc.hwndOwner=hwnd;
                    cc.lpCustColors=custom; cc.rgbResult=parse_color(fg?c->fg:c->bg); cc.Flags=CC_RGBINIT;
                    if (ChooseColorW(&cc)){
                        wchar_t col[16];
                        swprintf(col,16,L"#%02X%02X%02X",GetRValue(cc.rgbResult),GetGValue(cc.rgbResult),GetBValue(cc.rgbResult));
                        if (fg) wcscpy(c->fg,col); else wcscpy(c->bg,col);
                        etb_refresh_canvas(); etb_refresh_codes(); etb_refresh_props();
                    }
                }
            }
            return 0;
        }
        if (LOWORD(wp)==IDM_PROP_IMG){
            if (g_app.sel>=0){
                int ci = comp_index_by_id(g_app.sel);
                if (ci>=0){
                    Component *c = &g_app.proj.comps[ci];
                    wchar_t file[MAX_PATH]={0};
                    OPENFILENAMEW ofn;
                    memset(&ofn,0,sizeof(ofn)); ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd;
                    ofn.lpstrFilter=L"图片文件\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0所有文件\0*.*\0";
                    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)){
                        wcscpy(c->image_path, file);
                        wcscpy(c->text, L"图片");
                        etb_refresh_files(); etb_refresh_canvas(); etb_refresh_codes(); etb_refresh_props();
                        etb_log(L"已加载图片: %ls", file);
                    }
                }
            }
            return 0;
        }
        if (LOWORD(wp)==IDM_PROP_BIND){
            if (g_app.sel>=0){
                int ci = comp_index_by_id(g_app.sel);
                if (ci>=0){
                    Component *c = &g_app.proj.comps[ci];
                    wchar_t file[MAX_PATH]={0};
                    OPENFILENAMEW ofn;
                    memset(&ofn,0,sizeof(ofn)); ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd;
                    ofn.lpstrFilter=L"所有文件\0*.*\0"; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)){
                        wcscpy(c->file_path, file);
                        wcscpy(c->text, L"打开");
                        etb_refresh_canvas(); etb_refresh_codes();
                        etb_log(L"已绑定文件: %ls", file);
                    }
                }
            }
            return 0;
        }
        if (LOWORD(wp)==IDM_PROP_DEL){ etb_delete_selected(); return 0; }
        if (LOWORD(wp)==IDM_PROP_DUP){ etb_duplicate_selected(); return 0; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------- 组件删除/复制 ---------- */
void etb_delete_selected(void){
    if (g_app.sel < 0) return;
    {
        int ci = comp_index_by_id(g_app.sel);
        if (ci>=0){
            memmove(&g_app.proj.comps[ci], &g_app.proj.comps[ci+1], (g_app.proj.ncomps-ci-1)*sizeof(Component));
            g_app.proj.ncomps--;
            etb_log(L"已删除组件 ID:%d", g_app.sel);
        }
    }
    g_app.sel = -1;
    etb_refresh_canvas(); etb_refresh_props(); etb_refresh_codes();
}
void etb_duplicate_selected(void){
    if (g_app.sel < 0) return;
    {
        int ci = comp_index_by_id(g_app.sel);
        if (ci>=0 && g_app.proj.ncomps<MAX_COMPS){
            Component nc = g_app.proj.comps[ci];
            nc.id = g_app.proj.next_id++;
            nc.x += 20; nc.y += 20;
            g_app.proj.comps[g_app.proj.ncomps++] = nc;
            etb_select_component(nc.id);
            etb_refresh_codes();
            etb_log(L"已复制组件 (新ID:%d)", nc.id);
        }
    }
}

/* ---------- 项目文件列表 ---------- */
void etb_refresh_files(void){
    if (g_app.hRightFiles){
        int i;
        SendMessageW(g_app.hRightFiles, LB_RESETCONTENT, 0, 0);
        for (i=0;i<g_app.proj.nfiles;i++){
            SendMessageW(g_app.hRightFiles, LB_ADDSTRING, 0, (LPARAM)g_app.proj.files[i]);
        }
    }
}

/* ---------- 缩放 ---------- */
void etb_zoom_apply(void){
    wchar_t buf[32];
    if (g_app.hZoomLabel){
        swprintf(buf,32,L"%d%%",g_app.zoom);
        SetWindowTextW(g_app.hZoomLabel, buf);
    }
    etb_refresh_canvas();
}
void etb_zoom_in(void){ if (g_app.zoom<200) g_app.zoom+=10; etb_zoom_apply(); }
void etb_zoom_out(void){ if (g_app.zoom>50) g_app.zoom-=10; etb_zoom_apply(); }
void etb_zoom_reset(void){ g_app.zoom=100; etb_zoom_apply(); }

/* ---------- 控件风格 ---------- */
void etb_draw_owndraw(LPDRAWITEMSTRUCT dis){
    RECT rc = dis->rcItem;
    COLORREF bg = (COLORREF)dis->itemData;
    if (!bg) bg = C_PANEL;
    if (dis->itemState & ODS_SELECTED){ bg = RGB(60,120,180); }
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dis->hDC, &rc, br); DeleteObject(br);
    {
        HPEN pn = CreatePen(PS_SOLID, 1, RGB(70,70,70));
        HGDIOBJ op = SelectObject(dis->hDC, pn);
        HGDIOBJ ob = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dis->hDC, op); SelectObject(dis->hDC, ob); DeleteObject(pn);
    }
    SetTextColor(dis->hDC, RGB(255,255,255));
    SetBkMode(dis->hDC, TRANSPARENT);
    wchar_t txt[128];
    GetWindowTextW(dis->hwndItem, txt, 128);
    DrawTextW(dis->hDC, txt, -1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

HWND etb_make_btn(HWND parent, int id, const wchar_t *text, int x, int y, int w, int h, COLORREF bg){
    HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, g_app.hInst, NULL);
    SetWindowLongPtrW(b, GWLP_USERDATA, (LONG_PTR)bg);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    return b;
}

/* 面板类窗口过程: 深色背景 + 自绘按钮 */
LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg){
    case WM_ERASEBKGND:{
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(C_PANEL);
        FillRect((HDC)wp, &rc, br); DeleteObject(br);
        return 1;
    }
    case WM_CTLCOLORSTATIC:{
        SetTextColor((HDC)wp, RGB(255,255,255));
        SetBkColor((HDC)wp, C_PANEL);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLOREDIT:{
        SetTextColor((HDC)wp, RGB(255,255,255));
        SetBkColor((HDC)wp, C_EDITBG);
        static HBRUSH eb;
        if (!eb) eb = CreateSolidBrush(C_EDITBG);
        return (LRESULT)eb;
    }
    case WM_CTLCOLORLISTBOX:{
        SetTextColor((HDC)wp, RGB(255,255,255));
        SetBkColor((HDC)wp, C_EDITBG);
        static HBRUSH lb;
        if (!lb) lb = CreateSolidBrush(C_EDITBG);
        return (LRESULT)lb;
    }
    case WM_DRAWITEM:
        etb_draw_owndraw((LPDRAWITEMSTRUCT)lp);
        return TRUE;
    case WM_COMMAND:
        /* 转发到主窗口统一处理 */
        return SendMessageW(g_app.hMain, WM_COMMAND, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------- 模式切换 ---------- */
static void mode_btn_reset(void){
    HWND bs[5] = {g_app.hBtnDesign, g_app.hBtnBlock, g_app.hBtnC, g_app.hBtnPy, g_app.hBtnFile};
    int i;
    for (i=0;i<5;i++) if (bs[i]) SetWindowLongPtrW(bs[i], GWLP_USERDATA, (LONG_PTR)C_PANEL);
}
void etb_switch_mode(int mode){
    g_app.mode = mode;
    mode_btn_reset();
    {
        HWND active = mode==MODE_DESIGN?g_app.hBtnDesign:mode==MODE_BLOCK?g_app.hBtnBlock:mode==MODE_CODE_C?g_app.hBtnC:mode==MODE_CODE_PY?g_app.hBtnPy:g_app.hBtnFile;
        if (active) SetWindowLongPtrW(active, GWLP_USERDATA, (LONG_PTR)C_ACCENT);
    }
    ShowWindow(g_app.hCanvas, mode==MODE_DESIGN?SW_SHOW:SW_HIDE);
    ShowWindow(g_app.hBlocks, mode==MODE_BLOCK?SW_SHOW:SW_HIDE);
    ShowWindow(g_app.hCodeC, mode==MODE_CODE_C?SW_SHOW:SW_HIDE);
    ShowWindow(g_app.hCodePy, mode==MODE_CODE_PY?SW_SHOW:SW_HIDE);
    ShowWindow(g_app.hToolbox, mode==MODE_DESIGN?SW_SHOW:SW_HIDE);
    ShowWindow(g_app.hPalette, mode==MODE_BLOCK?SW_SHOW:SW_HIDE);
    ShowWindow(g_app.hExplorer, mode==MODE_FILE?SW_SHOW:SW_HIDE);
    {
        static const wchar_t *names[] = {L"设计模式", L"积木模式", L"C语言代码", L"Python代码", L"文件模式"};
        if (mode>=0 && mode<=4) etb_log(L"切换到%s", names[mode]);
    }
    InvalidateRect(g_app.hMain, NULL, TRUE);
}

/* ---------- 创建布局 ---------- */
static void create_toolbar(HWND parent){
    int y = 6, bh = 30;
    g_app.hBtnDesign = etb_make_btn(parent, IDM_DESIGN, L"设计", 10, y, 66, bh, C_ACCENT);
    g_app.hBtnBlock  = etb_make_btn(parent, IDM_BLOCK, L"积木", 80, y, 66, bh, C_PANEL);
    g_app.hBtnC      = etb_make_btn(parent, IDM_CC, L"C代码", 150, y, 66, bh, C_PANEL);
    g_app.hBtnPy     = etb_make_btn(parent, IDM_PY, L"Py代码", 220, y, 66, bh, C_PANEL);
    g_app.hBtnFile   = etb_make_btn(parent, IDM_FILE, L"文件", 290, y, 66, bh, C_PANEL);
    etb_make_btn(parent, IDM_ZOOMOUT, L"-", 380, y, 32, bh, C_PANEL);
    g_app.hZoomLabel = CreateWindowW(L"STATIC", L"100%", WS_CHILD|WS_VISIBLE|SS_CENTER, 414, y+4, 52, bh-8, parent, NULL, g_app.hInst, NULL);
    SendMessageW(g_app.hZoomLabel, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    etb_make_btn(parent, IDM_ZOOMIN, L"+", 468, y, 32, bh, C_PANEL);
    etb_make_btn(parent, IDM_ZOOMRST, L"100%", 502, y, 48, bh, C_PANEL);
    etb_make_btn(parent, IDM_RUN, L"▶ 运行", 560, y, 70, bh, C_OK);
    etb_make_btn(parent, IDM_PACK2, L"打包EXE", 634, y, 78, bh, C_ACCENT);
    etb_make_btn(parent, IDM_SYNC, L"同步代码", 716, y, 78, bh, C_WARN);
    etb_make_btn(parent, IDM_SETWIN, L"窗口设置", 798, y, 78, bh, C_PANEL);
    etb_make_btn(parent, IDM_PACKSET, L"打包设置", 880, y, 78, bh, C_PANEL);
    etb_make_btn(parent, IDM_HELP, L"帮助", 962, y, 54, bh, C_PANEL);
}

static void create_toolbox(HWND parent){
    static const struct { const wchar_t *name; int type; } items[] = {
        {L"按钮", CT_BUTTON}, {L"标签", CT_LABEL}, {L"输入框", CT_ENTRY},
        {L"复选框", CT_CHECK}, {L"单选钮", CT_RADIO}, {L"下拉列表", CT_COMBO},
        {L"进度条", CT_PROGRESS}, {L"图片", CT_IMAGE},
        {L"★ 星形按钮", CT_BUTTON}, {L"◆ 菱形按钮", CT_BUTTON}, {L"● 圆形按钮", CT_BUTTON}
    };
    int i, y = 8;
    HWND h = CreateWindowExW(0, L"ETBPanel", NULL, WS_CHILD|WS_VISIBLE,
                             0, 0, 250, 400, parent, NULL, g_app.hInst, NULL);
    g_app.hToolbox = h;
    for (i=0;i<11;i++){
        etb_make_btn(h, IDM_TOOLBASE+i, items[i].name, 14, y, 220, 32, C_EDITBG);
        y += 37;
    }
    y += 8;
    etb_make_btn(h, IDM_SETWIN, L"⚙ 窗口设置", 14, y, 220, 34, C_ACCENT); y += 40;
    etb_make_btn(h, IDM_BGCOLOR, L"背景颜色", 14, y, 220, 32, C_EDITBG); y += 38;
    etb_make_btn(h, IDM_BGIMG, L"背景图片", 14, y, 220, 32, C_EDITBG);
    /* 记住类型 */
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    (void)items;
}

static void create_palette(HWND parent){
    HWND h = CreateWindowExW(0, L"ETBPanel", NULL, WS_CHILD,
                             0, 0, 250, 400, parent, NULL, g_app.hInst, NULL);
    g_app.hPalette = h;
    etb_add_label(h, L"积木库 (双击添加到脚本)", 12, 8, 220, 26, 0);
    {
        HWND lb = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,
                                  12, 38, 226, 300, h, (HMENU)2001, g_app.hInst, NULL);
        SendMessageW(lb, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
        g_app.hPaletteList = lb;
    }
    {
        int i;
        wchar_t wcat[64]; wchar_t item[320];
        for (i=0;i<g_nblockdefs;i++){
            if (i==0 || wcscmp(g_blockdefs[i].cat, g_blockdefs[i-1].cat)!=0){
                swprintf(wcat, 64, L"——— %ls ———", g_blockdefs[i].cat);
                SendMessageW(g_app.hPaletteList, LB_ADDSTRING, 0, (LPARAM)wcat);
            }
            swprintf(item, 320, L"#%d %ls", g_blockdefs[i].id, g_blockdefs[i].name);
            SendMessageW(g_app.hPaletteList, LB_ADDSTRING, 0, (LPARAM)item);
        }
    }
    etb_make_btn(h, IDM_PALADD, L"➕ 添加到脚本", 14, 348, 220, 34, C_OK);
}

/* ---------- 主布局 ---------- */
void etb_create_layout(void){
    /* 顶部工具栏按钮 (子窗口直接挂在主窗口) */
    create_toolbar(g_app.hMain);

    /* 中心: 设计画布 / 积木区 / 两个代码编辑器 */
    g_app.hCanvas = CreateWindowExW(0, L"ETBCanvas", NULL, WS_CHILD, 0,0,600,600, g_app.hMain, NULL, g_app.hInst, NULL);
    g_app.hBlocks = CreateWindowExW(0, L"ETBBlocks", NULL, WS_CHILD|WS_VSCROLL, 0,0,600,600, g_app.hMain, NULL, g_app.hInst, NULL);
    g_app.hCodeC = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|ES_MULTILINE|WS_VSCROLL|WS_HSCROLL|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_WANTRETURN,
                                   0,0,600,600, g_app.hMain, (HMENU)IDM_CODEC_EDIT, g_app.hInst, NULL);
    SendMessageW(g_app.hCodeC, WM_SETFONT, (WPARAM)g_app.hFontMono, TRUE);
    g_app.hCodePy = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|ES_MULTILINE|WS_VSCROLL|WS_HSCROLL|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_WANTRETURN,
                                    0,0,600,600, g_app.hMain, (HMENU)IDM_CODEPY_EDIT, g_app.hInst, NULL);
    SendMessageW(g_app.hCodePy, WM_SETFONT, (WPARAM)g_app.hFontMono, TRUE);

    /* 左: 工具箱 / 积木库 / 文件浏览器 */
    create_toolbox(g_app.hMain);
    create_palette(g_app.hMain);
    {
        HWND h = CreateWindowExW(0, L"ETBPanel", NULL, WS_CHILD,
                                 0,0,250,400, g_app.hMain, NULL, g_app.hInst, NULL);
        g_app.hExplorer = h;
        etb_add_label(h, L"文件浏览器", 12, 8, 200, 26, 0);
        g_app.hExplorerPath = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_LEFT,
                                            12, 34, 226, 20, h, NULL, g_app.hInst, NULL);
        SendMessageW(g_app.hExplorerPath, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
        g_app.hExplorerList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,
                                              12, 58, 226, 240, h, (HMENU)2002, g_app.hInst, NULL);
        SendMessageW(g_app.hExplorerList, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
        etb_make_btn(h, IDM_EXP_UP, L"⬆ 上级", 12, 306, 108, 30, C_EDITBG);
        etb_make_btn(h, IDM_EXP_REF, L"🔄 刷新", 130, 306, 108, 30, C_EDITBG);
        etb_make_btn(h, IDM_EXP_IMP, L"📎 导入到项目", 12, 342, 226, 32, C_ACCENT);
        etb_make_btn(h, IDM_EXP_RUN, L"▶ 编译并运行文件 (C/Py/Java)", 12, 380, 226, 32, C_OK);
    }

    /* 右: 属性面板 */
    g_app.hProps = CreateWindowExW(0, L"ETBProps", NULL, WS_CHILD|WS_VISIBLE|WS_VSCROLL,
                                   0,0,330,300, g_app.hMain, NULL, g_app.hInst, NULL);

    /* 右: 项目文件 */
    g_app.hRightFilesLabel = etb_add_label(g_app.hMain, L"项目文件", 0,0,100,22,0);
    g_app.hRightFiles = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,
                                        0,0,300,80, g_app.hMain, (HMENU)2003, g_app.hInst, NULL);
    SendMessageW(g_app.hRightFiles, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    etb_make_btn(g_app.hMain, IDM_RIGHT_IMP, L"导入", 0,0,96,28, C_ACCENT);
    etb_make_btn(g_app.hMain, IDM_RIGHT_DEL, L"删除", 104,0,96,28, RGB(120,40,40));
    g_app.hRightImpBtn = GetDlgItem(g_app.hMain, IDM_RIGHT_IMP);
    g_app.hRightDelBtn = GetDlgItem(g_app.hMain, IDM_RIGHT_DEL);

    /* 右: 日志 */
    g_app.hLogLabel = etb_add_label(g_app.hMain, L"实时日志", 0,0,100,22,0);
    g_app.hLogClear = etb_make_btn(g_app.hMain, 0x9999, L"清空", 0,0,52,22, C_EDITBG);
    g_app.hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,
                                 0,0,300,80, g_app.hMain, NULL, g_app.hInst, NULL);
    SendMessageW(g_app.hLog, WM_SETFONT, (WPARAM)g_app.hFontMono, TRUE);

    /* 初始模式 */
    g_app.mode = MODE_DESIGN;
    etb_switch_mode(MODE_DESIGN);
    etb_refresh_codes();
    etb_refresh_files();
    etb_refresh_canvas();
    etb_refresh_props();
    etb_log(L"界面初始化完成");
}
