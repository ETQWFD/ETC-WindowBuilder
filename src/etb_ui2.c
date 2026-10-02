/* ============================================================
 * etb_ui2.c - 积木编辑 / 代码视图 / 对话框 / 项目读写 / 编译打包
 * 版权: (c) ET 2024-2026
 * ============================================================ */
#include "etb_internal.h"

/* ================= 代码视图 ================= */
static void utf8_to_wbuf(const char *utf8, wchar_t *out, size_t cap){
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, (int)cap);
}
void etb_refresh_codes(void){
    size_t len;
    char *c = etb_gen_c(&g_app.proj, &len);
    if (g_app.hCodeC){
        wchar_t *w = (wchar_t*)malloc((len+2)*sizeof(wchar_t));
        utf8_to_wbuf(c, w, len+1);
        SetWindowTextW(g_app.hCodeC, w);
        free(w);
    }
    free(c);
    c = etb_gen_py(&g_app.proj, &len);
    if (g_app.hCodePy){
        wchar_t *w = (wchar_t*)malloc((len+2)*sizeof(wchar_t));
        utf8_to_wbuf(c, w, len+1);
        SetWindowTextW(g_app.hCodePy, w);
        free(w);
    }
    free(c);
}
void etb_sync_from_code(void){
    /* 设计/积木为唯一数据源: 重新生成代码并刷新 */
    etb_refresh_codes();
    etb_log(L"代码已重新生成 (设计/积木 → C/Python)");
}

/* ================= 积木区 ================= */
#define BLOCK_ROW 46
static int g_scroll = 0;
static int g_bdrag = -1;   /* 正在拖动的块索引 */
static int g_bdragY = 0;

