/* ============================================================
 * etb_main.c - 程序入口 / 主窗口 / 菜单 / 布局管理
 * 版权: (c) ETC 2024-2026
 * ============================================================ */
#include "etb_internal.h"

App g_app;

/* ---------- 菜单 ---------- */
static HMENU create_menu_bar(void){
    HMENU bar = CreateMenu();
    HMENU mf = CreatePopupMenu();
    AppendMenuW(mf, MF_STRING, IDM_NEW, L"新建项目");
    AppendMenuW(mf, MF_STRING, IDM_OPEN, L"打开项目 (.nep)...");
    AppendMenuW(mf, MF_STRING, IDM_SAVE, L"保存项目 (.nep)...");
    AppendMenuW(mf, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mf, MF_STRING, IDM_IMPORT, L"导入文件...");
    AppendMenuW(mf, MF_STRING, IDM_PACK, L"打包为EXE与安装包...");
    AppendMenuW(mf, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mf, MF_STRING, IDM_EXIT, L"退出");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mf, L"文件(&F)");

    HMENU mv = CreatePopupMenu();
    AppendMenuW(mv, MF_STRING, IDM_DESIGN, L"设计模式");
    AppendMenuW(mv, MF_STRING, IDM_BLOCK, L"积木模式");
    AppendMenuW(mv, MF_STRING, IDM_CC, L"C语言代码");
    AppendMenuW(mv, MF_STRING, IDM_PY, L"Python代码");
    AppendMenuW(mv, MF_STRING, IDM_FILE, L"文件模式");
    AppendMenuW(mv, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mv, MF_STRING, IDM_ZOOMIN, L"放大");
    AppendMenuW(mv, MF_STRING, IDM_ZOOMOUT, L"缩小");
    AppendMenuW(mv, MF_STRING, IDM_ZOOMRST, L"重置缩放");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mv, L"视图(&V)");

    HMENU ms = CreatePopupMenu();
    AppendMenuW(ms, MF_STRING, IDM_SETWIN, L"窗口设置...");
    AppendMenuW(ms, MF_STRING, IDM_PACKSET, L"打包设置...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)ms, L"设置(&S)");

    HMENU mh = CreatePopupMenu();
    AppendMenuW(mh, MF_STRING, IDM_HELP, L"使用说明");
    AppendMenuW(mh, MF_STRING, IDM_UPDATE, L"检查更新...");
    AppendMenuW(mh, MF_STRING, IDM_ABOUT, L"关于");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mh, L"帮助(&H)");
    return bar;
}

/* ---------- 布局 ---------- */
static void layout_children(void){
    RECT rc; int W, H;
    if (!g_app.hMain) return;
    GetClientRect(g_app.hMain, &rc);
    W = rc.right; H = rc.bottom;
    {
        const int LEFTW = 252, RIGHTW = 336, TOPH = 48;
        int cx = LEFTW, cy = TOPH, cw = W - LEFTW - RIGHTW, ch = H - TOPH;
        if (cw < 200) cw = 200;
        if (ch < 200) ch = 200;
        /* 中心 */
        MoveWindow(g_app.hCanvas, cx, cy, cw, ch, TRUE);
        MoveWindow(g_app.hBlocks, cx, cy, cw, ch, TRUE);
        MoveWindow(g_app.hCodeC, cx, cy, cw, ch, TRUE);
        MoveWindow(g_app.hCodePy, cx, cy, cw, ch, TRUE);
        /* 左侧 */
        MoveWindow(g_app.hToolbox, 0, TOPH, LEFTW, H-TOPH, TRUE);
        MoveWindow(g_app.hPalette, 0, TOPH, LEFTW, H-TOPH, TRUE);
        MoveWindow(g_app.hExplorer, 0, TOPH, LEFTW, H-TOPH, TRUE);
        /* 右侧 */
        {
            int rx = W - RIGHTW, rw = RIGHTW - 8;
            int ph = H - 380 - TOPH - 8;
            if (ph < 80) ph = 80;
            MoveWindow(g_app.hProps, rx, TOPH+2, rw, ph, TRUE);
            MoveWindow(g_app.hRightFilesLabel, rx+4, H-330, 120, 22, TRUE);
            MoveWindow(g_app.hRightFiles, rx+4, H-306, rw-8, 92, TRUE);
            MoveWindow(g_app.hRightImpBtn, rx+4, H-210, 150, 28, TRUE);
            MoveWindow(g_app.hRightDelBtn, rx+160, H-210, 150, 28, TRUE);
            MoveWindow(g_app.hLogLabel, rx+4, H-178, 120, 22, TRUE);
            MoveWindow(g_app.hLogClear, rx+rw-60, H-178, 56, 22, TRUE);
            MoveWindow(g_app.hLog, rx+4, H-154, rw-8, 146, TRUE);
        }
    }
}

