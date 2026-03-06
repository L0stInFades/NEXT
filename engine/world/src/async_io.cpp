#include "next/streaming/async_io.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Next {
namespace Streaming {

// ===== Async IO System Implementation =====

AsyncIOSystem::AsyncIOSystem()
    : nextRequestId_(1)
    , totalBytesRead_(0)
    , totalBytesWritten_(0)
    , totalBytesDecompressed_(0)
    , ioCompletionPort_(nullptr)
    , shutdownEvent_(nullptr)
    , initialized_(false)
    , shuttingDown_(false)
    , directStorageFactory_(nullptr)
{
}

AsyncIOSystem::~AsyncIOSystem() {
    Shutdown();
}

bool AsyncIOSystem::Initialize(const AsyncIOConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("AsyncIOSystem already initialized");
        return true;
    }

    config_ = config;

    // Initialize statistics
    stats_ = IOStatistics();

    // Initialize IOCP (Windows)
#ifdef _WIN32
    if (!InitializeIOCP()) {
        NEXT_LOG_ERROR("Failed to initialize IOCP");
        return false;
    }

    // Initialize DirectStorage (optional, Windows 10 Build 20348+)
    if (config_.enableDirectStorage) {
        if (!InitializeDirectStorage()) {
            NEXT_LOG_WARNING("DirectStorage not available, falling back to IOCP");
            config_.enableDirectStorage = false;
        }
    }
#endif

    // Reserve space for queues
    activeRequests_.reserve(config_.maxPendingRequests);

    // NEXT_LOG_INFO("AsyncIOSystem initialized (CP7: World Streaming)");
    // NEXT_LOG_INFO("  IO threads: %u", config_.ioThreads);
    // NEXT_LOG_INFO("  Decompression threads: %u", config_.decompressionThreads);
    // NEXT_LOG_INFO("  Max pending requests: %u", config_.maxPendingRequests);
    // NEXT_LOG_INFO("  DirectStorage: %s", config_.enableDirectStorage ? "enabled" : "disabled");

    initialized_ = true;
    return true;
}

uint64_t AsyncIOSystem::SubmitRequest(const IORequest& request) {
    if (!initialized_) {
        return 0;
    }

    uint64_t requestId = GenerateRequestId();

    InternalRequest internalReq;
    internalReq.request = request;
    internalReq.request.requestId = requestId;
    internalReq.submitTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    internalReq.retryCount = 0;
    internalReq.inFlight = false;

    activeRequests_[requestId] = internalReq;

    // Add to appropriate queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);

        switch (request.type) {
            case IOOperationType::Read:
            case IOOperationType::Write:
                ioQueue_.push(internalReq);
                break;

            case IOOperationType::Decompress:
                decompressionQueue_.push(internalReq);
                break;

            case IOOperationType::UploadGPU:
                // GPU uploads are handled separately by renderer
                break;
        }
    }

    queueCondition_.notify_all();

    return requestId;
}

uint64_t AsyncIOSystem::SubmitReadRequest(
    const std::wstring& filePath,
    uint64_t offset,
    uint64_t size,
    void* outputBuffer,
    CompressionType compression,
    std::function<void(bool, uint64_t)> callback,
    uint32_t priority
) {
    IORequest request;
    request.type = IOOperationType::Read;
    request.filePath = filePath;
    request.offset = offset;
    request.size = size;
    request.outputBuffer = outputBuffer;
    request.outputSize = size;
    request.compressionType = compression;
    request.callback = callback;
    request.priority = priority;

    return SubmitRequest(request);
}

bool AsyncIOSystem::CancelRequest(uint64_t requestId) {
    auto it = activeRequests_.find(requestId);
    if (it != activeRequests_.end()) {
        // Note: In a full implementation, we would cancel the pending IO operation
        activeRequests_.erase(it);
        return true;
    }
    return false;
}

bool AsyncIOSystem::CancelAllRequests() {
    activeRequests_.clear();
    return true;
}

