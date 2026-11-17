# CallistoDLSS: findings

Game: The Callisto Protocol (Epic), `TheCallistoProtocol\Binaries\Win64\TheCallistoProtocol-Win64-Shipping.exe`
(115,887,616 bytes, 2026-11-02 install), UE **4.27.2** (Striking Distance's fork), DirectX 11/12. The exe is
**not protected** (plain sections, no SteamStub/Denuvo), so it can also be read offline. ReShade 6.8.0
and RenoDX (`renodx-callistoprotocol.addon64`) were already installed for HDR.

Forked 2026-11-12 from StrayDLSS 1.1.0 (UE 4.27: DLSS/FSR in place of UE's TAA, DLSS-G + FSR FG,
DLSS Ray Reconstruction; its FINDINGS are the reference for the method).

## From the exe (offline)

- Upscaling: **AMD's FSR 2.1 UE plugin**, statically linked (`r.FidelityFX.FSR2.*`: Enabled, QualityMode,
  Sharpness, AutoExposure, CreateReactiveMask + ReactiveMask* tuning, AdjustMipBias, HistoryFormat,
  DeDither, UseSSRExperimentalDenoiser, UseNativeDX12). No NGX/DLSS of its own.
  The plugin replaces UE's TAA when enabled, so the add-on holds `r.FidelityFX.FSR2.Enabled=0` while it
  is on (game's value and priority back when off) and replaces UE's TAA/TAAU as in Stray.
- PCGamingWiki: any AA other than TAA disables FSR, SSR, refraction **and ray tracing**: keep TAA.
- Ray tracing: stock UE 4.27 set (`r.RayTracing.Shadows`, `.Reflections`, `.AmbientOcclusion`,
  `.GlobalIllumination`, `.Translucency`, `.SkyLight`); transmission (`r.RayTracing.Shadows.EnableTransmission`,
  `GlobalIllumination.EnableTransmission`, `r.RayTracing.Transmission.*`) is the "RT transmission" the
  wiki lists. Plus custom tiled culling (`r.RayTracing.TiledCulling`, `.UpscaleShadows`,
  `.DenoisingWorkQueue`, strings `...TraceDenoiseDispatchParams/JobQueue`): likely a custom RT shadow
  denoise path. Which effects the game actually turns on is logged at run time.
- Denoisers: same names as Stray (`r.Shadow.Denoiser`, `r.Reflections.Denoiser`,
  `r.AmbientOcclusion.Denoiser`, `r.DiffuseIndirect.Denoiser`, `r.RayTracing.GlobalIllumination.Denoiser`,
  `r.RayTracing.SkyLight.Denoiser`) plus `r.Raytracing.Shadows.OverrideDenoiseAllLights`.

## Probe 0.0.1 (installed 2026-11-12)

Changes from StrayDLSS: names/paths, `kTaaMain` placeholder (TAA found by shape, id logged), FSR 2
hold, and the log dumps FSR 2 / AA / ray tracing / denoiser cvars at start and whenever they change.

Launch stall (2026-11-12): the first launch after install stopped at ~1.8 GB right after the D3D12
swapchain was made (no CPU, every thread waiting in game code, none in ReShade/RenoDX/the add-on).
Without the add-on it loaded. A `DebugSkip` bit mask (ReShade.ini `[CallistoDLSS]`: 1 descriptor
tracking, 2 upscaler, 4 frame generation, 8 pipeline ids, 16 capture events) was added to bisect it,
but the next five launches (31, 25, 6, then 0 three times) **all loaded**, with the full add-on included.
Not reproduced: possibly a one-off on the first launch with the new files. If it comes back, bisect
with DebugSkip (scratchpad `trial.ps1` launches through Epic and checks for memory > 3 GB).

## 0.0.1 test (2026-11-12, 4K, DLAA and Performance, ray tracing on)

- FSR 2 held off (`r.FidelityFX.FSR2.Enabled` 0 at 0x09); render resolution set (50% / 100%);
  `r.TemporalAA.Upscaler` = 1 (third-party upscaler slot), `r.TemporalAA.Upsampling` 1, AA = TAA.
- **No TAA matched** (diag "render 0x0"): DLSS never ran.
- Ray tracing on: **shadows and reflections** (AO, GI, translucency, sky light 0); transmission on.
  Denoisers: shadows 2, **reflections 0** (no UE reflection denoiser), AO 2, others 1.
- Capture 1: every dispatch has a pipeline id (`cs00000000` lines are graphics pipelines). **Scene
  colour is r11g11b10f.** Full-resolution compute passes (480x270 groups at 4K): cs4c276ea5 (x8 per
  frame), cseca20317, cs3d58af3a, cs1f086929, csfb3b53e2 (reads r32uint x2, rg16unorm, r16f),
  cs4e194ee4 (reads scene colour rt->srv r11g11b10f, another r11g11b10f, an r8unorm),
  cs34874c9f (reads two r11g11b10f, writes the one cs4e194ee4 read), cs1bf8ea52; csbd2ba00a 480x540.
  Looks like a custom multi-pass TAA rather than UE's one-pass TemporalAA.usf.
- 0.0.2: the capture details every compute pass (bound textures by declared register); the log
  says whether compute shaders are DXBC or DXIL.

## 0.0.2 capture (2026-11-12, DLAA 4K): the game's own TAA

Compute shaders are DXBC (1000+ DXBC, 15 other), so declarations are readable. Striking Distance's
TAA, one command list, after the lighting (cseca20317 just before it):

| pass | reads | writes |
|---|---|---|
| cs3d58af3a | | u0,u1 r32uint (cleared: reprojected depth) |
| cs1f086929 velocity dilation | t0 depth, **t1 UE velocity rg16unorm** | u0 rg16unorm dilated motion, u1 r16f, u2/u3 r32uint |
| csfb3b53e2 history reprojection | t0..t3 above, t4 previous history | u0 reprojected history r11g11b10f, u1 r8 mask |
| cs4e194ee4 accumulate | **t0 scene colour** r11g11b10f, t1 reprojected history, t2 mask | u0, u1 r11g11b10f |
| cs34874c9f / cs04a2c218 | mask, accumulate outputs | half-res r8 |
| **cs1bf8ea52 resolve** | t0 scene colour, t1 depth, t2 half-res r8, t3 dilated motion, t4 mask, t5 prev history, t6, t7 rg8 | **u0 next history**, u1, u2 rg8, **u3 output** |

History ping-pongs (frame 0 u0 = frame 1 csfb3b53e2 t4). The next graphics pass reads u3 (first a
1920x1080 bloom downsample). View buffer at root CBV 4 (same buffer offset bound to most passes).

0.0.3: kTaaMain = cs1bf8ea52 (colour t0, depth t1, output u3); UE velocity taken from cs1f086929's
t1 earlier in the same list; after DLSS the output is also copied into u0 (history), so frames the
game's TAA takes start from a current history. The other six passes still run (cost, to trim later).

## 0.0.3 test (2026-11-12, 4K): DLSS works

- UE velocity 2260x1272 rg16unorm (format 35) taken from cs1f086929; our motion vectors made.
- **DLSS ran**: first frame 2259x1271 -> 3840x2160 (the game's FSR 2 Balanced resolution, before the
  hold reached the game thread), then **DLAA 3840x2160** and **Balanced 2228x1253 -> 4K**, preset K.
- UE 4.25/4.27 View offsets hold in this fork (one frame "layout NOT as expected" at a transition,
  as in Stray). No problem diag lines in gameplay; no crash.
- User: "everything looked good".

## 0.0.3 frame generation test (2026-11-12, DLSS-G 2x): user saw no generated frames

- Streamline 2.14 started on the HDR swapchain (format 10) and DLSS-G initialised at the level load
  (cubins loaded, flip metering "good"); first frame tagged at submission; only a few frames untagged.
- **DLSS Super Resolution didn't start**: "DLSS not available". Streamline had started NGX first,
  with `streamline\` as feature path, and nvngx_dlss.dll was only next to the exe. FSR upscaled instead.
- Our DLSS-G state line only logged on change and never changed from "1 frame presented"; the diag
  lines were used up in the main menu, so nothing in gameplay showed whether frames were generated.
- 0.0.4: nvngx_dlss.dll + nvngx_dlssd.dll also in `streamline\`; DLSS-G state logged every ~10 s
  while on; menu windows without a TAA no longer use up the 30 diag lines.

## 0.0.4 frame generation test (2026-11-12)

- **DLSS SR now starts with Streamline running** (DLLs in `streamline\` too): Balanced 2228x1253 -> 4K,
  every gameplay frame upscaled and tagged ("tags set 119 of 120"), View check passed, pre-HUD
  picture found (3840x2160 format 10).
- DLSS-G "on" (3x, then 2x), status 0x0, but slDLSSGGetState said **1 frame presented per game frame**
  the whole time. Streamline's log: no errors besides one missing-constants frame; flip metering
  reached "good" (normally a sign of generated frames). Swapchain flags 0x842 (frame latency
  waitable object: Streamline "flip queue throttling disabled"); presents go through the proxy
  (Streamline saw every ResizeBuffers). No driver-profile override (SL_DLSS_OVERRIDE 0).
- Same frame generation code as StrayDLSS, where DLSS-G generated 1-6 frames.
- 0.0.5: logs DXGI presents (GetLastPresentCount, generated frames included) per game frame every
  ~10 s, to tell whether frames are really generated.

## 0.0.5 frame generation test (2026-11-12): confirmed, no frames generated

GetLastPresentCount: **1.00 DXGI presents per game frame** with DLSS-G 2x on (600/600 every window),
the same as with it off. RTSS agreed. DLSS-G on, status 0x0, every frame tagged, Reflex low latency
set: it silently generates nothing.

Difference from Stray: here a "pre-HUD picture" is found (a back-buffer-sized rgba16f copy, which
may be RenoDX's, not the game's) and tagged as HUDLessColor; Stray had none. 0.0.6: not tagged for
DLSS-G unless ReShade.ini `[CallistoDLSS] FgHudless=1`; Streamline log verbose.

## 0.0.6 test (2026-11-12): still nothing generated

Pre-HUD picture not tagged: still 1.00 DXGI presents per game frame (Steam overlay agrees). Verbose
Streamline log: setup only, nothing per frame in gameplay explaining it. Remaining difference from
Stray: **RenoDX makes the swapchain rgba16f scRGB** (ResizeBuffers to format 10 + SetColorSpace1(1)
at level load; Stray's was HDR10 rgb10a2). Next run with RenoDX's add-on renamed to `.off`.

RenoDX off (2026-11-12): still 1.00 presents per game frame, but **the game itself** switches to
rgba16f + scRGB (SetColorSpace1(1)) at level load when its HDR is on, so FP16 wasn't ruled out.
Next: RenoDX off and the game's HDR off (true SDR swapchain).

**SDR (RenoDX off, game HDR off): DLSS-G works** (user: frame rate went up; Steam overlay). So the
HDR swapchain (rgba16f, scRGB, set at level load) is what stops it. Our own counters are useless
here: GetLastPresentCount on the proxy and DLSSGState.numFramesActuallyPresented both said 1.00 in
the working SDR run too; judge by an overlay that counts real presents (Steam, NVIDIA).

0.0.7: DLSSGOptions now carry colorWidth/Height and colorBufferFormat (were empty), sent again
whenever the back buffers change (format 24 -> 10 at level load); back buffer format and colour
space logged at each change. RenoDX back on.

0.0.7 HDR test: options sent for format 10 at the switch (colour space 2 = scRGB in ReShade's
terms); **frame rate back down** in HDR. DLSS-G here doesn't generate on rgba16f scRGB and says
nothing. RenoDX for Callisto has no output-encoding option (shader replacements + nits only).

0.0.8: `TestCvar`/`TestValue` in `[CallistoDLSS]` hold one cvar on the game thread; HDR cvars
logged. Test: `r.HDR.Display.OutputDevice=4` (ACES 2000 nits ST-2084: rgb10a2 PQ swapchain) to see
whether DLSS-G works with HDR10, and whether RenoDX still applies.

0.0.8 test: the game's `r.HDR.Display.OutputDevice` is **11 (SDR) / 13 (HDR)**: Striking Distance's own
values, not stock UE's 0-7. Holding 4 left the swapchain rgba16f scRGB with PQ-encoded values in
it: grey, washed-out picture; FPS unchanged (not an HDR10 test at all). Test setting removed.

## 0.0.9: scRGB -> HDR10 swapchain layer for DLSS-G (src/hdrswap.cpp)

Layering seen in the logs: game -> ReShade's swapchain (its own object, 0x...338930) -> Streamline's
proxy -> native (Streamline's "native" 0x...6dc140). The add-on creates Streamline's swapchain from
the factory hook ReShade calls, so it can return its own wrapper (IDXGISwapChain4, everything
forwarded) that ReShade then wraps:
- the game asks for rgba16f: Streamline's swapchain is made / resized as **rgb10a2**, colour space
  **ST.2084 P2020** whenever the game sets scRGB; CheckColorSpaceSupport(scRGB) answers for ST.2084;
- GetBuffer hands out rgba16f textures of our own (one per buffer, indexed by Streamline's
  GetCurrentBackBufferIndex); GetDesc/GetDesc1 report rgba16f;
- Present/Present1 first draw our current buffer into Streamline's (hdr_convert.hlsl: Rec.709 ->
  Rec.2020, x80 nits, PQ) on the swapchain's native queue, then forward (our Present hook on
  Streamline's proxy still runs: Reflex markers, tagging);
- SDR (rgb10a2 asked) passes straight through. DLSSGOptions.colorBufferFormat maps rgba16f -> rgb10a2.
Only with DLSS frame generation (the Streamline path).

**0.0.9 test (2026-11-12, 4K HDR, RenoDX, DLSS Quality 2561x1441, DLSS-G 2x/3x): WORKS.** Layer in
place at creation (format 24), at the level load 3 rgba16f buffers for the game + HDR10 for
Streamline, colour space set to ST.2084 (S_OK), every frame converted; user: frame generation
works and "the picture looked great" (RenoDX look kept). No crash.

Still to clean up before a release: Streamline log back to eDefault, Detailed() capture-all
condition, DebugSkip, our misleading present counters, README.

## Ray Reconstruction: what the capture shows (2026-11-12)

Base pass (capture 1, 8 render targets at 4K): RT0 r11g11b10f scene colour, RT1 rgb10a2 (GBufferA),
RT2 + RT3 bgra8 **typeless** (format 90; GBufferB/C), RT4 rg16unorm velocity, RT5 bgra8 typeless,
RT6 format 12, RT7 rgb10a2 (the fork's extras). The matcher checks the RTV view formats, so it
should take it as is. Denoisers: reflections already 0 (no UE denoiser), shadows 2; the fork's
tiled RT shadows (`r.RayTracing.TiledCulling`, half-rate dithered tiles + `UpscaleShadows`) have no
clear off switch: left alone for the first test. RR tested as built in 0.0.9 first.

**0.0.9 RR test (2026-11-12, 4K HDR, DLSS Quality 2561x1441, DLSS-G on): WORKS.** G-buffer
2564x1444 found (8 targets; view formats A 24, B 87, C 91, as Stray's); View matrices check passed;
RR 2561x1441 -> 4K (preset default, then F); the 6 UE denoisers off/on with the toggle (game's:
shadows 2, reflections 0, AO 2, diffuse indirect 1, GI 1, sky light 1). No failed frames, no
"not invertible" spam when switching back to SR, no crash. User: a noticeable difference with RR
on, visible in the frame counter as well. Other 4+-target sets seen (214x120 formats 26/61,
128x128 rgba16f) are not G-buffers.

## 1.0.0 (2026-11-12): release cleanup

- **TAA passes skipped**: in capture 1 nothing outside the TAA chain reads its intermediates
  (outside the chain they only appear in srv->uav barriers before the next write); only the
  resolve's u3 goes on to post-processing. While the upscaler takes the resolve, the velocity pass
  (after its velocity binding is read), csfb3b53e2, cs4e194ee4, cs34874c9f and cs04a2c218 are
  skipped; cs3d58af3a (a clear, before the decision) still runs. Only when the previous resolve was
  upscaled and this frame's velocity is real (not the 1x1 camera-cut placeholder); the add-on
  switched back on runs one full game TAA first.
- Removed: Streamline verbose log, DebugSkip, the test cvar hold, the present-count log (it read
  1.00 even with frames generated), the compute-shader format count, the capture-everything rule.
  The pre-HUD picture stays off for DLSS-G unless `FgHudless=1`.

**1.0.0 test (2026-11-12, 4K HDR, DLSS Quality, RR on, DLSS-G 2x): OK.** "the game's TAA passes
before the resolve skipped" from the first upscaled frames on; no failed frames; HDR layer and
frame generation ran; F9 off/on twice: denoisers restored and off again, RR and frame generation
back at once; no crash. RR fell back to Super Resolution for the first ~12 s of the level ("G-buffer
not found yet"): the 2564x1444 G-buffer (depth-size key) was first matched at 55 s, like Stray's
first-level-load delay; the "not taken" log lines were used up by 128x128 sets, so why isn't known.
Next time: log unmatched 7+ target sets separately.

To find out:
- Is `r.FidelityFX.FSR2.Enabled` found and held at 0, and does UE's TAA then run (TAA shader id)?
- Does the UE 4.27 cvar layout check pass in this fork? Do the View offsets (1952/2016/2080) hold?
- DLAA first, then lower modes (TAAU permutation id), then frame generation.
- Which ray-traced effects are on (cvar dump), for Ray Reconstruction's worth.

## 1.0.1: render-size textures freed (2026-11-16)

- **Bug** (found in StrayDLSS on 2026-11-16, same code here): our render-size textures (motion,
  mask, composed colour, 3 Ray Reconstruction guides: 5 per size with RR on) were never freed and
  capped at 16. After about four DLSS mode changes with RR on the next one was refused:
  "motion: no texture (limit)" on every frame, the game's TAA took over silently and frame
  generation paused.
- 1.0.1 (port of StrayDLSS 1.2.1): textures unused for 8 presented frames are freed (with
  their views) when room is needed; limit 32; the log says how many were freed. Built, not
  yet run: to test, switch DLSS modes 4+ times and check the log for "no texture (limit)".

## 1.0.1 user run (2026-11-17, 4K HDR, DLSS Quality/Balanced, RR, DLSS-G 2x, UE4SS + UltraPlusExtensions also installed)

- DLSS, RR and DLSS-G all ran (Streamline: 2 presents per game frame). Single "TAA view: layout NOT as
  expected" frames at camera cuts only.
- **Frame generation flickering in black scene transitions**: Streamline logged missing depth/motion tags
  on nearly every present for seconds at a time and skipped presents ("Couldn't lock the mutex on sync
  present"), yet frame generation never paused: it only paused after 8 presents in a row without inputs,
  and inputs came for some presents only. 1.0.2: pauses when 4 of the last 16 presents lack inputs,
  resumes after 8 in a row have them.
- **RR noise around some lights**: the game traces RT shadows at reduced resolution and upscales them
  (`r.RayTracing.TiledCulling.UpscaleShadows` 1); with `r.Shadow.Denoiser` 0, RR got blotchy upscaled
  noise. 1.0.2: `RrShadows` (overlay "Ray-traced shadows"): 0 (default) also holds UpscaleShadows 0
  (full-res rays), 1 keeps the game's shadow denoiser, 2 = 1.0.1 behaviour. Untested.
- Exit crash (UE4SS dump, after returning to the main menu): null `this` in game code (lock-free list
  cmpxchg at exe+0xc44845); UE4SS's minidump has no stack. Not pursued.
- Build note: a PATH entry with "(x86)" from DSX breaks vcvars; build with DSX entries stripped from PATH.

## 1.0.2 test (2026-11-17): RR full-resolution shadows confirmed

- User: the new default (RR, full-resolution shadow rays) "looked much better" than 1.0.1's upscaled
  shadows. Log: `r.RayTracing.TiledCulling.UpscaleShadows` held 0 and given back when switching modes.
- Frame generation: Streamline tag errors down from hundreds to 8 single frames (camera cuts) in ~4 min.
  But at the first start it switched on/off 3 times in 0.1 s: after resuming (8 tagged in a row) the
  older misses still in the 16-present window paused it again. Fixed: history cleared on resume.
  Settings log line now includes the RR shadow mode.
- Retest (11:20): frame generation started once (2x), no flapping; 3 untagged single frames in ~80 s.
- Exit crash is reproducible: identical in all three 2026-11-17 sessions (exe+0xc44845, write to null+0x40,
  ~5 s after returning to the main menu, UE4SS dumps without stacks). No dump on 2026-11-16 with
  UE4SS 1.0.1 installed too. Not yet bisected (UE4SS off / add-on off).

## User log (1.0.2, 2026-11-17): Streamline refused the game's swapchain

`CreateSwapChain` through Streamline's proxy failed with 0x887a0001 (DXGI_ERROR_INVALID_CALL; DLSS-G's
"PFunCreateSwapChainBefore failed"), right after Streamline started. Log ends there: 1.0.2 returned that
error to the game, and UE treats a failed swapchain creation as fatal. Our runs take the same path
and succeed, so the user's swapchain description differs (exclusive fullscreen, buffer count/flags,
another overlay or injector?). 1.0.3: falls back to the game's own swapchain (no DLSS-G, DLSS
upscaling unaffected), stops offering later swapchains to Streamline, and logs the description
(size, format, buffers, swap effect, flags, windowed). Fallback path untested here (Streamline succeeds
on this PC).