/* ---------- 积木库选择 ---------- */
static void palette_add_selected(void){
    int sel = (int)SendMessageW(g_app.hPaletteList, LB_GETCURSEL, 0, 0);
    wchar_t item[320];
    if (sel < 0) return;
    SendMessageW(g_app.hPaletteList, LB_GETTEXT, sel, (LPARAM)item);
    if (wcsncmp(item, L"———", 3)==0) return;
    if (item[0]==L'#'){
        int type = _wtoi(item+1);
        if (etb_block_find(type) >= 0) etb_block_add(type);
    }
}

/* ---------- 主窗口过程 ---------- */
static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg){
    case WM_ERASEBKGND:{
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(C_BG);
        FillRect((HDC)wp, &rc, br); DeleteObject(br);
        return 1;
    }
    case WM_CTLCOLORSTATIC:{
        SetTextColor((HDC)wp, RGB(255,255,255));
        SetBkColor((HDC)wp, C_BG);
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
    case WM_SIZE:
        layout_children();
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)){
        /* 菜单-文件 */
        case IDM_NEW: etb_new_project(); return 0;
        case IDM_OPEN: etb_open_project(); return 0;
        case IDM_SAVE: etb_save_project(); return 0;
        case IDM_IMPORT: etb_import_file(); return 0;
        case IDM_PACK: case IDM_PACK2: etb_do_package(); return 0;
        case IDM_EXIT: DestroyWindow(hwnd); return 0;
        /* 模式 */
        case IDM_DESIGN: etb_switch_mode(MODE_DESIGN); return 0;
        case IDM_BLOCK: etb_switch_mode(MODE_BLOCK); return 0;
        case IDM_CC: etb_switch_mode(MODE_CODE_C); return 0;
        case IDM_PY: etb_switch_mode(MODE_CODE_PY); return 0;
        case IDM_FILE: etb_switch_mode(MODE_FILE); return 0;
        /* 缩放 */
        case IDM_ZOOMIN: etb_zoom_in(); return 0;
        case IDM_ZOOMOUT: etb_zoom_out(); return 0;
        case IDM_ZOOMRST: etb_zoom_reset(); return 0;
        /* 工具栏 */
        case IDM_RUN: etb_do_run_dispatch(); return 0;
        case IDM_SYNC: etb_sync_from_code(); return 0;
        case IDM_SETWIN: etb_show_window_settings(); return 0;
        case IDM_PACKSET: etb_show_pack_settings(); return 0;
        case IDM_BGCOLOR: etb_choose_bg_color(); return 0;
        case IDM_BGIMG: etb_import_bg_image(); return 0;
        case IDM_HELP: case IDM_BLOCKHELP: etb_show_help(); return 0;
        case IDM_UPDATE: etb_check_update(); return 0;
        case IDM_ABOUT: etb_show_about(); return 0;
        /* 代码编辑器手动修改 → 防抖反向同步到设计/积木 */
        case IDM_CODEC_EDIT:
            if (HIWORD(wp)==EN_CHANGE) etb_code_changed(0);
            return 0;
        case IDM_CODEPY_EDIT:
            if (HIWORD(wp)==EN_CHANGE) etb_code_changed(1);
            return 0;
        case 0x9999:
            if (g_app.hLog) SetWindowTextW(g_app.hLog, L"");
            return 0;
        /* 工具箱 */
        case IDM_TOOLBASE+8:
            etb_add_component(CT_BUTTON);
            if (g_app.proj.ncomps>0) wcscpy(g_app.proj.comps[g_app.proj.ncomps-1].shape, L"star");
            etb_refresh_canvas(); etb_refresh_codes();
            return 0;
        case IDM_TOOLBASE+9:
            etb_add_component(CT_BUTTON);
            if (g_app.proj.ncomps>0) wcscpy(g_app.proj.comps[g_app.proj.ncomps-1].shape, L"diamond");
            etb_refresh_canvas(); etb_refresh_codes();
            return 0;
        case IDM_TOOLBASE+10:
            etb_add_component(CT_BUTTON);
            if (g_app.proj.ncomps>0) wcscpy(g_app.proj.comps[g_app.proj.ncomps-1].shape, L"circle");
            etb_refresh_canvas(); etb_refresh_codes();
            return 0;
        default:
            if (LOWORD(wp)>=IDM_TOOLBASE && LOWORD(wp)<IDM_TOOLBASE+8){
                etb_add_component(LOWORD(wp)-IDM_TOOLBASE);
                return 0;
            }
            break;
        }
        /* 积木 */
        switch (LOWORD(wp)){
        case IDM_PALADD: palette_add_selected(); return 0;
        case IDM_PALDEL: etb_block_clear(); return 0;
        case IDM_PALUP: if (g_app.bsel>=0) etb_block_move(g_app.bsel, -1); return 0;
        case IDM_PALDOWN: if (g_app.bsel>=0) etb_block_move(g_app.bsel, 1); return 0;
        case IDM_PALEDIT: if (g_app.bsel>=0) etb_edit_block_dialog(g_app.bsel); return 0;
        case IDM_PALCLEAR: etb_block_clear(); return 0;
        case 2001:
            if (HIWORD(wp)==LBN_DBLCLK) palette_add_selected();
            return 0;
        case 2002:
            if (HIWORD(wp)==LBN_DBLCLK){
                int sel = (int)SendMessageW(g_app.hExplorerList, LB_GETCURSEL, 0, 0);
                wchar_t item[MAX_PATH];
                if (sel>=0){
                    SendMessageW(g_app.hExplorerList, LB_GETTEXT, sel, (LPARAM)item);
                    if (wcsncmp(item, L"[目录]", 5)==0) etb_explorer_enter(item+5);
                    else etb_explorer_open(item);
                }
            }
            return 0;
        case 2003:
            if (HIWORD(wp)==LBN_DBLCLK){
                int sel = (int)SendMessageW(g_app.hRightFiles, LB_GETCURSEL, 0, 0);
                if (sel>=0 && sel<g_app.proj.nfiles){
                    ShellExecuteW(NULL, L"open", g_app.proj.files[sel], NULL, NULL, SW_SHOWNORMAL);
                }
            }
            return 0;
        case IDM_EXP_UP: etb_explorer_up(); return 0;
        case IDM_EXP_REF: etb_explorer_refresh(); return 0;
        case IDM_EXP_IMP: etb_explorer_import(); return 0;
        case IDM_EXP_RUN: etb_explorer_compile_run(); return 0;
        case IDM_RIGHT_IMP: etb_import_file(); return 0;
        case IDM_RIGHT_DEL: etb_delete_file(); return 0;
        }
        return 0;
    case WM_TIMER:
        if (wp==IDT_CODEPARSE){ KillTimer(hwnd, IDT_CODEPARSE); etb_code_parse_now(); }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------- 注册窗口类 ---------- */