void AsyncIOSystem::Update() {
    if (!initialized_) {
        return;
    }

    // Process completions
    ProcessCompletions();

    // Update statistics
    stats_.pendingReads = static_cast<uint32_t>(ioQueue_.size());
    stats_.pendingWrites = 0;
    stats_.pendingDecompressions = static_cast<uint32_t>(decompressionQueue_.size());

    stats_.totalBytesRead = totalBytesRead_.load();
    stats_.totalBytesWritten = totalBytesWritten_.load();
    stats_.totalBytesDecompressed = totalBytesDecompressed_.load();
}

void AsyncIOSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    shuttingDown_ = true;
    queueCondition_.notify_all();

#ifdef _WIN32
    // Signal shutdown event
    if (shutdownEvent_) {
        SetEvent(reinterpret_cast<HANDLE>(shutdownEvent_));
    }

    // Wait for threads to finish
    for (void* threadPtr : ioThreads_) {
        HANDLE thread = reinterpret_cast<HANDLE>(threadPtr);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    for (void* threadPtr : decompressionThreads_) {
        HANDLE thread = reinterpret_cast<HANDLE>(threadPtr);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

    ShutdownIOCP();
    ShutdownDirectStorage();

    if (shutdownEvent_) {
        CloseHandle(reinterpret_cast<HANDLE>(shutdownEvent_));
        shutdownEvent_ = nullptr;
    }
#endif

    activeRequests_.clear();
    initialized_ = false;

    // NEXT_LOG_INFO("AsyncIOSystem shutdown complete");
}

// ===== Platform-specific Initialization =====

bool AsyncIOSystem::InitializeIOCP() {
#ifdef _WIN32
    // Create IO completion port
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, config_.ioThreads);
    ioCompletionPort_ = port;
    if (!ioCompletionPort_) {
        NEXT_LOG_ERROR("Failed to create IO completion port: %lu", GetLastError());
        return false;
    }

    // Create shutdown event
    HANDLE ev = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    shutdownEvent_ = ev;
    if (!shutdownEvent_) {
        NEXT_LOG_ERROR("Failed to create shutdown event: %lu", GetLastError());
        CloseHandle(reinterpret_cast<HANDLE>(ioCompletionPort_));
        return false;
    }

    // Spawn IO worker threads
    for (uint32_t i = 0; i < config_.ioThreads; ++i) {
        HANDLE thread = CreateThread(nullptr, 0,
            (LPTHREAD_START_ROUTINE)IOThreadProc, this, 0, nullptr);
        if (thread) {
            ioThreads_.push_back(reinterpret_cast<void*>(thread));
        } else {
            NEXT_LOG_ERROR("Failed to create IO thread %u", i);
        }
    }

    // Spawn decompression worker threads
    for (uint32_t i = 0; i < config_.decompressionThreads; ++i) {
        HANDLE thread = CreateThread(nullptr, 0,
            (LPTHREAD_START_ROUTINE)DecompressionThreadProc, this, 0, nullptr);
        if (thread) {
            decompressionThreads_.push_back(reinterpret_cast<void*>(thread));
        } else {
            NEXT_LOG_ERROR("Failed to create decompression thread %u", i);
        }
    }

    return true;
#else
    // TODO: Implement for Linux (io_uring) and macOS (kqueue)
    NEXT_LOG_WARNING("AsyncIO not fully implemented on this platform");
    return true;
#endif
}

bool AsyncIOSystem::InitializeDirectStorage() {
#ifdef _WIN32
    // TODO: Initialize DirectStorage when SDK is available
    // This requires Windows 10 Build 20348+ and DirectStorage headers
    // NEXT_LOG_INFO("DirectStorage initialization skipped (SDK not integrated)");
    return false;
#else
    return false;
#endif
}

void AsyncIOSystem::ShutdownIOCP() {
#ifdef _WIN32
    if (ioCompletionPort_) {
        CloseHandle(reinterpret_cast<HANDLE>(ioCompletionPort_));
        ioCompletionPort_ = nullptr;
    }
#endif
}

