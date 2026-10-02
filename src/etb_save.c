/* ============================================================
 * etb_save.c - .nep 项目文件序列化 / 反序列化 / 加解密保存
 * 版权: (c) ET 2024-2026
 * ============================================================ */
#include "etb.h"

/* ------- 简易转义: '|' -> "\p" , '\' -> "\\" ------- */
static void esc_append(wchar_t *dst, size_t cap, size_t *pos, const wchar_t *s){
    while (*s){
        if (*s == L'|'){ if (*pos+1 < cap) dst[(*pos)++] = L'\\'; if (*pos < cap) dst[(*pos)++] = L'p'; }
        else if (*s == L'\\'){ if (*pos+1 < cap) dst[(*pos)++] = L'\\'; if (*pos < cap) dst[(*pos)++] = L'\\'; }
        else { if (*pos < cap) dst[(*pos)++] = *s; }
        s++;
    }
}
static void unesc(wchar_t *dst, const wchar_t *s){
    while (*s){
        if (*s == L'\\' && s[1]){
            if (s[1] == L'p') *dst++ = L'|';
            else if (s[1] == L'\\') *dst++ = L'\\';
            else { *dst++ = *s; *dst++ = s[1]; }
            s += 2;
        } else *dst++ = *s++;
    }
    *dst = 0;
}

/* 把一行wchar文本追加到UTF-8输出缓冲 */
static void line_out(char **buf, size_t *len, size_t *cap, const wchar_t *line){
    char utf[4096]; int n;
    n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf, sizeof(utf), NULL, NULL);
    if (n <= 0) return;
    if (*len + n + 1 > *cap){ *cap = (*len + n + 1) * 2 + 64; *buf = realloc(*buf, *cap); }
    memcpy(*buf + *len, utf, n); *len += n;   /* 包含结尾\0, 下一段直接覆盖 */
}

int etb_project_serialize(Project *p, char **out, size_t *outlen){
    char *buf = NULL; size_t len = 0, cap = 0; int i, j;
    wchar_t line[4096];

    line_out(&buf,&len,&cap, L"ETNEP|11.0");
    swprintf(line, 4096, L"NAME|%ls", p->name); line_out(&buf,&len,&cap, line);
    swprintf(line, 4096, L"TITLE|%ls", p->title); line_out(&buf,&len,&cap, line);
    swprintf(line, 4096, L"SIZE|%ls", p->size); line_out(&buf,&len,&cap, line);
    swprintf(line, 4096, L"BG|%ls", p->bg); line_out(&buf,&len,&cap, line);
    swprintf(line, 4096, L"BGIMG|%ls", p->bg_image); line_out(&buf,&len,&cap, line);
    swprintf(line, 4096, L"NEXTID|%d", p->next_id); line_out(&buf,&len,&cap, line);

    for (i=0;i<p->ncomps;i++){
        Component *c = &p->comps[i];
        wchar_t e1[512], e2[512], e3[2048], e4[MAX_PATH*2], e5[MAX_PATH*2];
        size_t pos=0; wchar_t tmp[8192];
        memset(e1,0,sizeof(e1)); esc_append(e1,512,&pos,c->text); e1[511]=0;
        pos=0; memset(e2,0,sizeof(e2)); esc_append(e2,512,&pos,c->values); e2[511]=0;
        pos=0; memset(e3,0,sizeof(e3)); esc_append(e3,2048,&pos,c->image_path); e3[2047]=0;
        pos=0; memset(e4,0,sizeof(e4)); esc_append(e4,MAX_PATH*2,&pos,c->file_path); e4[MAX_PATH*2-1]=0;
        pos=0; memset(e5,0,sizeof(e5)); esc_append(e5,512,&pos,c->shape); e5[511]=0;
        swprintf(tmp, 8192, L"COMP|%d|%d|%ls|%d|%d|%d|%d|%ls|%ls|%d|%ls|%ls|%ls|%ls|%d",
                 c->id, c->type, e1, c->x, c->y, c->w, c->h,
                 c->bg, c->fg, c->font_size, e5, e2, e3, e4, c->visible);
        line_out(&buf,&len,&cap, tmp);
    }
    for (i=0;i<p->nblocks;i++){
        Block *b = &p->blocks[i];
        wchar_t tmp[2048]; size_t pos=0;
        swprintf(tmp, 2048, L"BLOCK|%d", b->type);
        line_out(&buf,&len,&cap, tmp);
        for (j=0;j<MAX_PARAMS;j++){
            if (b->params[j][0]){
                wchar_t ep[512]; pos=0; memset(ep,0,sizeof(ep));
                esc_append(ep,512,&pos,b->params[j]);
                swprintf(tmp, 2048, L"|%ls", ep);
                line_out(&buf,&len,&cap, tmp);
            }
        }
    }
    for (i=0;i<p->nfiles;i++){
        wchar_t tmp[MAX_PATH*2]; size_t pos=0; wchar_t ef[MAX_PATH*2];
        memset(ef,0,sizeof(ef)); esc_append(ef,MAX_PATH*2,&pos,p->files[i]);
        swprintf(tmp, MAX_PATH*2, L"FILE|%ls", ef);
        line_out(&buf,&len,&cap, tmp);
    }
    if (!buf){ buf = strdup(""); len = 1; }
    *out = buf; *outlen = len;
    return 1;
}