static void register_classes(HINSTANCE hInst){
    WNDCLASSEXW wc;
    HICON hIcoBig, hIcoSm;
    memset(&wc,0,sizeof(wc));
    wc.cbSize = sizeof(wc); wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    /* 修复左上角/任务栏图标: 从资源(IDI_APPICON=100)加载大、小两种尺寸 */
    hIcoBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(100), IMAGE_ICON,
                                GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    hIcoSm  = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(100), IMAGE_ICON,
                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (hIcoBig) wc.hIcon = hIcoBig;
    if (hIcoSm)  wc.hIconSm = hIcoSm;
    wc.lpfnWndProc = PanelProc;  wc.lpszClassName = L"ETBPanel";  RegisterClassExW(&wc);
    wc.lpfnWndProc = ModalProc;  wc.lpszClassName = L"ETBDialog"; RegisterClassExW(&wc);
    wc.lpfnWndProc = CanvasProc; wc.lpszClassName = L"ETBCanvas"; RegisterClassExW(&wc);
    wc.lpfnWndProc = BlocksProc; wc.lpszClassName = L"ETBBlocks"; RegisterClassExW(&wc);
    wc.lpfnWndProc = PropsProc;  wc.lpszClassName = L"ETBProps";  RegisterClassExW(&wc);
    wc.lpfnWndProc = MainProc;   wc.lpszClassName = L"ETCMain";   RegisterClassExW(&wc);
}

