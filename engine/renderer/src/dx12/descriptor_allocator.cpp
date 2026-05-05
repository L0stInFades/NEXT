#include "next/renderer/dx12/descriptor_allocator.h"
#include "next/foundation/logger.h"
#include <algorithm>

namespace {

float CalculateUsagePercent(UINT total, UINT used) {
    if (total == 0) {
        return 0.0f;
    }
    return static_cast<float>(used) * 100.0f / static_cast<float>(total);
}

void LogDescriptorAllocationPressure(
    const char* heapName,
    UINT total,
    UINT used,
    UINT freeBlocks,
    UINT allocationCount) {
    const float usagePercent = CalculateUsagePercent(total, used);
    if (usagePercent < 90.0f) {
        return;
    }

    const UINT freeDescriptors = total > used ? total - used : 0;
    NEXT_LOG_WARNING(
        "%s descriptor heap is under pressure (%.1f%% used, %u/%u allocated, %u free blocks, %u allocations)",
        heapName, usagePercent, used, total, freeBlocks, allocationCount);

    if (freeDescriptors <= 16) {
        NEXT_LOG_WARNING(
            "%s descriptor heap is critically low: only %u free descriptors remain",
            heapName, freeDescriptors);
    }
}

}

namespace Next {

//=============================================================================
// DX12DescriptorAllocator
//=============================================================================

DX12DescriptorAllocator::DX12DescriptorAllocator()
    : device_(nullptr)
    , heap_(nullptr)
    , descriptorSize_(0)
    , numDescriptors_(0)
    , numFramesInFlight_(2)
    , currentFrame_(0)
    , initialized_(false) {
}

DX12DescriptorAllocator::~DX12DescriptorAllocator() {
    Shutdown();
}

bool DX12DescriptorAllocator::Initialize(
    DX12Device* device,
    DX12DescriptorHeap* heap,
    UINT descriptorSize,
    UINT numDescriptors,
    UINT numFramesInFlight) {

    if (!device || !device->GetDevice() || !heap || !heap->GetHeap()) {
        NEXT_LOG_ERROR("Invalid device or heap for descriptor allocator");
        return false;
    }

    if (descriptorSize == 0 || numDescriptors == 0) {
        NEXT_LOG_ERROR("Invalid descriptor size or count");
        return false;
    }

    if (numDescriptors > heap->GetNumDescriptors()) {
        NEXT_LOG_ERROR("Descriptor allocator count exceeds heap capacity: %u > %u",
                       numDescriptors, heap->GetNumDescriptors());
        return false;
    }

    Shutdown();

    device_ = device;
    heap_ = heap;
    descriptorSize_ = descriptorSize;
    numDescriptors_ = numDescriptors;
    numFramesInFlight_ = numFramesInFlight > 0 ? numFramesInFlight : 2;
    currentFrame_ = 0;

    // Initialize with one large free block (entire heap)
    freeBlocks_.clear();
    freeBlocks_.push_back(FreeBlock(0, numDescriptors));

    allocations_.clear();

    initialized_ = true;

    NEXT_LOG_INFO("Descriptor allocator initialized: %u descriptors, %u frames in flight",
                  numDescriptors, numFramesInFlight_);

    return true;
}

void DX12DescriptorAllocator::Shutdown() {
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    freeBlocks_.clear();
    allocations_.clear();

    heap_ = nullptr;
    device_ = nullptr;
    descriptorSize_ = 0;
    numDescriptors_ = 0;
    initialized_ = false;

    NEXT_LOG_INFO("Descriptor allocator shutdown complete");
}

DescriptorAllocation DX12DescriptorAllocator::Allocate(UINT count, bool frameScoped) {
    DescriptorAllocation allocation;

    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot allocate: descriptor allocator not initialized");
        return allocation;
    }

    if (count == 0 || count > numDescriptors_) {
        NEXT_LOG_ERROR("Invalid allocation count: %u", count);
        return allocation;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!heap_ || !heap_->GetHeap()) {
        NEXT_LOG_ERROR("Cannot allocate: descriptor heap is invalid");
        return allocation;
    }

