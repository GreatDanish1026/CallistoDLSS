# StrayDLSS 1.1.0

ReShade add-on for **Stray** (Unreal Engine 4.27, DirectX 12) that replaces the game's TAA
anti-aliasing with **NVIDIA DLSS** (optionally **DLSS Ray Reconstruction**) or **AMD FSR**, and
adds **DLSS Frame Generation** or **FSR Frame Generation**. The game has none of these. It's separate from StrayTweaks (graphics
settings, ray tracing), and the two can be installed side by side.

Needs the game's **DirectX 12** mode (Steam launch option `-dx12`) and ReShade 6.8 with add-on
support. On DirectX 12 the game ray traces by itself and runs much slower than on DirectX 11;
upscaling and frame generation win that back.

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
  place of the game's TAA. DLSS Super Resolution 310.9.16; FSR through AMD's FSR SDK 2.4.0, which
  picks FSR 4 or FSR 3.1 for the card. Motion vectors are computed by the add-on from the depth
  buffer and the camera, plus the engine's own motion of moving objects.
- **Render resolution**: Native/DLAA, Quality, Balanced, Performance or Ultra Performance. The game
  renders at 100% by itself, so "Game's" is Native. The lower modes set the engine's screen
  percentage while the upscaler is on and give the game's value back when it's off.
- **DLSS model**: Automatic (default: K, M at Performance, L at Ultra Performance, as NVIDIA recommends), or K, J, L or M (DLSS 4.5) at every resolution, or NVIDIA's default. **FSR sharpness**: 0-1.
- **Ray Reconstruction** (DLSS only, off by default): NVIDIA's DLSS Ray Reconstruction 310.9.16
  denoises the game's ray-traced effects and upscales in one step, in place of the game's ray
  tracing denoisers and DLSS Super Resolution: sharper, steadier ray-traced reflections, shadows
  and ambient occlusion. While it runs, the game's six ray tracing denoisers are switched off
  (they come back when it's off). It needs ray-traced effects on (StrayTweaks) to make a
  difference, and costs a little more than Super Resolution. **Ray Reconstruction model**:
  NVIDIA's default, D, E or F. A Ray Reconstruction override in the NVIDIA App takes precedence.
  The surface pictures it gets from the game (colour, reflectivity, normals, roughness) can be
  shown on screen from the Advanced tab. In the main menu the add-on uses Super Resolution;
  Ray Reconstruction starts with the game.
- **Frame generation**: Off, DLSS 2x / 3x / 4x / Dynamic (NVIDIA Streamline 2.14.16, with Reflex),
  or FSR 2x (AMD's frame generation swapchain; no Reflex, so a little more latency). Runs in game,
  not in menus. Switching it on, or between DLSS and FSR, takes a game restart; DLSS's multipliers
  switch right away. The game draws its HUD straight onto the screen, so frame generation has no
  HUD-free picture to work from: on-screen text can shimmer a little with frame generation.

Camera cuts keep the game's own TAA for that frame (the engine has no motion data then). HDR isn't
part of this add-on; the RenoDX Unreal Engine add-on works alongside it, and FSR Frame Generation
picks up HDR from the swapchain.

The overlay (ReShade's Add-ons tab) shows a summary of the upscaler and frame generation at the top,
with **Settings**, **Status** and **Advanced** (testing tools including FSR's, frame capture) tabs.
Settings are saved in `ReShade.ini` under `[StrayDLSS]`.

## Install

1. Install ReShade 6.8 or later with full add-on support for `Stray-Win64-Shipping.exe`, in
   `Stray\Hk_project\Binaries\Win64`.
2. Copy everything from the release zip into that folder: `StrayDLSS.addon64`, `nvngx_dlss.dll`,
   `nvngx_dlssd.dll` (Ray Reconstruction), the three `amd_fidelityfx_*_dx12.dll` files and the `streamline` folder (the licence files can
   go along).
3. Add `-dx12` to Stray's launch options in Steam, start the game, open ReShade (Home key),
   Add-ons tab, StrayDLSS: switch the upscaler on (or press F9), pick DLSS or FSR and a render
   resolution and, if you like, frame generation (takes effect after a restart).
4. A DLSS model override for the game in the NVIDIA App takes precedence over DLSS model (and a
   Ray Reconstruction override over Ray Reconstruction model).

The zip includes NVIDIA's DLSS 310.9.16 (Super Resolution and Ray Reconstruction) and Streamline 2.14.16 runtime files, signed by NVIDIA
(the add-on checks Streamline's signature before loading it), and AMD's FSR SDK 2.4.0 runtime
files, signed by AMD. Their licences are in `licenses`, and attribution is in
`THIRD_PARTY_NOTICES.txt`.

Tested with Steam build 12863654 at 3840x2160 (HDR via RenoDX, StrayTweaks installed) on an
NVIDIA RTX 5080: DLSS (DLAA and Balanced) and FSR 3.1 upscaling, DLSS Ray Reconstruction
(Balanced, with ray-traced reflections, shadows and ambient occlusion, DLSS Frame Generation 2x),
DLSS Frame Generation (Dynamic) and FSR Frame Generation 2x. To uninstall, delete those files. Logs: `StrayDLSS.log` next to the
game's exe, Streamline's in `streamline\sl.log`.

## Build

`.\build.ps1` (Visual Studio 2022 C++ tools, Windows SDK for `fxc`). The runtimes aren't in the
repository: unzip `bin\x64` of NVIDIA's Streamline 2.14.16 release into
`third_party\streamline\bin\x64`, and copy `amd_fidelityfx_loader_dx12.dll`,
`amd_fidelityfx_upscaler_dx12.dll` and `amd_fidelityfx_framegeneration_dx12.dll` from
`Kits\FidelityFX\signedbin` of AMD's FSR SDK 2.4.0 into `third_party\fidelityfx\bin`.
`.\build.ps1 -Install` copies the add-on and the runtimes into the game folder (game closed);
`.\build.ps1 -Package` makes the release zip in `bin\release`.

## How it works

Built from TrueColorsDLSS 1.1.0 (UE 4.25). `docs/FINDINGS.md` has the Stray details: the TAA
passes (found by their bindings, since the exe is encrypted on disk), the View uniform buffer
offsets (unchanged from 4.25), how Ray Reconstruction's inputs are made from the G-buffer, and the
test results. The F10 frame capture (Advanced tab) writes
two frames of GPU commands to `StrayDLSS-frame-N.txt`, for finding the TAA pass again after a game
update.
