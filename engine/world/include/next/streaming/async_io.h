#pragma once

#include "next/streaming/world_partition.h"
#include <cstdint>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <unordered_map>

// IMPORTANT: Do not include <windows.h> in public headers.
// It pollutes the global macro namespace (e.g. CreateWindow) and breaks engine code.
// Windows APIs are included and used only from the .cpp translation unit.

namespace Next {
namespace Streaming {

// ===== Compression Algorithms =====

enum class CompressionType : uint32_t {
    None = 0,        // No compression
    Zstd = 1,        // Zstandard (fast, good compression)
    LZ4 = 2,         // LZ4 (extremely fast, decent compression)
    Draco = 3,       // Draco for mesh geometry
    Custom = 4       // Custom compression
};

// ===== IO Operation Type =====

enum class IOOperationType : uint32_t {
    Read = 0,        // Read from disk
    Write = 1,       // Write to disk (for saves/caching)
    Decompress = 2,  // Decompress data
    UploadGPU = 3    // Upload to GPU (copy queue)
};

// ===== IO Request =====

struct IORequest {
    uint64_t requestId;
    IOOperationType type;

    // File information
    std::wstring filePath;
    uint64_t offset;
    uint64_t size;

    // Data buffers
    void* outputBuffer;
    uint64_t outputSize;
    void* inputData;
    uint64_t inputSize;

    // Compression
    CompressionType compressionType;
    uint64_t compressedSize;
    uint64_t decompressedSize;

    // Priority (0 = highest, higher = lower)
    uint32_t priority;

    // Callback
    std::function<void(bool success, uint64_t bytesProcessed)> callback;

    // User data
    void* userData;

    // Internal state
    uint64_t asyncHandle;

    IORequest()
        : requestId(0)
        , type(IOOperationType::Read)
        , offset(0)
        , size(0)
        , outputBuffer(nullptr)
        , outputSize(0)
        , inputData(nullptr)
        , inputSize(0)
        , compressionType(CompressionType::None)
        , compressedSize(0)
        , decompressedSize(0)
        , priority(0)
        , userData(nullptr)
        , asyncHandle(0)
    {}
};

// ===== IO Statistics =====

struct IOStatistics {
    // Throughput
    uint64_t totalBytesRead;
    uint64_t totalBytesWritten;
    uint64_t totalBytesDecompressed;

    // Performance
    float averageReadSpeedMBps;      // Megabytes per second
    float averageWriteSpeedMBps;
    float averageDecompressSpeedMBps;

    // Queue depths
    uint32_t pendingReads;
    uint32_t pendingWrites;
    uint32_t pendingDecompressions;

    // Timing
    float averageReadTime;
    float averageWriteTime;
    float averageDecompressTime;

    // Errors
    uint32_t failedOperations;

    IOStatistics()
        : totalBytesRead(0)
        , totalBytesWritten(0)
        , totalBytesDecompressed(0)
        , averageReadSpeedMBps(0.0f)
        , averageWriteSpeedMBps(0.0f)
        , averageDecompressSpeedMBps(0.0f)
        , pendingReads(0)
        , pendingWrites(0)
        , pendingDecompressions(0)
        , averageReadTime(0.0f)
        , averageWriteTime(0.0f)
        , averageDecompressTime(0.0f)
        , failedOperations(0)
    {}
};

// ===== Async IO Configuration =====

struct AsyncIOConfig {
    // Thread counts
    uint32_t ioThreads = 2;           // Number of IO threads (for IOCP)
    uint32_t decompressionThreads = 2; // Number of decompression threads

    // Queue sizes
    uint32_t maxPendingRequests = 256;
    uint32_t maxConcurrentReads = 32;

    // Buffer sizes
    uint32_t readBufferSize = 1024 * 1024;  // 1MB default buffer
    uint32_t compressionBufferSize = 4 * 1024 * 1024;  // 4MB for decompression

    // Compression settings
    CompressionType defaultCompression = CompressionType::Zstd;
    int32_t compressionLevel = 3;     // Zstd compression level (1-19, default=3)

    // DirectStorage support (Windows only)
    bool enableDirectStorage = false; // Requires Windows 10 Build 20348+
    bool enableBatching = true;       // Batch multiple reads together

    // Performance tuning
    bool prioritizeStreaming = true;  // Prioritize streaming requests over other IO
    uint32_t streamingPriorityBoost = 10; // Priority boost for streaming reads

