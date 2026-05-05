#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <queue>
#include <mutex>

namespace Next {

//=============================================================================
// Descriptor Allocator - Manages free slots in descriptor heaps
//=============================================================================

/**
 * @brief Descriptor block for allocation tracking
 */
struct DescriptorAllocation {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    UINT heapIndex;
    UINT offset;
    UINT count;
    uint32_t frameIndex;
    bool frameScoped;

    DescriptorAllocation()
        : heapIndex(0), offset(0), count(0), frameIndex(0), frameScoped(false) {
        cpuHandle.ptr = 0;
        gpuHandle.ptr = 0;
    }
};

/**
 * @brief Thread-safe descriptor allocator for DX12 descriptor heaps
 *
 * This allocator manages descriptor slots in a heap using a free-list strategy.
 * It supports per-frame allocation and batched releasing to avoid fragmentation.
 *
 * Usage:
 *   1. Create allocator with heap and descriptor size
 *   2. Allocate descriptors (returns CPU and GPU handles)
 *   3. Use persistent descriptors until explicit release
 *   4. Use frame-scoped descriptors only for temporary per-frame tables
 */
class DX12DescriptorAllocator {
public:
    DX12DescriptorAllocator();
    ~DX12DescriptorAllocator();

    /**
     * @brief Initialize the allocator
     * @param device DX12 device
     * @param heap Descriptor heap to manage
     * @param descriptorSize Size of each descriptor (from device)
     * @param numDescriptors Total number of descriptors in heap
     * @param numFramesInFlight Number of frames for delayed releasing (typically 2-3)
     */
    bool Initialize(
        DX12Device* device,
        DX12DescriptorHeap* heap,
        UINT descriptorSize,
        UINT numDescriptors,
        UINT numFramesInFlight = 2);

    /**
     * @brief Shutdown and cleanup
     */
    void Shutdown();

    /**
     * @brief Allocate descriptors from the heap
     * @param count Number of descriptors to allocate
     * @param frameScoped Whether this allocation should be auto-released after the frame window
     * @return Allocation with CPU/GPU handles, or empty allocation if failed
     */
    DescriptorAllocation Allocate(UINT count = 1, bool frameScoped = false);

    /**
     * @brief Release an allocation back to the free list
     * @param allocation Allocation to release
     */
    void Release(const DescriptorAllocation& allocation);

    /**
     * @brief Release all allocations for a specific frame
     * @param frameIndex Frame index to release
     */
    void ReleaseFrameAllocations(uint32_t frameIndex);

    /**
     * @brief Advance allocator frame tracking
     * @param frameIndex Current frame index from the owning manager
     */
    void SetCurrentFrame(uint32_t frameIndex);

    /**
     * @brief Reset all allocations (use with caution)
     */
    void Reset();

    /**
     * @brief Get current allocation statistics
     */
    struct Statistics {
        UINT totalDescriptors;
        UINT allocatedDescriptors;
        UINT freeDescriptors;
        UINT allocationCount;
        float fragmentationPercent;
    };
    Statistics GetStatistics() const;

    /**
     * @brief Check if allocator is initialized
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * @brief Get the managed heap
     */
    DX12DescriptorHeap* GetHeap() const { return heap_; }

private:
    /**
     * @brief Free block structure
     */
    struct FreeBlock {
        UINT offset;
        UINT count;

        FreeBlock(UINT o = 0, UINT c = 0) : offset(o), count(c) {}

        // Sort by offset for coalescing
        bool operator<(const FreeBlock& other) const {
            return offset < other.offset;
        }
    };

    DX12Device* device_;
    DX12DescriptorHeap* heap_;
    UINT descriptorSize_;
    UINT numDescriptors_;
    UINT numFramesInFlight_;

    // Free list (sorted by offset for coalescing)
    std::vector<FreeBlock> freeBlocks_;

    // Track allocations for delayed releasing
    std::vector<DescriptorAllocation> allocations_;

    // Current frame index
    uint32_t currentFrame_;

    bool initialized_;

    // Mutex for thread safety
    mutable std::mutex mutex_;

    /**
     * @brief Coalesce adjacent free blocks to reduce fragmentation
     */
    void CoalesceFreeBlocks();

