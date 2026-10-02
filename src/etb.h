/* ============================================================
 * ET窗口构建器 专业版 (ETC WindowBuilder Professional)
 * C语言 / Win32 / GDI+ 实现
 * 开发者: ET        公司: ET Studio
 * 版权: (c) ET 2024-2026  版本: 12.0
 * ============================================================ */
#ifndef ETB_H
#define ETB_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define VERSION_W       L"12.0"
#define VERSION_A       "12.0"
#define APP_NAME_W      L"ETC WindowBuilder"
#define APP_TITLE_W     L"ET窗口构建器 专业版"
#define DEVELOPER_W     L"ET"
#define TESTERS_W       L"小态度轩, 威龙"
#define COMPANY_W       L"ET Studio"
#define COPYRIGHT_W     L"(c) ET 2024-2026"
#define FILE_EXT_W      L".nep"
#define FILE_EXT_A      ".nep"

#define MAX_COMPS   256
#define MAX_BLOCKS  512
#define MAX_FILES   128
#define MAX_PARAMS  6

/* ---------- 组件类型 ---------- */
enum { CT_BUTTON=0, CT_LABEL, CT_ENTRY, CT_CHECK, CT_RADIO, CT_COMBO, CT_PROGRESS, CT_IMAGE };

typedef struct {
    int   id;
    int   type;
    wchar_t text[256];
    int   x, y, w, h;
    wchar_t bg[32];
    wchar_t fg[32];
    int   font_size;
    wchar_t shape[16];        /* rectangle / star / diamond / circle */
    wchar_t values[512];      /* combobox 选项 */
    wchar_t image_path[MAX_PATH];
    wchar_t file_path[MAX_PATH];
    int   visible;
} Component;

typedef struct {
    int   type;               /* 块定义ID */
    wchar_t params[MAX_PARAMS][256];
} Block;

typedef struct {
    wchar_t name[128];
    wchar_t title[128];
    wchar_t size[64];
    wchar_t bg[32];
    wchar_t bg_image[MAX_PATH];
    Component comps[MAX_COMPS];
    int   ncomps;
    int   next_id;
    Block blocks[MAX_BLOCKS];
    int   nblocks;
    wchar_t files[MAX_FILES][MAX_PATH];
    int   nfiles;
} Project;

/* ---------- 积木块定义 ---------- */
typedef struct {
    int   id;
    const wchar_t *name;
    const wchar_t *cat;
    COLORREF color;
    int   nparams;
    const wchar_t *pname[MAX_PARAMS];
    const wchar_t *pdefault[MAX_PARAMS];
} BlockDef;

extern const BlockDef g_blockdefs[];
extern const int g_nblockdefs;

/* ---------- 应用全局状态 ---------- */
enum { MODE_DESIGN=0, MODE_BLOCK, MODE_CODE_C, MODE_CODE_PY, MODE_FILE };

typedef struct {
    HINSTANCE hInst;
    HWND hMain, hCanvas, hBlocks, hProps, hCodeC, hCodePy, hLog;
    HWND hBtnDesign,hBtnBlock,hBtnC,hBtnPy,hBtnFile;
    HWND hToolbox, hPalette, hPaletteList, hExplorer, hExplorerList, hExplorerPath, hRightFiles;
    HWND hRightFilesLabel, hLogLabel, hLogClear, hRightImpBtn, hRightDelBtn;
    HWND hZoomLabel, hTitleLabel, hTitleLabel2, hStatusLabel;
    HWND hCompList;          /* 右侧组件树(可选) */
    Project proj;
    int  mode;
    int  syncing;            /* 程序化刷新代码时置1, 屏蔽 EN_CHANGE 回环 */
    int  parse_which;        /* 待解析的代码页 0=C 1=Py */
    int  sel;                /* 选中的组件ID 或 -1 */
    int  zoom;               /* 百分比 50..200 */
    int  dragging;
    POINT dragStart;
    int  dragComp;
    wchar_t curPath[MAX_PATH];
    int  bsel;               /* 积木脚本中选中的块索引 */
    int  bsel2;              /* 拖动排序辅助 */
    wchar_t logbuf[4096*4];
    int  loglen;
    HFONT hFontUI, hFontMono, hFontBig;
    /* 打包设置 */
    wchar_t pkg_app[128], pkg_dev[64], pkg_ver[32], pkg_company[64], pkg_copy[64], pkg_desc[256], pkg_icon[MAX_PATH];
    int  pkg_mode;           /* 0 onefile 1 onedir */
    int  pkg_console;
    /* GDI+ */
    ULONG_PTR gdiplusToken;
} App;