    AsyncIOConfig() = default;
};

// ===== Async IO System =====

class AsyncIOSystem {
public:
    AsyncIOSystem();
    ~AsyncIOSystem();

    // Initialize with configuration
    bool Initialize(const AsyncIOConfig& config);

    // Submit IO requests
    uint64_t SubmitRequest(const IORequest& request);
    uint64_t SubmitReadRequest(
        const std::wstring& filePath,
        uint64_t offset,
        uint64_t size,
        void* outputBuffer,
        CompressionType compression = CompressionType::None,
        std::function<void(bool, uint64_t)> callback = nullptr,
        uint32_t priority = 0
    );

    // Cancel request
    bool CancelRequest(uint64_t requestId);
    bool CancelAllRequests();

    // Update (called every frame to process completions)
    void Update();

    // Statistics
    IOStatistics GetStatistics() const { return stats_; }

    // Configuration
    void SetConfig(const AsyncIOConfig& config) { config_ = config; }
    const AsyncIOConfig& GetConfig() const { return config_; }

    // Cleanup
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return initialized_; }

private:
    // Platform-specific initialization
    bool InitializeIOCP();
    bool InitializeDirectStorage();
    void ShutdownIOCP();
    void ShutdownDirectStorage();

    // Worker threads
    static void IOThreadProc(void* parameter);
    static void DecompressionThreadProc(void* parameter);

    // IO processing
    void ProcessIOQueue();
    void ProcessDecompressionQueue();
    void ProcessCompletions();

    // Compression utilities
    bool CompressData(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize, CompressionType type);
    bool DecompressData(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize, CompressionType type);

    // Zstd compression (requires lib)
    bool CompressZstd(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize);
    bool DecompressZstd(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize);

    // LZ4 compression (requires lib)
    bool CompressLZ4(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize);
    bool DecompressLZ4(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize);

    // Request management
    struct InternalRequest {
        IORequest request;
        uint64_t submitTime;
        uint32_t retryCount;
        bool inFlight;

        // Completion result (filled by worker threads, consumed on main thread in Update()).
        bool completedSuccess = false;
        uint64_t bytesProcessed = 0;
        uint32_t errorCode = 0;
    };

    uint64_t GenerateRequestId();

    // Configuration
    AsyncIOConfig config_;

    // Thread handles
    std::vector<void*> ioThreads_;
    std::vector<void*> decompressionThreads_;
    void* ioCompletionPort_ = nullptr;
    void* shutdownEvent_ = nullptr;

    // Request queues
    std::queue<InternalRequest> ioQueue_;
    std::queue<InternalRequest> decompressionQueue_;
    std::queue<InternalRequest> completionQueue_;

    // Active requests
    std::unordered_map<uint64_t, InternalRequest> activeRequests_;
    std::atomic<uint64_t> nextRequestId_;

    // Synchronization
    std::mutex queueMutex_;
    std::mutex completionMutex_;
    std::condition_variable queueCondition_;

    // Statistics
    IOStatistics stats_;
    std::atomic<uint64_t> totalBytesRead_;
    std::atomic<uint64_t> totalBytesWritten_;
    std::atomic<uint64_t> totalBytesDecompressed_;

    // State
    bool initialized_;
    bool shuttingDown_;

    // DirectStorage support (Windows only)
    void* directStorageFactory_;  // IDStorageFactory*
};

// ===== Memory Pool for Streaming =====

class StreamingMemoryPool {
public:
    StreamingMemoryPool();
    ~StreamingMemoryPool();

    // Initialize with fixed pool size
    bool Initialize(size_t poolSizeBytes, uint32_t maxAllocations = 1024);

    // Allocate/free memory
    void* Allocate(size_t size, size_t alignment = 16);
    void Free(void* ptr);

    // Statistics
    size_t GetTotalSize() const { return poolSize_; }
    size_t GetUsedSize() const { return usedSize_; }
    size_t GetFreeSize() const { return poolSize_ - usedSize_; }
    size_t GetAllocationCount() const { return allocationCount_; }

    // Defragmentation (optional)
    bool Defragment();

    // Cleanup
    void Shutdown();

private:
    struct Allocation {
        void* ptr;
        size_t size;
        uint32_t padding;
    };

    uint8_t* poolBase_;
    size_t poolSize_;
    size_t usedSize_;
    size_t allocationCount_;
    uint32_t maxAllocations_;

    std::vector<Allocation> allocations_;
    std::mutex mutex_;

    bool initialized_;
};

} // namespace Streaming
} // namespace Next