    /**
     * @brief Find a free block that can satisfy the allocation request
     * @param count Number of descriptors needed
     * @return Index in freeBlocks_, or -1 if not found
     */
    int FindFreeBlock(UINT count);
};

//=============================================================================
// Descriptor Heap Manager - Manages multiple heaps and allocators
//=============================================================================

/**
 * @brief Manages multiple descriptor heaps and their allocators
 *
 * This class provides a high-level interface for descriptor management.
 * It creates heaps, allocators, and handles automatic descriptor lifetime.
 *
 * Supported heap types:
 * - CBV_SRV_UAV (shader resources)
 * - SAMPLER (samplers)
 * - RTV (render targets, not shader visible)
 * - DSV (depth stencil, not shader visible)
 */
class DX12DescriptorHeapManager {
public:
    DX12DescriptorHeapManager();
    ~DX12DescriptorHeapManager();

    /**
     * @brief Initialize the manager
     * @param device DX12 device
     * @param numFramesInFlight Number of frames for resource tracking
     */
    bool Initialize(DX12Device* device, UINT numFramesInFlight = 2);

    /**
     * @brief Shutdown and cleanup
     */
    void Shutdown();

    /**
     * @brief Allocate a descriptor from a specific heap type
     * @param heapType Type of heap (CBV_SRV_UAV, SAMPLER, etc.)
     * @param count Number of descriptors to allocate
     * @param frameScoped Whether this allocation should be auto-released after the frame window
     * @return Allocation with handles
     */
    DescriptorAllocation Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        UINT count = 1,
        bool frameScoped = false);

    /**
     * @brief Release an allocation
     * @param heapType Heap type for the allocation
     * @param allocation Allocation to release
     */
    void Release(
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        const DescriptorAllocation& allocation);

    /**
     * @brief Release all allocations for a specific frame
     * @param frameIndex Frame index to release
     */
    void ReleaseFrameAllocations(uint32_t frameIndex);

    /**
     * @brief Get a descriptor heap by type
     */
    DX12DescriptorHeap* GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType) const;
    bool GetStatistics(
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        DX12DescriptorAllocator::Statistics& stats) const;

    /**
     * @brief Create a new heap with specified parameters
     * @param heapType Type of heap to create
     * @param numDescriptors Number of descriptors in heap
     * @param shaderVisible Whether heap is shader visible (for CBV_SRV_UAV and SAMPLER)
     * @return True if successful
     */
    bool CreateHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        UINT numDescriptors,
        bool shaderVisible = true);

    /**
     * @brief Advance to next frame (for frame-based releasing)
     */
    void AdvanceFrame();

    /**
     * @brief Get current frame index
     */
    uint32_t GetCurrentFrame() const { return currentFrame_; }

private:
    DX12Device* device_;
    UINT numFramesInFlight_;
    uint32_t currentFrame_;

    // Heaps by type (we manage one heap per type for simplicity)
    DX12DescriptorHeap* heaps_[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    // Allocators by type
    DX12DescriptorAllocator* allocators_[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    bool initialized_;
};

//=============================================================================
// Single Frame Allocator - For temporary per-frame allocations
//=============================================================================

/**
 * @brief Reset-able allocator for per-frame temporary descriptors
 *
 * This allocator is designed for temporary descriptors that are only needed
 * for the current frame (e.g., dynamic constant buffers, temporary textures).
 * The entire allocator can be reset with a single call.
 */
class DX12SingleFrameAllocator {
public:
    DX12SingleFrameAllocator();
    ~DX12SingleFrameAllocator();

    /**
     * @brief Initialize
     * @param device DX12 device
     * @param heap Descriptor heap to manage
     * @param descriptorSize Size of each descriptor
     * @param numDescriptors Total descriptors
     */
    bool Initialize(
        DX12Device* device,
        DX12DescriptorHeap* heap,
        UINT descriptorSize,
        UINT numDescriptors);

    /**
     * @brief Shutdown
     */
    void Shutdown();

    /**
     * @brief Allocate descriptors
     * @param count Number to allocate
     * @return Allocation
     */
    DescriptorAllocation Allocate(UINT count = 1);

    /**
     * @brief Reset all allocations (call at start of frame)
     */
    void Reset();

    /**
     * @brief Get current offset (for debugging)
     */
    UINT GetCurrentOffset() const { return currentOffset_; }

    /**
     * @brief Check if out of memory
     */
    bool IsOutOfMemory() const { return outOfMemory_; }

private:
    DX12Device* device_;
    DX12DescriptorHeap* heap_;
    UINT descriptorSize_;
    UINT numDescriptors_;
    UINT currentOffset_;
    bool outOfMemory_;
    bool initialized_;
};

} // namespace Next
