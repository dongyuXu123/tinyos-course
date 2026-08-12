/* Lesson B17: tokenizer 主机单元测试
 *
 * 在主机上编译 script.c + 本文件，验证 tokenizer 的引号 / $var / ${var} /
 * 注释 / 未定义变量 / 块括号等行为（freestanding 目标上无法单测的部分）。
 * 编译：gcc -o test_script test_script.c script.c
 */

#include <stdio.h>
#include <string.h>

int script_tokenize(const char *line, char **argv, int max);
int env_set(const char *name, const char *value);
const char *env_get(const char *name);

static int fails = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL line %d: %s\n", __LINE__, #cond); \
        fails++; \
    } \
} while (0)

static void dump(int argc, char **argv)
{
    int i;
    printf("  argc=%d", argc);
    for (i = 0; i < argc; i++)
        printf(" [%s]", argv[i]);
    printf("\n");
}

int main(void)
{
    char *argv[8];
    int argc;

    env_set("root", "(cd0)");
    env_set("timeout", "0");

    /* 用例 1：普通命令 + 引号（引号内空格保留为一个 token） */
    argc = script_tokenize("echo \"hello world\"", argv, 8);
    dump(argc, argv);
    CHECK(argc == 2);
    CHECK(strcmp(argv[0], "echo") == 0);
    CHECK(strcmp(argv[1], "hello world") == 0);

    /* 用例 2：$var 展开 */
    argc = script_tokenize("echo root is $root", argv, 8);
    dump(argc, argv);
    CHECK(argc == 4);
    CHECK(strcmp(argv[0], "echo") == 0);
    CHECK(strcmp(argv[3], "(cd0)") == 0);

    /* 用例 3：${var} 展开（拼在词中） */
    argc = script_tokenize("set timeout=${timeout}", argv, 8);
    dump(argc, argv);
    CHECK(argc == 2);
    CHECK(strcmp(argv[0], "set") == 0);
    CHECK(strcmp(argv[1], "timeout=0") == 0);

    /* 用例 4：未定义变量展开为空串 */
    argc = script_tokenize("echo $nonexistent", argv, 8);
    dump(argc, argv);
    CHECK(argc == 2);
    CHECK(argv[1][0] == 0);

    /* 用例 4b：引号内变量也展开（GRUB 语义，与 shell 不同） */
    argc = script_tokenize("echo \"root is $root\"", argv, 8);
    dump(argc, argv);
    CHECK(argc == 2);
    CHECK(strcmp(argv[1], "root is (cd0)") == 0);

    /* 用例 5：menuentry 块行（引号标题 + 花括号） */
    argc = script_tokenize("menuentry \"Test Kernel\" {", argv, 8);
    dump(argc, argv);
    CHECK(argc == 3);
    CHECK(strcmp(argv[0], "menuentry") == 0);
    CHECK(strcmp(argv[1], "Test Kernel") == 0);
    CHECK(strcmp(argv[2], "{") == 0);

    /* 用例 6：注释（# 起忽略到行尾） */
    argc = script_tokenize("set x=1 # this is a comment", argv, 8);
    dump(argc, argv);
    CHECK(argc == 2);
    CHECK(strcmp(argv[1], "x=1") == 0);

    /* 用例 7：空行 / 纯空白 / 纯注释 */
    argc = script_tokenize("   ", argv, 8);
    CHECK(argc == 0);
    argc = script_tokenize("# comment only", argv, 8);
    CHECK(argc == 0);

    /* 用例 8：连续空白与多余空格 */
    argc = script_tokenize("  echo    a   b  ", argv, 8);
    dump(argc, argv);
    CHECK(argc == 3);
    CHECK(strcmp(argv[1], "a") == 0);
    CHECK(strcmp(argv[2], "b") == 0);

    if (fails) {
        printf("tokenizer unit test FAIL (%d)\n", fails);
        return 1;
    }
    printf("tokenizer unit test PASS\n");
    return 0;
}
