/* ============================================================
 * etb_blocks.c - 积木块定义与辅助
 * 版权: (c) ETC 2024-2026
 * ============================================================ */
#include "etb.h"

#define B_EVENT    0
#define B_ACTION   1
#define B_VAR      2
#define B_LOGIC    3
#define B_LOOP     4

const BlockDef g_blockdefs[] = {
    /* 事件 */
    {0,  L"当窗口启动时",        L"事件", RGB(255,152,0),   0, {0,0,0,0,0,0}, {0,0,0,0,0,0}},
    {1,  L"当 {} 被点击",        L"事件", RGB(255,152,0),   1, {L"控件名称",0,0,0,0,0}, {L"按钮1",0,0,0,0,0}},
    {2,  L"当窗口关闭时",        L"事件", RGB(255,152,0),   0, {0,0,0,0,0,0}, {0,0,0,0,0,0}},
    /* 动作 */
    {10, L"显示消息框 \"{}\"",   L"动作", RGB(33,150,243),  1, {L"文本",0,0,0,0,0}, {L"你好！",0,0,0,0,0}},
    {11, L"设置 {} 的文本为 \"{}\"", L"动作", RGB(33,150,243), 2, {L"控件名称",L"文本",0,0,0,0}, {L"标签1",L"新文本",0,0,0,0}},
    {12, L"打开文件 \"{}\"",     L"动作", RGB(33,150,243),  1, {L"路径",0,0,0,0,0}, {L"C:\\Windows\\notepad.exe",0,0,0,0,0}},
    {13, L"打开网址 \"{}\"",     L"动作", RGB(33,150,243),  1, {L"网址",0,0,0,0,0}, {L"https://github.com",0,0,0,0,0}},
    {14, L"退出程序",            L"动作", RGB(33,150,243),  0, {0,0,0,0,0,0}, {0,0,0,0,0,0}},
    {15, L"隐藏 {}",             L"动作", RGB(33,150,243),  1, {L"控件名称",0,0,0,0,0}, {L"标签1",0,0,0,0,0}},
    {16, L"显示 {}",             L"动作", RGB(33,150,243),  1, {L"控件名称",0,0,0,0,0}, {L"标签1",0,0,0,0,0}},
    {17, L"播放提示音",          L"动作", RGB(33,150,243),  0, {0,0,0,0,0,0}, {0,0,0,0,0,0}},
    {18, L"等待 {} 毫秒",        L"动作", RGB(33,150,243),  1, {L"毫秒",0,0,0,0,0}, {L"1000",0,0,0,0,0}},
    /* 变量 */
    {20, L"变量 {} 赋值为 \"{}\"", L"变量", RGB(156,39,176), 2, {L"变量名",L"值",0,0,0,0}, {L"count",L"0",0,0,0,0}},
    {21, L"变量 {} 增加 \"{}\"", L"变量", RGB(156,39,176), 2, {L"变量名",L"数值",0,0,0,0}, {L"count",L"1",0,0,0,0}},
    /* 逻辑 */
    {30, L"如果 变量 {} 等于 \"{}\" 那么", L"逻辑", RGB(76,175,80), 2, {L"变量名",L"值",0,0,0,0}, {L"count",L"0",0,0,0,0}},
    {31, L"结束如果",            L"逻辑", RGB(76,175,80),  0, {0,0,0,0,0,0}, {0,0,0,0,0,0}},
    /* 循环 */
    {40, L"重复 {} 次",          L"循环", RGB(233,30,99),  1, {L"次数",0,0,0,0,0}, {L"3",0,0,0,0,0}},
    {41, L"结束循环",            L"循环", RGB(233,30,99),  0, {0,0,0,0,0,0}, {0,0,0,0,0,0}},
};
const int g_nblockdefs = sizeof(g_blockdefs)/sizeof(g_blockdefs[0]);

int etb_block_find(int type){
    int i;
    for (i=0;i<g_nblockdefs;i++) if (g_blockdefs[i].id == type) return i;
    return -1;
}

void etb_block_defaults(Block *b, int type){
    int di = etb_block_find(type);
    int i;
    if (di < 0) return;
    for (i=0;i<MAX_PARAMS;i++){
        if (g_blockdefs[di].pdefault[i] && !b->params[i][0])
            wcscpy(b->params[i], g_blockdefs[di].pdefault[i]);
    }
}

int etb_block_has_end(int type){ return (type==31 || type==41); }
int etb_block_is_event(int type){ return (type==0 || type==1 || type==2); }

/* 将块渲染成可读文本 ({} 依次替换为参数) */
const wchar_t *etb_block_display(Block *b, wchar_t *buf, size_t cap){
    int di = etb_block_find(b->type);
    const wchar_t *tpl; size_t pos=0; int pi=0;
    if (di < 0){ wcscpy(buf, L"未知块"); return buf; }
    tpl = g_blockdefs[di].name;
    while (*tpl && pos+1 < cap){
        if (tpl[0]==L'{' && tpl[1]==L'}'){
            const wchar_t *pv = (pi<MAX_PARAMS)? b->params[pi] : L"";
            if (!pv) pv = L"";
            while (*pv && pos+1 < cap) buf[pos++] = *pv++;
            tpl += 2; pi++;
        } else buf[pos++] = *tpl++;
    }
    buf[pos] = 0;
    return buf;
}
