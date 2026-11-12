// scRGB -> HDR10 swapchain layer for DLSS Frame Generation (hdrswap.hpp).
//
// Order of layers: game -> ReShade's swapchain -> this -> Streamline's proxy -> real swapchain.
// While the game's back buffers are rgba16f (its HDR mode), Streamline's swapchain is rgb10a2 with
// the ST.2084 colour space, the game renders into rgba16f textures of our own (handed out by
// GetBuffer, indexed like Streamline's buffers), and Present converts the current one into
// Streamline's back buffer (hdr_convert.hlsl) on the swapchain's command queue first. Anything else
// is passed through. The queue is the native one (ReShade unwraps it before the factory call), so
// this work doesn't show up as ReShade add-on events.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "hdrswap.hpp"

#include "common.hpp"
#include "hdr_convert_ps.h"  // kHdrConvertPs (build.ps1)
#include "hdr_convert_vs.h"  // kHdrConvertVs

#include <utility>
#include <vector>

namespace rd::hdrswap {
namespace {

constexpr DXGI_FORMAT kGameHdr = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kRealHdr = DXGI_FORMAT_R10G10B10A2_UNORM;
constexpr int kRing = 8;  // command allocators in flight

template <typename T>
void SafeRelease(T*& p) {
    if (p) p->Release(), p = nullptr;
}

class HdrSwapChain final : public IDXGISwapChain4 {
public:
    HdrSwapChain(IDXGISwapChain4* inner, ID3D12CommandQueue* queue, DXGI_FORMAT wanted) : inner_(inner), queue_(queue) {
        queue_->AddRef();
        queue_->GetDevice(IID_PPV_ARGS(&device_));
        game_ = wanted;
        Rebuild();
    }

