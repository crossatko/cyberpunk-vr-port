# VRCAM vs MAIN per-node CPU audit (v2, named)

Source: `CyberpunkVR_ProfDumpNodes()` -> testbed.log, parsed by `tools/node_audit_md.py`.

## How to read this

- **self** = node's own CPU time, children excluded. This is the ONLY column you may rank or sum.
- **incl** = self + the nodes this node dispatches. `SceneDrv_ALL_SCENE_PASSES` contains every scene pass, so inclusive columns overlap and must never be summed.
- Per-frame = per rendered frame of that view. Denominators are INFERRED from the data as the modal call count: main **1134** frames (55 nodes sit at exactly that count), vrcam **1134** (55 nodes). Every other node's count comes out a clean multiple of it.
- Not used as divisors: presents=19468 (frame generation inflates it) and the DLL's own view_frames counters (85616 / 58401) -- those count node *dispatches*, because the engine's graph runner calls the executor directly for most nodes, so almost every node is top-level.
- `ord` = first-seen dispatch order for that view (0 = the view never ran it).
- Window: 636.4 s wall -> 1.8 fps main / 1.8 fps vrcam, 561.17 ms per frame, 82 nodes.

## Bottom line

Self time summed over all nodes, per frame:

| view | node CPU (self) ms/frame | vs the 561.17 ms frame |
|---|---|---|
| MAIN | 16.787 | 3% |
| VRCAM | 6.317 | 1% |
| both | 23.105 | 4% |

VRCAM/MAIN = **0.38x**. Summing self is legitimate -- no double counting. But the last column is CPU time against WALL time, not a share of the frame: node work is dispatched onto worker threads as well as the render thread, so the total can exceed 100% without anything being wrong, and a saving of 1 ms of node CPU does NOT mean 1 ms off the frame unless that node sits on the critical path.

## Capture configuration

An experiment armed during a capture silently invalidates the table, so the toggles are recorded with it:

| flag | value |
|---|---|
| `CullReuseMode` | 0 |
| `DistantReuse` | 1 |
| `GiReuse` | 1 |
| `LocalShadowReuse` | 1 |
| `LodMask` | 3 |
| `LodOverride` | 0 |
| `LodValue` | 1.000 |
| `MainAaMode` | 0 |
| `MainUpscalerGroups` | 0x000 |
| `NodeCutEnable` | 0 |
| `NodeCutSkips` | 0 |
| `OcclusionGateForce` | 1 |
| `VrcamAaMode` | 0 |
| `VrcamComputeResolve` | 2 |
| `VrcamDlss` | 0 |
| `VrcamFlagMode` | 1 |
| `frameMs` | 32.678 |

## Nodes, ranked by self time

