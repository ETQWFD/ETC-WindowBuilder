/* ============================================================
 * etb_ui2.c - 积木编辑 / 代码视图 / 对话框 / 项目读写 / 编译打包
 * 版权: (c) ETC 2024-2026
 * ============================================================ */
#include "etb_internal.h"
#include <wctype.h>
#include <wininet.h>

/* ================= 代码视图 / 三向同步 ================= */
static void utf8_to_wbuf(const char *utf8, wchar_t *out, size_t cap){
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, (int)cap);
}

/* 把 wchar 文本转成 UTF-8, 长度显式、不含结尾 NUL (修复 null bytes) */
static char *wbuf_to_utf8(const wchar_t *w, size_t wlen, size_t *outlen){
    int n = WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen, NULL, 0, NULL, NULL);
    char *o;
    if (n <= 0) n = 0;
    o = (char*)malloc(n+1);
    if (n>0) WideCharToMultiByte(CP_UTF8, 0, w, (int)wlen, o, n, NULL, NULL);
    o[n]=0; if (outlen)*outlen=(size_t)n; return o;
}

/* 多行 EDIT 控件需要 \r\n; 生成器输出仅 \n, 这里转换后再显示 */
static void set_multiline(HWND hEdit, const char *utf8, size_t len){
    wchar_t *w, *out; size_t need, i, j;
    if (!hEdit) return;
    w = (wchar_t*)malloc((len+2)*sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, w, (int)len+1);
    w[len]=0;
    need = len + 1;
    for (i=0;i<len;i++){ if (w[i]==L'\n' && (i==0 || w[i-1]!=L'\r')) need++; }
    out = (wchar_t*)malloc((need+1)*sizeof(wchar_t));
    for (i=0,j=0;i<len;i++){
        if (w[i]==L'\n' && (i==0 || w[i-1]!=L'\r')) out[j++]=L'\r';
        out[j++]=w[i];
    }
    out[j]=0;
    g_app.syncing = 1;
    SetWindowTextW(hEdit, out);
    g_app.syncing = 0;
    free(w); free(out);
}

void etb_refresh_codes(void){
    size_t len; char *c;
    c = etb_gen_c(&g_app.proj, &len);
    set_multiline(g_app.hCodeC, c, len);
    free(c);
    c = etb_gen_py(&g_app.proj, &len);
    set_multiline(g_app.hCodePy, c, len);
    free(c);
}

/* 取编辑器 wchar 原文 (调用者 free) */
static wchar_t *editor_get_w(HWND hEdit){
    int n; wchar_t *buf;
    if (!hEdit) return NULL;
    n = GetWindowTextLengthW(hEdit);
    buf = (wchar_t*)malloc((size_t)(n+2)*sizeof(wchar_t));
    n = GetWindowTextW(hEdit, buf, n+1);
    buf[n]=0;
    return buf;
}

char *etb_current_code_utf8(int which, size_t *len){
    HWND h = which==0 ? g_app.hCodeC : g_app.hCodePy;
    if (h && GetWindowTextLengthW(h) > 0){
        wchar_t *w = editor_get_w(h);
        char *u = wbuf_to_utf8(w, wcslen(w), len);
        free(w);
        return u;
    }
    /* 编辑器不可用(如文件模式)时回退到生成器 */
    return which==0 ? etb_gen_c(&g_app.proj, len) : etb_gen_py(&g_app.proj, len);
}