void AsyncIOSystem::ShutdownDirectStorage() {
#ifdef _WIN32
    if (directStorageFactory_) {
        // TODO: Release DirectStorage factory
        directStorageFactory_ = nullptr;
    }
#endif
}

// ===== Worker Thread Procs =====

#ifdef _WIN32
void AsyncIOSystem::IOThreadProc(void* parameter) {
    AsyncIOSystem* system = static_cast<AsyncIOSystem*>(parameter);

    while (!system->shuttingDown_) {
        system->ProcessIOQueue();
    }
}

void AsyncIOSystem::DecompressionThreadProc(void* parameter) {
    AsyncIOSystem* system = static_cast<AsyncIOSystem*>(parameter);

    while (!system->shuttingDown_) {
        system->ProcessDecompressionQueue();
    }
}
#endif

// ===== Queue Processing =====

void AsyncIOSystem::ProcessIOQueue() {
    InternalRequest request;
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCondition_.wait(lock, [this]() {
            return shuttingDown_ || !ioQueue_.empty();
        });

        if (shuttingDown_) {
            return;
        }

        request = ioQueue_.front();
        ioQueue_.pop();
    }

    bool success = false;
    uint64_t bytesProcessed = 0;
    uint32_t errorCode = 0;

#ifdef _WIN32
    if (request.request.type == IOOperationType::Read) {
        HANDLE h = CreateFileW(
            request.request.filePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (h == INVALID_HANDLE_VALUE) {
            errorCode = static_cast<uint32_t>(GetLastError());
        } else {
            LARGE_INTEGER li;
            li.QuadPart = static_cast<LONGLONG>(request.request.offset);
            if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) {
                errorCode = static_cast<uint32_t>(GetLastError());
            } else {
                if (!request.request.outputBuffer || request.request.size == 0) {
                    errorCode = ERROR_INVALID_PARAMETER;
                } else {
                    DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(request.request.size, 0xFFFFFFFFull));
                    DWORD readBytes = 0;
                    if (ReadFile(h, request.request.outputBuffer, toRead, &readBytes, nullptr)) {
                        bytesProcessed = static_cast<uint64_t>(readBytes);
                        success = (bytesProcessed == toRead);
                    } else {
                        errorCode = static_cast<uint32_t>(GetLastError());
                    }
                }
            }

            CloseHandle(h);
        }
    } else if (request.request.type == IOOperationType::Write) {
        HANDLE h = CreateFileW(
            request.request.filePath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (h == INVALID_HANDLE_VALUE) {
            errorCode = static_cast<uint32_t>(GetLastError());
        } else {
            LARGE_INTEGER li;
            li.QuadPart = static_cast<LONGLONG>(request.request.offset);
            if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) {
                errorCode = static_cast<uint32_t>(GetLastError());
            } else {
                if (!request.request.inputData || request.request.size == 0) {
                    errorCode = ERROR_INVALID_PARAMETER;
                } else {
                    DWORD toWrite = static_cast<DWORD>(std::min<uint64_t>(request.request.size, 0xFFFFFFFFull));
                    DWORD written = 0;
                    if (WriteFile(h, request.request.inputData, toWrite, &written, nullptr)) {
                        bytesProcessed = static_cast<uint64_t>(written);
                        success = (bytesProcessed == toWrite);
                    } else {
                        errorCode = static_cast<uint32_t>(GetLastError());
                    }
                }
            }
            CloseHandle(h);
        }
    } else {
        // Unsupported in this queue.
        errorCode = ERROR_INVALID_PARAMETER;
    }
#else
    (void)request;
#endif

    // Update statistics
    if (success) {
        if (request.request.type == IOOperationType::Read) {
            totalBytesRead_ += bytesProcessed;
        } else if (request.request.type == IOOperationType::Write) {
            totalBytesWritten_ += bytesProcessed;
        }
    } else {
        stats_.failedOperations++;
    }

    // Enqueue completion (callbacks are executed on main thread in Update()).
    request.completedSuccess = success;
    request.bytesProcessed = bytesProcessed;
    request.errorCode = errorCode;
    {
        std::lock_guard<std::mutex> completionLock(completionMutex_);
        completionQueue_.push(request);
    }
}