    // Find a free block
    int blockIndex = FindFreeBlock(count);
    if (blockIndex < 0) {
        UINT allocated = 0;
        for (const auto& alloc : allocations_) {
            allocated += alloc.count;
        }
        const float usagePercent = CalculateUsagePercent(numDescriptors_, allocated);
        const UINT freeDescriptors = numDescriptors_ > allocated ? numDescriptors_ - allocated : 0;

        NEXT_LOG_ERROR("Failed to allocate %u descriptors: out of descriptor space", count);
        NEXT_LOG_ERROR(
            "DX12 descriptor allocator state: %.1f%% used, %u used, %u free, free blocks: %zu",
            usagePercent, allocated, freeDescriptors, freeBlocks_.size());
        return allocation;
    }

    // Get the free block
    FreeBlock& block = freeBlocks_[blockIndex];

    // Fill in allocation
    UINT allocationOffset = block.offset;
    allocation.heapIndex = 0;  // We only support single heap for now
    allocation.offset = allocationOffset;
    allocation.count = count;
    allocation.frameIndex = currentFrame_;
    allocation.frameScoped = frameScoped;

    // Get CPU and GPU handles
    allocation.cpuHandle = heap_->GetCPUDescriptorHandle(block.offset);
    allocation.gpuHandle = heap_->GetGPUDescriptorHandle(block.offset);
    if (allocation.cpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Failed to allocate %u descriptors: invalid CPU descriptor handle at offset %u",
                       count, block.offset);
        return DescriptorAllocation();
    }
    if (heap_->IsShaderVisible() && allocation.gpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Failed to allocate %u descriptors: invalid GPU descriptor handle at offset %u",
                       count, block.offset);
        return DescriptorAllocation();
    }

    // Update free block
    if (block.count == count) {
        // Exact match - remove block
        freeBlocks_.erase(freeBlocks_.begin() + blockIndex);
    } else {
        // Partial match - reduce block size
        block.offset += count;
        block.count -= count;
    }

    // Track allocation
    allocations_.push_back(allocation);

    UINT allocatedNow = 0;
    for (const auto& alloc : allocations_) {
        allocatedNow += alloc.count;
    }
    LogDescriptorAllocationPressure(
        "DX12 descriptor allocator",
        numDescriptors_,
        allocatedNow,
        static_cast<UINT>(freeBlocks_.size()),
        static_cast<UINT>(allocations_.size()));

    NEXT_LOG_DEBUG("Allocated %u descriptors at offset %u", count, allocationOffset);

    return allocation;
}

void DX12DescriptorAllocator::Release(const DescriptorAllocation& allocation) {
    if (!initialized_) {
        return;
    }

    if (allocation.count == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (allocation.offset >= numDescriptors_ || allocation.count > numDescriptors_ - allocation.offset) {
        NEXT_LOG_WARNING("Ignoring invalid descriptor release: offset=%u count=%u capacity=%u",
                         allocation.offset, allocation.count, numDescriptors_);
        return;
    }

    auto allocationIt = std::find_if(allocations_.begin(), allocations_.end(),
        [&allocation](const DescriptorAllocation& alloc) {
            return alloc.offset == allocation.offset &&
                   alloc.count == allocation.count;
        });
    if (allocationIt == allocations_.end()) {
        NEXT_LOG_WARNING("Ignoring descriptor release for unknown allocation: offset=%u count=%u",
                         allocation.offset, allocation.count);
        return;
    }

    // Add to free list
    freeBlocks_.push_back(FreeBlock(allocation.offset, allocation.count));

    // Remove from allocations list
    allocations_.erase(allocationIt);

    // Coalesce adjacent blocks
    CoalesceFreeBlocks();

    UINT allocated = 0;
    for (const auto& alloc : allocations_) {
        allocated += alloc.count;
    }
    LogDescriptorAllocationPressure(
        "DX12 descriptor allocator",
        numDescriptors_,
        allocated,
        static_cast<UINT>(freeBlocks_.size()),
        static_cast<UINT>(allocations_.size()));

    NEXT_LOG_DEBUG("Released %u descriptors at offset %u", allocation.count, allocation.offset);
}

void DX12DescriptorAllocator::ReleaseFrameAllocations(uint32_t frameIndex) {
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = allocations_.begin(); it != allocations_.end(); ) {
        if (!it->frameScoped) {
            ++it;
            continue;
        }

        // Use modular subtraction to keep behavior correct across frame index wraparound.
        const uint32_t age = frameIndex - it->frameIndex;
        if (age >= numFramesInFlight_) {
            // Add to free list
            freeBlocks_.push_back(FreeBlock(it->offset, it->count));

            NEXT_LOG_DEBUG("Released frame %u allocation: %u descriptors at offset %u",
                          it->frameIndex, it->count, it->offset);

            it = allocations_.erase(it);
        } else {
            ++it;
        }
    }

    // Coalesce adjacent blocks
    CoalesceFreeBlocks();

    UINT allocated = 0;
    for (const auto& alloc : allocations_) {
        allocated += alloc.count;
    }
    LogDescriptorAllocationPressure(
        "DX12 descriptor allocator",
        numDescriptors_,
        allocated,
        static_cast<UINT>(freeBlocks_.size()),
        static_cast<UINT>(allocations_.size()));
}

