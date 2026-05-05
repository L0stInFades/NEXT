#include "next/renderer/metal/metal_renderer.h"
#include "next/foundation/logger.h"
#include "next/platform/window.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace Next {

namespace {

struct MetalVertex {
    float position[3];
    float normal[3];
    float texcoord[2];
    float albedo[3];
};

struct MetalUniforms {
    float mvp[16];
    float model[16];
    float lightDirection[4];
    float cameraPosition[4];
    float material[4];      // roughness, metallic, exposure, padding
    float ambientColor[4];
    float debugTint[4];
};

constexpr size_t kMetalUniformAlignment = 256;
constexpr size_t kMetalUniformStride =
    ((sizeof(MetalUniforms) + kMetalUniformAlignment - 1) / kMetalUniformAlignment) * kMetalUniformAlignment;

constexpr MetalVertex kCubeVertices[] = {
    {{-1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}, {0.90f, 0.18f, 0.16f}},
    {{ 1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}, {0.90f, 0.18f, 0.16f}},
    {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}, {0.90f, 0.18f, 0.16f}},
    {{-1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}, {0.90f, 0.18f, 0.16f}},

    {{-1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}, {0.18f, 0.78f, 0.80f}},
    {{ 1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}, {0.18f, 0.78f, 0.80f}},
    {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}, {0.18f, 0.78f, 0.80f}},
    {{-1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}, {0.18f, 0.78f, 0.80f}},

    {{-1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}, {0.96f, 0.62f, 0.16f}},
    {{ 1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}, {0.96f, 0.62f, 0.16f}},
    {{ 1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}, {0.96f, 0.62f, 0.16f}},
    {{-1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}, {0.96f, 0.62f, 0.16f}},

    {{-1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}, {0.22f, 0.72f, 0.30f}},
    {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}, {0.22f, 0.72f, 0.30f}},
    {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}, {0.22f, 0.72f, 0.30f}},
    {{-1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}, {0.22f, 0.72f, 0.30f}},

    {{-1.0f, -1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.70f, 0.38f, 0.95f}},
    {{-1.0f,  1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {0.70f, 0.38f, 0.95f}},
    {{-1.0f,  1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {0.70f, 0.38f, 0.95f}},
    {{-1.0f, -1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.70f, 0.38f, 0.95f}},

    {{ 1.0f, -1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.92f, 0.92f, 0.32f}},
    {{ 1.0f,  1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {0.92f, 0.92f, 0.32f}},
    {{ 1.0f,  1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {0.92f, 0.92f, 0.32f}},
    {{ 1.0f, -1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.92f, 0.92f, 0.32f}},
};

constexpr uint16_t kCubeIndices[] = {
    0, 2, 1, 0, 3, 2,
    4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,
    12, 15, 14, 12, 14, 13,
    16, 18, 17, 16, 19, 18,
    20, 21, 22, 20, 22, 23,
};

constexpr const char* kMetalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    packed_float3 position;
    packed_float3 normal;
    packed_float2 texcoord;
    packed_float3 albedo;
};

struct Uniforms {
    float4x4 mvp;
    float4x4 model;
    float4 lightDirection;
    float4 cameraPosition;
    float4 material;
    float4 ambientColor;
    float4 debugTint;
};

struct VertexOut {
    float4 position [[position]];
    float3 worldPosition;
    float3 worldNormal;
    float2 texcoord;
    float3 albedo;
};

float3 ACESFilm(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

vertex VertexOut vertex_main(const device VertexIn* vertices [[buffer(0)]],
                             constant Uniforms& uniforms [[buffer(1)]],
                             uint vertexId [[vertex_id]]) {
    VertexOut out;
    const float4 worldPosition = uniforms.model * float4(vertices[vertexId].position, 1.0);
    out.position = uniforms.mvp * float4(vertices[vertexId].position, 1.0);
    out.worldPosition = worldPosition.xyz;
    out.worldNormal = normalize((uniforms.model * float4(vertices[vertexId].normal, 0.0)).xyz);
    out.texcoord = vertices[vertexId].texcoord;
    out.albedo = vertices[vertexId].albedo;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant Uniforms& uniforms [[buffer(1)]],
                              texture2d<float> baseColorTexture [[texture(0)]],
                              sampler baseColorSampler [[sampler(0)]]) {
    const float3 normal = normalize(in.worldNormal);
    const float3 lightDir = normalize(-uniforms.lightDirection.xyz);
    const float3 viewDir = normalize(uniforms.cameraPosition.xyz - in.worldPosition);
    const float3 halfVec = normalize(lightDir + viewDir);

    const float roughness = clamp(uniforms.material.x, 0.08, 1.0);
    const float metallic = clamp(uniforms.material.y, 0.0, 1.0);
    const float exposure = max(uniforms.material.z, 0.001);

    const float nDotL = saturate(dot(normal, lightDir));
    const float nDotH = saturate(dot(normal, halfVec));
    const float vDotH = saturate(dot(viewDir, halfVec));

    const float3 sampledBaseColor = baseColorTexture.sample(baseColorSampler, in.texcoord).rgb;
    const float3 baseColor = max(in.albedo * sampledBaseColor * uniforms.debugTint.rgb, float3(0.0));
    const float3 f0 = mix(float3(0.04), baseColor, metallic);
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
    const float specPower = mix(96.0, 8.0, roughness);
    const float3 specular = fresnel * pow(nDotH, specPower) * nDotL;
    const float3 diffuse = baseColor * (1.0 - metallic) * nDotL;
    const float3 ambient = uniforms.ambientColor.xyz * baseColor;

    const float3 hdr = (ambient + diffuse + specular) * exposure;
    return float4(ACESFilm(hdr), 1.0);
}
)";

void MatrixIdentity(float* m) {
    std::fill(m, m + 16, 0.0f);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

void MatrixMultiply(const float* a, const float* b, float* out) {
    float result[16] = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
            }
        }
    }
    std::memcpy(out, result, sizeof(result));
}

