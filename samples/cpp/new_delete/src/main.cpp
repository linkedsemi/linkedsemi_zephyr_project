#include <cstdio>
#include <cstdlib>
#include <new>
#include <cstring>
#include <vector>
#include <algorithm>
#include <random>
#include <zephyr/shell/shell.h>


// ----------------- 测试函数模板 -----------------
bool test_malloc_free(bool use_nothrow)
{
    printf("test_malloc_free (%s) ...\n", use_nothrow ? "nothrow" : "normal");
    int* p = use_nothrow ? new (std::nothrow) int(123) : new int(123);
    if (!p) return true;
    delete p;

    double* arr = use_nothrow ? new (std::nothrow) double[10] : new double[10];
    if (!arr) return true;
    delete[] arr;

    return false;
}

bool test_calloc(bool use_nothrow)
{
    printf("test_calloc (%s) ...\n", use_nothrow ? "nothrow" : "normal");
    size_t n = 16;
    int* arr = use_nothrow ? new (std::nothrow) int[n] : new int[n];
    if (!arr) return true;

    // 模拟 calloc：初始化为 0
    for (size_t i = 0; i < n; i++) arr[i] = 0;
    for (size_t i = 0; i < n; i++)
        if (arr[i] != 0) { delete[] arr; return true; }

    delete[] arr;
    return false;
}

bool test_realloc_expand(bool use_nothrow)
{
    printf("test_realloc_expand (%s) ...\n", use_nothrow ? "nothrow" : "normal");
    size_t old_size = 10, new_size = 20;

    int* arr = use_nothrow ? new (std::nothrow) int[old_size] : new int[old_size];
    if (!arr) return true;
    for (size_t i = 0; i < old_size; i++) arr[i] = (int)i;

    int* new_arr = use_nothrow ? new (std::nothrow) int[new_size] : new int[new_size];
    if (!new_arr) { delete[] arr; return true; }
    memcpy(new_arr, arr, old_size * sizeof(int));

    delete[] arr;
    delete[] new_arr;
    return false;
}

bool test_realloc_shrink(bool use_nothrow)
{
    printf("test_realloc_shrink (%s) ...\n", use_nothrow ? "nothrow" : "normal");
    size_t old_size = 20, new_size = 10;

    int* arr = use_nothrow ? new (std::nothrow) int[old_size] : new int[old_size];
    if (!arr) return true;
    for (size_t i = 0; i < old_size; i++) arr[i] = (int)i;

    int* new_arr = use_nothrow ? new (std::nothrow) int[new_size] : new int[new_size];
    if (!new_arr) { delete[] arr; return true; }
    memcpy(new_arr, arr, new_size * sizeof(int));

    delete[] arr;
    delete[] new_arr;
    return false;
}

bool test_fragment_allocate(bool use_nothrow)
{
    printf("test_fragment_allocate (%s) ...\n", use_nothrow ? "nothrow" : "normal");
    const int N = 100;
    std::vector<int*> ptrs;
    std::default_random_engine rng;
    std::uniform_int_distribution<size_t> dist(1, 10);

    for (int i = 0; i < N; i++) {
        size_t sz = dist(rng);
        int* p = use_nothrow ? new (std::nothrow) int[sz] : new int[sz];
        if (!p) break;
        ptrs.push_back(p);
    }

    std::shuffle(ptrs.begin(), ptrs.end(), rng);
    for (size_t i = 0; i < ptrs.size() / 2; i++) {
        delete[] ptrs[i];
        ptrs[i] = nullptr;
    }

    for (size_t i = 0; i < ptrs.size() / 2; i++) {
        size_t sz = dist(rng);
        int* p = use_nothrow ? new (std::nothrow) int[sz] : new int[sz];
        if (!p) return true;
        delete[] p;
    }

    for (auto p : ptrs) if (p) delete[] p;
    return false;
}

bool test_multiple_alloc_free(bool use_nothrow)
{
    printf("test_multiple_alloc_free (%s) ...\n", use_nothrow ? "nothrow" : "normal");
    const int N = 50;
    std::vector<int*> ptrs;

    for (int i = 0; i < N; i++) {
        int* p = use_nothrow ? new (std::nothrow) int[10] : new int[10];
        if (!p) return true;
        ptrs.push_back(p);
    }

    for (auto p : ptrs) delete[] p;
    ptrs.clear();

    for (int i = 0; i < N; i++) {
        int* p = use_nothrow ? new (std::nothrow) int[5] : new int[5];
        if (!p) return true;
        delete[] p;
    }

    return false;
}

extern "C" int new_delete_test()
{
    // 普通 new/delete 测试
    if (test_malloc_free(false))        return -1;
    if (test_calloc(false))             return -1;
    if (test_realloc_expand(false))     return -1;
    if (test_realloc_shrink(false))     return -1;
    if (test_fragment_allocate(false))  return -1;
    if (test_multiple_alloc_free(false))return -1;

    // nothrow new/delete 测试
    if (test_malloc_free(true))         return -1;
    if (test_calloc(true))              return -1;
    if (test_realloc_expand(true))      return -1;
    if (test_realloc_shrink(true))      return -1;
    if (test_fragment_allocate(true))   return -1;
    if (test_multiple_alloc_free(true)) return -1;

    return 0;
}

static struct k_thread thread;

#define ABORT_TEST_THREAD_STACK_SIZE (32 * 1024)

K_KERNEL_STACK_MEMBER(abort_test_stack, ABORT_TEST_THREAD_STACK_SIZE);

static void abort_test_entry(void* p1, void* p2, void* p3)
{
    while (1)
    {
        uint8_t* p = new uint8_t[1024*1024];
        printk("new point = %p\n", p);
        k_msleep(2000);
    }
}

static int abort_test(const struct shell *shell, size_t argc, char **argv, void *data)
{
    k_thread_create(&thread, abort_test_stack, K_THREAD_STACK_SIZEOF(abort_test_stack), abort_test_entry, 
        NULL, NULL, NULL, 13, 0, K_NO_WAIT);

    return 0;
}
SHELL_CMD_REGISTER(abort_test, NULL, "gpio_test", abort_test);

int main()
{
    return 0;
}
