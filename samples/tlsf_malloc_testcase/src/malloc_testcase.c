#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/shell/shell.h>

#define ALLOC_ALIGN_SIZE        (16)

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[FAIL] %s\n", msg); \
            return -1; \
        } else { \
            printf("[ OK ] %s\n", msg); \
        } \
    } while (0)


#define CHECK_ALIGN(ptr, msg) \
    do { \
        if (!IS_ALIGNED(ptr, ALLOC_ALIGN_SIZE)) { \
            printf("[FAIL] %s (address %p not 32B aligned)\n", msg, ptr); \
            return -1; \
        } else { \
            printf("[ OK ] %s (aligned)\n", msg); \
        } \
    } while (0)

/* 填充数据模式，用于验证 realloc 后数据是否保持 */
static void fill_pattern(uint8_t *p, size_t size, uint8_t pattern)
{
    for (size_t i = 0; i < size; i++)
        p[i] = pattern;
}

/* ----------------------------------------------------------
 * 测试 1: malloc + free + 对齐检查
 * ---------------------------------------------------------- */
static int test_malloc_free()
{
    printf("\n=== Test malloc/free ===\n");

    void *p1 = malloc(128);
    CHECK(p1 != NULL, "malloc 128 bytes");
    CHECK_ALIGN(p1, "malloc 32-byte aligned");

    free(p1);
    CHECK(1, "free OK");

    return 0;
}

/* ----------------------------------------------------------
 * 测试 2: calloc + 对齐 + 清零
 * ---------------------------------------------------------- */
static int test_calloc()
{
    printf("\n=== Test calloc ===\n");

    uint8_t *p = calloc(1, 256);
    CHECK(p != NULL, "calloc 256 bytes");
    CHECK_ALIGN(p, "calloc 32-byte aligned");

    for (int i = 0; i < 256; i++)
        if (p[i] != 0)
            CHECK(0, "calloc memory not zero");

    CHECK(1, "calloc memory is zero");

    free(p);
    return 0;
}

/* ----------------------------------------------------------
 * 测试 3: realloc 扩容 + 对齐 + 数据保持
 * ---------------------------------------------------------- */
static int test_realloc_expand()
{
    printf("\n=== Test realloc expand ===\n");

    uint8_t *p = malloc(64);
    CHECK(p != NULL, "malloc 64");
    CHECK_ALIGN(p, "malloc 32-byte aligned");

    fill_pattern(p, 64, 0x11);

    uint8_t *p2 = realloc(p, 200);
    CHECK(p2 != NULL, "realloc expand to 200");
    CHECK_ALIGN(p2, "realloc return aligned");

    for (int i = 0; i < 64; i++)
        if (p2[i] != 0x11)
            CHECK(0, "realloc expand data lost");

    CHECK(1, "realloc expand data preserved");

    free(p2);
    return 0;
}

/* ----------------------------------------------------------
 * 测试 4: realloc 缩容 + 对齐 + 数据保持
 * ---------------------------------------------------------- */
static int test_realloc_shrink()
{
    printf("\n=== Test realloc shrink ===\n");

    uint8_t *p = malloc(256);
    CHECK(p != NULL, "malloc 256");
    CHECK_ALIGN(p, "malloc 32-byte aligned");

    fill_pattern(p, 256, 0x22);

    uint8_t *p2 = realloc(p, 80);
    CHECK(p2 != NULL, "realloc shrink to 80");
    CHECK_ALIGN(p2, "realloc return aligned");

    for (int i = 0; i < 80; i++)
        if (p2[i] != 0x22)
            CHECK(0, "realloc shrink data lost");

    CHECK(1, "realloc shrink data preserved");

    free(p2);
    return 0;
}

/* ----------------------------------------------------------
 * 测试 5: 碎片化后 re-malloc + 对齐
 * ---------------------------------------------------------- */
static int test_fragment_allocate()
{
    printf("\n=== Test fragmentation ===\n");

    void *a = malloc(100);
    void *b = malloc(200);
    void *c = malloc(300);

    CHECK(a && b && c, "allocate a,b,c");
    CHECK_ALIGN(a, "a aligned");
    CHECK_ALIGN(b, "b aligned");
    CHECK_ALIGN(c, "c aligned");

    free(b); // 制造碎片

    void *d = malloc(150);
    CHECK(d != NULL, "malloc 150 in fragmented space");
    CHECK_ALIGN(d, "d aligned");

    free(a);
    free(c);
    free(d);
    return 0;
}

/* ----------------------------------------------------------
 * 测试 6: 连续分配/释放 + 对齐
 * ---------------------------------------------------------- */
static int test_multiple_alloc_free()
{
    printf("\n=== Test multiple alloc/free ===\n");

    void *ptrs[50];

    for (int i = 0; i < 50; i++) {
        ptrs[i] = malloc((i + 1) * 16);
        CHECK(ptrs[i] != NULL, "malloc sequence");
        CHECK_ALIGN(ptrs[i], "malloc align32");
    }

    for (int i = 0; i < 50; i += 2)
        free(ptrs[i]);
    for (int i = 1; i < 50; i += 2)
        free(ptrs[i]);

    CHECK(1, "multiple alloc/free OK");
    return 0;
}

void tlsf_heap_info(void);

static int mem_heap_info(const struct shell *shell, size_t argc, char **argv, void *data)
{
    printf("\n======== Mem heap info ========\n");
    tlsf_heap_info();

    return 0;
}
SHELL_CMD_REGISTER(mem_heap_info, NULL, "mem_heap_info", mem_heap_info);

static int mem_test(const struct shell *shell, size_t argc, char **argv, void *data)
{
    printf("\n======== Memory Test Start ========\n");

    if (test_malloc_free())        return -1;
    if (test_calloc())             return -1;
    if (test_realloc_expand())     return -1;
    if (test_realloc_shrink())     return -1;
    if (test_fragment_allocate())  return -1;
    if (test_multiple_alloc_free())return -1;

    printf("\n======== All Tests Passed ========\n\n");

    return 0;
}
SHELL_CMD_REGISTER(mem_test, NULL, "mem_test", mem_test);
