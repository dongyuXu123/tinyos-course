/* Lesson B17: 脚本 tokenizer 与环境变量（无硬件依赖，可主机单元测试）
 *
 * 本文件不含 VGA/端口等硬件依赖，因此既能链进 loader（freestanding），
 * 也能在主机上用 test_script.c 做单元测试。对照：
 *   grub-core/kern/env.c（环境变量读写）
 *   grub-core/script/tokenizer.c（引号、$var/${var} 展开、注释）
 * B17 简化边界：无 if/for/函数定义；token 缓冲固定 256 字节。
 */

typedef unsigned int u32;

/* ---- 环境变量（固定槽位链表，对照 kern/env.c）----------------------------- */
#define ENV_MAX   16
#define ENV_NAME  16
#define ENV_VALUE 48

struct env {
    char name[ENV_NAME];
    char value[ENV_VALUE];
    struct env *next;
};

static struct env env_slots[ENV_MAX];
static u32 env_count = 0;
static struct env *env_head = 0;

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static u32 str_len(const char *s)
{
    u32 n = 0;
    while (s[n])
        n++;
    return n;
}

static void str_copy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static struct env *env_find(const char *name)
{
    struct env *e;
    for (e = env_head; e; e = e->next)
        if (str_eq(e->name, name))
            return e;
    return 0;
}

const char *env_get(const char *name)
{
    struct env *e = env_find(name);
    return e ? e->value : 0;
}

/* env_get_len: 按名字+长度查找（tokenizer 展开用，避免复制名字） */
const char *env_get_len(const char *name, u32 len)
{
    struct env *e;
    for (e = env_head; e; e = e->next)
        if (str_len(e->name) == len) {
            u32 i;
            for (i = 0; i < len; i++)
                if (e->name[i] != name[i])
                    break;
            if (i == len)
                return e->value;
        }
    return 0;
}

int env_set(const char *name, const char *value)
{
    struct env *e = env_find(name);
    if (e) {
        str_copy(e->value, value, ENV_VALUE);
        return 0;
    }
    if (env_count >= ENV_MAX)
        return -1;
    e = &env_slots[env_count++];
    str_copy(e->name, name, ENV_NAME);
    str_copy(e->value, value, ENV_VALUE);
    e->next = env_head;
    env_head = e;
    return 0;
}

/* env_foreach: 遍历全部变量（"set" 命令列示用，回调不依赖硬件） */
void env_foreach(void (*fn)(const char *name, const char *value, void *data),
                 void *data)
{
    struct env *e;
    for (e = env_head; e; e = e->next)
        fn(e->name, e->value, data);
}

/* ---- 脚本 tokenizer -------------------------------------------------------- */
#define TOK_MAX 256

static char tok_buf[TOK_MAX];

static int is_ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* script_tokenize: 展开 $var/${var}、去引号、按空白切词。
 * 输出 token 写入内部 tok_buf，argv 指向各 token。返回 argc。
 * 对照 GRUB script/tokenizer：引号内空白保留为同一 token；'#' 起为注释；
 * 未定义变量展开为空串。 */
int script_tokenize(const char *line, char **argv, int max)
{
    int argc = 0;
    const char *p = line;
    char *out = tok_buf;
    char *out_end = tok_buf + TOK_MAX - 1;

    while (*p) {
        int in_quote = 0;

        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '#')
            break;              /* 空行或注释行 */
        if (argc >= max)
            break;

        argv[argc++] = out;
        for (;;) {
            char c = *p;
            if (c == 0)
                break;
            if (c == '"') {             /* 引号：不写入，仅切换状态 */
                in_quote = !in_quote;
                p++;
                continue;
            }
            if (c == '#' && !in_quote) { /* 注释：跳到行尾 */
                while (*p)
                    p++;
                break;
            }
            if (!in_quote && (c == ' ' || c == '\t'))
                break;                  /* 词结束 */
            if (c == '$') {
                /* 变量展开（GRUB 语义：引号内也展开，与 shell 不同——
                 * 对照 script/tokenizer.c 的 GRUB_TOKEN_VARIABLE） */
                const char *name, *v;
                u32 nlen;
                if (p[1] == '{') {      /* ${name} */
                    const char *e = p + 2;
                    while (*e && *e != '}')
                        e++;
                    name = p + 2;
                    nlen = (u32)(e - name);
                    p = (*e == '}') ? e + 1 : e;
                } else {                /* $name */
                    const char *e = p + 1;
                    while (is_ident_char(*e))
                        e++;
                    name = p + 1;
                    nlen = (u32)(e - name);
                    p = e;
                }
                v = env_get_len(name, nlen);
                if (v)
                    while (*v && out < out_end)
                        *out++ = *v++;
                continue;
            }
            if (out < out_end)
                *out++ = c;
            p++;
        }
        *out++ = 0;
    }
    return argc;
}