/* ---- 反向解析: 从代码文本重建组件, 实现 代码→设计/积木 同步 ---- */
static const wchar_t *grab_quoted(const wchar_t *from, wchar_t *out, size_t cap){
    const wchar_t *p = from?wcschr(from, L'"'):NULL;
    size_t n=0;
    if (!p) return NULL;
    p++;
    while (*p && n+1<cap){
        if (*p==L'\\' && p[1]){ p++; out[n++]=*p++; }
        else if (*p==L'"'){ out[n]=0; return p+1; }
        else out[n++]=*p++;
    }
    out[n]=0; return NULL;
}
static void grab_attr(const wchar_t *a, const wchar_t *b, const wchar_t *key, wchar_t *out, size_t cap, const wchar_t *dft){
    size_t kl = wcslen(key); const wchar_t *p=a;
    out[0]=0;
    while (p && p<b){
        const wchar_t *q = wcsstr(p, key);
        if (!q || q>=b) break;
        if (grab_quoted(q+kl, out, cap)) return;
        p = q+1;
    }
    if (dft) wcscpy(out, dft);
}
/* 在 [a,b) 中取最后4个非负整数 (x,y,w,h) */
static int last4_ints(const wchar_t *a, const wchar_t *b, int*x,int*y,int*w,int*h){
    int v[4]={0}, got=0; const wchar_t *q=b;
    while (q>a && got<4){
        while (q>a && !iswdigit((wint_t)q[-1])) q--;
        if (q<=a || !iswdigit((wint_t)q[-1])) break;
        { long val=0; const wchar_t *e=q, *s=q;
          while (s>a && iswdigit((wint_t)s[-1])) s--;
          { const wchar_t*t=s; while(t<e){val=val*10+(*t-L'0');t++;} }
          v[got++]=(int)val; q=s; }
    }
    if (got<4) return 0;
    *x=v[3];*y=v[2];*w=v[1];*h=v[0]; return 1;
}
static void comp_init(Component *c, int type, int id){
    memset(c,0,sizeof(*c));
    c->id=id; c->type=type; c->x=40;c->y=40;c->w=100;c->h=34;
    c->font_size=14; wcscpy(c->shape,L"rectangle"); c->visible=1;
    wcscpy(c->bg,L"#0078d4"); wcscpy(c->fg,L"#ffffff");
    if (type==CT_LABEL||type==CT_ENTRY||type==CT_IMAGE||type==CT_COMBO||type==CT_PROGRESS) wcscpy(c->bg,L"#2b2b2b");
    if (type==CT_COMBO) wcscpy(c->values,L"选项1,选项2,选项3");
}

static int parse_c_code(const wchar_t *s){
    Component arr[MAX_COMPS]; int n=0; wchar_t imgp[MAX_COMPS][MAX_PATH]; int hasimg=0;
    const wchar_t *p=s;
    memset(imgp,0,sizeof(imgp));
    /* 收集图片: GdipCreateBitmapFromFile(L"..") ... g_img[i] */
    { const wchar_t *q=s;
      while ((q=wcsstr(q,L"GdipCreateBitmapFromFile(L\""))){
          wchar_t path[MAX_PATH]; int idx=-1; const wchar_t *r;
          r = grab_quoted(q+wcslen(L"GdipCreateBitmapFromFile(L"), path, MAX_PATH);
          if (r){ const wchar_t *g=wcsstr(r,L"g_img["); if(g){ const wchar_t*t=g+6; idx=0; while(iswdigit((wint_t)*t)){idx=idx*10+(*t-L'0');t++;} } }
          if (idx>=0 && idx<MAX_COMPS){ wcscpy(imgp[idx],path); hasimg=1; }
          q=r?r:q+1;
      } }
    while ((p=wcsstr(p,L"g_h[")) && n<MAX_COMPS){
        const wchar_t *cw = wcsstr(p,L"CreateWindowW(L\"");
        const wchar_t *hwndp = wcsstr(p,L", hwnd,");
        if (!cw || !hwndp || hwndp>p+600){ p+=3; continue; }
        {
            wchar_t cls[128]={0}, txt[260]={0}; int x,y,w,h,type;
            const wchar_t *after;
            after = grab_quoted(cw+wcslen(L"CreateWindowW(L"), cls, 128);
            if (after) grab_quoted(after, txt, 260);
            { const wchar_t *vis=wcsstr(p,L"WS_VISIBLE"); const wchar_t *a = vis?vis+10:p;
              if (!last4_ints(a, hwndp, &x,&y,&w,&h)){ p+=3; continue; } }
            if (wcscmp(cls,L"BUTTON")==0){
                if (wcsstr(p,L"BS_AUTOCHECKBOX")) type=CT_CHECK;
                else if (wcsstr(p,L"BS_AUTORADIOBUTTON")) type=CT_RADIO;
                else type=CT_BUTTON;
            } else if (wcscmp(cls,L"STATIC")==0) type=CT_LABEL;
            else if (wcscmp(cls,L"EDIT")==0) type=CT_ENTRY;
            else if (wcscmp(cls,L"COMBOBOX")==0) type=CT_COMBO;
            else if (wcsstr(cls,L"progress")||wcsstr(cls,L"Progress")) type=CT_PROGRESS;
            else { p+=3; continue; }
            comp_init(&arr[n],type,n);
            wcscpy(arr[n].text, txt[0]?txt:L"");
            arr[n].x=x;arr[n].y=y;arr[n].w=w;arr[n].h=h;
            if (hasimg && imgp[n][0]){ arr[n].type=CT_IMAGE; wcscpy(arr[n].image_path,imgp[n]); }
            n++;
        }
        p = hwndp+1;
    }
    if (n==0) return 0;
    { int i; for (i=0;i<n;i++) g_app.proj.comps[i]=arr[i];
      g_app.proj.ncomps=n; if (g_app.proj.next_id<n) g_app.proj.next_id=n; }
    /* 窗口标题/尺寸/背景 */
    { wchar_t t[256]; const wchar_t*q=wcsstr(s,L"CreateWindowExW(0, L\"ETCGenApp\", L\"");
      if (q && grab_quoted(q+wcslen(L"CreateWindowExW(0, L\"ETCGenApp\", L"), t, 256)) wcscpy(g_app.proj.title,t); }
    return 1;
}

