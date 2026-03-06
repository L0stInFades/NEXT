#include "next/renderer/render_graph.h"
#include "next/renderer/dx12/command_list.h"
#include "next/foundation/logger.h"

namespace Next {

RenderGraph::RenderGraph() = default;
RenderGraph::~RenderGraph() = default;

void RenderGraph::Reset() {
    resources_.clear();
    passes_.clear();
}

RenderGraphResourceHandle RenderGraph::ImportRenderTarget(
    const std::string& name,
    ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    D3D12_RESOURCE_STATES initialState) {
    Resource res;
    res.name = name;
    res.resource = resource;
    res.state = initialState;
    res.rtv = rtv;
    res.hasRTV = true;
    resources_.push_back(res);
    return {static_cast<uint32_t>(resources_.size() - 1)};
}

RenderGraphResourceHandle RenderGraph::ImportDepthTarget(
    const std::string& name,
    ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    D3D12_RESOURCE_STATES initialState) {
    Resource res;
    res.name = name;
    res.resource = resource;
    res.state = initialState;
    res.dsv = dsv;
    res.hasDSV = true;
    resources_.push_back(res);
    return {static_cast<uint32_t>(resources_.size() - 1)};
}

void RenderGraph::AddPass(
    const std::string& name,
    const std::function<void(RenderGraphBuilder&)>& setup,
    const std::function<void(RenderGraphContext&)>& execute) {
    Pass pass;
    pass.name = name;
    pass.execute = execute;

    RenderGraphBuilder builder(*this, pass);
    setup(builder);

    passes_.push_back(std::move(pass));
}

void RenderGraph::Execute(DX12CommandList* commandList) {
    if (!commandList) {
        return;
    }

    RenderGraphContext context(*this, commandList);

    for (auto& pass : passes_) {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve(pass.accesses.size());

        for (const auto& access : pass.accesses) {
            if (access.resourceIndex >= resources_.size()) {
                continue;
            }
            Resource& res = resources_[access.resourceIndex];
            if (!res.resource || res.state == access.state) {
                continue;
            }

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = res.resource;
            barrier.Transition.StateBefore = res.state;
            barrier.Transition.StateAfter = access.state;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers.push_back(barrier);

            res.state = access.state;
        }

        if (!barriers.empty()) {
            commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }

        if (pass.execute) {
            pass.execute(context);
        }
    }
}

RenderGraphBuilder::RenderGraphBuilder(RenderGraph& graph, RenderGraph::Pass& pass)
    : graph_(graph), pass_(pass) {}

void RenderGraphBuilder::Read(RenderGraphResourceHandle handle, D3D12_RESOURCE_STATES state) {
    pass_.accesses.push_back({handle.index, state, false});
}

void RenderGraphBuilder::Write(RenderGraphResourceHandle handle, D3D12_RESOURCE_STATES state) {
    pass_.accesses.push_back({handle.index, state, true});
}

RenderGraphContext::RenderGraphContext(RenderGraph& graph, DX12CommandList* commandList)
    : graph_(graph), commandList_(commandList) {}

ID3D12Resource* RenderGraphContext::GetResource(RenderGraphResourceHandle handle) const {
    if (handle.index >= graph_.resources_.size()) {
        return nullptr;
    }
    return graph_.resources_[handle.index].resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderGraphContext::GetRTV(RenderGraphResourceHandle handle) const {
    if (handle.index >= graph_.resources_.size()) {
        return {};
    }
    return graph_.resources_[handle.index].rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderGraphContext::GetDSV(RenderGraphResourceHandle handle) const {
    if (handle.index >= graph_.resources_.size()) {
        return {};
    }
    return graph_.resources_[handle.index].dsv;
}

DX12CommandList* RenderGraphContext::GetCommandList() const {
    return commandList_;
}

} // namespace Next