void MatrixPerspective(float fovyRadians, float aspect, float nearZ, float farZ, float* out) {
    std::fill(out, out + 16, 0.0f);
    const float f = 1.0f / std::tan(fovyRadians * 0.5f);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = farZ / (farZ - nearZ);
    out[11] = 1.0f;
    out[14] = -(farZ * nearZ) / (farZ - nearZ);
}

void MatrixTranslation(float x, float y, float z, float* out) {
    MatrixIdentity(out);
    out[12] = x;
    out[13] = y;
    out[14] = z;
}

void MatrixScale(float x, float y, float z, float* out) {
    MatrixIdentity(out);
    out[0] = x;
    out[5] = y;
    out[10] = z;
}

float VectorDot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void VectorCross3(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void VectorNormalize3(float* v) {
    const float length = std::sqrt(VectorDot3(v, v));
    if (length <= 0.0001f) {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = 1.0f;
        return;
    }

    const float invLength = 1.0f / length;
    v[0] *= invLength;
    v[1] *= invLength;
    v[2] *= invLength;
}

void MatrixLookAt(float eyeX, float eyeY, float eyeZ,
                  float targetX, float targetY, float targetZ,
                  const float* up, float* out) {
    const float eye[3] = {eyeX, eyeY, eyeZ};
    float zAxis[3] = {targetX - eyeX, targetY - eyeY, targetZ - eyeZ};
    VectorNormalize3(zAxis);

    float xAxis[3] = {};
    VectorCross3(up, zAxis, xAxis);
    VectorNormalize3(xAxis);

    float yAxis[3] = {};
    VectorCross3(zAxis, xAxis, yAxis);

    MatrixIdentity(out);
    out[0] = xAxis[0];
    out[4] = xAxis[1];
    out[8] = xAxis[2];
    out[12] = -VectorDot3(xAxis, eye);
    out[1] = yAxis[0];
    out[5] = yAxis[1];
    out[9] = yAxis[2];
    out[13] = -VectorDot3(yAxis, eye);
    out[2] = zAxis[0];
    out[6] = zAxis[1];
    out[10] = zAxis[2];
    out[14] = -VectorDot3(zAxis, eye);
}

void MatrixRotationX(float radians, float* out) {
    MatrixIdentity(out);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    out[5] = c;
    out[6] = s;
    out[9] = -s;
    out[10] = c;
}

void MatrixRotationY(float radians, float* out) {
    MatrixIdentity(out);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    out[0] = c;
    out[2] = -s;
    out[8] = s;
    out[10] = c;
}

CGFloat BackingScaleForView(NSView* view) {
    if (view && view.window && view.window.screen) {
        return view.window.screen.backingScaleFactor;
    }
    NSScreen* screen = [NSScreen mainScreen];
    return screen ? screen.backingScaleFactor : 1.0;
}

id<MTLTexture> CreateProceduralBaseColorTexture(id<MTLDevice> device) {
    constexpr int kTextureSize = 128;
    constexpr int kBytesPerPixel = 4;
    uint8_t pixels[kTextureSize * kTextureSize * kBytesPerPixel] = {};

    for (int y = 0; y < kTextureSize; ++y) {
        for (int x = 0; x < kTextureSize; ++x) {
            const bool checker = (((x / 16) ^ (y / 16)) & 1) != 0;
            const bool grid = (x % 16 == 0) || (y % 16 == 0);
            const uint8_t grain = static_cast<uint8_t>((x * 17 + y * 29) & 0x0f);

            uint8_t r = checker ? 212 : 84;
            uint8_t g = checker ? 224 : 100;
            uint8_t b = checker ? 210 : 132;

            if (grid) {
                r = static_cast<uint8_t>(std::min<int>(255, r + 34));
                g = static_cast<uint8_t>(std::min<int>(255, g + 34));
                b = static_cast<uint8_t>(std::min<int>(255, b + 34));
            }

            const int offset = (y * kTextureSize + x) * kBytesPerPixel;
            pixels[offset + 0] = static_cast<uint8_t>(std::min<int>(255, r + grain));
            pixels[offset + 1] = static_cast<uint8_t>(std::min<int>(255, g + grain));
            pixels[offset + 2] = static_cast<uint8_t>(std::min<int>(255, b + grain));
            pixels[offset + 3] = 255;
        }
    }

    MTLTextureDescriptor* textureDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:kTextureSize
                                                          height:kTextureSize
                                                       mipmapped:NO];
    textureDesc.storageMode = MTLStorageModeShared;
    textureDesc.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> texture = [device newTextureWithDescriptor:textureDesc];
    if (!texture) {
        return nil;
    }

    [texture replaceRegion:MTLRegionMake2D(0, 0, kTextureSize, kTextureSize)
               mipmapLevel:0
                 withBytes:pixels
               bytesPerRow:kTextureSize * kBytesPerPixel];
    return texture;
}

} // namespace