static int parse_py_code(const wchar_t *s){
    static const struct { const wchar_t *kw; int type; int isimg; } keys[] = {
        {L"tk.Button",CT_BUTTON,0},{L"tk.Label",CT_LABEL,0},{L"tk.Entry",CT_ENTRY,0},
        {L"tk.Checkbutton",CT_CHECK,0},{L"tk.Radiobutton",CT_RADIO,0},
        {L"ttk.Combobox",CT_COMBO,0},{L"ttk.Progressbar",CT_PROGRESS,0},
        {L"Image.open",CT_IMAGE,1},
    };
    Component arr[MAX_COMPS]; int n=0; const wchar_t *cur=s;
    for (;;){
        int k, best=-1; const wchar_t *bestp=NULL;
        for (k=0;k<8;k++){ const wchar_t *q=wcsstr(cur,keys[k].kw);
            if (q && (!bestp || q<bestp)){ bestp=q; best=k; } }
        if (best<0 || !bestp || n>=MAX_COMPS) break;
        {
            const wchar_t *place = wcsstr(bestp,L".place(");
            const wchar_t *next = place? place+7 : bestp+wcslen(keys[best].kw);
            int x,y,w,h;
            if (!place){ cur=bestp+1; continue; }
            if (swscanf(place,L".place(x=%d, y=%d, width=%d, height=%d)",&x,&y,&w,&h)!=4){
                cur=next; continue;
            }
            comp_init(&arr[n], keys[best].type, n);
            arr[n].x=x;arr[n].y=y;arr[n].w=w;arr[n].h=h;
            if (keys[best].isimg){
                wchar_t path[MAX_PATH];
                if (grab_quoted(bestp, path, MAX_PATH)) wcscpy(arr[n].image_path,path);
            } else {
                wchar_t t[260],bg[40],fg[40];
                grab_attr(bestp,place,L"text=\"",t,260,NULL);
                grab_attr(bestp,place,L"bg=\"",bg,40,NULL);
                grab_attr(bestp,place,L"fg=\"",fg,40,NULL);
                if (t[0]) wcscpy(arr[n].text,t);
                if (bg[0]) wcscpy(arr[n].bg,bg);
                if (fg[0]) wcscpy(arr[n].fg,fg);
            }
            n++; cur=next;
        }
    }
    if (n==0) return 0;
    { int i; for (i=0;i<n;i++) g_app.proj.comps[i]=arr[i];
      g_app.proj.ncomps=n; if (g_app.proj.next_id<n) g_app.proj.next_id=n; }
    { wchar_t t[260]; const wchar_t*q;
      q=wcsstr(s,L".title(\""); if(q){ if(grab_quoted(q+8,t,260)) wcscpy(g_app.proj.title,t); }
      q=wcsstr(s,L".geometry(\""); if(q){ if(grab_quoted(q+11,t,260)) wcscpy(g_app.proj.size,t); }
      q=wcsstr(s,L"configure(bg=\""); if(q){ if(grab_quoted(q+14,t,40)) wcscpy(g_app.proj.bg,t); } }
    return 1;
}