/* ---------- 入口 ---------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nCmdShow){
    GdiplusStartupInput gsi;
    (void)hPrev; (void)lpCmd;
    memset(&g_app, 0, sizeof(g_app));
    g_app.hInst = hInstance;
    g_app.zoom = 100;
    g_app.sel = -1;
    g_app.bsel = -1;
    /* 默认打包设置 */
    wcscpy(g_app.pkg_app, L"ETCWindowBuilderApp");
    wcscpy(g_app.pkg_dev, DEVELOPER_W);
    wcscpy(g_app.pkg_ver, VERSION_W);
    wcscpy(g_app.pkg_company, COMPANY_W);
    wcscpy(g_app.pkg_copy, COPYRIGHT_W);
    wcscpy(g_app.pkg_desc, L"由 ETC WindowBuilder 专业版生成");
    /* 默认项目 */
    wcscpy(g_app.proj.name, L"未命名项目");
    wcscpy(g_app.proj.title, L"我的应用程序");
    wcscpy(g_app.proj.size, L"800x600");
    wcscpy(g_app.proj.bg, L"#2b2b2b");
    GdiplusStartup(&g_app.gdiplusToken, &gsi, NULL);
    InitCommonControlsEx(&(INITCOMMONCONTROLSEX){sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES|ICC_STANDARD_CLASSES});
    register_classes(hInstance);
    g_app.hFontUI = CreateFontW(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    if (!g_app.hFontUI) g_app.hFontUI = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_app.hFontMono = CreateFontW(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
    if (!g_app.hFontMono) g_app.hFontMono = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    g_app.hFontBig = CreateFontW(-22,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    if (!g_app.hFontBig) g_app.hFontBig = g_app.hFontUI;

    g_app.hMain = CreateWindowExW(0, L"ETCMain", L"ETC WindowBuilder 专业版",
                                  WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1600, 900,
                                  NULL, NULL, hInstance, NULL);
    if (!g_app.hMain) return 0;
    SetWindowLongPtrW(g_app.hMain, GWLP_USERDATA, 0);
    {
        /* 显式设置标题栏(小)与任务栏/Alt-Tab(大)图标, 双保险 */
        HICON _hb = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(100), IMAGE_ICON,
                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        HICON _hs = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(100), IMAGE_ICON,
                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (_hb) SendMessageW(g_app.hMain, WM_SETICON, ICON_BIG, (LPARAM)_hb);
        if (_hs) SendMessageW(g_app.hMain, WM_SETICON, ICON_SMALL, (LPARAM)_hs);
    }
    SetMenu(g_app.hMain, create_menu_bar());
    etb_create_layout();
    ShowWindow(g_app.hMain, nCmdShow);
    UpdateWindow(g_app.hMain);
    layout_children();
    etb_log(L"ETC WindowBuilder 专业版 v%s 启动成功", VERSION_W);
    etb_log(L"开发者: %ls | 公司: %ls | 版权: %ls", DEVELOPER_W, COMPANY_W, COPYRIGHT_W);
    etb_log(L"项目文件格式: %ls (支持ET引擎加密)", FILE_EXT_W);
    etb_log(L"模式: 设计 / 积木 / C代码 / Python代码 / 文件");
    etb_log(L"提示: 积木模式下双击积木可编辑参数, 拖动可排序");

    {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0){
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    GdiplusShutdown(g_app.gdiplusToken);
    return 0;
}
