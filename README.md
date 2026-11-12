# CallistoDLSS 1.0.0

ReShade add-on for **The Callisto Protocol** (Unreal Engine 4.27, DirectX 12) that replaces the
game's own TAA with **NVIDIA DLSS** (optionally **DLSS Ray Reconstruction**) or **AMD FSR**, and
adds **DLSS Frame Generation** or **FSR Frame Generation**, in SDR and in HDR. The game itself only
offers FSR 2.1.

Needs the game's **DirectX 12** mode with **TAA** anti-aliasing (any other anti-aliasing setting
also switches off the game's ray tracing and its upscaler) and ReShade 6.8 with add-on support.

| | Graphics card |
|---|---|
| DLSS upscaling | NVIDIA RTX |
| DLSS Ray Reconstruction | NVIDIA RTX |
| FSR upscaling | any DirectX 12 card: FSR 4 on Radeon RX 6000 and newer, FSR 3.1 elsewhere |
| DLSS Frame Generation | NVIDIA RTX 40 or 50 (3x, 4x and Dynamic: RTX 50), hardware-accelerated GPU scheduling |
| FSR Frame Generation | any DirectX 12 card: FSR 4 on Radeon RX 9000 under Windows 11, FSR 3.1 elsewhere |

Upscaler and frame generation can be mixed (for example DLSS upscaling with FSR Frame Generation on
an RTX 30 card).

## What it does

- **Upscaler** (F9 to switch on and off): Auto (DLSS on NVIDIA RTX, FSR elsewhere), DLSS or FSR in
  place of the game's TAA. The game has a TAA of its own in seven passes; the upscaler takes its
  final pass, and while it does, the game's earlier TAA passes are skipped (they'd be wasted work).
  The game's FSR 2 is switched off while the add-on is on and comes back when it's off. DLSS Super
  Resolution 310.9.16; FSR through AMD's FSR SDK 2.4.0. Motion vectors are made by the add-on from
  the depth buffer and the camera, plus the engine's own motion of moving objects.
- **Render resolution**: Native/DLAA, Quality, Balanced, Performance or Ultra Performance (the
  engine's screen percentage, given back when the add-on is off).
- **DLSS model**: Automatic (default: K, M at Performance, L at Ultra Performance), or K, J, L or M
  at every resolution, or NVIDIA's default. **FSR sharpness**: 0-1.
- **Ray Reconstruction** (DLSS only, off by default): one AI model denoises the game's ray-traced
  shadows and reflections and upscales, in place of the game's denoisers and DLSS Super Resolution.
  The game's reflections have no denoiser of their own, so they gain the most. The game's ray
  tracing denoisers are switched off while it runs and come back when it's off. Needs the game's
  ray tracing on; costs a little more than Super Resolution. **Ray Reconstruction model**: NVIDIA's
  default, D, E or F. The surface pictures it gets from the game (colour, reflectivity, normals,
  roughness) can be shown from the Advanced tab.
- **Frame generation**: Off, DLSS 2x / 3x / 4x / Dynamic (NVIDIA Streamline 2.14.16, with Reflex),
  or FSR 2x. Runs in game, not in menus. Switching it on, or between DLSS and FSR, takes a game
  restart; DLSS's multipliers switch right away.
- **HDR with DLSS Frame Generation**: the game's HDR output is 16-bit floating point (scRGB), on
  which DLSS Frame Generation generates nothing. The add-on keeps the game, RenoDX and ReShade on
  scRGB and gives DLSS Frame Generation a 10-bit HDR10 copy of every frame (converted on the GPU
  just before it). In SDR nothing is converted.

Camera cuts keep the game's own TAA for that frame. HDR tone mapping isn't part of this add-on; the
RenoDX Callisto Protocol add-on works alongside it.

The overlay (ReShade's Add-ons tab) has **Settings**, **Status** and **Advanced** (testing tools,
frame capture) tabs. Settings are saved in `ReShade.ini` under `[CallistoDLSS]`.

## Install

1. Install ReShade 6.8 or later with full add-on support for
   `TheCallistoProtocol-Win64-Shipping.exe`, in `TheCallistoProtocol\Binaries\Win64`.
2. Copy everything from the release zip into that folder: `CallistoDLSS.addon64`,
   `nvngx_dlss.dll`, `nvngx_dlssd.dll`, the three `amd_fidelityfx_*_dx12.dll` files and the
   `streamline` folder (which also carries a copy of the two DLSS files: with frame generation on,
   NVIDIA's runtime looks for them there).
3. In the game: DirectX 12, anti-aliasing TAA, ray tracing as you like. Open ReShade (Home key),
   Add-ons tab, CallistoDLSS: switch the upscaler on (or press F9), pick DLSS or FSR and a render
   resolution and, if you like, Ray Reconstruction and frame generation (restart after picking
   frame generation).
4. A DLSS model override for the game in the NVIDIA App takes precedence over DLSS model (and a
   Ray Reconstruction override over Ray Reconstruction model).

Frame counters that hook the game's own presents (RTSS, for one) may not count generated frames;
the Steam or NVIDIA overlay does.

The zip includes NVIDIA's DLSS 310.9.16 (Super Resolution and Ray Reconstruction) and Streamline
2.14.16 runtime files, signed by NVIDIA (the add-on checks Streamline's signature before loading
it), and AMD's FSR SDK 2.4.0 runtime files, signed by AMD. Their licences are in `licenses`, and
attribution is in `THIRD_PARTY_NOTICES.txt`. To uninstall, delete those files. Logs:
`CallistoDLSS.log` next to the game's exe, Streamline's in `streamline\sl.log`.

Tested with the Epic Games Store version at 3840x2160 on an NVIDIA RTX 5080, HDR via RenoDX: DLSS
(DLAA, Quality, Balanced), DLSS Ray Reconstruction (ray-traced shadows and reflections), DLSS
Frame Generation 2x/3x in SDR and in HDR, FSR upscaling. FSR Frame Generation isn't tested in
this game yet.

## Build

`.\build.ps1` (Visual Studio 2022 C++ tools, Windows SDK for `fxc`). The runtimes aren't in the
repository: unzip `bin\x64` of NVIDIA's Streamline 2.14.16 release into
`third_party\streamline\bin\x64`, and copy `amd_fidelityfx_loader_dx12.dll`,
`amd_fidelityfx_upscaler_dx12.dll` and `amd_fidelityfx_framegeneration_dx12.dll` from
`Kits\FidelityFX\signedbin` of AMD's FSR SDK 2.4.0 into `third_party\fidelityfx\bin`.
`.\build.ps1 -Install` copies the add-on and the runtimes into the game folder (game closed);
`.\build.ps1 -Package` makes the release zip in `bin\release`.

## How it works

Forked from StrayDLSS 1.1.0 (UE 4.27). `docs/FINDINGS.md` has the details: the game's seven-pass
TAA and which pass is replaced, the View uniform buffer offsets (unchanged from UE 4.25), the
G-buffer behind Ray Reconstruction, why DLSS Frame Generation needed the HDR10 swapchain layer
(`src/hdrswap.cpp`), and the test results. The F10 frame capture (Advanced tab) writes two frames
of GPU commands to `CallistoDLSS-frame-N.txt`, for finding the TAA passes again after a game update.