struct MetalRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    CAMetalLayer* layer = nil;
    id<CAMetalDrawable> drawable = nil;
    id<MTLCommandBuffer> commandBuffer = nil;
    MTLRenderPassDescriptor* passDescriptor = nil;
    id<MTLLibrary> shaderLibrary = nil;
    id<MTLRenderPipelineState> pipelineState = nil;
    id<MTLDepthStencilState> depthState = nil;
    id<MTLBuffer> vertexBuffer = nil;
    id<MTLBuffer> indexBuffer = nil;
    id<MTLBuffer> uniformBuffer = nil;
    id<MTLTexture> materialTexture = nil;
    id<MTLSamplerState> materialSampler = nil;
    id<MTLTexture> depthTexture = nil;
    MTLPixelFormat colorFormat = MTLPixelFormatBGRA8Unorm;
    MTLPixelFormat depthFormat = MTLPixelFormatDepth32Float;
};

MetalRenderer::MetalRenderer()
    : impl_(new Impl())
    , window_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false)
    , frameActive_(false)
    , time_(0.0f) {}

MetalRenderer::~MetalRenderer() {
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool MetalRenderer::Initialize(Window* window) {
    if (!window || !window->GetNativeHandle()) {
        NEXT_LOG_ERROR("Invalid window for Metal renderer");
        return false;
    }

    @autoreleasepool {
        window_ = window;
        width_ = window->GetWidth();
        height_ = window->GetHeight();

        impl_->device = MTLCreateSystemDefaultDevice();
        if (!impl_->device) {
            NEXT_LOG_ERROR("Metal is not supported on this device");
            return false;
        }

        impl_->queue = [impl_->device newCommandQueue];
        if (!impl_->queue) {
            NEXT_LOG_ERROR("Failed to create Metal command queue");
            return false;
        }

        NSView* view = (__bridge NSView*)window->GetNativeHandle();
        [view setWantsLayer:YES];

        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.device = impl_->device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.opaque = YES;
        view.layer = layer;
        impl_->layer = layer;

        NSError* shaderError = nil;
        NSString* shaderSource = [NSString stringWithUTF8String:kMetalShaderSource];
        impl_->shaderLibrary = [impl_->device newLibraryWithSource:shaderSource options:nil error:&shaderError];
        if (!impl_->shaderLibrary) {
            NEXT_LOG_ERROR("Failed to compile Metal shaders: %s",
                           shaderError ? [[shaderError localizedDescription] UTF8String] : "unknown error");
            return false;
        }

        id<MTLFunction> vertexFunction = [impl_->shaderLibrary newFunctionWithName:@"vertex_main"];
        id<MTLFunction> fragmentFunction = [impl_->shaderLibrary newFunctionWithName:@"fragment_main"];
        if (!vertexFunction || !fragmentFunction) {
            NEXT_LOG_ERROR("Failed to resolve Metal shader entry points");
            return false;
        }

        MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDesc.vertexFunction = vertexFunction;
        pipelineDesc.fragmentFunction = fragmentFunction;
        pipelineDesc.colorAttachments[0].pixelFormat = impl_->colorFormat;
        pipelineDesc.depthAttachmentPixelFormat = impl_->depthFormat;

        NSError* pipelineError = nil;
        impl_->pipelineState = [impl_->device newRenderPipelineStateWithDescriptor:pipelineDesc error:&pipelineError];
        if (!impl_->pipelineState) {
            NEXT_LOG_ERROR("Failed to create Metal render pipeline: %s",
                           pipelineError ? [[pipelineError localizedDescription] UTF8String] : "unknown error");
            return false;
        }

        MTLDepthStencilDescriptor* depthDesc = [[MTLDepthStencilDescriptor alloc] init];
        depthDesc.depthCompareFunction = MTLCompareFunctionLess;
        depthDesc.depthWriteEnabled = YES;
        impl_->depthState = [impl_->device newDepthStencilStateWithDescriptor:depthDesc];
        if (!impl_->depthState) {
            NEXT_LOG_ERROR("Failed to create Metal depth state");
            return false;
        }

        impl_->vertexBuffer = [impl_->device newBufferWithBytes:kCubeVertices
                                                         length:sizeof(kCubeVertices)
                                                        options:MTLResourceStorageModeShared];
        impl_->indexBuffer = [impl_->device newBufferWithBytes:kCubeIndices
                                                        length:sizeof(kCubeIndices)
                                                       options:MTLResourceStorageModeShared];
        impl_->uniformBuffer = [impl_->device newBufferWithLength:kMetalUniformStride * (kMaxRendererDebugCells + 1)
                                                           options:MTLResourceStorageModeShared];
        if (!impl_->vertexBuffer || !impl_->indexBuffer || !impl_->uniformBuffer) {
            NEXT_LOG_ERROR("Failed to create Metal cube buffers");
            return false;
        }

        impl_->materialTexture = CreateProceduralBaseColorTexture(impl_->device);
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
        samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
        samplerDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        samplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
        samplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;
        samplerDesc.maxAnisotropy = 4;
        impl_->materialSampler = [impl_->device newSamplerStateWithDescriptor:samplerDesc];
        if (!impl_->materialTexture || !impl_->materialSampler) {
            NEXT_LOG_ERROR("Failed to create Metal material texture resources");
            return false;
        }

        Resize(width_, height_);
        window_->SetResizeCallback([this](int w, int h) { Resize(w, h); });

        initialized_ = true;
        NEXT_LOG_INFO("Metal renderer initialized (%dx%d)", width_, height_);
        return true;
    }
}

void MetalRenderer::Shutdown() {
    if (!initialized_ && !impl_->device && !impl_->queue && !impl_->layer && !window_) {
        return;
    }

    @autoreleasepool {
        if (window_) {
            window_->SetResizeCallback({});
        }

        impl_->passDescriptor = nil;
        impl_->commandBuffer = nil;
        impl_->drawable = nil;
        impl_->depthTexture = nil;
        impl_->uniformBuffer = nil;
        impl_->materialSampler = nil;
        impl_->materialTexture = nil;
        impl_->indexBuffer = nil;
        impl_->vertexBuffer = nil;
        impl_->depthState = nil;
        impl_->pipelineState = nil;
        impl_->shaderLibrary = nil;
        impl_->layer = nil;
        impl_->queue = nil;
        impl_->device = nil;

        window_ = nullptr;
        width_ = 0;
        height_ = 0;
        frameActive_ = false;
        initialized_ = false;

        NEXT_LOG_INFO("Metal renderer shutdown complete");
    }
}

void MetalRenderer::SetFrameDesc(const RendererFrameDesc& frame) {
    frameDesc_ = frame;
}

void MetalRenderer::BeginFrame() {
    if (!initialized_ || frameActive_) {
        return;
    }

    @autoreleasepool {
        impl_->drawable = [impl_->layer nextDrawable];
        if (!impl_->drawable) {
            NEXT_LOG_WARNING("Metal layer did not provide a drawable this frame");
            return;
        }

        impl_->commandBuffer = [impl_->queue commandBuffer];
        impl_->passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        impl_->passDescriptor.colorAttachments[0].texture = impl_->drawable.texture;
        impl_->passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        impl_->passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

        const double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(time_) * 0.7);
        impl_->passDescriptor.colorAttachments[0].clearColor =
            MTLClearColorMake(0.05, 0.09 + 0.08 * pulse, 0.13 + 0.12 * pulse, 1.0);
        impl_->passDescriptor.depthAttachment.texture = impl_->depthTexture;
        impl_->passDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
        impl_->passDescriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
        impl_->passDescriptor.depthAttachment.clearDepth = 1.0;

        frameActive_ = true;
    }
}