    // ------------------------------------------------------------ IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
            riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) ||
            riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4)) {
            *ppv = static_cast<IDXGISwapChain4*>(this);
            AddRef();
            return S_OK;
        }
        return inner_->QueryInterface(riid, ppv);  // private interfaces (Streamline's, the driver's)
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = InterlockedDecrement(&refs_);
        if (n == 0) {
            WaitIdle();
            ReleaseBuffers();
            for (int i = 0; i < kRing; ++i) SafeRelease(alloc_[i]);
            SafeRelease(list_), SafeRelease(fence_), SafeRelease(pso_), SafeRelease(root_);
            SafeRelease(srvHeap_), SafeRelease(rtvHeap_);
            if (event_) CloseHandle(event_);
            SafeRelease(device_), SafeRelease(queue_);
            IDXGISwapChain4* inner = inner_;
            inner_ = nullptr;
            delete this;
            inner->Release();
        }
        return n;
    }

    // ------------------------------------------------------------ IDXGIObject / DeviceSubObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID n, UINT s, const void* d) override { return inner_->SetPrivateData(n, s, d); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID n, const IUnknown* u) override {
        return inner_->SetPrivateDataInterface(n, u);
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID n, UINT* s, void* d) override { return inner_->GetPrivateData(n, s, d); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID r, void** p) override { return inner_->GetParent(r, p); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID r, void** p) override { return inner_->GetDevice(r, p); }

    // ------------------------------------------------------------ IDXGISwapChain
    HRESULT STDMETHODCALLTYPE Present(UINT sync, UINT flags) override {
        if (Converting() && !(flags & DXGI_PRESENT_TEST)) Convert();
        return inner_->Present(sync, flags);
    }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT i, REFIID r, void** p) override {
        if (!Converting()) return inner_->GetBuffer(i, r, p);
        if (i >= ours_.size() || !ours_[i]) return DXGI_ERROR_INVALID_CALL;
        return ours_[i]->QueryInterface(r, p);
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL f, IDXGIOutput* o) override { return inner_->SetFullscreenState(f, o); }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* f, IDXGIOutput** o) override { return inner_->GetFullscreenState(f, o); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* d) override {
        const HRESULT hr = inner_->GetDesc(d);
        if (SUCCEEDED(hr) && d && Converting()) d->BufferDesc.Format = game_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) override {
        BeforeResize();
        const DXGI_FORMAT want = fmt == DXGI_FORMAT_UNKNOWN ? game_ : fmt;
        const HRESULT hr = inner_->ResizeBuffers(count, w, h, fmt == DXGI_FORMAT_UNKNOWN ? fmt : RealFormat(fmt), flags);
        AfterResize(hr, want);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* m) override { return inner_->ResizeTarget(m); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** o) override { return inner_->GetContainingOutput(o); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* s) override { return inner_->GetFrameStatistics(s); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* c) override { return inner_->GetLastPresentCount(c); }

    // ------------------------------------------------------------ IDXGISwapChain1
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* d) override {
        const HRESULT hr = inner_->GetDesc1(d);
        if (SUCCEEDED(hr) && d && Converting()) d->Format = game_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* d) override { return inner_->GetFullscreenDesc(d); }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* h) override { return inner_->GetHwnd(h); }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID r, void** p) override { return inner_->GetCoreWindow(r, p); }
    HRESULT STDMETHODCALLTYPE Present1(UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) override {
        if (Converting() && !(flags & DXGI_PRESENT_TEST)) Convert();
        return inner_->Present1(sync, flags, p);
    }
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return inner_->IsTemporaryMonoSupported(); }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** o) override { return inner_->GetRestrictToOutput(o); }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* c) override { return inner_->SetBackgroundColor(c); }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* c) override { return inner_->GetBackgroundColor(c); }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION r) override { return inner_->SetRotation(r); }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* r) override { return inner_->GetRotation(r); }

    // ------------------------------------------------------------ IDXGISwapChain2
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT w, UINT h) override { return inner_->SetSourceSize(w, h); }
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* w, UINT* h) override { return inner_->GetSourceSize(w, h); }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT n) override { return inner_->SetMaximumFrameLatency(n); }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* n) override { return inner_->GetMaximumFrameLatency(n); }
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return inner_->GetFrameLatencyWaitableObject(); }
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* m) override { return inner_->SetMatrixTransform(m); }
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* m) override { return inner_->GetMatrixTransform(m); }

    // ------------------------------------------------------------ IDXGISwapChain3
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return inner_->GetCurrentBackBufferIndex(); }
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE cs, UINT* support) override {
        // scRGB can always be offered: it's converted to HDR10, which is what's asked of the real one.
        if (cs == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) cs = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        return inner_->CheckColorSpaceSupport(cs, support);
    }
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE cs) override {
        gameCs_ = cs;
        if (!Converting()) return inner_->SetColorSpace1(cs);
        const HRESULT hr = inner_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
        Log("HDR for frame generation: the game's colour space %u -> HDR10 (ST.2084) for Streamline: 0x%08lx",
            static_cast<unsigned>(cs), static_cast<unsigned long>(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags, const UINT* nodes,
                                             IUnknown* const* queues) override {
        BeforeResize();
        const DXGI_FORMAT want = fmt == DXGI_FORMAT_UNKNOWN ? game_ : fmt;
        const HRESULT hr =
            inner_->ResizeBuffers1(count, w, h, fmt == DXGI_FORMAT_UNKNOWN ? fmt : RealFormat(fmt), flags, nodes, queues);
        AfterResize(hr, want);
        return hr;
    }

    // ------------------------------------------------------------ IDXGISwapChain4
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE t, UINT s, void* d) override {
        return inner_->SetHDRMetaData(t, s, d);
    }

private:
    bool Converting() const { return game_ == kGameHdr && ok_; }

    void WaitIdle() {
        if (!fence_ || !queue_) return;
        const UINT64 v = ++fenceValue_;
        if (FAILED(queue_->Signal(fence_, v))) return;
        if (fence_->GetCompletedValue() < v && SUCCEEDED(fence_->SetEventOnCompletion(v, event_)))
            WaitForSingleObject(event_, 5000);
    }

    void ReleaseBuffers() {
        for (ID3D12Resource*& r : ours_) SafeRelease(r);
        for (ID3D12Resource*& r : real_) SafeRelease(r);
        ours_.clear(), real_.clear();
    }

    void BeforeResize() {
        WaitIdle();  // our conversions may still be in flight; the game waited for its own work
        ReleaseBuffers();  // Streamline's buffers must have no references left for the resize
    }

    void AfterResize(HRESULT hr, DXGI_FORMAT want) {
        if (SUCCEEDED(hr)) game_ = want;
        Rebuild();
        if (SUCCEEDED(hr) && game_ == kGameHdr && gameCs_ == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
            inner_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
    }

    bool CreatePipeline() {
        if (pso_) return true;
        D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable = {1, &range};
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs = {1, &param, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ID3DBlob* blob = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr))) return false;
        const HRESULT hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_));
        blob->Release();
        if (FAILED(hr)) return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = root_;
        d.VS = {kHdrConvertVs, sizeof(kHdrConvertVs)};
        d.PS = {kHdrConvertPs, sizeof(kHdrConvertPs)};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = kRealHdr;
        d.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso_)))) return false;
        for (int i = 0; i < kRing; ++i)
            if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc_[i])))) return false;
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_[0], nullptr, IID_PPV_ARGS(&list_))))
            return false;
        list_->Close();
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return event_ != nullptr;
    }

    // After creation and every resize: our rgba16f back buffers (HDR) and the views.
    void Rebuild() {
        ok_ = false;
        if (game_ != kGameHdr) return;
        if (!device_ || !CreatePipeline()) {
            Log("HDR for frame generation: couldn't create the conversion pipeline: scRGB passed through");
            return;
        }
        DXGI_SWAP_CHAIN_DESC1 d = {};
        if (FAILED(inner_->GetDesc1(&d)) || !d.BufferCount) return;
        SafeRelease(srvHeap_), SafeRelease(rtvHeap_);
        D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, d.BufferCount,
                                         D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap_)))) return;
        hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, d.BufferCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap_)))) return;
        srvStep_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        rtvStep_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        ours_.assign(d.BufferCount, nullptr);
        real_.assign(d.BufferCount, nullptr);
        D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC td = {};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = d.Width, td.Height = d.Height, td.DepthOrArraySize = 1, td.MipLevels = 1;
        td.Format = kGameHdr;
        td.SampleDesc.Count = 1;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        for (UINT i = 0; i < d.BufferCount; ++i) {
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COMMON,
                                                         nullptr, IID_PPV_ARGS(&ours_[i]))))
                return ReleaseBuffers();
            ours_[i]->SetName(L"CallistoDLSS scRGB back buffer");
            D3D12_CPU_DESCRIPTOR_HANDLE h = srvHeap_->GetCPUDescriptorHandleForHeapStart();
            h.ptr += static_cast<SIZE_T>(i) * srvStep_;
            device_->CreateShaderResourceView(ours_[i], nullptr, h);
            if (FAILED(inner_->GetBuffer(i, IID_PPV_ARGS(&real_[i])))) return ReleaseBuffers();
            D3D12_CPU_DESCRIPTOR_HANDLE r = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
            r.ptr += static_cast<SIZE_T>(i) * rtvStep_;
            device_->CreateRenderTargetView(real_[i], nullptr, r);
        }
        width_ = d.Width, height_ = d.Height;
        ok_ = true;
        Log("HDR for frame generation: %u scRGB back buffers %ux%u for the game, Streamline's are HDR10 (format %u)",
            d.BufferCount, d.Width, d.Height, static_cast<unsigned>(d.Format));
    }

    // Our current rgba16f buffer -> Streamline's current rgb10a2 one, on the swapchain's queue.
    void Convert() {
        const UINT i = inner_->GetCurrentBackBufferIndex();
        if (i >= ours_.size() || !ours_[i] || !real_[i]) return;
        const int k = static_cast<int>(frame_++ % kRing);
        if (fence_->GetCompletedValue() < used_[k] && SUCCEEDED(fence_->SetEventOnCompletion(used_[k], event_)))
            WaitForSingleObject(event_, 1000);
        if (FAILED(alloc_[k]->Reset()) || FAILED(list_->Reset(alloc_[k], pso_))) return;
        D3D12_RESOURCE_BARRIER b[2] = {};
        b[0].Type = b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[0].Transition = {ours_[i], D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT,
                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        b[1].Transition = {real_[i], D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT,
                           D3D12_RESOURCE_STATE_RENDER_TARGET};
        list_->ResourceBarrier(2, b);
        list_->SetGraphicsRootSignature(root_);
        list_->SetDescriptorHeaps(1, &srvHeap_);
        D3D12_GPU_DESCRIPTOR_HANDLE g = srvHeap_->GetGPUDescriptorHandleForHeapStart();
        g.ptr += static_cast<UINT64>(i) * srvStep_;
        list_->SetGraphicsRootDescriptorTable(0, g);
        D3D12_CPU_DESCRIPTOR_HANDLE r = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        r.ptr += static_cast<SIZE_T>(i) * rtvStep_;
        list_->OMSetRenderTargets(1, &r, FALSE, nullptr);
        const D3D12_VIEWPORT vp = {0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
        const D3D12_RECT sc = {0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
        list_->RSSetViewports(1, &vp);
        list_->RSSetScissorRects(1, &sc);
        list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list_->DrawInstanced(3, 1, 0, 0);
        for (D3D12_RESOURCE_BARRIER& x : b) std::swap(x.Transition.StateBefore, x.Transition.StateAfter);
        list_->ResourceBarrier(2, b);
        if (FAILED(list_->Close())) return;
        ID3D12CommandList* lists[] = {list_};
        queue_->ExecuteCommandLists(1, lists);
        used_[k] = ++fenceValue_;
        queue_->Signal(fence_, used_[k]);
        if (++converted_ == 1) Log("HDR for frame generation: first frame converted to HDR10");
    }

    volatile LONG refs_ = 1;
    IDXGISwapChain4* inner_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12Device* device_ = nullptr;
    DXGI_FORMAT game_ = DXGI_FORMAT_UNKNOWN;  // the back buffer format the game asked for
    DXGI_COLOR_SPACE_TYPE gameCs_ = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    bool ok_ = false;
    UINT width_ = 0, height_ = 0;
    std::vector<ID3D12Resource*> ours_, real_;
    ID3D12DescriptorHeap *srvHeap_ = nullptr, *rtvHeap_ = nullptr;
    UINT srvStep_ = 0, rtvStep_ = 0;
    ID3D12RootSignature* root_ = nullptr;
    ID3D12PipelineState* pso_ = nullptr;
    ID3D12CommandAllocator* alloc_[kRing] = {};
    ID3D12GraphicsCommandList* list_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    HANDLE event_ = nullptr;
    UINT64 fenceValue_ = 0, used_[kRing] = {}, frame_ = 0, converted_ = 0;
};

}  // namespace

DXGI_FORMAT RealFormat(DXGI_FORMAT wanted) { return wanted == kGameHdr ? kRealHdr : wanted; }

IDXGISwapChain* Wrap(IDXGISwapChain* inner, ID3D12CommandQueue* queue, DXGI_FORMAT wanted) {
    IDXGISwapChain4* sc4 = nullptr;
    if (!inner || !queue || FAILED(inner->QueryInterface(IID_PPV_ARGS(&sc4)))) {
        Log("HDR for frame generation: the swapchain has no IDXGISwapChain4: not wrapped");
        return inner;
    }
    inner->Release();  // the caller's reference becomes sc4's
    Log("HDR for frame generation: swapchain layer in place (game's format %u)", static_cast<unsigned>(wanted));
    return new HdrSwapChain(sc4, queue, wanted);
}

}  // namespace rd::hdrswap