void etb_code_parse_now(void){
    wchar_t *w; int ok=0;
    if (g_app.parse_which==0){
        if ((w=editor_get_w(g_app.hCodeC))){ ok=parse_c_code(w); free(w); }
    } else {
        if ((w=editor_get_w(g_app.hCodePy))){ ok=parse_py_code(w); free(w); }
    }
    if (!ok){ etb_log(L"代码格式暂无法解析为组件(可继续手动编辑)"); return; }
    g_app.sel=-1;
    etb_refresh_canvas(); etb_refresh_props(); etb_refresh_blocks();
    /* 用更新后的项目重新生成"另一种"语言; 当前页保留用户代码 */
    {
        size_t len; char *c;
        g_app.syncing=1;
        c = etb_gen_c(&g_app.proj,&len); set_multiline(g_app.hCodeC,c,len); free(c);
        c = etb_gen_py(&g_app.proj,&len); set_multiline(g_app.hCodePy,c,len); free(c);
        g_app.syncing=0;
    }
    etb_refresh_files();
    etb_log(L"已从%s代码同步到设计/积木", g_app.parse_which==0?L"C":L"Python");
}

void etb_code_changed(int which){
    if (g_app.syncing) return;
    g_app.parse_which = which;
    SetTimer(g_app.hMain, IDT_CODEPARSE, 400, NULL);   /* 防抖 */
}