static int block_depth(int idx){
    int d=0, i;
    for (i=0;i<idx;i++){
        int t = g_app.proj.blocks[i].type;
        if (t==30 || t==40) d++;
        else if (t==31 || t==41) d--;
        if (d<0) d=0;
    }
    return d;
}
static void block_rect(int idx, RECT *rc, int cw){
    int y = idx*BLOCK_ROW - g_scroll;
    rc->left = 8; rc->top = y + 4;
    rc->right = cw - 16; rc->bottom = y + BLOCK_ROW - 4;
}
static int block_hit(POINT pt, int cw){
    int i;
    for (i=0;i<g_app.proj.nblocks;i++){
        RECT rc; block_rect(i, &rc, cw);
        if (pt.x>=rc.left && pt.x<=rc.right && pt.y>=rc.top && pt.y<=rc.bottom) return i;
    }
    return -1;
}
static void etb_blocks_scroll(int delta){
    int max = g_app.proj.nblocks*BLOCK_ROW > 400 ? g_app.proj.nblocks*BLOCK_ROW - 400 : 0;
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll > max) g_scroll = max;
    InvalidateRect(g_app.hBlocks, NULL, TRUE);
}
LRESULT CALLBACK BlocksProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg){
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(C_BG); FillRect(hdc, &rc, br); DeleteObject(br);
        {
            int i;
            for (i=0;i<g_app.proj.nblocks;i++){
                Block *b = &g_app.proj.blocks[i];
                int di = etb_block_find(b->type);
                COLORREF col = di>=0 ? g_blockdefs[di].color : RGB(120,120,120);
                RECT r; wchar_t txt[512];
                block_rect(i, &r, rc.right-rc.left);
                r.left += block_depth(i)*18;
                /* 圆角块 */
                HRGN rg = CreateRoundRectRgn(r.left, r.top, r.right, r.bottom, 12, 12);
                HBRUSH fb = CreateSolidBrush(col);
                FillRgn(hdc, rg, fb);
                {
                    HPEN pn = CreatePen(PS_SOLID, i==g_app.bsel?2:1, i==g_app.bsel?RGB(120,220,255):RGB(255,255,255));
                    HGDIOBJ op = SelectObject(hdc, pn);
                    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
                    SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(pn);
                }
                DeleteObject(fb); DeleteObject(rg);
                etb_block_display(b, txt, 512);
                {
                    RECT tr = r; tr.left += 12; tr.right -= 6;
                    SetTextColor(hdc, RGB(255,255,255));
                    SetBkMode(hdc, TRANSPARENT);
                    HFONT f = CreateFontW(-16,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
                    HFONT of = (HFONT)SelectObject(hdc, f);
                    DrawTextW(hdc, txt, -1, &tr, DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                    SelectObject(hdc, of); DeleteObject(f);
                }
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_VSCROLL:
        switch (LOWORD(wp)){
        case SB_LINEUP: etb_blocks_scroll(-BLOCK_ROW); break;
        case SB_LINEDOWN: etb_blocks_scroll(BLOCK_ROW); break;
        case SB_PAGEUP: etb_blocks_scroll(-BLOCK_ROW*5); break;
        case SB_PAGEDOWN: etb_blocks_scroll(BLOCK_ROW*5); break;
        case SB_THUMBTRACK: g_scroll = HIWORD(wp); etb_blocks_scroll(0); break;
        }
        return 0;
    case WM_LBUTTONDOWN:{
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc; GetClientRect(hwnd, &rc);
        int idx = block_hit(pt, rc.right);
        if (idx >= 0){
            g_app.bsel = idx; g_bdrag = idx; g_bdragY = pt.y;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
        } else { g_app.bsel = -1; InvalidateRect(hwnd, NULL, TRUE); }
        return 0;
    }
    case WM_MOUSEMOVE:{
        if (g_bdrag >= 0){
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT rc; GetClientRect(hwnd, &rc);
            int idx = block_hit(pt, rc.right);
            if (idx >= 0 && idx != g_bdrag){
                Block tmp = g_app.proj.blocks[g_bdrag];
                g_app.proj.blocks[g_bdrag] = g_app.proj.blocks[idx];
                g_app.proj.blocks[idx] = tmp;
                g_app.bsel = idx; g_bdrag = idx;
                etb_refresh_blocks();
                etb_refresh_codes();
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_bdrag >= 0){ g_bdrag = -1; ReleaseCapture(); }
        return 0;
    case WM_LBUTTONDBLCLK:{
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc; GetClientRect(hwnd, &rc);
        int idx = block_hit(pt, rc.right);
        if (idx >= 0){ g_app.bsel = idx; etb_edit_block_dialog(idx); }
        return 0;
    }
    case WM_RBUTTONUP:{
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc; GetClientRect(hwnd, &rc);
        int idx = block_hit(pt, rc.right);
        if (idx >= 0){
            g_app.bsel = idx;
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"编辑参数...");
            AppendMenuW(m, MF_STRING, 2, L"上移");
            AppendMenuW(m, MF_STRING, 3, L"下移");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, 4, L"删除");
            POINT sp; GetCursorPos(&sp);
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD|TPM_RIGHTBUTTON, sp.x, sp.y, 0, hwnd, NULL);
            DestroyMenu(m);
            if (cmd==1) etb_edit_block_dialog(idx);
            else if (cmd==2) etb_block_move(idx, -1);
            else if (cmd==3) etb_block_move(idx, 1);
            else if (cmd==4) etb_block_delete(idx);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void etb_refresh_blocks(void){
    if (!g_app.hBlocks) return;
    {
        SCROLLINFO si; memset(&si,0,sizeof(si));
        si.cbSize = sizeof(si); si.fMask = SIF_RANGE|SIF_PAGE|SIF_POS;
        si.nMin = 0; si.nMax = g_app.proj.nblocks*BLOCK_ROW;
        si.nPage = 400; si.nPos = g_scroll;
        SetScrollInfo(g_app.hBlocks, SB_VERT, &si, TRUE);
    }
    InvalidateRect(g_app.hBlocks, NULL, TRUE);
}
void etb_block_add(int type){
    if (g_app.proj.nblocks >= MAX_BLOCKS){ etb_log(L"积木数量已达上限"); return; }
    {
        Block *b = &g_app.proj.blocks[g_app.proj.nblocks++];
        memset(b, 0, sizeof(Block));
        b->type = type;
        etb_block_defaults(b, type);
        g_app.bsel = g_app.proj.nblocks-1;
        etb_refresh_blocks(); etb_refresh_codes();
        {
            wchar_t txt[512]; etb_block_display(b, txt, 512);
            etb_log(L"已添加积木: %ls", txt);
        }
    }
}
void etb_block_delete(int idx){
    if (idx<0 || idx>=g_app.proj.nblocks) return;
    memmove(&g_app.proj.blocks[idx], &g_app.proj.blocks[idx+1], (g_app.proj.nblocks-idx-1)*sizeof(Block));
    g_app.proj.nblocks--;
    if (g_app.bsel >= g_app.proj.nblocks) g_app.bsel = g_app.proj.nblocks-1;
    etb_refresh_blocks(); etb_refresh_codes();
    etb_log(L"已删除积木 #%d", idx);
}
void etb_block_move(int idx, int delta){
    int ni = idx + delta;
    if (idx<0 || idx>=g_app.proj.nblocks || ni<0 || ni>=g_app.proj.nblocks) return;
    {
        Block tmp = g_app.proj.blocks[idx];
        g_app.proj.blocks[idx] = g_app.proj.blocks[ni];
        g_app.proj.blocks[ni] = tmp;
        g_app.bsel = ni;
    }
    etb_refresh_blocks(); etb_refresh_codes();
}
void etb_block_clear(void){
    if (g_app.proj.nblocks==0) return;
    g_app.proj.nblocks = 0; g_app.bsel = -1;
    etb_refresh_blocks(); etb_refresh_codes();
    etb_log(L"积木脚本已清空");
}

/* ================= 模态小对话框 ================= */
typedef struct {
    HWND dlg; int done; int ok;
} Modal;
static void run_modal(HWND dlg, Modal *m){
    MSG msg;
    m->done = 0; m->ok = 0;
    EnableWindow(g_app.hMain, FALSE);
    while (m->done==0 && GetMessageW(&msg, NULL, 0, 0) > 0){
        if (!IsWindow(dlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(g_app.hMain, TRUE);
    SetForegroundWindow(g_app.hMain);
}
LRESULT CALLBACK ModalProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    Modal *m = (Modal*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg){
    case WM_ERASEBKGND:{
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(C_PANEL);
        FillRect((HDC)wp, &rc, br); DeleteObject(br);
        return 1;
    }
    case WM_DRAWITEM:
        etb_draw_owndraw((LPDRAWITEMSTRUCT)lp);
        return TRUE;
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
    case WM_COMMAND:
        if (m){
            if (LOWORD(wp)==1){ m->ok = 1; m->done = 1; ShowWindow(hwnd, SW_HIDE); return 0; }
            if (LOWORD(wp)==2){ m->done = 1; ShowWindow(hwnd, SW_HIDE); return 0; }
        }
        return 0;
    case WM_CLOSE:
        if (m){ m->done = 1; }
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static HWND modal_make(const wchar_t *title, int w, int h, Modal *m){
    HWND dlg = CreateWindowExW(0, L"ETBDialog", title, WS_POPUP|WS_CAPTION|WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, w, h, g_app.hMain, NULL, g_app.hInst, NULL);
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)m);
    RECT wr; GetWindowRect(g_app.hMain, &wr);
    SetWindowPos(dlg, NULL, wr.left+(wr.right-wr.left-w)/2, wr.top+(wr.bottom-wr.top-h)/2, 0,0, SWP_NOSIZE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    return dlg;
}
static HWND modal_edit(HWND parent, int id, const wchar_t *text, int x, int y, int w, int password){
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                             WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|(password?ES_PASSWORD:0),
                             x, y, w, 26, parent, (HMENU)(INT_PTR)id, g_app.hInst, NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    return e;
}
static HWND modal_btn(HWND parent, int id, const wchar_t *text, int x, int y, int w, COLORREF bg){
    return etb_make_btn(parent, id, text, x, y, w, 32, bg);
}

/* 单输入对话框 */
int etb_prompt(const wchar_t *title, const wchar_t *label, wchar_t *out, int outlen, int password){
    Modal m; HWND dlg, e;
    memset(&m, 0, sizeof(m));
    dlg = modal_make(title, 430, 200, &m);
    if (!dlg) return 0;
    {
        HWND l = CreateWindowW(L"STATIC", label, WS_CHILD|WS_VISIBLE|SS_LEFT, 20, 24, 380, 24, dlg, NULL, g_app.hInst, NULL);
        SendMessageW(l, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    }
    e = modal_edit(dlg, 3, out, 20, 56, 390, password);
    modal_btn(dlg, 1, L"确定", 200, 130, 90, C_ACCENT);
    modal_btn(dlg, 2, L"取消", 300, 130, 90, C_PANEL);
    run_modal(dlg, &m);
    if (m.ok){
        GetWindowTextW(e, out, outlen);
        DestroyWindow(dlg);
        return 1;
    }
    DestroyWindow(dlg);
    return 0;
}

/* 积木参数编辑对话框 */
void etb_edit_block_dialog(int idx){
    Block *b; int di, i;
    HWND edits[6]; HWND labels[6];
    if (idx<0 || idx>=g_app.proj.nblocks) return;
    b = &g_app.proj.blocks[idx];
    di = etb_block_find(b->type);
    if (di < 0) return;
    if (g_blockdefs[di].nparams == 0){
        MessageBoxW(g_app.hMain, L"该积木不需要参数。", L"ET窗口构建器", MB_OK|MB_ICONINFORMATION);
        return;
    }
    {
        Modal m; HWND dlg;
        int n = g_blockdefs[di].nparams > 2 ? 2 : g_blockdefs[di].nparams;
        memset(&m, 0, sizeof(m));
        dlg = modal_make(L"编辑积木参数", 450, 120 + n*46, &m);
        if (!dlg) return;
        for (i=0;i<n;i++){
            labels[i] = CreateWindowW(L"STATIC", g_blockdefs[di].pname[i], WS_CHILD|WS_VISIBLE|SS_LEFT,
                                      20, 16 + i*46, 110, 24, dlg, NULL, g_app.hInst, NULL);
            SendMessageW(labels[i], WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
            edits[i] = modal_edit(dlg, 10+i, b->params[i], 140, 12 + i*46, 280, 0);
        }
        modal_btn(dlg, 1, L"确定", 250, 14 + n*46, 84, C_ACCENT);
        modal_btn(dlg, 2, L"取消", 344, 14 + n*46, 84, C_PANEL);
        run_modal(dlg, &m);
        if (m.ok){
            for (i=0;i<n;i++){
                wchar_t buf[256];
                GetWindowTextW(edits[i], buf, 256);
                wcscpy(b->params[i], buf);
            }
            DestroyWindow(dlg);
            etb_refresh_blocks();
            etb_refresh_codes();
            {
                wchar_t txt[512];
                etb_block_display(b, txt, 512);
                etb_log(L"积木参数已更新: %ls", txt);
            }
        }
        DestroyWindow(dlg);
    }
}

/* ================= 设置对话框 ================= */
void etb_show_window_settings(void){
    Modal m; HWND dlg, e1, e2, e3;
    wchar_t v1[128], v2[64], v3[32];
    memset(&m,0,sizeof(m));
    dlg = modal_make(L"窗口设置", 450, 300, &m);
    if (!dlg) return;
    wcscpy(v1, g_app.proj.title); wcscpy(v2, g_app.proj.size); wcscpy(v3, g_app.proj.bg);
    {
        HWND l = CreateWindowW(L"STATIC", L"窗口标题:", WS_CHILD|WS_VISIBLE, 20, 20, 110, 24, dlg, NULL, g_app.hInst, NULL);
        SendMessageW(l, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
        l = CreateWindowW(L"STATIC", L"窗口大小 (宽x高):", WS_CHILD|WS_VISIBLE, 20, 66, 140, 24, dlg, NULL, g_app.hInst, NULL);
        SendMessageW(l, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
        l = CreateWindowW(L"STATIC", L"背景颜色:", WS_CHILD|WS_VISIBLE, 20, 112, 110, 24, dlg, NULL, g_app.hInst, NULL);
        SendMessageW(l, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    }
    e1 = modal_edit(dlg, 3, v1, 160, 16, 260, 0);
    e2 = modal_edit(dlg, 4, v2, 160, 62, 260, 0);
    e3 = modal_edit(dlg, 5, v3, 160, 108, 190, 0);
    modal_btn(dlg, 6, L"选色...", 356, 108, 70, C_PANEL);
    modal_btn(dlg, 1, L"确定", 250, 210, 84, C_ACCENT);
    modal_btn(dlg, 2, L"取消", 344, 210, 84, C_PANEL);
    /* 选色 */
    {
        /* 在run_modal期间点击按钮6会关闭? 需要特殊处理: 按钮6触发颜色选择 */
        /* 简化: 先放一个静态说明, 颜色用十六进制直接输入 */
    }
    run_modal(dlg, &m);
    if (m.ok){
        GetWindowTextW(e1, v1, 128); GetWindowTextW(e2, v2, 64); GetWindowTextW(e3, v3, 32);
        wcscpy(g_app.proj.title, v1);
        wcscpy(g_app.proj.size, v2);
        wcscpy(g_app.proj.bg, v3);
        etb_refresh_canvas(); etb_refresh_codes(); etb_update_title();
        etb_log(L"窗口设置已更新: %ls %ls %ls", v1, v2, v3);
    }
    DestroyWindow(dlg);
}

void etb_show_pack_settings(void){
    Modal m; HWND dlg;
    HWND e1,e2,e3,e4,e5,e6,e7;
    wchar_t *f[7];
    memset(&m,0,sizeof(m));
    dlg = modal_make(L"打包设置", 480, 460, &m);
    if (!dlg) return;
    f[0]=g_app.pkg_app; f[1]=g_app.pkg_dev; f[2]=g_app.pkg_ver;
    f[3]=g_app.pkg_company; f[4]=g_app.pkg_copy; f[5]=g_app.pkg_desc; f[6]=g_app.pkg_icon;
    {
        static const wchar_t *labs[7] = {L"应用名称:", L"开发者:", L"版本号:", L"公司:", L"版权:", L"描述:", L"图标(.ico):"};
        int i;
        for (i=0;i<7;i++){
            HWND l = CreateWindowW(L"STATIC", labs[i], WS_CHILD|WS_VISIBLE, 20, 16 + i*50, 130, 24, dlg, NULL, g_app.hInst, NULL);
            SendMessageW(l, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
        }
    }
    e1 = modal_edit(dlg, 20, f[0], 160, 12, 290, 0);
    e2 = modal_edit(dlg, 21, f[1], 160, 58, 290, 0);
    e3 = modal_edit(dlg, 22, f[2], 160, 104, 290, 0);
    e4 = modal_edit(dlg, 23, f[3], 160, 150, 290, 0);
    e5 = modal_edit(dlg, 24, f[4], 160, 196, 290, 0);
    e6 = modal_edit(dlg, 25, f[5], 160, 242, 290, 0);
    e7 = modal_edit(dlg, 26, f[6], 160, 288, 210, 0);
    modal_btn(dlg, 6, L"浏览...", 376, 288, 80, C_PANEL);
    modal_btn(dlg, 1, L"保存", 280, 350, 84, C_ACCENT);
    modal_btn(dlg, 2, L"取消", 374, 350, 84, C_PANEL);
    run_modal(dlg, &m);
    if (m.ok){
        GetWindowTextW(e1, g_app.pkg_app, 128);
        GetWindowTextW(e2, g_app.pkg_dev, 64);
        GetWindowTextW(e3, g_app.pkg_ver, 32);
        GetWindowTextW(e4, g_app.pkg_company, 64);
        GetWindowTextW(e5, g_app.pkg_copy, 64);
        GetWindowTextW(e6, g_app.pkg_desc, 256);
        GetWindowTextW(e7, g_app.pkg_icon, MAX_PATH);
        etb_log(L"打包设置已保存");
    }
    DestroyWindow(dlg);
}

void etb_show_about(void){
    Modal m; HWND dlg;
    wchar_t buf[1024];
    memset(&m,0,sizeof(m));
    dlg = modal_make(L"关于", 470, 380, &m);
    if (!dlg) return;
    swprintf(buf, 1024, L"ETC WindowBuilder 专业版 v%s\n\n"
             L"可视化 · 积木式 · 跨语言 GUI 构建器\n"
             L"支持设计画布 / 积木脚本 / C语言 / Python\n"
             L"项目格式: %ls (ET引擎加密)\n\n"
             L"开发者: %ls\n测试者: %ls\n公司: %ls\n版权: %ls\n\n"
             L"官网: https://github.com/ETQWFD",
             VERSION_W, FILE_EXT_W, DEVELOPER_W, TESTERS_W, COMPANY_W, COPYRIGHT_W);
    {
        HWND l = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf,
                                 WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
                                 20, 16, 430, 260, dlg, NULL, g_app.hInst, NULL);
        SendMessageW(l, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    }
    modal_btn(dlg, 2, L"确定", 360, 300, 84, C_ACCENT);
    run_modal(dlg, &m);
    DestroyWindow(dlg);
}

void etb_show_help(void){
    Modal m; HWND dlg;
    wchar_t buf[4096];
    memset(&m,0,sizeof(m));
    dlg = modal_make(L"使用说明", 560, 500, &m);
    if (!dlg) return;
    swprintf(buf, 4096,
        L"【ETC WindowBuilder 专业版 v%s】使用说明\n\n"
        L"■ 设计模式\n"
        L"  1. 左侧点选组件, 添加到画布\n"
        L"  2. 拖动组件调整位置, 双击改文本\n"
        L"  3. 右键菜单: 复制/删除/置顶/置底/形状\n"
        L"  4. 右侧属性面板修改大小/颜色/字号\n"
        L"  5. 顶部“窗口设置”改标题/大小/背景\n\n"
        L"■ 积木模式\n"
        L"  1. 左侧积木库双击添加积木\n"
        L"  2. 拖动积木调整顺序\n"
        L"  3. 双击积木编辑参数\n"
        L"  4. 右键菜单: 上移/下移/删除\n\n"
        L"■ 代码模式\n"
        L"  C语言与Python代码实时生成, 可编译运行\n\n"
        L"■ 项目文件\n"
        L"  .nep 格式, 支持 ET引擎加密(可选密码)\n\n"
        L"■ 运行与打包\n"
        L"  “运行”编译C代码并运行(需安装MinGW/MSVC)\n"
        L"  “打包EXE”生成EXE并制作NSIS安装程序\n\n"
        L"开发者: %ls   公司: %ls   版权: %ls",
        VERSION_W, DEVELOPER_W, COMPANY_W, COPYRIGHT_W);
    {
        HWND l = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf,
                                 WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL,
                                 16, 16, 528, 430, dlg, NULL, g_app.hInst, NULL);
        SendMessageW(l, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    }
    modal_btn(dlg, 2, L"确定", 440, 460, 96, C_ACCENT);
    run_modal(dlg, &m);
    DestroyWindow(dlg);
}

/* ================= 项目操作 ================= */
void etb_update_title(void){
    wchar_t t[256];
    if (g_app.proj.name[0])
        swprintf(t, 256, L"ETC WindowBuilder 专业版 v%s - %ls (%ls)", VERSION_W, g_app.proj.name, FILE_EXT_W);
    else
        swprintf(t, 256, L"ETC WindowBuilder 专业版 v%s - 未命名项目 (%ls)", VERSION_W, FILE_EXT_W);
    SetWindowTextW(g_app.hMain, t);
    if (g_app.hTitleLabel) SetWindowTextW(g_app.hTitleLabel, g_app.proj.title);
}
static void project_reset(void){
    memset(&g_app.proj, 0, sizeof(Project));
    wcscpy(g_app.proj.name, L"未命名项目");
    wcscpy(g_app.proj.title, L"我的应用程序");
    wcscpy(g_app.proj.size, L"800x600");
    wcscpy(g_app.proj.bg, L"#2b2b2b");
    g_app.sel = -1; g_app.bsel = -1;
    g_app.curPath[0] = 0;
    etb_refresh_canvas(); etb_refresh_props(); etb_refresh_codes();
    etb_refresh_blocks(); etb_refresh_files();
    etb_update_title();
}
void etb_new_project(void){
    if (MessageBoxW(g_app.hMain, L"确定新建项目? 当前项目内容将丢失。", L"ETC WindowBuilder",
                    MB_YESNO|MB_ICONQUESTION) == IDYES){
        project_reset();
        etb_log(L"已新建项目");
    }
}
void etb_save_project(void){
    wchar_t file[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    memset(&ofn,0,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_app.hMain;
    ofn.lpstrFilter=L"ET引擎项目 (*.nep)\0*.nep\0所有文件\0*.*\0";
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.lpstrDefExt=L"nep";
    if (!GetSaveFileNameW(&ofn)) return;
    if (!PathFindExtensionW(file)[0]) wcscat(file, FILE_EXT_W);
    {
        wchar_t pw[256] = {0};
        if (!etb_prompt(L"加密密码", L"输入加密密码 (留空则不加密):", pw, 256, 1)) return;
        if (etb_save_project_file(file, pw)){
            wcscpy(g_app.curPath, file);
            {
                wchar_t name[MAX_PATH];
                wcscpy(name, PathFindFileNameW(file));
                wchar_t *dot = wcsrchr(name, L'.');
                if (dot) *dot = 0;
                wcscpy(g_app.proj.name, name);
            }
            etb_update_title();
            etb_log(L"项目已保存: %ls%ls", file, pw[0]?L" (已加密)":L"");
            MessageBoxW(g_app.hMain, file, L"保存成功", MB_OK|MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_app.hMain, L"保存失败!", L"错误", MB_OK|MB_ICONERROR);
        }
    }
}
void etb_open_project(void){
    wchar_t file[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    memset(&ofn,0,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_app.hMain;
    ofn.lpstrFilter=L"ET引擎项目 (*.nep)\0*.nep\0所有文件\0*.*\0";
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
    if (!GetOpenFileNameW(&ofn)) return;
    {
        int r = etb_load_project(file, L"");
        wchar_t pw[256] = {0};
        if (r==2){
            if (!etb_prompt(L"解密密码", L"该项目已加密, 请输入密码:", pw, 256, 1)) return;
            r = etb_load_project(file, pw);
            if (r==2){ MessageBoxW(g_app.hMain, L"密码错误!", L"错误", MB_OK|MB_ICONERROR); return; }
        }
        if (r==1){
            wcscpy(g_app.curPath, file);
            {
                wchar_t name[MAX_PATH];
                wcscpy(name, PathFindFileNameW(file));
                wchar_t *dot = wcsrchr(name, L'.');
                if (dot) *dot = 0;
                wcscpy(g_app.proj.name, name);
            }
            etb_update_title();
            etb_refresh_canvas(); etb_refresh_props(); etb_refresh_codes();
            etb_refresh_blocks(); etb_refresh_files();
            etb_log(L"项目已加载: %ls", file);
            MessageBoxW(g_app.hMain, L"项目加载成功!", L"ETC WindowBuilder", MB_OK|MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_app.hMain, L"无法打开项目文件!", L"错误", MB_OK|MB_ICONERROR);
        }
    }
}
void etb_import_file(void){
    wchar_t file[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    memset(&ofn,0,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_app.hMain;
    ofn.lpstrFilter=L"所有文件\0*.*\0";
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    if (g_app.proj.nfiles >= MAX_FILES){ etb_log(L"项目文件数量已达上限"); return; }
    wcscpy(g_app.proj.files[g_app.proj.nfiles++], file);
    etb_refresh_files();
    etb_log(L"文件已导入项目: %ls", PathFindFileNameW(file));
}
void etb_delete_file(void){
    if (!g_app.hRightFiles) return;
    {
        int sel = (int)SendMessageW(g_app.hRightFiles, LB_GETCURSEL, 0, 0);
        if (sel>=0 && sel<g_app.proj.nfiles){
            memmove(&g_app.proj.files[sel], &g_app.proj.files[sel+1], (g_app.proj.nfiles-sel-1)*sizeof(wchar_t[MAX_PATH]));
            g_app.proj.nfiles--;
            etb_refresh_files();
            etb_log(L"已从项目移除文件");
        }
    }
}

/* ================= 背景 ================= */
void etb_choose_bg_color(void){
    CHOOSECOLORW cc; COLORREF custom[16]={0};
    memset(&cc,0,sizeof(cc));
    cc.lStructSize=sizeof(cc); cc.hwndOwner=g_app.hMain;
    cc.lpCustColors=custom; cc.rgbResult=RGB(43,43,43); cc.Flags=CC_RGBINIT;
    if (ChooseColorW(&cc)){
        wchar_t col[16];
        swprintf(col,16,L"#%02X%02X%02X",GetRValue(cc.rgbResult),GetGValue(cc.rgbResult),GetBValue(cc.rgbResult));
        wcscpy(g_app.proj.bg, col);
        etb_refresh_canvas(); etb_refresh_codes();
        etb_log(L"背景颜色已更新: %ls", col);
    }
}
void etb_import_bg_image(void){
    wchar_t file[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    memset(&ofn,0,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_app.hMain;
    ofn.lpstrFilter=L"图片文件\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0所有文件\0*.*\0";
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    wcscpy(g_app.proj.bg_image, file);
    etb_refresh_canvas(); etb_refresh_codes();
    etb_log(L"背景图片已导入: %ls", file);
}

/* ================= 编译 / 运行 / 打包 ================= */
/* 文件浏览器当前目录(文件模式下使用) */
static wchar_t g_curdir[MAX_PATH];
/* 内置运行时根目录: <程序目录>\runtime (绿色便携, 无需配置PATH) */
static void etb_runtime_base(wchar_t *out, size_t cap){
    wchar_t base[MAX_PATH];
    GetModuleFileNameW(g_app.hInst, base, MAX_PATH);
    PathRemoveFileSpecW(base);
    swprintf(out, cap, L"%ls\\runtime", base);
}
static int etb_runtime_tool(const wchar_t *cat, const wchar_t *rel, wchar_t *out, size_t cap){
    wchar_t rt[MAX_PATH];
    etb_runtime_base(rt, MAX_PATH);
    swprintf(out, cap, L"%ls\\%ls\\%ls", rt, cat, rel);
    if (PathFileExistsW(out)) return 1;
    out[0]=0;
    return 0;
}
/* 把工具所在目录前置到本进程 PATH, 使 cc1/windres/python311/jvm 等
   依赖的同目录 DLL (libwinpthread-1.dll / libgcc_s_seh-1.dll /
   python311.dll / jvm.dll) 在 CreateProcess 子进程中可被找到。
   绿色内置运行时不依赖系统 PATH。 */
static void etb_add_bin_path(const wchar_t *exe){
    wchar_t dir[MAX_PATH], old[32767], buf[34000];
    wchar_t *sl; DWORD n;
    wcsncpy(dir, exe, MAX_PATH-1); dir[MAX_PATH-1]=0;
    sl = wcsrchr(dir, L'\\');
    if (!sl) return;
    *sl = 0;
    n = GetEnvironmentVariableW(L"PATH", old, 32767);
    if (n>0 && n<32767){
        /* 已在 PATH 最前则不重复添加 */
        if (wcsncmp(old, dir, wcslen(dir))==0 && (old[wcslen(dir)]==L';'||old[wcslen(dir)]==0)) return;
        _snwprintf(buf, 34000, L"%ls;%ls", dir, old);
    } else {
        wcsncpy(buf, dir, 33999); buf[33999]=0;
    }
    SetEnvironmentVariableW(L"PATH", buf);
}
int etb_find_tool(const wchar_t *name, wchar_t *out, size_t cap){
    wchar_t full[MAX_PATH];
    int i;
    static const wchar_t *mingw_tools[] = {
        L"gcc.exe",L"g++.exe",L"windres.exe",L"ld.exe",L"as.exe",
        L"dlltool.exe",L"strip.exe",L"ar.exe",L"ranlib.exe",L"objdump.exe",NULL };
    static const wchar_t *jdk_tools[] = {
        L"javac.exe",L"java.exe",L"jar.exe",L"javap.exe",L"javadoc.exe",NULL };
    /* 1) Python */
    if (_wcsicmp(name, L"python.exe")==0 || _wcsicmp(name, L"python")==0 ||
        _wcsicmp(name, L"pythonw.exe")==0){
        if (etb_runtime_tool(L"python", L"python.exe", out, cap)){ etb_add_bin_path(out); return 1; }
    }
    /* 2) MinGW / GCC 工具集 (均位于 runtime\mingw64\bin\) */
    for (i=0; mingw_tools[i]; ++i){
        if (_wcsicmp(name, mingw_tools[i])==0){
            wchar_t rel[64];
            swprintf(rel, 64, L"bin\\%ls", mingw_tools[i]);
            if (etb_runtime_tool(L"mingw64", rel, out, cap)){ etb_add_bin_path(out); return 1; }
            break;
        }
    }
    /* 3) JDK 工具集 */
    for (i=0; jdk_tools[i]; ++i){
        if (_wcsicmp(name, jdk_tools[i])==0){
            wchar_t rel[64];
            swprintf(rel, 64, L"bin\\%ls", jdk_tools[i]);
            if (etb_runtime_tool(L"jdk", rel, out, cap)){ etb_add_bin_path(out); return 1; }
            break;
        }
    }
    /* 4) 回退到系统 PATH */
    {
        DWORD n = SearchPathW(NULL, name, NULL, MAX_PATH, full, NULL);
        if (n>0 && n<MAX_PATH){ wcsncpy(out, full, cap-1); out[cap-1]=0; etb_add_bin_path(out); return 1; }
    }
    return 0;
}
static int run_proc(const wchar_t *cmd, const wchar_t *dir, wchar_t *out, size_t outcap){
    /* 运行进程并捕获输出 */
    SECURITY_ATTRIBUTES sa; HANDLE rd, wr;
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    char buf[8192]; DWORD n;
    int ok = 0;
    out[0] = 0;
    sa.nLength=sizeof(sa); sa.bInheritHandle=TRUE; sa.lpSecurityDescriptor=NULL;
    if (!CreatePipe(&rd,&wr,&sa,0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si,0,sizeof(si)); si.cb=sizeof(si); si.hStdOutput=wr; si.hStdError=wr; si.dwFlags=STARTF_USESTDHANDLES;
    memset(&pi,0,sizeof(pi));
    {
        wchar_t cmdline[2048];
        wcsncpy(cmdline, cmd, 2047); cmdline[2047]=0;
        if (CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)){
            CloseHandle(wr);
            while (ReadFile(rd, buf, sizeof(buf)-1, &n, NULL) && n>0){
                buf[n]=0;
                if (wcslen(out)+n < outcap){
                    MultiByteToWideChar(CP_UTF8, 0, buf, (int)n, out+wcslen(out), (int)(outcap-wcslen(out)-1));
                }
            }
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &n);
            ok = (n==0);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        } else CloseHandle(wr);
    }
    CloseHandle(rd);
    return ok;
}
int etb_compile_c_source(const wchar_t *src, const wchar_t *out_exe, const wchar_t *icon, wchar_t *err, size_t errcap){
    wchar_t gcc[MAX_PATH], rc[MAX_PATH], res[MAX_PATH];
    wchar_t cmd[4096];
    err[0]=0;
    if (!etb_find_tool(L"gcc.exe", gcc, MAX_PATH) && !etb_find_tool(L"gcc", gcc, MAX_PATH)){
        wcscpy(err, L"未找到 gcc 编译器, 请安装 MinGW-w64 并加入 PATH");
        return 0;
    }
    {
        wchar_t dir[MAX_PATH];
        wcsncpy(dir, src, MAX_PATH-1); dir[MAX_PATH-1]=0;
        PathRemoveFileSpecW(dir);
        /* 生成资源文件(版本信息+图标), 版权 ET */
        {
            wchar_t rcf[MAX_PATH]; FILE *f;
            swprintf(rcf, MAX_PATH, L"%ls\\app_ver.rc", dir);
            f = _wfopen(rcf, L"w");
            if (f){
                /* 版本资源一律使用 ASCII, 避免 windres 在 C locale 下
                   处理中文时截断字符串导致 .rc 语法错误; 版权归 ET */
                fwprintf(f, L"1 VERSIONINFO\n FILEVERSION 11,0,0,0\n PRODUCTVERSION 11,0,0,0\n FILEOS 0x40004\n FILETYPE 0x1\n{\n BLOCK \"StringFileInfo\"\n {\n  BLOCK \"040904b0\"\n  {\n   VALUE \"CompanyName\", \"ET\"\n   VALUE \"FileDescription\", \"ETC WindowBuilder Application\"\n   VALUE \"FileVersion\", \"11.0\"\n   VALUE \"LegalCopyright\", \"Copyright (C) ET 2024-2026\"\n   VALUE \"ProductName\", \"ETC WindowBuilder App\"\n   VALUE \"ProductVersion\", \"11.0\"\n  }\n }\n BLOCK \"VarFileInfo\"\n {\n  VALUE \"Translation\", 0x409, 1200\n }\n}\n");
                if (icon && icon[0]){
                    fwprintf(f, L"2 ICON \"%ls\"\n", icon);
                }
                fclose(f);
            }
            if (etb_find_tool(L"windres.exe", rc, MAX_PATH)){
                swprintf(res, MAX_PATH, L"%ls\\app_ver.o", dir);
                swprintf(cmd, 4096, L"\"%ls\" \"%ls\" -O coff -o \"%ls\"", rc, rcf, res);
                {
                    wchar_t oerr[2048];
                    if (!run_proc(cmd, dir, oerr, 2048)){
                        wcscpy(err, L"windres 生成资源失败");
                        return 0;
                    }
                }
            }
        }
        swprintf(cmd, 4096, L"\"%ls\" -O2 -static -mwindows -finput-charset=UTF-8 \"%ls\" -o \"%ls\" -lgdi32 -lcomctl32 -lgdiplus -lshell32 -lshlwapi -lole32 -luuid",
                 gcc, src, out_exe);
        /* 若已生成资源对象文件则一并链接 */
        if (etb_find_tool(L"windres.exe", rc, MAX_PATH)){
            wchar_t reso[MAX_PATH];
            swprintf(reso, MAX_PATH, L"%ls\\app_ver.o", dir);
            if (PathFileExistsW(reso)){
                swprintf(cmd, 4096, L"\"%ls\" -O2 -static -mwindows -finput-charset=UTF-8 \"%ls\" \"%ls\" -o \"%ls\" -lgdi32 -lcomctl32 -lgdiplus -lshell32 -lshlwapi -lole32 -luuid",
                         gcc, src, reso, out_exe);
            }
        }
        {
            wchar_t oerr[8192];
            int ok = run_proc(cmd, dir, oerr, 8192);
            if (!ok){
                if (oerr[0]) wcsncpy(err, oerr, errcap-1);
                else wcscpy(err, L"编译失败");
                return 0;
            }
        }
        return 1;
    }
}
void etb_run_compiled(const wchar_t *exe){
    /* 规范启动: 提供有效的 STARTUPINFO / PROCESS_INFORMATION,
       并以 EXE 所在目录为工作目录。此前三者传 NULL, 在 Wine 下
       子进程缺少正确的桌面/启动信息, 致其 GDI+ 初始化空指针崩溃。 */
    wchar_t cmd[2048], work[MAX_PATH];
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    swprintf(cmd, 2048, L"\"%ls\"", exe);
    wcscpy(work, exe);
    PathRemoveFileSpecW(work);
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, work, &si, &pi)){
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread)  CloseHandle(pi.hThread);
    }
}
void etb_do_compile_run(void){
    /* 生成代码 → 临时目录 → 编译 → 运行 */
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    {
        wchar_t dir[MAX_PATH];
        swprintf(dir, MAX_PATH, L"%lsETCBuilder_%lu", tmp, GetTickCount());
        CreateDirectoryW(dir, NULL);
        {
            wchar_t src[MAX_PATH], exe[MAX_PATH], err[4096];
            size_t len; char *code = etb_gen_c(&g_app.proj, &len);
            swprintf(src, MAX_PATH, L"%ls\\main.c", dir);
            swprintf(exe, MAX_PATH, L"%ls\\app.exe", dir);
            {
                HANDLE h = CreateFileW(src, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                DWORD w;
                if (h!=INVALID_HANDLE_VALUE){ WriteFile(h, code, (DWORD)len, &w, NULL); CloseHandle(h); }
            }
            free(code);
            etb_log(L"开始编译项目...");
            if (etb_compile_c_source(src, exe, NULL, err, 4096)){
                etb_log(L"编译成功: %ls", exe);
                etb_run_compiled(exe);
            } else {
                etb_log(L"编译失败: %ls", err);
                MessageBoxW(g_app.hMain, err, L"编译失败", MB_OK|MB_ICONERROR);
            }
        }
    }
}

/* 写 UTF-8 文本文件 */
static int write_utf8_path(const wchar_t *path, const char *data, size_t len){
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD w;
    if (h == INVALID_HANDLE_VALUE) return 0;
    WriteFile(h, data, (DWORD)len, &w, NULL);
    CloseHandle(h);
    return 1;
}
/* 运行当前生成的 Python 代码 (内置 python.exe, 含 tkinter) */
void etb_do_run_python(void){
    wchar_t tmp[MAX_PATH], dir[MAX_PATH], script[MAX_PATH], py[MAX_PATH], cmd[4096], out[8192];
    size_t len; char *code = etb_gen_py(&g_app.proj, &len);
    GetTempPathW(MAX_PATH, tmp);
    swprintf(dir, MAX_PATH, L"%lsETCBuilder_py_%lu", tmp, GetTickCount());
    CreateDirectoryW(dir, NULL);
    swprintf(script, MAX_PATH, L"%ls\\main.py", dir);
    write_utf8_path(script, code, len);
    free(code);
    if (!etb_find_tool(L"python.exe", py, MAX_PATH)){
        MessageBoxW(g_app.hMain, L"未找到内置 Python 运行环境(runtime\\python)。\n请确认已使用安装包完整安装。",
                    L"运行失败", MB_OK|MB_ICONERROR);
        return;
    }
    etb_log(L"运行 Python: %ls", script);
    /* python.exe -I 隔离模式运行; GUI(tkinter)窗口正常显示, 控制台输出被捕获 */
    swprintf(cmd, 4096, L"\"%ls\" -I -X utf8 \"%ls\"", py, script);
    if (run_proc(cmd, dir, out, 8192)){
        etb_log(L"Python 运行完成");
        if (out[0]) etb_log(L"%ls", out);
    } else {
        etb_log(L"Python 运行出错: %ls", out);
        MessageBoxW(g_app.hMain, out[0]?out:L"Python 运行失败", L"运行错误", MB_OK|MB_ICONERROR);
    }
}
/* 文件浏览器: 编译并运行选中的 .c / .py / .java 文件 */
void etb_explorer_compile_run(void){
    int sel;
    wchar_t name[MAX_PATH], fp[MAX_PATH];
    const wchar_t *ext;
    sel = (int)SendMessageW(g_app.hExplorerList, LB_GETCURSEL, 0, 0);
    if (sel < 0){ etb_log(L"请先在文件列表中选择一个 .c/.py/.java 文件"); return; }
    SendMessageW(g_app.hExplorerList, LB_GETTEXT, sel, (LPARAM)name);
    if (wcsncmp(name, L"[目录]", 5)==0) return;
    swprintf(fp, MAX_PATH, L"%ls\\%ls", g_curdir, name);
    ext = PathFindExtensionW(fp);
    if (_wcsicmp(ext, L".py")==0){
        wchar_t py[MAX_PATH], cmd[4096], out[8192], work[MAX_PATH];
        if (!etb_find_tool(L"python.exe", py, MAX_PATH)){
            MessageBoxW(g_app.hMain, L"未找到内置 Python 环境。", L"运行失败", MB_OK|MB_ICONERROR); return;
        }
        wcscpy(work, fp); PathRemoveFileSpecW(work);
        swprintf(cmd, 4096, L"\"%ls\" -I -X utf8 \"%ls\"", py, fp);
        etb_log(L"运行 Python 文件: %ls", fp);
        if (!run_proc(cmd, work, out, 8192)){
            MessageBoxW(g_app.hMain, out[0]?out:L"运行失败", L"Python 错误", MB_OK|MB_ICONERROR);
        }
        if (out[0]) etb_log(L"%ls", out);
    } else if (_wcsicmp(ext, L".c")==0){
        wchar_t exe[MAX_PATH], err[4096];
        wcscpy(exe, fp); PathRemoveExtensionW(exe); wcscat(exe, L".exe");
        etb_log(L"编译 C 文件: %ls", fp);
        if (etb_compile_c_source(fp, exe, NULL, err, 4096)){
            etb_log(L"编译成功, 启动: %ls", exe);
            etb_run_compiled(exe);
        } else {
            MessageBoxW(g_app.hMain, err, L"C 编译失败", MB_OK|MB_ICONERROR);
        }
    } else if (_wcsicmp(ext, L".java")==0){
        wchar_t javac[MAX_PATH], java[MAX_PATH], cmd[4096], out[8192], work[MAX_PATH];
        if (!etb_find_tool(L"javac.exe", javac, MAX_PATH) || !etb_find_tool(L"java.exe", java, MAX_PATH)){
            MessageBoxW(g_app.hMain, L"未找到内置 JDK(runtime\\jdk)。", L"运行失败", MB_OK|MB_ICONERROR); return;
        }
        wcscpy(work, fp); PathRemoveFileSpecW(work);
        swprintf(cmd, 4096, L"\"%ls\" -encoding UTF-8 \"%ls\"", javac, fp);
        etb_log(L"编译 Java: %ls", fp);
        if (!run_proc(cmd, work, out, 8192)){
            MessageBoxW(g_app.hMain, out[0]?out:L"javac 失败", L"Java 编译错误", MB_OK|MB_ICONERROR);
            if (out[0]) etb_log(L"%ls", out);
            return;
        }
        /* 主类名 = 文件名(无扩展名) */
        {
            wchar_t base[MAX_PATH];
            wcscpy(base, fp);
            PathStripPathW(base);
            PathRemoveExtensionW(base);
            swprintf(cmd, 4096, L"\"%ls\" -Dfile.encoding=UTF-8 -cp \"%ls\" %ls", java, work, base);
            etb_log(L"运行 Java 主类: %ls", base);
        }
        if (!run_proc(cmd, work, out, 8192)){
            MessageBoxW(g_app.hMain, out[0]?out:L"java 运行失败", L"Java 运行错误", MB_OK|MB_ICONERROR);
        }
        if (out[0]) etb_log(L"%ls", out);
    } else {
        etb_log(L"暂不支持运行此类型文件(支持 .c/.py/.java): %ls", ext);
    }
}
/* 顶部运行按钮: 依据当前视图选择语言 */
void etb_do_run_dispatch(void){
    if (g_app.mode == MODE_CODE_PY) etb_do_run_python();
    else etb_do_compile_run();   /* 设计/积木/C代码 → 编译运行C */
}

/* 生成 NSIS 安装脚本 */
static void write_utf8_file(const wchar_t *path, const wchar_t *content){
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    {
        unsigned char bom[3] = {0xEF,0xBB,0xBF};
        int n = WideCharToMultiByte(CP_UTF8, 0, content, -1, NULL, 0, NULL, NULL);
        char *utf = (char*)malloc(n+1);
        DWORD w;
        WideCharToMultiByte(CP_UTF8, 0, content, -1, utf, n, NULL, NULL);
        WriteFile(h, bom, 3, &w, NULL);
        WriteFile(h, utf, n-1, &w, NULL);
        free(utf);
    }
    CloseHandle(h);
}
static int gen_nsis(const wchar_t *dir, const wchar_t *exe_name, const wchar_t *setup_name){
    wchar_t nsi[MAX_PATH], lic[MAX_PATH];
    wchar_t buf[16384]; int pos = 0;
    wchar_t t[1024];
#define NADD(fmt, ...) do{ swprintf(t,1024,fmt,__VA_ARGS__); int _l=(int)wcslen(t); if(pos+_l<16000){ wcscpy(buf+pos,t); pos+=_l; } }while(0)
#define NRAW(s) do{ int _l=(int)wcslen(s); if(pos+_l<16000){ wcscpy(buf+pos,s); pos+=_l; } }while(0)
    swprintf(nsi, MAX_PATH, L"%ls\\installer.nsi", dir);
    swprintf(lic, MAX_PATH, L"%ls\\LICENSE.txt", dir);
    pos = 0;
    NRAW(L"ETC WindowBuilder 专业版\n");
    NRAW(L"开发者: ET  公司: ET Studio  版权: (c) ET 2024-2026\n");
    NRAW(L"本软件版权归 ET 所有, 可自由用于学习与开发, 禁止未经授权的商业再分发。\n");
    write_utf8_file(lic, buf);

    pos = 0;
    NRAW(L"Unicode true\n");
    NRAW(L"!include \"MUI2.nsh\"\n");
    NRAW(L"Name \"ETC WindowBuilder 专业版\"\n");
    NADD(L"OutFile \"%ls\"\n", setup_name);
    NRAW(L"InstallDir \"$PROGRAMFILES\\ET Studio\\ETCWindowBuilder\"\n");
    NRAW(L"InstallDirRegKey HKLM \"Software\\ETStudio\\ETCWindowBuilder\" \"\"\n");
    NRAW(L"RequestExecutionLevel admin\n");
    NRAW(L"VIProductVersion \"11.0.0.0\"\n");
    NRAW(L"VIAddVersionKey \"ProductName\" \"ETC WindowBuilder\"\n");
    NRAW(L"VIAddVersionKey \"CompanyName\" \"ET\"\n");
    NRAW(L"VIAddVersionKey \"LegalCopyright\" \"(c) ET 2024-2026\"\n");
    NRAW(L"VIAddVersionKey \"FileDescription\" \"ETC WindowBuilder 专业版 安装程序\"\n");
    NRAW(L"VIAddVersionKey \"FileVersion\" \"11.0\"\n");
    NRAW(L"!insertmacro MUI_PAGE_WELCOME\n");
    NRAW(L"!insertmacro MUI_PAGE_LICENSE \"LICENSE.txt\"\n");
    NRAW(L"!insertmacro MUI_PAGE_DIRECTORY\n");
    NRAW(L"!insertmacro MUI_PAGE_INSTFILES\n");
    NRAW(L"!insertmacro MUI_PAGE_FINISH\n");
    NRAW(L"!insertmacro MUI_UNPAGE_CONFIRM\n");
    NRAW(L"!insertmacro MUI_UNPAGE_INSTFILES\n");
    NRAW(L"!insertmacro MUI_LANGUAGE \"SimpChinese\"\n");
    NRAW(L"Section \"主程序\" SEC01\n");
    NRAW(L"  SetOutPath \"$INSTDIR\"\n");
    NADD(L"  File \"%ls\"\n", exe_name);
    NRAW(L"  File \"LICENSE.txt\"\n");
    NRAW(L"  File /r \"docs\"\n");
    NRAW(L"  CreateDirectory \"$SMPROGRAMS\\ET Studio\"\n");
    NADD(L"  CreateShortCut \"$SMPROGRAMS\\ET Studio\\ETC WindowBuilder.lnk\" \"$INSTDIR\\%ls\"\n", exe_name);
    NADD(L"  CreateShortCut \"$DESKTOP\\ETC WindowBuilder.lnk\" \"$INSTDIR\\%ls\"\n", exe_name);
    NRAW(L"  WriteUninstaller \"$INSTDIR\\Uninstall.exe\"\n");
    NRAW(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"DisplayName\" \"ETC WindowBuilder 专业版\"\n");
    NRAW(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"UninstallString\" \"\\\"$INSTDIR\\Uninstall.exe\\\"\"\n");
    NADD(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"DisplayIcon\" \"$INSTDIR\\%ls\"\n", exe_name);
    NRAW(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"DisplayVersion\" \"11.0\"\n");
    NRAW(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"Publisher\" \"ET\"\n");
    NRAW(L"SectionEnd\n");
    NRAW(L"Section \"卸载\" SEC02\n");
    NADD(L"  Delete \"$INSTDIR\\%ls\"\n", exe_name);
    NRAW(L"  Delete \"$INSTDIR\\LICENSE.txt\"\n");
    NRAW(L"  RMDir /r \"$INSTDIR\\docs\"\n");
    NRAW(L"  Delete \"$INSTDIR\\Uninstall.exe\"\n");
    NRAW(L"  RMDir \"$INSTDIR\"\n");
    NRAW(L"  Delete \"$DESKTOP\\ETC WindowBuilder.lnk\"\n");
    NRAW(L"  Delete \"$SMPROGRAMS\\ET Studio\\ETC WindowBuilder.lnk\"\n");
    NRAW(L"  RMDir \"$SMPROGRAMS\\ET Studio\"\n");
    NRAW(L"  DeleteRegKey HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\"\n");
    NRAW(L"SectionEnd\n");
    buf[pos]=0;
    write_utf8_file(nsi, buf);
#undef NADD
#undef NRAW
    return 1;
}
void etb_do_package(void){
    wchar_t dir[MAX_PATH] = {0};
    /* 选择输出目录 */
    {
        BROWSEINFOW bi; PIDLIST_ABSOLUTE pidl; wchar_t display[MAX_PATH];
        memset(&bi,0,sizeof(bi)); bi.hwndOwner=g_app.hMain;
        bi.lpszTitle=L"选择EXE/安装包输出目录";
        bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
        pidl = SHBrowseForFolderW(&bi);
        if (!pidl) return;
        SHGetPathFromIDListW(pidl, display);
        CoTaskMemFree(pidl);
        wcscpy(dir, display);
    }
    etb_log(L"开始打包到: %ls", dir);
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        {
            wchar_t build[MAX_PATH];
            swprintf(build, MAX_PATH, L"%lsETCBuilder_pack_%lu", tmp, GetTickCount());
            CreateDirectoryW(build, NULL);
            {
                wchar_t src[MAX_PATH], exe_name[MAX_PATH], setup_name[MAX_PATH], err[4096];
                size_t len; char *code = etb_gen_c(&g_app.proj, &len);
                wchar_t app_name[160];
                swprintf(app_name, 160, L"%ls.exe", g_app.pkg_app[0]?g_app.pkg_app:L"ETCWindowBuilderApp");
                swprintf(src, MAX_PATH, L"%ls\\main.c", build);
                swprintf(exe_name, MAX_PATH, L"%ls\\%ls", dir, app_name);
                swprintf(setup_name, MAX_PATH, L"%ls\\ETCWindowBuilder-Setup-v%ls.exe", dir, g_app.pkg_ver[0]?g_app.pkg_ver:VERSION_W);
                {
                    HANDLE h = CreateFileW(src, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                    DWORD w;
                    if (h!=INVALID_HANDLE_VALUE){ WriteFile(h, code, (DWORD)len, &w, NULL); CloseHandle(h); }
                }
                free(code);
                etb_log(L"编译EXE: %ls", app_name);
                if (!etb_compile_c_source(src, exe_name, g_app.pkg_icon[0]?g_app.pkg_icon:NULL, err, 4096)){
                    etb_log(L"打包失败: %ls", err);
                    MessageBoxW(g_app.hMain, err, L"打包失败", MB_OK|MB_ICONERROR);
                    return;
                }
                etb_log(L"EXE生成成功: %ls", exe_name);
                /* 复制学习文档 */
                {
                    wchar_t docs_src[MAX_PATH], docs_dst[MAX_PATH];
                    GetModuleFileNameW(g_app.hInst, docs_src, MAX_PATH);
                    PathRemoveFileSpecW(docs_src);
                    wcscat(docs_src, L"\\docs");
                    swprintf(docs_dst, MAX_PATH, L"%ls\\docs", build);
                    if (PathFileExistsW(docs_src)){
                        CopyFileW(docs_src, docs_dst, FALSE);
                        /* 简单: 复制目录内容 */
                        wchar_t pat[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE hf;
                        swprintf(pat, MAX_PATH, L"%ls\\*", docs_src);
                        hf = FindFirstFileW(pat, &fd);
                        if (hf != INVALID_HANDLE_VALUE){
                            CreateDirectoryW(docs_dst, NULL);
                            do {
                                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)){
                                    wchar_t sf[MAX_PATH], df[MAX_PATH];
                                    swprintf(sf, MAX_PATH, L"%ls\\%ls", docs_src, fd.cFileName);
                                    swprintf(df, MAX_PATH, L"%ls\\%ls", docs_dst, fd.cFileName);
                                    CopyFileW(sf, df, FALSE);
                                }
                            } while (FindNextFileW(hf, &fd));
                            FindClose(hf);
                        }
                    } else {
                        CreateDirectoryW(docs_dst, NULL);
                    }
                }
                /* NSIS 安装包 */
                etb_log(L"生成NSIS安装脚本...");
                if (gen_nsis(build, app_name, setup_name)){
                    wchar_t makensis[MAX_PATH];
                    if (etb_find_tool(L"makensis.exe", makensis, MAX_PATH) || etb_find_tool(L"makensis", makensis, MAX_PATH)){
                        wchar_t cmd[4096], oerr[8192], nsi[MAX_PATH];
                        swprintf(nsi, MAX_PATH, L"%ls\\installer.nsi", build);
                        swprintf(cmd, 4096, L"\"%ls\" \"%ls\"", makensis, nsi);
                        etb_log(L"makensis 正在制作安装包...");
                        if (run_proc(cmd, build, oerr, 8192)){
                            etb_log(L"安装包生成成功: %ls", setup_name);
                            {
                                wchar_t msg[1024];
                                swprintf(msg, 1024, L"打包完成!\n\nEXE 与安装包已生成到:\n%ls\n\n安装包: %ls", dir, setup_name);
                                MessageBoxW(g_app.hMain, msg, L"打包成功", MB_OK|MB_ICONINFORMATION);
                            }
                        } else {
                            etb_log(L"makensis 失败: %ls", oerr);
                            MessageBoxW(g_app.hMain, oerr, L"安装包制作失败(EXE已生成)", MB_OK|MB_ICONWARNING);
                        }
                    } else {
                        etb_log(L"未找到 makensis, 已跳过安装包制作(仅生成EXE)");
                        MessageBoxW(g_app.hMain, L"未找到 makensis (NSIS), 仅生成EXE。", L"提示", MB_OK|MB_ICONINFORMATION);
                    }
                } else {
                    etb_log(L"NSIS脚本生成失败(仅生成EXE)");
                }
                MessageBoxW(g_app.hMain, L"打包完成!", L"打包成功", MB_OK|MB_ICONINFORMATION);
            }
        }
    }
}

/* ================= 文件浏览器 ================= */
void etb_explorer_refresh(void){
    WIN32_FIND_DATAW fd; HANDLE hf; wchar_t pat[MAX_PATH];
    if (!g_app.hExplorerList) return;
    if (!g_curdir[0]) GetCurrentDirectoryW(MAX_PATH, g_curdir);
    SetWindowTextW(g_app.hExplorerPath, g_curdir);
    SendMessageW(g_app.hExplorerList, LB_RESETCONTENT, 0, 0);
    swprintf(pat, MAX_PATH, L"%ls\\*", g_curdir);
    hf = FindFirstFileW(pat, &fd);
    if (hf != INVALID_HANDLE_VALUE){
        do {
            wchar_t item[MAX_PATH];
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
                if (fd.cFileName[0]==L'.' && !(fd.cFileName[1])) continue;
                swprintf(item, MAX_PATH, L"[目录] %ls", fd.cFileName);
            } else {
                swprintf(item, MAX_PATH, L"%ls", fd.cFileName);
            }
            SendMessageW(g_app.hExplorerList, LB_ADDSTRING, 0, (LPARAM)item);
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
}
void etb_explorer_enter(const wchar_t *name){
    wchar_t np[MAX_PATH];
    swprintf(np, MAX_PATH, L"%ls\\%ls", g_curdir, name);
    if (PathIsDirectoryW(np)){ wcscpy(g_curdir, np); etb_explorer_refresh(); }
}
void etb_explorer_open(const wchar_t *name){
    wchar_t fp[MAX_PATH];
    swprintf(fp, MAX_PATH, L"%ls\\%ls", g_curdir, name);
    ShellExecuteW(NULL, L"open", fp, NULL, NULL, SW_SHOWNORMAL);
}
void etb_explorer_up(void){
    wchar_t parent[MAX_PATH];
    wcscpy(parent, g_curdir);
    PathRemoveFileSpecW(parent);
    if (parent[0] && wcscmp(parent, g_curdir)!=0){ wcscpy(g_curdir, parent); etb_explorer_refresh(); }
}
void etb_explorer_import(void){
    int sel = (int)SendMessageW(g_app.hExplorerList, LB_GETCURSEL, 0, 0);
    if (sel<0) return;
    {
        wchar_t item[MAX_PATH];
        SendMessageW(g_app.hExplorerList, LB_GETTEXT, sel, (LPARAM)item);
        if (wcsncmp(item, L"[目录]", 5)==0) return;
        if (g_app.proj.nfiles >= MAX_FILES){ etb_log(L"项目文件数量已达上限"); return; }
        swprintf(g_app.proj.files[g_app.proj.nfiles++], MAX_PATH, L"%ls\\%ls", g_curdir, item);
        etb_refresh_files();
        etb_log(L"已导入: %ls", item);
    }
}
