#include <zephyr/posix/pthread.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <stdexcept>
#include <string>
#include <limits>
#include <vector>
#include <cstring>  // memset

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// ===================== 配置项 =====================
#define PTHREAD_STACK_SIZE_NORMAL (512 * 1024)  // 正常场景：512KB栈
#define PTHREAD_STACK_SIZE_SMALL (8 * 1024)     // 栈溢出场景：8KB极小栈
#define MAX_RECURSION_DEPTH 400                 // 递归深度触发溢出

// 静态栈分配（正常栈+溢出测试栈）
static char pthread_stack_normal[PTHREAD_STACK_SIZE_NORMAL] __aligned(8);
static char pthread_stack_small[PTHREAD_STACK_SIZE_SMALL] __aligned(8);

// ===================== 工具函数：简化版栈信息打印（无内核API依赖） =====================
void print_stack_info(const char* tag, const char* stack_type) {
    // 直接打印静态配置的栈大小（无需动态获取内核栈信息）
    if (strcmp(stack_type, "normal") == 0) {
        printk("[%s] 栈信息：配置大小=512KB(正常栈)\n", tag);
    } else if (strcmp(stack_type, "small") == 0) {
        printk("[%s] 栈信息：配置大小=8KB(极小测试栈)\n", tag);
    }
}

// ===================== 场景1：多类型C++异常测试 =====================
// 自定义异常
class CustomException : public std::exception {
private:
    std::string msg;
public:
    explicit CustomException(const std::string& s) : msg(s) {}
    const char* what() const noexcept override { return msg.c_str(); }
};

void test_multi_exception_types() {
    printk("\n[SCENE 1] 多类型C++异常测试\n");
    // 标准库异常
    try { printk("  → 抛出std::runtime_error\n"); throw std::runtime_error("runtime error: file not found"); }
    catch (const std::runtime_error& e) { printk("  ✅ 捕获runtime_error: %s\n", e.what()); }

    try { printk("  → 抛出std::invalid_argument\n"); throw std::invalid_argument("invalid arg: value > 100"); }
    catch (const std::invalid_argument& e) { printk("  ✅ 捕获invalid_argument: %s\n", e.what()); }

    try { printk("  → 抛出std::out_of_range\n"); throw std::out_of_range("out of range: index > vector size"); }
    catch (const std::out_of_range& e) { printk("  ✅ 捕获out_of_range: %s\n", e.what()); }

    // 自定义异常
    try { printk("  → 抛出CustomException\n"); throw CustomException("custom exception: business error"); }
    catch (const CustomException& e) { printk("  ✅ 捕获CustomException: %s\n", e.what()); }

    // 未知异常兜底
    try { printk("  → 抛出int类型未知异常\n"); throw 100; }
    catch (const std::exception& e) { printk("  ❌ 未捕获（类型不匹配）\n"); }
    catch (...) { printk("  ✅ 兜底捕获未知异常(int: 100)\n"); }

    printk("[SCENE 1] 多类型异常测试完成 ✅\n");
}

// ===================== 场景2：POSIX线程多场景 + 异常 =====================
// 场景2-1：线程内异常+资源释放
void* pthread_scene1(void* arg) {
    printk("\n[SCENE 2-1] 线程内异常 + 资源释放\n");
    std::vector<int>* vec = new std::vector<int>(1000);

    try {
        printk("  → 线程内抛出异常，测试资源是否释放\n");
        throw std::logic_error("thread exception: logic error");
    } catch (const std::logic_error& e) {
        printk("  ✅ 线程内捕获: %s\n", e.what());
        delete vec;
        printk("  ✅ 动态资源已释放\n");
    }

    print_stack_info("SCENE 2-1", "small");
    return nullptr;
}

// 场景2-2：多线程并发异常
void* pthread_scene2(void* arg) {
    int thread_id = *(int*)arg;
    printk("\n[SCENE 2-2] 并发线程<%d> 抛出异常\n", thread_id);

    try {
        throw std::string("concurrent thread ") + std::to_string(thread_id) + " exception";
    } catch (const std::string& e) {
        printk("  ✅ 线程<%d> 捕获: %s\n", thread_id, e.c_str());
    }

    return nullptr;
}

// ===================== 场景3：栈溢出测试（核心） =====================
// 递归消耗栈空间
void recursive_stack_consume(int depth) {
    char big_buf[1024];  // 每次递归占1KB栈
    memset(big_buf, 0, sizeof(big_buf));

    printk("  [递归深度=%d] 栈已使用≈%dKB\n", depth, depth * 1);
    if (depth >= MAX_RECURSION_DEPTH) return;
    recursive_stack_consume(depth + 1);  // 递归耗尽栈
}

// 栈溢出测试线程
void* pthread_stack_overflow(void* arg) {
    printk("\n[SCENE 3] 栈溢出测试(512KB正常栈)\n");
    print_stack_info("OVERFLOW PRE", "normal");

    try {
        printk("  → 开始递归消耗栈（目标深度=%d)\n", MAX_RECURSION_DEPTH);
        recursive_stack_consume(0);  // 触发栈溢出
    } catch (...) {
        printk("  ❌ 溢出后异常未捕获（栈已损坏）\n");
    }

    return nullptr;
}

// ===================== 主函数 =====================
int main(void) {
    printk("==================== 多场景异常+线程+栈溢出测试 ====================\n");

    // 场景1：多类型C++异常 ---主线程抛出测试 请取消CONFIG_POSIX_API后尝试
    //test_multi_exception_types();

    // 场景2：POSIX线程多场景
    pthread_t tid1, tid2_1, tid2_2, tid3;
    pthread_attr_t attr;
    int ret;

    ret = pthread_attr_init(&attr);
    printk("\n[MAIN] pthread_attr_init: %d (errno: %s)\n", ret, strerror(ret));

    // 场景2-1：单线程异常+资源释放
    pthread_attr_setstack(&attr, pthread_stack_small, PTHREAD_STACK_SIZE_SMALL);
    ret = pthread_create(&tid1, &attr, pthread_scene1, nullptr);
    pthread_join(tid1, nullptr);

    // 场景2-2：多线程并发异常
    int id1 = 1, id2 = 2;
    ret = pthread_create(&tid2_1, &attr, pthread_scene2, &id1);
    ret = pthread_create(&tid2_2, &attr, pthread_scene2, &id2);
    pthread_join(tid2_1, nullptr);
    pthread_join(tid2_2, nullptr);

    // 场景3：栈溢出测试
    pthread_attr_setstack(&attr, pthread_stack_normal, PTHREAD_STACK_SIZE_NORMAL);
    ret = pthread_create(&tid3, &attr, pthread_stack_overflow, nullptr);
    pthread_join(tid3, nullptr);  // 溢出后线程可能崩溃，join可能超时

    // 清理资源
    pthread_attr_destroy(&attr);

    printk("\n==================== 所有测试完成 ====================\n");
    return 0;
}