void DX12DescriptorAllocator::SetCurrentFrame(uint32_t frameIndex) {
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    currentFrame_ = frameIndex;
}

void DX12DescriptorAllocator::Reset() {
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Return all allocations to free list
    freeBlocks_.clear();
    freeBlocks_.push_back(FreeBlock(0, numDescriptors_));
    allocations_.clear();

    NEXT_LOG_INFO("Descriptor allocator reset");
}

DX12DescriptorAllocator::Statistics DX12DescriptorAllocator::GetStatistics() const {
    Statistics stats = {};
    stats.totalDescriptors = numDescriptors_;

    if (!initialized_) {
        return stats;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    stats.allocationCount = static_cast<UINT>(allocations_.size());

    // Calculate allocated descriptors
    UINT allocated = 0;
    for (const auto& alloc : allocations_) {
        allocated += alloc.count;
    }
    stats.allocatedDescriptors = allocated;
    stats.freeDescriptors = numDescriptors_ - allocated;

    // Calculate fragmentation (number of free blocks)
    stats.fragmentationPercent = freeBlocks_.size() > 1 ?
        static_cast<float>(freeBlocks_.size() - 1) * 100.0f / numDescriptors_ : 0.0f;

    return stats;
}

void DX12DescriptorAllocator::CoalesceFreeBlocks() {
    if (freeBlocks_.size() < 2) {
        return;
    }

    // Sort by offset
    std::sort(freeBlocks_.begin(), freeBlocks_.end());

    // Merge adjacent blocks
    std::vector<FreeBlock> coalesced;
    coalesced.push_back(freeBlocks_[0]);

    for (size_t i = 1; i < freeBlocks_.size(); ++i) {
        FreeBlock& last = coalesced.back();
        const FreeBlock& current = freeBlocks_[i];

        if (last.offset + last.count == current.offset) {
            // Adjacent - merge
            last.count += current.count;
        } else {
            // Not adjacent - add new block
            coalesced.push_back(current);
        }
    }

    freeBlocks_ = std::move(coalesced);
}

int DX12DescriptorAllocator::FindFreeBlock(UINT count) {
    // First-fit strategy
    for (size_t i = 0; i < freeBlocks_.size(); ++i) {
        if (freeBlocks_[i].count >= count) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

//=============================================================================
// DX12DescriptorHeapManager
//=============================================================================

DX12DescriptorHeapManager::DX12DescriptorHeapManager()
    : device_(nullptr)
    , numFramesInFlight_(2)
    , currentFrame_(0)
    , initialized_(false) {

    // Initialize heap and allocator pointers to null
    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i) {
        heaps_[i] = nullptr;
        allocators_[i] = nullptr;
    }
}

DX12DescriptorHeapManager::~DX12DescriptorHeapManager() {
    Shutdown();
}

bool DX12DescriptorHeapManager::Initialize(DX12Device* device, UINT numFramesInFlight) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for descriptor heap manager");
        return false;
    }

    Shutdown();

    device_ = device;
    numFramesInFlight_ = numFramesInFlight > 0 ? numFramesInFlight : 2;
    currentFrame_ = 0;

    initialized_ = true;

    NEXT_LOG_INFO("Descriptor heap manager initialized: %u frames in flight", numFramesInFlight_);

    return true;
}

void DX12DescriptorHeapManager::Shutdown() {
    if (!initialized_) {
        return;
    }

    // Shutdown allocators first
    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i) {
        if (allocators_[i]) {
            allocators_[i]->Shutdown();
            delete allocators_[i];
            allocators_[i] = nullptr;
        }
    }

    // Shutdown heaps
    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i) {
        if (heaps_[i]) {
            heaps_[i]->Shutdown();
            delete heaps_[i];
            heaps_[i] = nullptr;
        }
    }

    device_ = nullptr;
    initialized_ = false;

    NEXT_LOG_INFO("Descriptor heap manager shutdown complete");
}

