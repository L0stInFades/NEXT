#pragma once

#include <string>
#include <sstream>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>
#include <fstream>

namespace Next {

/**
 * @brief 统一日志系统
 *
 * 特性：
 * 1. 多级日志：Debug, Info, Warning, Error, Fatal
 * 2. 多输出目标：控制台、文件、调试器
 * 3. 线程安全
 * 4. 性能优化：支持异步日志
 * 5. 格式化输出
 * 6. 时间戳、文件名、行号等信息
 */

/**
 * @brief 日志级别
 */
enum class LogLevel {
    Debug,      // 调试信息
    Info,       // 一般信息
    Warning,    // 警告
    Error,      // 错误
    Fatal       // 致命错误
};

/**
 * @brief 日志输出目标
 */
enum class LogTarget {
    Console,    // 控制台
    File,       // 文件
    Debugger,   // 调试器（Windows OutputDebugString）
    Custom      // 自定义输出
};

/**
 * @brief 日志记录
 */
struct LogRecord {
    LogLevel level;
    std::string message;
    std::string file;
    int line;
    std::string function;
    double timestamp;  // 秒
    uint32_t threadId;

    LogRecord() : line(0), timestamp(0.0), threadId(0) {}
};

/**
 * @brief 日志输出器接口
 */
class LogAppender {
public:
    virtual ~LogAppender() = default;

    virtual void Write(const LogRecord& record) = 0;
    virtual void Flush() = 0;
};

/**
 * @brief 控制台输出器
 */
class ConsoleLogAppender : public LogAppender {
public:
    ConsoleLogAppender(bool useColor = true);
    ~ConsoleLogAppender() override = default;

    void Write(const LogRecord& record) override;
    void Flush() override;

private:
    bool useColor_;

    std::string GetLevelString(LogLevel level);
    std::string GetColorCode(LogLevel level);
};

/**
 * @brief 文件输出器
 */
class FileLogAppender : public LogAppender {
public:
    explicit FileLogAppender(const std::string& filePath);
    ~FileLogAppender() override;

    void Write(const LogRecord& record) override;
    void Flush() override;

    bool IsOpen() const { return file_.is_open(); }

private:
    std::ofstream file_;
    std::string filePath_;
    std::mutex mutex_;

    void OpenFile();
    void CloseFile();
};

/**
 * @brief 调试器输出器（Windows专用）
 */
class DebuggerLogAppender : public LogAppender {
public:
    DebuggerLogAppender() = default;
    ~DebuggerLogAppender() override = default;

    void Write(const LogRecord& record) override;
    void Flush() override;
};

/**
 * @brief 自定义输出器
 */
class CustomLogAppender : public LogAppender {
public:
    using LogCallback = std::function<void(const LogRecord&)>;

    explicit CustomLogAppender(LogCallback callback);
    ~CustomLogAppender() override = default;

    void Write(const LogRecord& record) override;
    void Flush() override;

private:
    LogCallback callback_;
};

/**
 * @brief 日志系统配置
 */
struct LogConfig {
    LogLevel minLevel = LogLevel::Info;         // 最低日志级别
    bool useTimestamp = true;                   // 使用时间戳
    bool useFileLocation = true;                // 使用文件位置
    bool useThreadId = false;                   // 使用线程ID
    bool flushOnWrite = false;                  // 每次写入后刷新
    bool asyncMode = false;                     // 异步模式（待实现）
    size_t queueSize = 1024;                    // 异步队列大小
};

/**
 * @brief 统一日志系统
 */
class Logger {
public:
    static Logger& GetInstance();

    /**
     * @brief 初始化日志系统
     */
    void Initialize(const LogConfig& config = LogConfig{});

    /**
     * @brief 关闭日志系统
     */
    void Shutdown();

    /**
     * @brief 添加输出器
     */
    void AddAppender(std::unique_ptr<LogAppender> appender);

    /**
     * @brief 移除所有输出器
     */
    void ClearAppenders();

    /**
     * @brief 记录日志
     */
    void Log(LogLevel level, const std::string& message,
             const char* file = nullptr, int line = 0,
             const char* function = nullptr);

    /**
     * @brief 刷新所有输出器
     */
    void Flush();

    /**
     * @brief 设置最低日志级别
     */
    void SetMinLevel(LogLevel level) { config_.minLevel = level; }

    /**
     * @brief 获取配置
     */
    const LogConfig& GetConfig() const { return config_; }

    /**
     * @brief 格式化日志记录
     */
    std::string FormatRecord(const LogRecord& record);

private:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogConfig config_;
    std::vector<std::unique_ptr<LogAppender>> appenders_;
    std::mutex mutex_;
    bool initialized_ = false;

    bool ShouldLog(LogLevel level) const;
};

/**
 * @brief 日志流辅助类
 */
class LogStream {
public:
    LogStream(LogLevel level, const char* file, int line, const char* function);
    ~LogStream();

    template<typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    const char* file_;
    int line_;
    const char* function_;
    std::ostringstream stream_;
};

} // namespace Next

// =============================================================================
// 便捷宏定义
// =============================================================================

// 日志级别宏
#define NEXT_LOG_DEBUG() ::Next::LogStream(::Next::LogLevel::Debug, __FILE__, __LINE__, __FUNCTION__)
#define NEXT_LOG_INFO()  ::Next::LogStream(::Next::LogLevel::Info, __FILE__, __LINE__, __FUNCTION__)
#define NEXT_LOG_WARN()  ::Next::LogStream(::Next::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__)
#define NEXT_LOG_ERROR() ::Next::LogStream(::Next::LogLevel::Error, __FILE__, __LINE__, __FUNCTION__)
#define NEXT_LOG_FATAL() ::Next::LogStream(::Next::LogLevel::Fatal, __FILE__, __LINE__, __FUNCTION__)

// 带格式化的日志宏（待实现）
#define NEXT_LOG_DEBUG_FMT(fmt, ...) NEXT_LOG_DEBUG() << ::Next::FormatString(fmt, ##__VA_ARGS__)
#define NEXT_LOG_INFO_FMT(fmt, ...)  NEXT_LOG_INFO() << ::Next::FormatString(fmt, ##__VA_ARGS__)
#define NEXT_LOG_WARN_FMT(fmt, ...)  NEXT_LOG_WARN() << ::Next::FormatString(fmt, ##__VA_ARGS__)
#define NEXT_LOG_ERROR_FMT(fmt, ...) NEXT_LOG_ERROR() << ::Next::FormatString(fmt, ##__VA_ARGS__)
#define NEXT_LOG_FATAL_FMT(fmt, ...) NEXT_LOG_FATAL() << ::Next::FormatString(fmt, ##__VA_ARGS__)

// 条件日志宏
#define NEXT_LOG_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            NEXT_LOG_FATAL() << "Assertion failed: " << #cond << " - " << msg; \
        } \
    } while(0)

namespace Next {

// 字符串格式化辅助函数（简化版）
inline std::string FormatString(const char* format) {
    return format ? format : "";
}

template<typename... Args>
std::string FormatString(const char* format, Args... args);

} // namespace Next