void AsyncIOSystem::ProcessDecompressionQueue() {
    InternalRequest request;
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCondition_.wait(lock, [this]() {
            return shuttingDown_ || !decompressionQueue_.empty();
        });

        if (shuttingDown_) {
            return;
        }

        request = decompressionQueue_.front();
        decompressionQueue_.pop();
    }

    // Process decompression
    bool success = false;
    uint64_t bytesProcessed = 0;
    uint32_t errorCode = 0;

    if (request.request.compressionType == CompressionType::Zstd) {
        success = DecompressZstd(request.request.inputData, request.request.inputSize,
                                 request.request.outputBuffer, bytesProcessed);
    } else if (request.request.compressionType == CompressionType::LZ4) {
        success = DecompressLZ4(request.request.inputData, request.request.inputSize,
                                request.request.outputBuffer, bytesProcessed);
    } else {
            // No compression
            if (request.request.outputBuffer && request.request.inputData) {
                // Security: Validate buffer sizes to prevent overflow
                if (request.request.outputSize >= request.request.inputSize) {
                    memcpy(request.request.outputBuffer, request.request.inputData, request.request.inputSize);
                    bytesProcessed = request.request.inputSize;
                    success = true;
                } else {
                    // Buffer too small - this is a critical error
                    NEXT_LOG_ERROR("Buffer overflow prevented: outputSize=%llu < inputSize=%llu",
                              request.request.outputSize, request.request.inputSize);
                    success = false;
                    errorCode = 1;
                }
            }
        }

    // Update statistics
    if (success) {
        totalBytesDecompressed_ += bytesProcessed;
    } else {
        stats_.failedOperations++;
    }

    // Add to completion queue
    request.completedSuccess = success;
    request.bytesProcessed = bytesProcessed;
    request.errorCode = errorCode;
    {
        std::lock_guard<std::mutex> completionLock(completionMutex_);
        completionQueue_.push(request);
    }
}

void AsyncIOSystem::ProcessCompletions() {
    // Drain completions and execute callbacks on the main thread.
    std::queue<InternalRequest> completions;
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        std::swap(completions, completionQueue_);
    }

    while (!completions.empty()) {
        InternalRequest completed = completions.front();
        completions.pop();

        auto it = activeRequests_.find(completed.request.requestId);
        if (it == activeRequests_.end()) {
            // Possibly canceled.
            continue;
        }

        // Execute callback if provided.
        if (it->second.request.callback) {
            it->second.request.callback(completed.completedSuccess, completed.bytesProcessed);
        }

        activeRequests_.erase(it);
    }
}

// ===== Compression Utilities =====

bool AsyncIOSystem::CompressData(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize, CompressionType type) {
    switch (type) {
        case CompressionType::Zstd:
            return CompressZstd(input, inputSize, output, outputSize);
        case CompressionType::LZ4:
            return CompressLZ4(input, inputSize, output, outputSize);
        default:
            return false;
    }
}

bool AsyncIOSystem::DecompressData(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize, CompressionType type) {
    switch (type) {
        case CompressionType::Zstd:
            return DecompressZstd(input, inputSize, output, outputSize);
        case CompressionType::LZ4:
            return DecompressLZ4(input, inputSize, output, outputSize);
        default:
            return false;
    }
}

bool AsyncIOSystem::CompressZstd(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize) {
    // TODO: Implement Zstd compression (requires libzstd)
    NEXT_LOG_WARNING("Zstd compression not implemented, requires libzstd");
    return false;
}

bool AsyncIOSystem::DecompressZstd(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize) {
    // TODO: Implement Zstd decompression (requires libzstd)
    NEXT_LOG_WARNING("Zstd decompression not implemented, requires libzstd");
    return false;
}