DescriptorAllocation DX12DescriptorHeapManager::Allocate(
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT count,
    bool frameScoped) {

    DescriptorAllocation allocation;

    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot allocate: descriptor heap manager not initialized");
        return allocation;
    }

    if (heapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) {
        NEXT_LOG_ERROR("Invalid heap type: %d", heapType);
        return allocation;
    }

    // Get allocator for this heap type
    DX12DescriptorAllocator* allocator = allocators_[heapType];
    if (!allocator) {
        NEXT_LOG_ERROR("No allocator for heap type %d (heap not created?)", heapType);
        return allocation;
    }

    return allocator->Allocate(count, frameScoped);
}

void DX12DescriptorHeapManager::Release(
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    const DescriptorAllocation& allocation) {

    if (!initialized_) {
        return;
    }

    if (heapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) {
        return;
    }

    DX12DescriptorAllocator* allocator = allocators_[heapType];
    if (!allocator) {
        return;
    }

    allocator->Release(allocation);
}

void DX12DescriptorHeapManager::ReleaseFrameAllocations(uint32_t frameIndex) {
    if (!initialized_) {
        return;
    }

    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i) {
        if (allocators_[i]) {
            allocators_[i]->ReleaseFrameAllocations(frameIndex);
        }
    }
}

DX12DescriptorHeap* DX12DescriptorHeapManager::GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType) const {
    if (!initialized_ || heapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) {
        return nullptr;
    }
    return heaps_[heapType];
}

bool DX12DescriptorHeapManager::CreateHeap(
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors,
    bool shaderVisible) {

    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot create heap: descriptor heap manager not initialized");
        return false;
    }

    if (heapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) {
        NEXT_LOG_ERROR("Invalid heap type: %d", heapType);
        return false;
    }

    // Check if heap already exists
    if (heaps_[heapType]) {
        NEXT_LOG_WARNING("Heap of type %d already exists", heapType);
        return true;
    }

    // Create heap based on type
    DX12DescriptorHeap* heap = nullptr;
    bool initSuccess = false;

    switch (heapType) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
            heap = new DX12RTVHeap();
            initSuccess = static_cast<DX12RTVHeap*>(heap)->Initialize(device_, numDescriptors);
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            heap = new DX12DSVHeap();
            initSuccess = static_cast<DX12DSVHeap*>(heap)->Initialize(device_, numDescriptors);
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            heap = new DX12CBVSRVUAVHeap();
            initSuccess = static_cast<DX12CBVSRVUAVHeap*>(heap)->Initialize(device_, numDescriptors, shaderVisible);
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            heap = new DX12SamplerHeap();
            initSuccess = static_cast<DX12SamplerHeap*>(heap)->Initialize(device_, numDescriptors, shaderVisible);
            break;

        default:
            NEXT_LOG_ERROR("Unsupported heap type: %d", heapType);
            return false;
    }

    if (!heap || !initSuccess || !heap->GetHeap()) {
        NEXT_LOG_ERROR("Failed to create heap of type %d", heapType);
        delete heap;
        return false;
    }

    heaps_[heapType] = heap;

    // Create allocator for this heap
    DX12DescriptorAllocator* allocator = new DX12DescriptorAllocator();
    UINT descriptorSize = device_->GetDevice()->GetDescriptorHandleIncrementSize(heapType);

    if (!allocator->Initialize(device_, heap, descriptorSize, numDescriptors, numFramesInFlight_)) {
        NEXT_LOG_ERROR("Failed to create allocator for heap type %d", heapType);
        delete allocator;
        heap->Shutdown();
        delete heap;
        heaps_[heapType] = nullptr;
        return false;
    }

    allocators_[heapType] = allocator;

    NEXT_LOG_INFO("Created heap and allocator: type=%d, descriptors=%u, shaderVisible=%d",
                  heapType, numDescriptors, shaderVisible);

    return true;
}

bool DX12DescriptorHeapManager::GetStatistics(
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    DX12DescriptorAllocator::Statistics& stats) const {

    if (!initialized_) {
        return false;
    }

    if (heapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) {
        return false;
    }

    DX12DescriptorAllocator* allocator = allocators_[heapType];
    if (!allocator) {
        return false;
    }

    stats = allocator->GetStatistics();
    return true;
}