extern App g_app;

/* ---------- 工具函数 (etb_util) ---------- */
void etb_log(const wchar_t *fmt, ...);
void etb_loga(const char *fmt, ...);
const wchar_t *comp_type_name(int type);
void comp_default_name(Component *c);
int  comp_index_by_id(int id);
void etb_zoom_apply(void);

/* ---------- 加密 (etb_crypto) ---------- */
int etb_encrypt_mem(const void *data, size_t len, const wchar_t *pw, unsigned char **out, size_t *outlen);
int etb_decrypt_mem(const unsigned char *data, size_t len, const wchar_t *pw, unsigned char **out, size_t *outlen);

/* ---------- 保存/加载 (etb_save) ---------- */
int etb_project_serialize(Project *p, char **out, size_t *outlen);
int etb_project_deserialize(const char *txt, size_t len, Project *p);
int etb_load_project(const wchar_t *path, const wchar_t *pw);
int etb_save_project_file(const wchar_t *path, const wchar_t *pw);
void etb_save_project(void);

/* ---------- 代码生成 (etb_gen) ---------- */
char *etb_gen_c(Project *p, size_t *len);
char *etb_gen_py(Project *p, size_t *len);

/* ---------- 积木 (etb_blocks) ---------- */
int  etb_block_find(int type);
void etb_block_defaults(Block *b, int type);
int  etb_block_has_end(int type);          /* 是否是结束块 */
int  etb_block_is_event(int type);         /* 是否是事件块 */
const wchar_t *etb_block_display(Block *b, wchar_t *buf, size_t cap);
void etb_gen_blocks_c(Project *p, wchar_t *buf, size_t cap);  /* 生成块逻辑的C代码段 */
void etb_gen_blocks_py(Project *p, wchar_t *buf, size_t cap); /* 生成块逻辑的Python代码段 */

/* ---------- UI (etb_ui) ---------- */
LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK BlocksProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK PropsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void etb_create_layout(void);
void etb_switch_mode(int mode);
void etb_refresh_canvas(void);
void etb_refresh_blocks(void);
void etb_refresh_props(void);
void etb_refresh_codes(void);
void etb_refresh_files(void);
void etb_update_title(void);
void etb_do_compile_run(void);
void etb_do_run_python(void);
void etb_do_run_dispatch(void);
void etb_explorer_compile_run(void);
void etb_do_package(void);
void etb_choose_bg_color(void);
void etb_import_bg_image(void);
void etb_sync_from_code(void);
char *etb_current_code_utf8(int which, size_t *len); /* 0=C 1=Py: 优先取编辑器文本 */
void etb_code_changed(int which);                     /* 代码编辑器 EN_CHANGE (防抖解析) */
void etb_code_parse_now(void);                        /* 把当前代码反向同步到设计/积木 */
void etb_check_update(void);                          /* 帮助: 检查更新 */
void etb_show_window_settings(void);
void etb_show_pack_settings(void);
void etb_show_about(void);
void etb_show_help(void);
void etb_show_blocks_help(void);
void etb_new_project(void);
void etb_open_project(void);
void etb_import_file(void);
void etb_delete_file(void);
void etb_select_component(int id);


/* 从 GDI+ 加载图片绘制 */
void etb_paint_canvas(HDC hdc, RECT *rc);

#endif /* ETB_H */