| ord m/v | node | rva | calls/frame m/v | self ms/frame m | self ms/frame v | incl ms/frame v | verdict |
|---|---|---|---|---|---|---|---|
| 9/0 | Present | 0x21CDC8 | 2.00 / 0.00 | 9.7721 | 0.0000 | 0.0000 | MAIN-only |
| 22/10 | RenderElements | 0x23A938 | 32.00 / 29.00 | 2.5290 | 2.7477 | 2.7477 | symmetric |
| 4/27 | SceneDrv_ALL_SCENE_PASSES | 0x1EC1D0 | 40.00 / 31.00 | 0.9499 | 1.0566 | 5.7415 | symmetric |
| 8/7 | Synchronize | 0x58DA9C | 8.00 / 6.00 | 0.3933 | 0.3728 | 0.3728 | symmetric |
| 33/29 | RenderShadowCascade | 0x153844 | 2.00 / 2.00 | 0.2334 | 0.1032 | 0.1032 | MAIN-heavy 2.3x (vrcam cheap) |
| 19/6 | ViewSetup_perview_RT_state | 0x1EDD64 | 28.00 / 25.00 | 0.1366 | 0.1380 | 0.1380 | symmetric |
| 56/4 | PrepareRenderElements | 0xC26DB8 | 1.00 / 1.00 | 0.1240 | 0.1274 | 0.1274 | symmetric |
| 6/0 | AdvanceSpeedTreeWind | 0xCC4DF4 | 2.00 / 0.00 | 0.2383 | 0.0000 | 0.0000 | MAIN-only |
| 16/2 | SimulateOnScreenCPUParticles | 0xCBB23C | 4.00 / 4.00 | 0.0955 | 0.1123 | 0.1123 | symmetric |
| 57/57 | ClusteredLightsCull | 0x77CED4 | 1.00 / 1.00 | 0.0842 | 0.0785 | 0.0785 | symmetric |
| 7/0 | RenderRainMap | 0x3726CC | 2.00 / 0.00 | 0.1595 | 0.0000 | 0.0000 | MAIN-only |
| 107/116 | DecoupledParticleLighting | 0x6212EC | 1.00 / 1.00 | 0.0659 | 0.0739 | 0.0739 | symmetric |
| 64/63 | ReflectionProbes | 0x77E610 | 1.00 / 1.00 | 0.0193 | 0.1198 | 0.1198 | VRCAM-heavy 6.2x |
| 2/0 | StartRender | 0x21AB08 | 2.00 / 0.00 | 0.1363 | 0.0000 | 0.0000 | MAIN-only |
| 136/128 | ResolveDistortion | 0x1599D4 | 1.00 / 1.00 | 0.0628 | 0.0697 | 0.0697 | symmetric |
| 67/65 | GlobalIllumination | 0x77E664 | 1.00 / 1.00 | 0.1022 | 0.0289 | 0.0289 | MAIN-heavy 3.5x (vrcam cheap) |
| 68/45 | RenderLocalShadowMaps | 0xAD5770 | 3.00 / 3.00 | 0.1275 | 0.0011 | 0.0011 | MAIN-heavy 118.0x (vrcam cheap) |
| 114/69 | SetStreamlineConstants | 0x788A9C | 1.00 / 1.00 | 0.0623 | 0.0580 | 0.0580 | symmetric |
| 23/36 | PrepareSceneRendering | 0x784ABC | 1.00 / 1.00 | 0.0537 | 0.0587 | 0.0587 | symmetric |
| 117/39 | RenderDistantShadows | 0x373998 | 1.00 / 1.00 | 0.1093 | 0.0017 | 0.0017 | MAIN-heavy 63.6x (vrcam cheap) |
| 121/120 | SSS_Blur | 0x62130C | 1.00 / 1.00 | 0.0430 | 0.0478 | 0.0478 | symmetric |
| 36/22 | RenderVolumetricCloudsLighting | 0x61BE74 | 1.00 / 1.00 | 0.0280 | 0.0609 | 0.0609 | VRCAM-heavy 2.2x |
| 72/31 | AutoSpawnOnTerrain | 0x77D214 | 1.00 / 1.00 | 0.0835 | 0.0049 | 0.0049 | MAIN-heavy 17.2x (vrcam cheap) |
| 21/20 | SetRenderTargetsGBuffer | 0x20C3AC | 10.00 / 7.00 | 0.0417 | 0.0388 | 0.0388 | symmetric |
| 52/24 | HistogramUpdate | 0x774CF8 | 3.00 / 3.00 | 0.0382 | 0.0409 | 0.0409 | symmetric |
| 44/15 | RenderVolumetricClouds | 0x61B5B4 | 1.00 / 1.00 | 0.0280 | 0.0479 | 0.0479 | VRCAM-heavy 1.7x |
| 70/66 | VolumetricFog | 0x61C3BC | 1.00 / 1.00 | 0.0337 | 0.0422 | 0.0422 | symmetric |
| 90/100 | RenderLightBuffers | 0x77D308 | 1.00 / 1.00 | 0.0377 | 0.0353 | 0.0353 | symmetric |
| 127/122 | ScreenSpacePlanarReflections | 0x78A6B8 | 1.00 / 1.00 | 0.0349 | 0.0376 | 0.0376 | symmetric |
| 95/111 | CompositionPostProcess | 0x1F8928 | 1.00 / 1.00 | 0.0686 | 0.0003 | 0.0003 | MAIN-heavy 225.3x (vrcam cheap) |
| 38/12 | BindLightingGlobalConstants | 0xBB8D40 | 9.00 / 9.00 | 0.0311 | 0.0364 | 0.0364 | symmetric |
| 93/110 | DrawComposition | 0x20A264 | 1.00 / 1.00 | 0.0648 | 0.0004 | 0.0004 | MAIN-heavy 147.3x (vrcam cheap) |
| 46/51 | RenderLightChannelVolumes | 0x77AAE0 | 1.00 / 1.00 | 0.0290 | 0.0316 | 0.0316 | symmetric |
| 108/21 | BuildDepthChain | 0x10703E0 | 1.00 / 1.00 | 0.0281 | 0.0314 | 0.0314 | symmetric |
| 37/11 | PrepareAutoSpawnOnTerrain | 0x77B638 | 1.00 / 1.00 | 0.0403 | 0.0178 | 0.0178 | MAIN-heavy 2.3x (vrcam cheap) |
| 47/16 | SetRenderTargetsMain | 0x775ACC | 8.00 / 8.00 | 0.0265 | 0.0303 | 0.0303 | symmetric |
| 39/41 | RenderTopDownCarProxies | 0x7876C4 | 1.00 / 1.00 | 0.0271 | 0.0288 | 0.0288 | symmetric |
| 94/104 | ScreenSpaceReflections | 0x157B24 | 1.00 / 1.00 | 0.0247 | 0.0302 | 0.0302 | symmetric |
| 96/109 | RenderLightsIntegrate | 0x154610 | 2.00 / 2.00 | 0.0255 | 0.0294 | 0.0294 | symmetric |
| 28/55 | HairFullscreenPass | 0x77460C | 2.00 / 2.00 | 0.0254 | 0.0288 | 0.0288 | symmetric |
| 110/32 | DrawConeAO | 0x61EE78 | 1.00 / 1.00 | 0.0262 | 0.0274 | 0.0274 | symmetric |
| 73/88 | BuildTemporalInvalidationMask | 0x156F80 | 1.00 / 1.00 | 0.0229 | 0.0240 | 0.0240 | symmetric |
| 124/98 | ApplyTXAA | 0x768510 | 1.00 / 1.00 | 0.0220 | 0.0245 | 0.0245 | symmetric |
| 92/101 | RenderBackground | 0x782EB8 | 1.00 / 1.00 | 0.0127 | 0.0313 | 0.0313 | VRCAM-heavy 2.5x |
| 34/3 | WindImpulseVolumeUpdate | 0x6EAEDC | 1.00 / 1.00 | 0.0023 | 0.0417 | 0.0417 | VRCAM-heavy 18.0x |
| 43/49 | RenderSkyScattering | 0x7818B0 | 1.00 / 1.00 | 0.0231 | 0.0205 | 0.0205 | symmetric |
| 60/58 | RenderShadowmask | 0x786BCC | 1.00 / 1.00 | 0.0193 | 0.0222 | 0.0222 | symmetric |
| 35/80 | InitVelocityBuffer | 0x7883B0 | 2.00 / 2.00 | 0.0185 | 0.0210 | 0.0210 | symmetric |
| 98/95 | ScreenSpaceRain | 0x77198C | 1.00 / 1.00 | 0.0188 | 0.0197 | 0.0197 | symmetric |
| 62/59 | ClassifyMaterials | 0x7793C8 | 1.00 / 1.00 | 0.0174 | 0.0192 | 0.0192 | symmetric |
| 49/9 | SetRenderTargetsGBufferWithVelocityBuffer | 0x20B294 | 8.00 / 8.00 | 0.0166 | 0.0183 | 0.0183 | symmetric |
| 69/75 | ApplyBloomAndTonemapping | 0x769308 | 1.00 / 1.00 | 0.0158 | 0.0175 | 0.0175 | symmetric |
| 133/125 | SetRenderTargetDistortion | 0x778FB4 | 1.00 / 1.00 | 0.0153 | 0.0167 | 0.0167 | symmetric |
| 11/0 | CleanupBatchDataAllocator | 0x611830 | 2.00 / 0.00 | 0.0319 | 0.0000 | 0.0000 | MAIN-only |
| 106/19 | FlattenNormals | 0x61F11C | 1.00 / 1.00 | 0.0140 | 0.0151 | 0.0151 | symmetric |
| 65/84 | ModifyDepth | 0x378640 | 1.00 / 1.00 | 0.0132 | 0.0151 | 0.0151 | symmetric |
| 48/54 | RenderGIVolumes | 0xB779DC | 1.00 / 1.00 | 0.0130 | 0.0138 | 0.0138 | symmetric |
| 91/107 | CopyToTexture | 0x377B58 | 1.00 / 1.00 | 0.0012 | 0.0249 | 0.0249 | VRCAM-heavy 20.1x |
| 29/56 | HairRenderTargets | 0x776934 | 8.00 / 8.00 | 0.0125 | 0.0135 | 0.0135 | symmetric |
| 100/30 | PrepareFeedbackNormalBuffer | 0x767288 | 1.00 / 1.00 | 0.0130 | 0.0127 | 0.0127 | symmetric |
| 55/60 | PersistentCameraData | 0xC03500 | 1.00 / 1.00 | 0.0101 | 0.0149 | 0.0149 | symmetric |
| 3/0 | EndRender | 0x79A7E4 | 2.00 / 0.00 | 0.0249 | 0.0000 | 0.0000 | MAIN-only |
| 130/123 | SetHologramDepthBuffer | 0x7728E8 | 1.00 / 1.00 | 0.0116 | 0.0127 | 0.0127 | symmetric |
| 131/44 | ComputeShadingRateImage | 0x772BAC | 1.00 / 1.00 | 0.0104 | 0.0130 | 0.0130 | symmetric |
| 51/61 | ComputeWaterFFT | 0xAD01F0 | 1.00 / 1.00 | 0.0204 | 0.0015 | 0.0015 | MAIN-heavy 13.4x (vrcam cheap) |
| 61/73 | RenderScreenSpaceWaterDepth | 0x378AD8 | 1.00 / 1.00 | 0.0101 | 0.0117 | 0.0117 | symmetric |
| 111/33 | GenerateTonemappingLUT | 0xEFC110 | 1.00 / 1.00 | 0.0103 | 0.0111 | 0.0111 | symmetric |
| 30/47 | ClearShadowCascades | 0x1D59B40 | 1.00 / 1.00 | 0.0095 | 0.0113 | 0.0113 | symmetric |
| 99/112 | DrawHUD | 0x1EE760 | 1.00 / 1.00 | 0.0181 | 0.0003 | 0.0003 | MAIN-heavy 58.8x (vrcam cheap) |
| 54/23 | UnbindLightingGlobalConstants | 0x774F34 | 9.00 / 9.00 | 0.0089 | 0.0091 | 0.0091 | symmetric |
| 102/114 | RenderFinal2D | 0x209FF0 | 1.00 / 1.00 | 0.0127 | 0.0039 | 0.0039 | MAIN-heavy 3.3x (vrcam cheap) |
| 1/0 | EndFrame | 0xB79578 | 2.00 / 0.00 | 0.0165 | 0.0000 | 0.0000 | MAIN-only |
| 41/46 | UpdateParticlesRenderData | 0xD630D4 | 1.00 / 1.00 | 0.0084 | 0.0080 | 0.0080 | symmetric |
| 71/67 | PrepareFeedbackSSRBuffer_PreSSR | 0xD2D514 | 1.00 / 1.00 | 0.0071 | 0.0092 | 0.0092 | symmetric |
| 112/118 | SetRT_SSS_Emissive | 0xB9B438 | 1.00 / 1.00 | 0.0068 | 0.0088 | 0.0088 | symmetric |
| 88/99 | ApplyContrastAdaptiveSharpening | 0x376F74 | 1.00 / 1.00 | 0.0063 | 0.0078 | 0.0078 | symmetric |
| 76/82 | GameplayPostFX | 0x77120C | 1.00 / 1.00 | 0.0134 | 0.0005 | 0.0005 | MAIN-heavy 28.4x (vrcam cheap) |
| 17/5 | DoCulling | 0xB2BEFC | 3.00 / 3.00 | 0.0068 | 0.0061 | 0.0061 | symmetric |
| 125/121 | RenderFogOverlay | 0x61F9E0 | 1.00 / 1.00 | 0.0061 | 0.0066 | 0.0066 | symmetric |
| 15/1 | PrepareCollector | 0x79B03C | 1.00 / 1.00 | 0.0060 | 0.0062 | 0.0062 | symmetric |
| 12/0 | SimulateOffScreenCPUParticles | 0xB4C3CC | 2.00 / 0.00 | 0.0118 | 0.0000 | 0.0000 | MAIN-only |
| 27/26 | UnbindGlobalConstants | 0xA2BABC | 28.00 / 25.00 | 0.0061 | 0.0053 | 0.0053 | symmetric |

## VRCAM-heavy (>=1.5x MAIN)

A big ratio is NOT proof of duplication: for a shared incremental resource the first view in the frame (VRCAM) becomes the OWNER and pays once for everybody. Check the owner bit `[a2+0x30]&2` before skipping anything.

| node | rva | self ms/frame v | ratio |
|---|---|---|---|
| ReflectionProbes | 0x77E610 | 0.1198 | 6.2x |
| RenderVolumetricCloudsLighting | 0x61BE74 | 0.0609 | 2.2x |
| RenderVolumetricClouds | 0x61B5B4 | 0.0479 | 1.7x |
| WindImpulseVolumeUpdate | 0x6EAEDC | 0.0417 | 18.0x |
| RenderBackground | 0x782EB8 | 0.0313 | 2.5x |
| CopyToTexture | 0x377B58 | 0.0249 | 20.1x |
