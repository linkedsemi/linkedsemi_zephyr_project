#include <zephyr/posix/pthread.h> 
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <stdexcept>
#include <string>

// ===================== 配置：线程栈大小（512KB） =====================
#define PTHREAD_STACK_SIZE (512 * 1024)  // 512KB静态栈，满足大栈展开需求

// 静态分配线程栈（512KB，RISCV 8字节对齐）
static char pthread_stack[PTHREAD_STACK_SIZE] __aligned(8);


// ===================== 步骤1：纯C++异常测试（主线程中验证） =====================
void test_pure_cpp_exception() {
    printk("\n[TEST 1] Pure C++ Exception Test (Main Thread)\n");
    try {
        printk("  Throw std::runtime_error...\n");
        throw std::runtime_error("Pure C++ Exception: Test error message");
    } catch (const std::runtime_error& e) {
        printk("  Caught std::runtime_error: %s\n", e.what());
    }

    // 多层异常嵌套测试
    try {
        printk("  Nested exception: Throw std::invalid_argument...\n");
        try {
            throw std::invalid_argument("Inner exception: Invalid argument");
        } catch (const std::invalid_argument& e) {
            printk("  Inner catch: %s → Re-throw\n", e.what());
            throw;  // 重新抛出到外层
        }
    } catch (const std::exception& e) {
        printk("  Outer catch: %s\n", e.what());
    }
    printk("[TEST 1] Pure C++ Exception Test Done ✅\n");
}

// ===================== 步骤2：POSIX pthread + C++异常测试 =====================
// 子线程函数：包含异常抛出/捕获、异常传递
void* pthread_exception_func(void* arg) {
    printk("\n[TEST 2] POSIX Thread + C++ Exception Test (Sub Thread)\n");
    
    // 场景1：线程内捕获异常
    try {
        printk("  Sub thread throw std::logic_error...\n");
        throw std::logic_error("Sub thread exception: Logic error");
    } catch (const std::logic_error& e) {
        printk("  Sub thread caught: %s\n", e.what());
    }

    // 场景2：异常传递到主线程（纯ASCII字符串）
    printk("  Sub thread throw exception and pass to main thread...\n");
    try {
        throw std::string("Exception message from sub thread!");  // 纯ASCII
    } catch (const std::string& err_msg) {
        // 将异常信息作为线程返回值传递
        char* ret_msg = new char[err_msg.size() + 1];
        strcpy(ret_msg, err_msg.c_str());
        return ret_msg;
    }
}


int main(void) {
    printk("==================== 程序启动 ====================\n");

    // 步骤1：验证纯C++异常在主线程中会异常
    //test_pure_cpp_exception();

    // 步骤2：验证POSIX pthread + C++异常
    pthread_t tid;
    pthread_attr_t attr;
    int ret;

    printk("\n==================== 启动POSIX线程 ====================\n");
    // 初始化线程属性
    ret = pthread_attr_init(&attr);
    printk("[MAIN] pthread_attr_init: %d (errno: %s)\n", ret, strerror(ret));
    if (ret != 0) return ret;

    // 设置512KB静态栈
    ret = pthread_attr_setstack(&attr, pthread_stack, PTHREAD_STACK_SIZE);
    printk("[MAIN] 设置512KB静态栈: %d (errno: %s)\n", ret, strerror(ret));
    if (ret != 0) {
        pthread_attr_destroy(&attr);
        return ret;
    }

    // 创建线程
    ret = pthread_create(&tid, &attr, pthread_exception_func, NULL);
    printk("[MAIN] 创建POSIX线程: %d (errno: %s)\n", ret, strerror(ret));
    if (ret != 0) {
        pthread_attr_destroy(&attr);
        return ret;
    }

    // 等待线程结束，接收异常传递
    void* thread_ret;
    ret = pthread_join(tid, &thread_ret);
    printk("[MAIN] 回收POSIX线程: %d (errno: %s)\n", ret, strerror(ret));
    if (ret == 0 && thread_ret != NULL) {
        printk("[MAIN] 接收到子线程传递的异常信息：%s\n", (char*)thread_ret);
        delete[] (char*)thread_ret;  // 释放内存
    }

    // 清理资源
    pthread_attr_destroy(&attr);

    printk("\n==================== 所有测试完成 ====================\n");
    return 0;
}