// Minimal D3DX12 helper structures
// Based on Microsoft's D3DX12.h from GitHub

#pragma once

#include <d3d12.h>

// Define SAL annotations if not already defined
#ifndef _In_
#define _In_
#endif
#ifndef _In_reads_
#define _In_reads_(size)
#endif
#ifndef _In_reads_opt_
#define _In_reads_opt_(size)
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif

//-----------------------------------------------------------------------------
// D3DX12 Utility Structures
//-----------------------------------------------------------------------------

struct CD3DX12_DEFAULT {};
extern const DECLSPEC_SELECTANY CD3DX12_DEFAULT D3D12_DEFAULT;

//------------------------------------------------------------------------------
// D3DX12 Resource Barrier Helpers
//------------------------------------------------------------------------------

struct CD3DX12_RESOURCE_BARRIER : public D3D12_RESOURCE_BARRIER
{
    CD3DX12_RESOURCE_BARRIER()
    {
        Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    }

    static inline CD3DX12_RESOURCE_BARRIER Transition(
        _In_ ID3D12Resource* pResource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
    {
        CD3DX12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = flags;
        barrier.Transition.pResource = pResource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter = stateAfter;
        barrier.Transition.Subresource = subresource;
        return barrier;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Blend Description Helpers
//------------------------------------------------------------------------------

struct CD3DX12_BLEND_DESC : public D3D12_BLEND_DESC
{
    CD3DX12_BLEND_DESC() = default;

    explicit CD3DX12_BLEND_DESC(const D3D12_BLEND_DESC& o) :
        D3D12_BLEND_DESC(o)
    {}

    explicit CD3DX12_BLEND_DESC(CD3DX12_DEFAULT)
    {
        AlphaToCoverageEnable = FALSE;
        IndependentBlendEnable = FALSE;
        const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
        {
            FALSE, // BlendEnable
            FALSE, // LogicOpEnable
            D3D12_BLEND_ONE, // SrcBlend
            D3D12_BLEND_ZERO, // DestBlend
            D3D12_BLEND_OP_ADD, // BlendOp
            D3D12_BLEND_ONE, // SrcBlendAlpha
            D3D12_BLEND_ZERO, // DestBlendAlpha
            D3D12_BLEND_OP_ADD, // BlendOpAlpha
            D3D12_LOGIC_OP_NOOP, // LogicOp
            D3D12_COLOR_WRITE_ENABLE_ALL, // RenderTargetWriteMask
        };
        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            RenderTarget[i] = defaultRenderTargetBlendDesc;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Rasterizer Description Helpers
//------------------------------------------------------------------------------

struct CD3DX12_RASTERIZER_DESC : public D3D12_RASTERIZER_DESC
{
    CD3DX12_RASTERIZER_DESC() = default;

    explicit CD3DX12_RASTERIZER_DESC(const D3D12_RASTERIZER_DESC& o) :
        D3D12_RASTERIZER_DESC(o)
    {}

    explicit CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT)
    {
        FillMode = D3D12_FILL_MODE_SOLID;
        CullMode = D3D12_CULL_MODE_BACK;
        FrontCounterClockwise = FALSE;
        DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        DepthClipEnable = TRUE;
        MultisampleEnable = FALSE;
        AntialiasedLineEnable = FALSE;
        ForcedSampleCount = 0;
        ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Depth-Stencil Description Helpers
//------------------------------------------------------------------------------

struct CD3DX12_DEPTH_STENCIL_DESC : public D3D12_DEPTH_STENCIL_DESC
{
    CD3DX12_DEPTH_STENCIL_DESC() = default;

    explicit CD3DX12_DEPTH_STENCIL_DESC(const D3D12_DEPTH_STENCIL_DESC& o) :
        D3D12_DEPTH_STENCIL_DESC(o)
    {}

    explicit CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT)
    {
        DepthEnable = TRUE;
        DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        StencilEnable = FALSE;
        StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
        { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
        FrontFace = defaultStencilOp;
        BackFace = defaultStencilOp;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Root Signature Description Helpers
//------------------------------------------------------------------------------

struct CD3DX12_ROOT_SIGNATURE_DESC : public D3D12_ROOT_SIGNATURE_DESC
{
    CD3DX12_ROOT_SIGNATURE_DESC() = default;

    explicit CD3DX12_ROOT_SIGNATURE_DESC(const D3D12_ROOT_SIGNATURE_DESC& o) :
        D3D12_ROOT_SIGNATURE_DESC(o)
    {}

    CD3DX12_ROOT_SIGNATURE_DESC(
        UINT numParameters,
        _In_reads_opt_(numParameters) const D3D12_ROOT_PARAMETER* _pParameters,
        UINT numStaticSamplers = 0,
        _In_reads_opt_(numStaticSamplers) const D3D12_STATIC_SAMPLER_DESC* _pStaticSamplers = nullptr,
        D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
    {
        Init(numParameters, _pParameters, numStaticSamplers, _pStaticSamplers, flags);
    }

    inline void Init(
        UINT numParameters,
        _In_reads_opt_(numParameters) const D3D12_ROOT_PARAMETER* _pParameters,
        UINT numStaticSamplers = 0,
        _In_reads_opt_(numStaticSamplers) const D3D12_STATIC_SAMPLER_DESC* _pStaticSamplers = nullptr,
        D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
    {
        NumParameters = numParameters;
        pParameters = _pParameters;
        NumStaticSamplers = numStaticSamplers;
        pStaticSamplers = _pStaticSamplers;
        Flags = flags;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Descriptor Range Helpers
//------------------------------------------------------------------------------

struct CD3DX12_DESCRIPTOR_RANGE : public D3D12_DESCRIPTOR_RANGE
{
    CD3DX12_DESCRIPTOR_RANGE() = default;

    explicit CD3DX12_DESCRIPTOR_RANGE(const D3D12_DESCRIPTOR_RANGE& o) :
        D3D12_DESCRIPTOR_RANGE(o)
    {}

    inline void Init(
        D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
        UINT numDescriptors,
        UINT baseRegister,
        UINT registerSpace = 0,
        D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE)
    {
        RangeType = rangeType;
        NumDescriptors = numDescriptors;
        BaseShaderRegister = baseRegister;
        RegisterSpace = registerSpace;
        FlagsInMaskinglyValid = flags;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Root Parameter Helpers
//------------------------------------------------------------------------------

struct CD3DX12_ROOT_PARAMETER : public D3D12_ROOT_PARAMETER
{
    CD3DX12_ROOT_PARAMETER() = default;

    explicit CD3DX12_ROOT_PARAMETER(const D3D12_ROOT_PARAMETER& o) :
        D3D12_ROOT_PARAMETER(o)
    {}

    inline void InitAsConstantBufferView(
        UINT shaderRegister,
        UINT registerSpace = 0,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
    {
        ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        ShaderVisibility = visibility;
        Descriptor.RegisterSpace = registerSpace;
        Descriptor.ShaderRegister = shaderRegister;
    }

    inline void InitAsDescriptorTable(
        UINT numDescriptorRanges,
        _In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE* pDescriptorRanges,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
    {
        ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ShaderVisibility = visibility;
        DescriptorTable.NumDescriptorRanges = numDescriptorRanges;
        DescriptorTable.pDescriptorRanges = pDescriptorRanges;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Static Sampler Description Helpers
//------------------------------------------------------------------------------

struct CD3DX12_STATIC_SAMPLER_DESC : public D3D12_STATIC_SAMPLER_DESC
{
    CD3DX12_STATIC_SAMPLER_DESC() = default;

    explicit CD3DX12_STATIC_SAMPLER_DESC(const D3D12_STATIC_SAMPLER_DESC& o) :
        D3D12_STATIC_SAMPLER_DESC(o)
    {}

    inline void Init(
        UINT shaderRegister,
        D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        FLOAT mipLODBias = 0,
        UINT maxAnisotropy = 16,
        D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
        FLOAT minLOD = 0.0f,
        FLOAT maxLOD = D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        UINT registerSpace = 0)
    {
        Filter = filter;
        AddressU = addressU;
        AddressV = addressV;
        AddressW = addressW;
        MipLODBias = mipLODBias;
        MaxAnisotropy = maxAnisotropy;
        ComparisonFunc = comparisonFunc;
        BorderColor = borderColor;
        MinLOD = minLOD;
        MaxLOD = maxLOD;
        ShaderRegister = shaderRegister;
        RegisterSpace = registerSpace;
        ShaderVisibility = shaderVisibility;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Viewport Helpers
//------------------------------------------------------------------------------

struct CD3DX12_VIEWPORT : public D3D12_VIEWPORT
{
    CD3DX12_VIEWPORT() = default;

    explicit CD3DX12_VIEWPORT(const D3D12_VIEWPORT& o) :
        D3D12_VIEWPORT(o)
    {}

    inline void Init(
        FLOAT topLeftX,
        FLOAT topLeftY,
        FLOAT width,
        FLOAT height,
        FLOAT minDepth = D3D12_MIN_DEPTH,
        FLOAT maxDepth = D3D12_MAX_DEPTH)
    {
        TopLeftX = topLeftX;
        TopLeftY = topLeftY;
        Width = width;
        Height = height;
        MinDepth = minDepth;
        MaxDepth = maxDepth;
    }
};

//------------------------------------------------------------------------------
// D3DX12 Rect Helpers
//------------------------------------------------------------------------------

struct CD3DX12_RECT : public D3D12_RECT
{
    CD3DX12_RECT() = default;

    explicit CD3DX12_RECT(const D3D12_RECT& o) :
        D3D12_RECT(o)
    {}

    inline void Init(
        LONG left,
        LONG top,
        LONG right,
        LONG bottom)
    {
        left = left;
        top = top;
        right = right;
        bottom = bottom;
    }
};