void DX12DescriptorHeapManager::AdvanceFrame() {
    if (!initialized_) {
        return;
    }

    currentFrame_++;

    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i) {
        if (allocators_[i]) {
            allocators_[i]->SetCurrentFrame(currentFrame_);
        }
    }
    // Release allocations that are older than the configured frame window.
    ReleaseFrameAllocations(currentFrame_);

    // Extremely chatty at runtime; keep at trace level.
    NEXT_LOG_TRACE("Advanced to frame %u", currentFrame_);
}

//=============================================================================
// DX12SingleFrameAllocator
//=============================================================================

DX12SingleFrameAllocator::DX12SingleFrameAllocator()
    : device_(nullptr)
    , heap_(nullptr)
    , descriptorSize_(0)
    , numDescriptors_(0)
    , currentOffset_(0)
    , outOfMemory_(false)
    , initialized_(false) {
}

DX12SingleFrameAllocator::~DX12SingleFrameAllocator() {
    Shutdown();
}

bool DX12SingleFrameAllocator::Initialize(
    DX12Device* device,
    DX12DescriptorHeap* heap,
    UINT descriptorSize,
    UINT numDescriptors) {

    if (!device || !device->GetDevice() || !heap || !heap->GetHeap()) {
        NEXT_LOG_ERROR("Invalid device or heap for single-frame allocator");
        return false;
    }

    if (descriptorSize == 0 || numDescriptors == 0) {
        NEXT_LOG_ERROR("Invalid descriptor size or count for single-frame allocator");
        return false;
    }

    if (numDescriptors > heap->GetNumDescriptors()) {
        NEXT_LOG_ERROR("Single-frame allocator count exceeds heap capacity: %u > %u",
                       numDescriptors, heap->GetNumDescriptors());
        return false;
    }

    Shutdown();

    device_ = device;
    heap_ = heap;
    descriptorSize_ = descriptorSize;
    numDescriptors_ = numDescriptors;
    currentOffset_ = 0;
    outOfMemory_ = false;

    initialized_ = true;

    NEXT_LOG_INFO("Single-frame allocator initialized: %u descriptors", numDescriptors);

    return true;
}

void DX12SingleFrameAllocator::Shutdown() {
    if (!initialized_) {
        return;
    }

    heap_ = nullptr;
    device_ = nullptr;
    descriptorSize_ = 0;
    numDescriptors_ = 0;
    currentOffset_ = 0;
    outOfMemory_ = false;
    initialized_ = false;
}

DescriptorAllocation DX12SingleFrameAllocator::Allocate(UINT count) {
    DescriptorAllocation allocation;

    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot allocate: single-frame allocator not initialized");
        return allocation;
    }

    if (count == 0) {
        return allocation;
    }

    if (!heap_ || !heap_->GetHeap()) {
        NEXT_LOG_ERROR("Cannot allocate: single-frame descriptor heap is invalid");
        return allocation;
    }

    if (count > numDescriptors_ || currentOffset_ > numDescriptors_ - count) {
        NEXT_LOG_ERROR("Single-frame allocator out of memory: need %u, have %u",
                      count, numDescriptors_ - currentOffset_);
        outOfMemory_ = true;
        return allocation;
    }

    // Fill in allocation
    allocation.heapIndex = 0;
    allocation.offset = currentOffset_;
    allocation.count = count;
    allocation.frameIndex = 0;  // Not used

    // Get handles
    allocation.cpuHandle = heap_->GetCPUDescriptorHandle(currentOffset_);
    allocation.gpuHandle = heap_->GetGPUDescriptorHandle(currentOffset_);
    if (allocation.cpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Single-frame allocator returned invalid CPU descriptor handle at offset %u",
                       currentOffset_);
        return DescriptorAllocation();
    }
    if (heap_->IsShaderVisible() && allocation.gpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Single-frame allocator returned invalid GPU descriptor handle at offset %u",
                       currentOffset_);
        return DescriptorAllocation();
    }

    // Advance offset
    currentOffset_ += count;

    return allocation;
}

void DX12SingleFrameAllocator::Reset() {
    if (!initialized_) {
        return;
    }

    currentOffset_ = 0;
    outOfMemory_ = false;

    NEXT_LOG_DEBUG("Single-frame allocator reset");
}

} // namespace Next