void MetalRenderer::Render() {
    if (!initialized_ || !frameActive_ || !impl_->commandBuffer || !impl_->passDescriptor) {
        return;
    }

    @autoreleasepool {
        MetalUniforms uniforms = {};
        float rotationX[16] = {};
        float rotationY[16] = {};
        float model[16] = {};
        float view[16] = {};
        float modelView[16] = {};
        float projection[16] = {};

        MatrixRotationX(time_ * 0.65f, rotationX);
        MatrixRotationY(time_, rotationY);
        MatrixMultiply(rotationY, rotationX, model);
        MatrixLookAt(frameDesc_.cameraPosition[0],
                     frameDesc_.cameraPosition[1],
                     frameDesc_.cameraPosition[2],
                     frameDesc_.cameraTarget[0],
                     frameDesc_.cameraTarget[1],
                     frameDesc_.cameraTarget[2],
                     frameDesc_.cameraUp,
                     view);
        MatrixMultiply(view, model, modelView);

        const float aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
        MatrixPerspective(60.0f * 3.1415926535f / 180.0f, aspect, 0.1f, 2000.0f, projection);
        MatrixMultiply(projection, modelView, uniforms.mvp);
        std::memcpy(uniforms.model, model, sizeof(model));
        uniforms.lightDirection[0] = -0.35f;
        uniforms.lightDirection[1] = -0.75f;
        uniforms.lightDirection[2] = -0.55f;
        uniforms.lightDirection[3] = 0.0f;
        uniforms.cameraPosition[0] = frameDesc_.cameraPosition[0];
        uniforms.cameraPosition[1] = frameDesc_.cameraPosition[1];
        uniforms.cameraPosition[2] = frameDesc_.cameraPosition[2];
        uniforms.cameraPosition[3] = 1.0f;
        uniforms.material[0] = 0.42f; // roughness
        uniforms.material[1] = 0.05f; // metallic
        uniforms.material[2] = 1.25f; // exposure
        uniforms.material[3] = 0.0f;
        uniforms.ambientColor[0] = 0.05f;
        uniforms.ambientColor[1] = 0.06f;
        uniforms.ambientColor[2] = 0.08f;
        uniforms.ambientColor[3] = 1.0f;
        uniforms.debugTint[0] = 1.0f;
        uniforms.debugTint[1] = 1.0f;
        uniforms.debugTint[2] = 1.0f;
        uniforms.debugTint[3] = 1.0f;

        uint8_t* uniformBytes = static_cast<uint8_t*>([impl_->uniformBuffer contents]);
        std::memcpy(uniformBytes, &uniforms, sizeof(uniforms));

        id<MTLRenderCommandEncoder> encoder =
            [impl_->commandBuffer renderCommandEncoderWithDescriptor:impl_->passDescriptor];
        [encoder setRenderPipelineState:impl_->pipelineState];
        [encoder setDepthStencilState:impl_->depthState];
        [encoder setCullMode:MTLCullModeBack];
        [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [encoder setVertexBuffer:impl_->vertexBuffer offset:0 atIndex:0];
        [encoder setVertexBuffer:impl_->uniformBuffer offset:0 atIndex:1];
        [encoder setFragmentBuffer:impl_->uniformBuffer offset:0 atIndex:1];
        [encoder setFragmentTexture:impl_->materialTexture atIndex:0];
        [encoder setFragmentSamplerState:impl_->materialSampler atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:sizeof(kCubeIndices) / sizeof(kCubeIndices[0])
                             indexType:MTLIndexTypeUInt16
                            indexBuffer:impl_->indexBuffer
                      indexBufferOffset:0];

        const size_t debugCellCount = std::min(frameDesc_.debugCells.size(), kMaxRendererDebugCells);
        for (size_t i = 0; i < debugCellCount; ++i) {
            const RendererDebugCell& cell = frameDesc_.debugCells[i];
            const bool placeholder = (cell.flags & kRendererDebugCellPlaceholder) != 0;
            const float halfSize = std::max(1.0f, cell.size) * 0.47f;

            float scale[16] = {};
            float translation[16] = {};
            MetalUniforms cellUniforms = uniforms;

            MatrixScale(halfSize, 0.08f, halfSize, scale);
            MatrixTranslation(cell.center[0], cell.center[1] - 1.0f, cell.center[2], translation);
            MatrixMultiply(translation, scale, cellUniforms.model);
            MatrixMultiply(view, cellUniforms.model, modelView);
            MatrixMultiply(projection, modelView, cellUniforms.mvp);

            cellUniforms.material[0] = 0.85f;
            cellUniforms.material[1] = 0.0f;
            cellUniforms.material[2] = 1.4f;
            cellUniforms.debugTint[0] = placeholder ? 1.0f : 0.14f;
            cellUniforms.debugTint[1] = placeholder ? 0.64f : 0.86f;
            cellUniforms.debugTint[2] = placeholder ? 0.18f : 0.58f;
            cellUniforms.debugTint[3] = 1.0f;

            const NSUInteger uniformOffset = static_cast<NSUInteger>((i + 1) * kMetalUniformStride);
            std::memcpy(uniformBytes + uniformOffset, &cellUniforms, sizeof(cellUniforms));
            [encoder setVertexBuffer:impl_->uniformBuffer offset:uniformOffset atIndex:1];
            [encoder setFragmentBuffer:impl_->uniformBuffer offset:uniformOffset atIndex:1];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:sizeof(kCubeIndices) / sizeof(kCubeIndices[0])
                                 indexType:MTLIndexTypeUInt16
                                indexBuffer:impl_->indexBuffer
                          indexBufferOffset:0];
        }
        [encoder endEncoding];
    }
}

