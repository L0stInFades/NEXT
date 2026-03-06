#pragma once

#include <d3d12.h>
#include <functional>
#include <string>
#include <vector>

namespace Next {

class DX12CommandList;

struct RenderGraphResourceHandle {
    uint32_t index;
};

class RenderGraphContext;
class RenderGraphBuilder;

class RenderGraph {
public:
    RenderGraph();
    ~RenderGraph();

    void Reset();

    RenderGraphResourceHandle ImportRenderTarget(
        const std::string& name,
        ID3D12Resource* resource,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        D3D12_RESOURCE_STATES initialState);

    RenderGraphResourceHandle ImportDepthTarget(
        const std::string& name,
        ID3D12Resource* resource,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        D3D12_RESOURCE_STATES initialState);

    void AddPass(
        const std::string& name,
        const std::function<void(RenderGraphBuilder&)>& setup,
        const std::function<void(RenderGraphContext&)>& execute);

    void Execute(DX12CommandList* commandList);

private:
    struct Resource {
        std::string name;
        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
        bool hasRTV = false;
        bool hasDSV = false;
    };

    struct Access {
        uint32_t resourceIndex;
        D3D12_RESOURCE_STATES state;
        bool write;
    };

    struct Pass {
        std::string name;
        std::vector<Access> accesses;
        std::function<void(RenderGraphContext&)> execute;
    };

    std::vector<Resource> resources_;
    std::vector<Pass> passes_;

    friend class RenderGraphBuilder;
    friend class RenderGraphContext;
};

class RenderGraphBuilder {
public:
    explicit RenderGraphBuilder(RenderGraph& graph, RenderGraph::Pass& pass);

    void Read(RenderGraphResourceHandle handle, D3D12_RESOURCE_STATES state);
    void Write(RenderGraphResourceHandle handle, D3D12_RESOURCE_STATES state);

private:
    RenderGraph& graph_;
    RenderGraph::Pass& pass_;
};

class RenderGraphContext {
public:
    RenderGraphContext(RenderGraph& graph, DX12CommandList* commandList);

    ID3D12Resource* GetResource(RenderGraphResourceHandle handle) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(RenderGraphResourceHandle handle) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(RenderGraphResourceHandle handle) const;
    DX12CommandList* GetCommandList() const;

private:
    RenderGraph& graph_;
    DX12CommandList* commandList_;
};

} // namespace Next
