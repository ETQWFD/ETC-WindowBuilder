/* etb_internal.h - 内部共享声明 (etb_ui.c / etb_ui2.c) */
#ifndef ETB_INTERNAL_H
#define ETB_INTERNAL_H
#include "etb.h"

#define C_BG       RGB(26,26,26)
#define C_PANEL    RGB(37,37,37)
#define C_ACCENT   RGB(0,120,212)
#define C_HOVER    RGB(0,90,158)
#define C_BORDER   RGB(60,60,60)
#define C_EDITBG   RGB(43,43,43)
#define C_OK       RGB(76,175,80)
#define C_WARN     RGB(255,152,0)
#define C_ERR      RGB(244,67,54)

/* 控件ID */
#define IDM_NEW      4001
#define IDM_OPEN     4002
#define IDM_SAVE     4003
#define IDM_IMPORT   4004
#define IDM_PACK     4005
#define IDM_EXIT     4006
#define IDM_DESIGN   4100
#define IDM_BLOCK    4101
#define IDM_CC       4102
#define IDM_PY       4103
#define IDM_FILE     4104
#define IDM_ZOOMIN   4200
#define IDM_ZOOMOUT  4201
#define IDM_ZOOMRST  4202
#define IDM_RUN      4300
#define IDM_PACK2    4301
#define IDM_SETWIN   4302
#define IDM_PACKSET  4303
#define IDM_BGCOLOR  4304
#define IDM_BGIMG    4305
#define IDM_SYNC     4306
#define IDM_UPDATE   4307
#define IDM_ABOUT    4400
#define IDM_HELP     4401
#define IDM_BLOCKHELP 4402
#define IDM_CODEC_EDIT 7001
#define IDM_CODEPY_EDIT 7002
#define IDT_CODEPARSE  5501
#define IDM_TOOLBASE 5000
#define IDM_PALADD   5100
#define IDM_PALDEL   5101
#define IDM_PALUP    5102
#define IDM_PALDOWN  5103
#define IDM_PALEDIT  5104
#define IDM_PALCLEAR 5105
#define IDM_EXP_UP   5200
#define IDM_EXP_REF  5201
#define IDM_EXP_IMP  5202
#define IDM_RIGHT_IMP 5203
#define IDM_RIGHT_DEL 5204
#define IDM_EXP_RUN  5205
#define IDM_PROP_BASE 6000   /* 文本行 */
#define IDM_PROP_COLOR 6100  /* 颜色按钮 */
#define IDM_PROP_SHAPE 6150
#define IDM_PROP_IMG  6200
#define IDM_PROP_BIND 6210
#define IDM_PROP_DEL  6220
#define IDM_PROP_DUP  6221

/* 通用小对话框 */
int etb_prompt(const wchar_t *title, const wchar_t *label, wchar_t *out, int outlen, int password);
void etb_edit_block_dialog(int idx);

/* 编译/打包 */
int etb_compile_c_source(const wchar_t *src_utf8_path, const wchar_t *out_exe, const wchar_t *icon_path, wchar_t *err, size_t errcap);
void etb_run_compiled(const wchar_t *exe_path);
int etb_find_tool(const wchar_t *name, wchar_t *out, size_t cap);

/* 图片缓存 */
HBITMAP etb_load_image(const wchar_t *path, int *w, int *h);

/* 块操作 */
void etb_block_add(int type);
void etb_block_delete(int idx);
void etb_block_move(int idx, int delta);
void etb_block_clear(void);

/* 组件操作 */
void etb_add_component(int type);
void etb_delete_selected(void);
void etb_duplicate_selected(void);

/* 文件浏览器 */
void etb_explorer_refresh(void);
void etb_explorer_up(void);
void etb_explorer_import(void);
void etb_explorer_enter(const wchar_t *name);
void etb_explorer_open(const wchar_t *name);

/* 通用控件 */
LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK ModalProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void etb_draw_owndraw(LPDRAWITEMSTRUCT dis);
HWND etb_make_btn(HWND parent, int id, const wchar_t *text, int x, int y, int w, int h, COLORREF bg);
void etb_zoom_in(void);
void etb_zoom_out(void);
void etb_zoom_reset(void);

#endif
