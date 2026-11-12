// A swapchain layer between ReShade and Streamline's proxy: the game, RenoDX and ReShade see the
// FP16 scRGB swapchain the game asks for (back buffers of our own), Streamline and DLSS Frame
// Generation see an HDR10 (rgb10a2, ST.2084) one, and each Present converts. DLSS-G generated
// nothing on The Callisto Protocol's scRGB swapchain (docs/FINDINGS.md). In SDR it passes through.
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

namespace rd::hdrswap {

// The format the real (Streamline) swapchain gets for the one the game asks for.
DXGI_FORMAT RealFormat(DXGI_FORMAT wanted);

// Wraps Streamline's proxy swapchain <inner> (this takes over the caller's reference), created on
// the native command queue <queue> with the game's format <wanted>. Returns the wrapper (one reference).
IDXGISwapChain* Wrap(IDXGISwapChain* inner, ID3D12CommandQueue* queue, DXGI_FORMAT wanted);

}  // namespace rd::hdrswap