void etb_sync_from_code(void){
    /* “同步代码”按钮: 以当前所在代码页为准, 反向同步到设计器 */
    g_app.parse_which = (g_app.mode==MODE_CODE_PY)?1:0;
    etb_code_parse_now();
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
            HBRUSH hbg = CreateSolidBrush(C_BG);
            for (i=0;i<g_app.proj.nblocks;i++){
                Block *b = &g_app.proj.blocks[i];
                int di = etb_block_find(b->type);
                COLORREF col = di>=0 ? g_blockdefs[di].color : RGB(120,120,120);
                COLORREF edge = (i==g_app.bsel)? RGB(120,220,255) : RGB(255,255,255);
                int isEvent = etb_block_is_event(b->type);
                int isEnd = etb_block_has_end(b->type);
                int depth = block_depth(i);
                int cx;
                RECT r; wchar_t txt[512];
                block_rect(i, &r, rc.right-rc.left);
                r.left += depth*22;
                if (isEnd) r.left += 26;          /* 结束块内缩、变短 */
                cx = r.left + 60;
                {
                    HBRUSH fb = CreateSolidBrush(col);
                    HPEN pn = CreatePen(PS_SOLID, i==g_app.bsel?2:1, edge);
                    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    HGDIOBJ op = SelectObject(hdc, pn);
                    SelectObject(hdc, fb);
                    if (isEvent){
                        /* 事件块: 编程猫式圆帽(顶部大圆角, 向上凸起) */
                        RoundRect(hdc, r.left, r.top-10, r.right, r.bottom, 20, 20);
                    } else {
                        if (!isEnd){
                            /* 顶部凸舌(插入上一块底部凹槽) */
                            RoundRect(hdc, cx-24, r.top-8, cx+24, r.top+6, 10, 10);
                        }
                        RoundRect(hdc, r.left, r.top, r.right, r.bottom, isEnd?8:12, isEnd?8:12);
                    }
                    SelectObject(hdc, ob); SelectObject(hdc, op);
                    DeleteObject(fb); DeleteObject(pn);
                    if (!isEnd){
                        /* 底部凹槽: 用背景色挖一个半圆缺口, 下一块凸舌嵌入 */
                        HGDIOBJ ob2 = SelectObject(hdc, GetStockObject(NULL_PEN));
                        SelectObject(hdc, hbg);
                        Ellipse(hdc, cx-9, r.bottom-5, cx+9, r.bottom+5);
                        SelectObject(hdc, ob2);
                    }
                }
                etb_block_display(b, txt, 512);
                {
                    RECT tr = r; tr.left += (isEvent?40:16); tr.right -= 8; tr.top -= isEvent?6:0;
                    SetTextColor(hdc, RGB(255,255,255));
                    SetBkMode(hdc, TRANSPARENT);
                    HFONT f = CreateFontW(-16,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
                    HFONT of = (HFONT)SelectObject(hdc, f);
                    DrawTextW(hdc, txt, -1, &tr, DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                    SelectObject(hdc, of); DeleteObject(f);
                }
            }
            DeleteObject(hbg);
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
        MessageBoxW(g_app.hMain, L"该积木不需要参数。", L"ETC窗口构建器", MB_OK|MB_ICONINFORMATION);
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
/* 仅保留 ASCII 字符(其余替成 '?'), 保证 windres 在 C locale 下
   处理 .rc 版本资源不会因非 ASCII 字符串截断而报语法错误。 */
static void ascii_only(const wchar_t *src, wchar_t *dst, size_t cap){
    size_t i;
    if (!cap) return;
    for (i=0; i+1<cap && src && src[i]; i++)
        dst[i] = (src[i] < 0x80 && src[i] != L'"') ? src[i] : L'?';
    dst[i]=0;
}
/* 把 "12.0" / "12.0.1" 形式版本号转为 FILEVERSION 用的 "12,0,0,0"。 */
static void ver_comma(const wchar_t *ver, wchar_t *out, size_t cap){
    int vals[4]={12,0,0,0}; int n=0, cur=0; const wchar_t *p;
    for (p=ver; p && *p && n<4; p++){
        if (*p>=L'0' && *p<=L'9'){ cur=cur*10+(*p-L'0'); }
        else { vals[n++]=cur; cur=0; }
    }
    if (n<4) vals[n]=cur;
    swprintf(out, cap, L"%d,%d,%d,%d", vals[0],vals[1],vals[2],vals[3]);
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
        /* 生成资源文件(版本信息+图标), 版权 ETC */
        {
            wchar_t rcf[MAX_PATH]; FILE *f;
            swprintf(rcf, MAX_PATH, L"%ls\\app_ver.rc", dir);
            f = _wfopen(rcf, L"w");
            if (f){
                /* 版本/署名取打包设置并全部 ASCII 化, 避免 windres 在
                   C locale 下截断非 ASCII 字符串; 开发者/版权归 ETC。 */
                {
                    wchar_t vc[32], va[64], vd[256], vapp[128], vcomp[128], vcopy[128];
                    const wchar_t *ver = g_app.pkg_ver[0] ? g_app.pkg_ver : L"12.1";
                    ver_comma(ver, vc, 32);
                    ascii_only(ver, va, 64);
                    ascii_only(g_app.pkg_desc[0]?g_app.pkg_desc:L"ETC WindowBuilder Application", vd, 256);
                    ascii_only(g_app.pkg_app[0]?g_app.pkg_app:L"ETCWindowBuilderApp", vapp, 128);
                    ascii_only(g_app.pkg_company[0]?g_app.pkg_company:L"ETC Studio", vcomp, 128);
                    ascii_only(g_app.pkg_copy[0]?g_app.pkg_copy:L"Copyright (C) ETC 2024-2026", vcopy, 128);
                    fwprintf(f,
                        L"1 VERSIONINFO\n FILEVERSION %ls\n PRODUCTVERSION %ls\n FILEOS 0x40004\n FILETYPE 0x1\n{\n BLOCK \"StringFileInfo\"\n {\n  BLOCK \"040904b0\"\n {\n"
                        L"   VALUE \"CompanyName\", \"%ls\"\n"
                        L"   VALUE \"FileDescription\", \"%ls\"\n"
                        L"   VALUE \"FileVersion\", \"%ls\"\n"
                        L"   VALUE \"InternalName\", \"%ls\"\n"
                        L"   VALUE \"LegalCopyright\", \"%ls\"\n"
                        L"   VALUE \"OriginalFilename\", \"%ls.exe\"\n"
                        L"   VALUE \"ProductName\", \"%ls\"\n"
                        L"   VALUE \"ProductVersion\", \"%ls\"\n  }\n }\n"
                        L" BLOCK \"VarFileInfo\"\n {\n  VALUE \"Translation\", 0x409, 1200\n }\n}\n",
                        vc, vc, vcomp, vd, va, vapp, vcopy, vapp, vapp, va);
                }
                {
                    /* 图标: 用户选择优先, 否则用构建器自带 app.ico;
                       复制到构建目录的纯 ASCII 名 app.ico, 规避 windres
                       处理中文/空格路径失败。资源 ID 固定 100。 */
                    wchar_t ico_src[MAX_PATH], ico_dst2[MAX_PATH]; int have_icon=0;
                    if (icon && icon[0]){ wcsncpy(ico_src,icon,MAX_PATH-1); ico_src[MAX_PATH-1]=0; }
                    else { GetModuleFileNameW(g_app.hInst,ico_src,MAX_PATH); PathRemoveFileSpecW(ico_src); wcscat(ico_src,L"\\app.ico"); }
                    swprintf(ico_dst2,MAX_PATH,L"%ls\\app.ico",dir);
                    if (PathFileExistsW(ico_src) && CopyFileW(ico_src,ico_dst2,FALSE) && PathFileExistsW(ico_dst2)) have_icon=1;
                    if (have_icon) fwprintf(f, L"100 ICON \"app.ico\"\n");
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
    NRAW(L"开发者: " DEVELOPER_W L"  公司: " COMPANY_W L"  版权: " COPYRIGHT_W L"\n");
    NRAW(L"本软件版权归 " DEVELOPER_W L" 所有, 可自由用于学习与开发, 禁止未经授权的商业再分发。\n");
    write_utf8_file(lic, buf);

    pos = 0;
    NRAW(L"Unicode true\n");
    NRAW(L"!include \"MUI2.nsh\"\n");
    NRAW(L"Name \"ETC WindowBuilder 专业版\"\n");
    NADD(L"OutFile \"%ls\"\n", setup_name);
    NRAW(L"InstallDir \"$PROGRAMFILES\\ETC Studio\\ETCWindowBuilder\"\n");
    NRAW(L"InstallDirRegKey HKLM \"Software\\ETCStudio\\ETCWindowBuilder\" \"\"\n");
    NRAW(L"RequestExecutionLevel admin\n");
    NRAW(L"VIProductVersion \"" VERSION_W L".0.0\"\n");
    NRAW(L"VIAddVersionKey \"ProductName\" \"ETC WindowBuilder\"\n");
    NRAW(L"VIAddVersionKey \"CompanyName\" \"" DEVELOPER_W L"\"\n");
    NRAW(L"VIAddVersionKey \"LegalCopyright\" \"" COPYRIGHT_W L"\"\n");
    NRAW(L"VIAddVersionKey \"FileDescription\" \"ETC WindowBuilder 专业版 安装程序\"\n");
    NRAW(L"VIAddVersionKey \"FileVersion\" \"" VERSION_W L"\"\n");
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
    NRAW(L"  CreateDirectory \"$SMPROGRAMS\\ETC Studio\"\n");
    NADD(L"  CreateShortCut \"$SMPROGRAMS\\ETC Studio\\ETC WindowBuilder.lnk\" \"$INSTDIR\\%ls\"\n", exe_name);
    NADD(L"  CreateShortCut \"$DESKTOP\\ETC WindowBuilder.lnk\" \"$INSTDIR\\%ls\"\n", exe_name);
    NRAW(L"  WriteUninstaller \"$INSTDIR\\Uninstall.exe\"\n");
    NRAW(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"DisplayName\" \"ETC WindowBuilder 专业版\"\n");
    NRAW(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"UninstallString\" \"\\\"$INSTDIR\\Uninstall.exe\\\"\"\n");
    NADD(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"DisplayIcon\" \"$INSTDIR\\%ls\"\n", exe_name);
    NRAW(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"DisplayVersion\" \"12.1\"\n");
    NRAW(L"  WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETCWindowBuilder\" \"Publisher\" \"ETC\"\n");
    NRAW(L"SectionEnd\n");
    NRAW(L"Section \"卸载\" SEC02\n");
    NADD(L"  Delete \"$INSTDIR\\%ls\"\n", exe_name);
    NRAW(L"  Delete \"$INSTDIR\\LICENSE.txt\"\n");
    NRAW(L"  RMDir /r \"$INSTDIR\\docs\"\n");
    NRAW(L"  Delete \"$INSTDIR\\Uninstall.exe\"\n");
    NRAW(L"  RMDir \"$INSTDIR\"\n");
    NRAW(L"  Delete \"$DESKTOP\\ETC WindowBuilder.lnk\"\n");
    NRAW(L"  Delete \"$SMPROGRAMS\\ETC Studio\\ETC WindowBuilder.lnk\"\n");
    NRAW(L"  RMDir \"$SMPROGRAMS\\ETC Studio\"\n");
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
                wchar_t src[MAX_PATH], exe_name[MAX_PATH], exe_tmp[MAX_PATH], setup_name[MAX_PATH], err[4096];
                size_t len; char *code = etb_gen_c(&g_app.proj, &len);
                wchar_t app_name[160];
                int exe_ok = 0;
                swprintf(app_name, 160, L"%ls.exe", g_app.pkg_app[0]?g_app.pkg_app:L"ETCWindowBuilderApp");
                swprintf(src, MAX_PATH, L"%ls\\main.c", build);
                swprintf(exe_tmp, MAX_PATH, L"%ls\\%ls", build, app_name);     /* 先在临时目录编译 */
                swprintf(exe_name, MAX_PATH, L"%ls\\%ls", dir, app_name);     /* 最终目标 */
                swprintf(setup_name, MAX_PATH, L"%ls\\ETCWindowBuilder-Setup-v%ls.exe", dir, g_app.pkg_ver[0]?g_app.pkg_ver:VERSION_W);
                {
                    HANDLE h = CreateFileW(src, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                    DWORD w;
                    if (h!=INVALID_HANDLE_VALUE){ WriteFile(h, code, (DWORD)len, &w, NULL); CloseHandle(h); }
                }
                free(code);
                etb_log(L"编译EXE: %ls", app_name);
                if (!etb_compile_c_source(src, exe_tmp, g_app.pkg_icon[0]?g_app.pkg_icon:NULL, err, 4096)){
                    etb_log(L"打包失败: %ls", err);
                    MessageBoxW(g_app.hMain, err, L"打包失败", MB_OK|MB_ICONERROR);
                    return;
                }
                /* 部署到用户目录: 旧文件可能正被占用(Permission denied),
                   先尝试删除旧文件再复制; 仍失败则改用带时间戳的新文件名。 */
                if (PathFileExistsW(exe_name)) DeleteFileW(exe_name);
                if (CopyFileW(exe_tmp, exe_name, FALSE)){
                    exe_ok = 1;
                    etb_log(L"EXE生成成功: %ls", exe_name);
                } else {
                    wchar_t alt[MAX_PATH];
                    swprintf(alt, MAX_PATH, L"%ls\\%ls_%lu.exe", dir,
                             g_app.pkg_app[0]?g_app.pkg_app:L"ETCWindowBuilderApp", GetTickCount());
                    if (CopyFileW(exe_tmp, alt, FALSE)){
                        wcscpy(exe_name, alt); exe_ok = 1;
                        etb_log(L"目标文件被占用, 已另存为: %ls", alt);
                        MessageBoxW(g_app.hMain,
                            L"原来的 EXE 正在运行或被占用, 无法覆盖。\n已改用新文件名保存(见日志), 请关闭旧程序后重试以使用原文件名。",
                            L"文件被占用", MB_OK|MB_ICONWARNING);
                    } else {
                        etb_log(L"无法写入目标目录, EXE 保留在临时构建目录");
                        MessageBoxW(g_app.hMain,
                            L"无法写入所选目录(权限不足或文件被占用)。\nEXE 已在临时构建目录生成, 请更换输出目录(如文档/桌面)后重试。",
                            L"打包失败", MB_OK|MB_ICONERROR);
                    }
                }
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

/* ================= 帮助: 检查更新 ================= */
static void ver_parts(const char *s, int *maj, int *minv){
    *maj=0;*minv=0;
    while (*s && (*s<'0'||*s>'9')) s++;   /* 跳过 v 等前缀 */
    while (*s>='0'&&*s<='9'){ *maj=*maj*10+(*s-'0'); s++; }
    if (*s=='.'){ s++; while (*s>='0'&&*s<='9'){ *minv=*minv*10+(*s-'0'); s++; } }
}
void etb_check_update(void){
    static const wchar_t *ua = L"ETC-WindowBuilder-Updater/12.0";
    static const wchar_t *accept = L"application/vnd.github+json";
    static const wchar_t *api = L"/repos/ETQWFD/ETC-WindowBuilder/releases/latest";
    HINTERNET hNet=NULL,hConn=NULL,hReq=NULL;
    char *body=NULL; DWORD total=0,cap=0;
    int ok=0; char tag[64]={0};
    etb_log(L"正在检查更新...");
    hNet = InternetOpenW(ua, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hNet) goto done;
    { DWORD to=8000; InternetSetOptionW(hNet, INTERNET_OPTION_CONNECT_TIMEOUT,&to,sizeof(to));
      InternetSetOptionW(hNet, INTERNET_OPTION_RECEIVE_TIMEOUT,&to,sizeof(to)); }
    hConn = InternetConnectW(hNet, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT,
                             NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) goto done;
    hReq = HttpOpenRequestW(hConn, L"GET", api, NULL, NULL, NULL,
                            INTERNET_FLAG_SECURE|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_RELOAD, 0);
    if (!hReq) goto done;
    HttpAddRequestHeadersW(hReq, accept, (DWORD)-1L, HTTP_ADDREQ_FLAG_ADD);
    if (!HttpSendRequestW(hReq, NULL, 0, NULL, 0)) goto done;
    { DWORD sc=0, lsz=sizeof(sc);
      HttpQueryInfoW(hReq, HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER, &sc, &lsz, NULL);
      if (sc!=200) goto done; }
    cap=8192; body=(char*)malloc(cap+1);
    for (;;){
        DWORD got=0;
        if (total+4096 > cap){ cap*=2; body=(char*)realloc(body,cap+1); }
        if (!InternetReadFile(hReq, body+total, 4096, &got) || got==0) break;
        total+=got;
    }
    if (body){ body[total]=0;
        const char *p=strstr(body,"\"tag_name\"");
        if (p){ p=strchr(p,':'); if(p){ p=strchr(p,'"'); if(p){ p++; int k=0; while(*p && *p!='"' && k<60) tag[k++]=*p++; tag[k]=0; ok=1; } } }
    }
done:
    if (hReq)InternetCloseHandle(hReq); if(hConn)InternetCloseHandle(hConn); if(hNet)InternetCloseHandle(hNet);
    if (ok){
        int lm,ln,rm,rn; ver_parts(VERSION_A,&lm,&ln); ver_parts(tag,&rm,&rn);
        if (rm>lm || (rm==lm&&rn>ln)){
            wchar_t msg[512];
            swprintf(msg,512,L"发现新版本: %hs\n当前版本: %hs\n\n是否前往 GitHub Releases 下载最新安装包?", tag, VERSION_A);
            if (MessageBoxW(g_app.hMain,msg,L"检查更新",MB_YESNO|MB_ICONINFORMATION)==IDYES)
                ShellExecuteW(NULL,L"open",L"https://github.com/ETQWFD/ETC-WindowBuilder/releases/latest",NULL,NULL,SW_SHOWNORMAL);
        } else {
            wchar_t msg[256]; swprintf(msg,256,L"当前已是最新版本。\n本地版本: %hs\n最新版本: %hs",VERSION_A,tag);
            MessageBoxW(g_app.hMain,msg,L"检查更新",MB_OK|MB_ICONINFORMATION);
        }
        etb_log(L"最新版本: %hs (本地 %hs)", tag, VERSION_A);
    } else {
        if (MessageBoxW(g_app.hMain,L"无法连接到更新服务器(GitHub)。\n是否手动打开下载页面?",L"检查更新失败",MB_YESNO|MB_ICONWARNING)==IDYES)
            ShellExecuteW(NULL,L"open",L"https://github.com/ETQWFD/ETC-WindowBuilder/releases",NULL,NULL,SW_SHOWNORMAL);
    }
    free(body);
}