bool AsyncIOSystem::CompressLZ4(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize) {
    // TODO: Implement LZ4 compression (requires liblz4)
    NEXT_LOG_WARNING("LZ4 compression not implemented, requires liblz4");
    return false;
}

bool AsyncIOSystem::DecompressLZ4(const void* input, uint64_t inputSize, void* output, uint64_t& outputSize) {
    // TODO: Implement LZ4 decompression (requires liblz4)
    NEXT_LOG_WARNING("LZ4 decompression not implemented, requires liblz4");
    return false;
}

// ===== Request Management =====

uint64_t AsyncIOSystem::GenerateRequestId() {
    return nextRequestId_.fetch_add(1);
}

// ===== Streaming Memory Pool =====

StreamingMemoryPool::StreamingMemoryPool()
    : poolBase_(nullptr)
    , poolSize_(0)
    , usedSize_(0)
    , allocationCount_(0)
    , maxAllocations_(1024)
    , initialized_(false)
{
}

StreamingMemoryPool::~StreamingMemoryPool() {
    Shutdown();
}

bool StreamingMemoryPool::Initialize(size_t poolSizeBytes, uint32_t maxAllocations) {
    if (initialized_) {
        return true;
    }

    poolSize_ = poolSizeBytes;
    maxAllocations_ = maxAllocations;

    // Allocate memory pool
#ifdef _WIN32
    poolBase_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, poolSize_,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
    poolBase_ = static_cast<uint8_t*>(malloc(poolSize_));
#endif

    if (!poolBase_) {
        NEXT_LOG_ERROR("Failed to allocate streaming memory pool (%zu MB)", poolSize_ / (1024 * 1024));
        return false;
    }

    allocations_.reserve(maxAllocations_);

    // NEXT_LOG_INFO("StreamingMemoryPool initialized: %zu MB", poolSize_ / (1024 * 1024));
    initialized_ = true;
    return true;
}

void* StreamingMemoryPool::Allocate(size_t size, size_t alignment) {
    if (!initialized_) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if we have enough space
    if (usedSize_ + size > poolSize_) {
        NEXT_LOG_ERROR("Streaming memory pool exhausted (%zu / %zu MB used)",
                      usedSize_ / (1024 * 1024), poolSize_ / (1024 * 1024));
        return nullptr;
    }

    // Check allocation count
    if (allocationCount_ >= maxAllocations_) {
        NEXT_LOG_ERROR("Streaming memory pool: max allocations reached");
        return nullptr;
    }

    // Allocate from pool
    size_t alignedSize = (size + alignment - 1) & ~(alignment - 1);
    uint8_t* ptr = poolBase_ + usedSize_;

    Allocation alloc;
    alloc.ptr = ptr;
    alloc.size = alignedSize;

    allocations_.push_back(alloc);
    usedSize_ += alignedSize;
    allocationCount_++;

    return ptr;
}

void StreamingMemoryPool::Free(void* ptr) {
    if (!ptr || !initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Find allocation
    for (auto it = allocations_.begin(); it != allocations_.end(); ++it) {
        if (it->ptr == ptr) {
            usedSize_ -= it->size;
            allocationCount_--;
            allocations_.erase(it);
            return;
        }
    }

    NEXT_LOG_WARNING("Attempted to free unknown pointer: %p", ptr);
}

bool StreamingMemoryPool::Defragment() {
    // TODO: Implement defragmentation
    // This is complex and requires moving allocations
    NEXT_LOG_WARNING("Memory pool defragmentation not implemented");
    return false;
}

void StreamingMemoryPool::Shutdown() {
    if (!initialized_) {
        return;
    }

#ifdef _WIN32
    if (poolBase_) {
        VirtualFree(poolBase_, 0, MEM_RELEASE);
        poolBase_ = nullptr;
    }
#else
    free(poolBase_);
    poolBase_ = nullptr;
#endif

    allocations_.clear();
    usedSize_ = 0;
    allocationCount_ = 0;
    initialized_ = false;

    // NEXT_LOG_INFO("StreamingMemoryPool shutdown complete");
}

} // namespace Streaming
} // namespace Next
