// 统一日志系统使用示例

#include "next/log/log.h"
#include <thread>
#include <vector>

using namespace Next;

void BasicLoggingExample() {
    std::cout << "\n=== 基础日志示例 ===\n";

    // 使用流式 API
    NEXT_LOG_DEBUG() << "This is a debug message";
    NEXT_LOG_INFO() << "This is an info message";
    NEXT_LOG_WARN() << "This is a warning message";
    NEXT_LOG_ERROR() << "This is an error message";
    NEXT_LOG_FATAL() << "This is a fatal error message";

    // 带上下文信息的日志
    int value = 42;
    NEXT_LOG_INFO() << "The answer to life, the universe, and everything is " << value;

    // 条件日志
    int errorCode = 0;
    NEXT_LOG_ASSERT(errorCode == 0, "Error code should be zero");
}

void MultipleAppendersExample() {
    std::cout << "\n=== 多输出器示例 ===\n";

    auto& logger = Logger::GetInstance();

    // 清除默认输出器
    logger.ClearAppenders();

    // 添加控制台输出器
    logger.AddAppender(std::make_unique<ConsoleLogAppender>(true));

    // 添加文件输出器
    logger.AddAppender(std::make_unique<FileLogAppender>("log_example.txt"));

    // 添加调试器输出器（仅 Windows）
#ifdef _WIN32
    logger.AddAppender(std::make_unique<DebuggerLogAppender>());
#endif

    // 添加自定义输出器
    logger.AddAppender(std::make_unique<CustomLogAppender>(
        [](const LogRecord& record) {
            // 自定义处理逻辑
            std::cout << "[CUSTOM] Level: " << static_cast<int>(record.level)
                      << ", Message: " << record.message << std::endl;
        }
    ));

    // 测试日志输出
    NEXT_LOG_INFO() << "This message will be sent to all appenders";

    // 刷新所有输出器
    logger.Flush();
}

void LogLevelsExample() {
    std::cout << "\n=== 日志级别过滤示例 ===\n";

    auto& logger = Logger::GetInstance();

    // 设置最低日志级别为 Warning
    logger.SetMinLevel(LogLevel::Warning);

    NEXT_LOG_DEBUG() << "This debug message won't be shown";
    NEXT_LOG_INFO() << "This info message won't be shown";
    NEXT_LOG_WARN() << "This warning message will be shown";
    NEXT_LOG_ERROR() << "This error message will be shown";

    // 恢复为 Debug 级别
    logger.SetMinLevel(LogLevel::Debug);

    NEXT_LOG_DEBUG() << "Now debug messages will be shown again";
}

void ThreadSafetyExample() {
    std::cout << "\n=== 线程安全示例 ===\n";

    auto& logger = Logger::GetInstance();
    LogConfig config;
    config.useThreadId = true;  // 启用线程ID
    logger.Initialize(config);

    std::vector<std::thread> threads;

    // 创建多个线程同时记录日志
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < 10; ++j) {
                NEXT_LOG_INFO() << "Thread " << i << ", Message " << j;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }

    logger.Flush();
}

void ConfigurationExample() {
    std::cout << "\n=== 日志配置示例 ===\n";

    auto& logger = Logger::GetInstance();

    // 自定义配置
    LogConfig config;
    config.minLevel = LogLevel::Debug;
    config.useTimestamp = true;
    config.useFileLocation = true;
    config.useThreadId = false;
    config.flushOnWrite = false;

    logger.Initialize(config);

    NEXT_LOG_INFO() << "Logger initialized with custom configuration";
}

void PerformanceExample() {
    std::cout << "\n=== 性能测试示例 ===\n";

    auto& logger = Logger::GetInstance();

    // 禁用文件位置和时间戳以获得最佳性能
    LogConfig config;
    config.minLevel = LogLevel::Info;
    config.useTimestamp = false;
    config.useFileLocation = false;
    config.useThreadId = false;
    logger.Initialize(config);

    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        NEXT_LOG_INFO() << "Performance test message " << i;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Logged " << iterations << " messages in "
              << duration.count() << " ms" << std::endl;
    std::cout << "Average: " << (duration.count() * 1000.0 / iterations)
              << " microseconds per message" << std::endl;
}

int main() {
    std::cout << "统一日志系统使用示例\n";
    std::cout << "======================\n";

    // 初始化日志系统
    auto& logger = Logger::GetInstance();
    LogConfig config;
    config.minLevel = LogLevel::Debug;
    config.useTimestamp = true;
    config.useFileLocation = true;
    logger.Initialize(config);

    // 运行所有示例
    BasicLoggingExample();
    MultipleAppendersExample();
    LogLevelsExample();
    ConfigurationExample();
    ThreadSafetyExample();
    PerformanceExample();

    // 关闭日志系统
    logger.Shutdown();

    std::cout << "\n所有示例运行完成！\n";
    std::cout << "日志文件已保存到: log_example.txt\n";

    return 0;
}