static wchar_t *utf8_to_w(const char *s){
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t*)malloc((n+1)*sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void parse_field(const wchar_t *src, wchar_t *dst, size_t cap){
    wchar_t tmp[4096]; size_t i;
    for (i=0; src[i] && src[i]!=L'|' && i+1<sizeof(tmp)/sizeof(wchar_t); i++) tmp[i]=src[i];
    tmp[i]=0;
    unesc(dst, tmp);
    (void)cap;
}

int etb_project_deserialize(const char *txt, size_t len, Project *p){
    char *copy = (char*)malloc(len+1); char *line, *save;
    wchar_t *wline;
    memset(p, 0, sizeof(Project));
    wcscpy(p->name, L"未命名项目");
    wcscpy(p->title, L"我的应用程序");
    wcscpy(p->size, L"800x600");
    wcscpy(p->bg, L"#2b2b2b");
    p->next_id = 0;
    if (!copy) return 0;
    memcpy(copy, txt, len); copy[len]=0;

    for (line = strtok_s(copy, "\r\n", &save); line; line = strtok_s(NULL, "\r\n", &save)){
        if (!*line || *line=='\n' || *line=='\r') continue;
        wline = utf8_to_w(line);
        if (!wline) continue;
        if (wcsncmp(wline, L"NAME|", 5)==0) parse_field(wline+5, p->name, 128);
        else if (wcsncmp(wline, L"TITLE|", 6)==0) parse_field(wline+6, p->title, 128);
        else if (wcsncmp(wline, L"SIZE|", 5)==0) parse_field(wline+5, p->size, 64);
        else if (wcsncmp(wline, L"BG|", 3)==0) parse_field(wline+3, p->bg, 32);
        else if (wcsncmp(wline, L"BGIMG|", 6)==0) parse_field(wline+6, p->bg_image, MAX_PATH);
        else if (wcsncmp(wline, L"NEXTID|", 7)==0) p->next_id = _wtoi(wline+7);
        else if (wcsncmp(wline, L"COMP|", 5)==0 && p->ncomps < MAX_COMPS){
            const wchar_t *t = wline+5; Component *c = &p->comps[p->ncomps];
            int i; const wchar_t *fields[15];
            memset(c, 0, sizeof(Component));
            for (i=0;i<15 && t;i++){
                const wchar_t *e = wcschr(t, L'|');
                if (e){ fields[i] = t; t = e+1; } else { fields[i]=t; t=NULL; }
            }
            if (i>=9){
                c->id = _wtoi(fields[0]);
                c->type = _wtoi(fields[1]);
                wchar_t tmp[256]; unesc(tmp, fields[2]); wcscpy(c->text, tmp);
                c->x=_wtoi(fields[3]); c->y=_wtoi(fields[4]); c->w=_wtoi(fields[5]); c->h=_wtoi(fields[6]);
                unesc(tmp, fields[7]); wcscpy(c->bg, tmp);
                unesc(tmp, fields[8]); wcscpy(c->fg, tmp);
                c->font_size = _wtoi(fields[9]);
                if (i>=11){ unesc(tmp, fields[10]); wcscpy(c->shape, tmp); }
                if (i>=12){ unesc(tmp, fields[11]); wcscpy(c->values, tmp); }
                if (i>=13){ unesc(tmp, fields[12]); wcscpy(c->image_path, tmp); }
                if (i>=14){ unesc(tmp, fields[13]); wcscpy(c->file_path, tmp); c->visible = _wtoi(fields[14]); }
                else c->visible = 1;
                if (c->text[0]==0) comp_default_name(c);
                p->ncomps++;
            }
        }
        else if (wcsncmp(wline, L"BLOCK|", 6)==0 && p->nblocks < MAX_BLOCKS){
            Block *b = &p->blocks[p->nblocks];
            const wchar_t *t = wline+6; int i;
            memset(b, 0, sizeof(Block));
            b->type = _wtoi(t);
            t = wcschr(t, L'|');
            for (i=0;i<MAX_PARAMS && t;i++){
                t++;
                const wchar_t *e = wcschr(t, L'|');
                wchar_t tmp[256];
                if (e){ int n = (int)(e-t); wcsncpy(tmp, t, n); tmp[n]=0; t=e; }
                else { wcscpy(tmp, t); t=NULL; }
                unesc(b->params[i], tmp);
            }
            etb_block_defaults(b, b->type);
            p->nblocks++;
        }
        else if (wcsncmp(wline, L"FILE|", 5)==0 && p->nfiles < MAX_FILES){
            parse_field(wline+5, p->files[p->nfiles], MAX_PATH);
            p->nfiles++;
        }
        free(wline);
    }
    free(copy);
    return 1;
}

int etb_save_project_file(const wchar_t *path, const wchar_t *pw){
    char *txt=NULL; size_t tlen=0;
    etb_project_serialize(&g_app.proj, &txt, &tlen);
    if (!txt) return 0;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE){ free(txt); return 0; }
    DWORD written = 0;
    if (pw && pw[0]){
        unsigned char *enc=NULL; size_t elen=0;
        if (!etb_encrypt_mem(txt, tlen, pw, &enc, &elen)){ CloseHandle(h); free(txt); return 0; }
        WriteFile(h, enc, (DWORD)elen, &written, NULL);
        free(enc);
    } else {
        WriteFile(h, txt, (DWORD)tlen, &written, NULL);
    }
    CloseHandle(h);
    free(txt);
    return 1;
}

/* 返回: 1成功 0文件错误 2密码错误 */
int etb_load_project(const wchar_t *path, const wchar_t *pw){
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, read=0;
    unsigned char *data;
    int r = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(h, NULL);
    if (size <= 0 || size > 64*1024*1024){ CloseHandle(h); return 0; }
    data = (unsigned char*)malloc(size+1);
    ReadFile(h, data, size, &read, NULL);
    CloseHandle(h);
    if (read != size){ free(data); return 0; }
    data[size] = 0;

    if (size >= 6 && memcmp(data, "ETNEP1", 6) == 0){
        /* 加密文件 */
        unsigned char *plain=NULL; size_t plen=0;
        if (!pw) pw = L"";
        if (!etb_decrypt_mem(data, size, pw, &plain, &plen)){ free(data); r = 2; }
        else {
            if (etb_project_deserialize((const char*)plain, plen, &g_app.proj)) r = 1;
            free(plain);
        }
    } else {
        if (etb_project_deserialize((const char*)data, size, &g_app.proj)) r = 1;
    }
    free(data);
    return r;
}
