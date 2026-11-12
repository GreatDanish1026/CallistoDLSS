# CallistoDLSS 0.0.9 (work in progress)

ReShade add-on for **The Callisto Protocol** (Unreal Engine 4.27, DirectX 12) that replaces the
game's own TAA with **NVIDIA DLSS** or **AMD FSR** upscaling, and adds **DLSS Frame Generation**
or **FSR Frame Generation**. The game itself only offers FSR 2.1. Forked from StrayDLSS 1.1.0.

Needs ReShade 6.8 with add-on support and the game's DirectX 12 mode with TAA anti-aliasing (any
other anti-aliasing setting also turns off the game's ray tracing).

## What works (0.0.9)

- **DLSS / FSR upscaling** in place of the game's seven-pass TAA (its final resolve pass), Native
  (DLAA) to Ultra Performance. The game's FSR 2 plugin is held off while the add-on is on.
- **DLSS Frame Generation, in SDR and in HDR.** The game's HDR swapchain is FP16 scRGB, on which
  DLSS-G generates nothing; the add-on puts a swapchain layer between ReShade and Streamline that
  keeps the game (and RenoDX) on scRGB and hands DLSS-G a 10-bit HDR10 copy of each frame.
- Works alongside RenoDX's Callisto add-on.

## Not done yet

- DLSS Ray Reconstruction (the game ray traces shadows and reflections).
- Skipping the game's TAA passes that DLSS makes redundant.
- Debug leftovers (verbose Streamline log, capture detail, bisect switch) and a proper README.

## Build and install

`.\build.ps1 -Install` (game closed) builds with MSVC and copies the add-on, NVIDIA's DLSS and
Streamline runtime and AMD's FSR runtime next to `TheCallistoProtocol-Win64-Shipping.exe`. The
NVIDIA and AMD DLLs aren't in this repository: see the top of `build.ps1` for where they come from.

Findings, pass layouts and test results: [docs/FINDINGS.md](docs/FINDINGS.md).