void MetalRenderer::EndFrame() {
    if (!initialized_ || !frameActive_) {
        return;
    }

    @autoreleasepool {
        if (impl_->commandBuffer && impl_->drawable) {
            [impl_->commandBuffer presentDrawable:impl_->drawable];
            [impl_->commandBuffer commit];
        }

        impl_->passDescriptor = nil;
        impl_->commandBuffer = nil;
        impl_->drawable = nil;
        frameActive_ = false;
        time_ += 1.0f / 60.0f;
    }
}

void MetalRenderer::Resize(int width, int height) {
    if (!impl_ || !impl_->layer || width <= 0 || height <= 0) {
        return;
    }

    @autoreleasepool {
        width_ = width;
        height_ = height;

        NSView* view = window_ ? (__bridge NSView*)window_->GetNativeHandle() : nil;
        const CGFloat scale = BackingScaleForView(view);
        const NSUInteger drawableWidth = static_cast<NSUInteger>(std::max<CGFloat>(1.0, width_ * scale));
        const NSUInteger drawableHeight = static_cast<NSUInteger>(std::max<CGFloat>(1.0, height_ * scale));

        impl_->layer.contentsScale = scale;
        impl_->layer.drawableSize = CGSizeMake(drawableWidth, drawableHeight);

        MTLTextureDescriptor* depthDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:impl_->depthFormat
                                                               width:drawableWidth
                                                              height:drawableHeight
                                                           mipmapped:NO];
        depthDesc.usage = MTLTextureUsageRenderTarget;
        depthDesc.storageMode = MTLStorageModePrivate;
        impl_->depthTexture = [impl_->device newTextureWithDescriptor:depthDesc];
        if (!impl_->depthTexture) {
            NEXT_LOG_ERROR("Failed to create Metal depth texture");
        }

        NEXT_LOG_DEBUG("Metal drawable resized to %.0fx%.0f",
                       impl_->layer.drawableSize.width,
                       impl_->layer.drawableSize.height);
    }
}

} // namespace Next
