// 1 (default) = label each captured frame with the head orientation the camera injection
// actually used for it, instead of the pose cache as of Present. See the use site.
extern "C" __declspec(dllexport) int CyberpunkVR_BindPoseToImage = 1;
// Render-ahead depth, in PRESENT INTERVALS (not in pushes -- see the ring in the header).
//
// 0, and it is not a preference -- it is the only value CONSISTENT with the exact path.
//
// The exact path matches m_framePoseSerial == S, and that serial is (interval the write
// happened in) + 1, i.e. it returns the write made during interval S-1. The ring lookup with
// lag L returns the newest entry stamped <= S-1-L. Those two agree only at L = 0. With L = 1
// the ring handed back a pose one whole interval OLDER than the exact path -- so the 82% of
// frames that hit exact and the 18% that fell through were labelled a frame apart from each
// other. Every fallback frame therefore submitted an image with a stale orientation, the
// compositor re-warped it by a rotation already baked in, and the result is the judder that
// only shows on head turns. Measured live: PoseExact 29570 vs PoseEstimated 6363.
//
// 1 and 2 stay reachable because a different runtime could genuinely render deeper, but they
// must be compared against the exact path, not chosen on feel.
extern "C" __declspec(dllexport) int CyberpunkVR_PoseFrameLag = 0;
// One [vrik] line every two seconds: present rate vs the skeleton's real update rate.
extern "C" __declspec(dllexport) int CyberpunkVR_VrikRateLog = 1;   // (declared above <cstdint>, so plain int)
// How often the frame's pose is the exact one recorded inside that frame, versus the
// interval estimate. Exact should dominate; a rising estimate count means the depth barrier
// is not firing for those frames and the binding is back to guessing there.
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_DebugPoseExact = 0;
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_DebugPoseEstimated = 0;
// Thread the present/capture runs on; see the write site for why this settles PoseFrameLag.
extern "C" __declspec(dllexport) unsigned int CyberpunkVR_DebugTidPresent = 0;
// How often the frame being submitted had its own published slot (pose + per-eye offset + per-eye
// FOV, all located together for THIS serial) versus had to fall back to the live values. A miss
// is the old mixed-timeline path; it should be rare and it should not grow during gameplay.
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_DebugSlotHit  = 0;
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_DebugSlotMiss = 0;
// Intervals that contained no locate of their own, so the frame legitimately carries the previous
// slot. Expected to be substantial whenever the game presents faster than the XR loop cycles;
// only DebugSlotMiss (no slot at all) is a real fallback to live values.
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_DebugSlotReused = 0;
// Defined in openxr_frameloop.cpp -- which source the submitted centre pose came from.
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_DebugPoseFromWrite;
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_DebugPoseFromSlot;
// Frames labelled with the pose read back out of the engine at frame-open (vr_core.cpp).
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_DebugPoseReadBack = 0;
extern "C" __declspec(dllexport) int CyberpunkVR_PoseReadBack;

// openxr_present.cpp - OnPresent(): per-Present capture/submit trigger + eye schedule.
// Split verbatim from openxr_manager.cpp (OpenXRManager method). Shared module
// state/helpers via openxr_internal.h (inline).
#include "openxr_manager.h"
#include "openxr_internal.h"
#include "openxr_math.h"
#include "shared_slots.h"
#include "runtime_fov_correction.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <utility>
#include <chrono>
#include <thread>
#include <memory>
#include <algorithm>
#include <dxgi1_4.h>

void OpenXRManager::OnPresent(IDXGISwapChain* swapChain) {
    // [HANDS] Shared Memory Output
    static HANDLE s_hMapFile = NULL;
    static float* s_pSharedHands = nullptr;
    if (!s_hMapFile) {
        s_hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 1024, "CyberpunkVR_Hands_Shared");
        if (s_hMapFile) {
            s_pSharedHands = (float*)MapViewOfFile(s_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 1024);
            m_sharedHandsPtr = s_pSharedHands;   // expose to GetSharedSlot (overlay barrel crosshair)
        }
    }
    if (s_pSharedHands) {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_handMutex));
        // Slot [32]: VR hand-tracking request for the RED4ext plugin (set from the overlay menu).
        s_pSharedHands[32] = static_cast<float>(m_vrHandTrackingMode.load(std::memory_order_relaxed));
        s_pSharedHands[58] = static_cast<float>(m_weaponAimEnable.load(std::memory_order_relaxed)); // weapon-aim enable
        // shared[23]: 0/unset = immersive holsters (default), 1 = simple slot mapping. Inverted so the
        // zero-initialized shared block defaults to the immersive (current) behaviour before the first
        // publish. The CET Holster mod reads this via GetVRSharedSlot(23).
        s_pSharedHands[23] = (m_immersiveHolsters.load(std::memory_order_relaxed) != 0) ? 0.0f : 1.0f;
        s_pSharedHands[59] = 5.0f;  // mode 5 = game muzzle xform (the working solution)
        // [70..75]: anatomical HMD/body->shoulder offsets (auto-calibration result).
        // Right (rx,ry,rz), then left (lx,ly,lz). [76] = valid flag. Kept outside
        // [34..47], which is the regular calibration block.
        if (m_calibExtValid.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 6; ++i) s_pSharedHands[70 + i] = m_calibExt[i].load(std::memory_order_relaxed);
            s_pSharedHands[76] = 1.0f;
        }
        // [77..80]: T-pose measured anatomy (real arm length R/L, HMD eye height) + valid flag.
        // The plugin scales the avatar arm bones to match (gizmo-path), straightening a relaxed arm.
        if (m_measureValid.load(std::memory_order_relaxed)) {
            s_pSharedHands[77] = m_userArmLenR.load(std::memory_order_relaxed);
            s_pSharedHands[78] = m_userArmLenL.load(std::memory_order_relaxed);
            s_pSharedHands[79] = m_userEyeHeight.load(std::memory_order_relaxed);
            s_pSharedHands[80] = 1.0f;
        }
        // [89]: HMD PHYSICAL height relative to the recenter base (~0 standing, negative when the
        // user physically squats). The game FPP camera Lua samples is a FIXED eye height, so the
        // plugin needs this to actually lower the body / bend the knees on a real-life squat.
        // [85..88] are written by the plugin (camera->head offset) -- do not touch them here.
        // PAIR-LOCKED: use the frozen physical head height (snapshot at the pair
        // boundary). [89] head height + [90] neck-pivot are now written from the
        // frozen snapshot inside FlushHandsToShared (published at the pair boundary,
        // BEFORE the next pair's animation) together with the hand slots [0..19], so
        // they are no longer sampled live per present here.
        // [91..93]: the ACTIVE baked camera->head offset (game-local right/fwd/up). dxgi shifts the
        // VIEW by this in LocateCamera; the plugin adds the SAME offset to camModelPos so the avatar
        // head sits exactly where the (offset-tuned) view sits -> head = camera, body follows.
        {
            float cb[3]; GetCameraOffset(cb);
            s_pSharedHands[91] = cb[0]; s_pSharedHands[92] = cb[1]; s_pSharedHands[93] = cb[2];
        }
        // IMPORTANT: hand pose slots [0..19] are flushed in OnLocateCameraCallback
        // BEFORE render (FlushHandsToShared). Do NOT rewrite them here after render,
        // or the next frame may see a mixed temporal state (one wrong frame even
        // on the flat monitor). Keep OnPresent for config/static
        // slots only.

        // [33..47] IK calibration from the overlay; [48] one-shot diag request.
        s_pSharedHands[33] = static_cast<float>(m_calibValid.load(std::memory_order_relaxed));
        for (int i = 0; i < 14; ++i) s_pSharedHands[34 + i] = m_calib[i].load(std::memory_order_relaxed);
        s_pSharedHands[48] = static_cast<float>(m_logDiagReq.load(std::memory_order_relaxed));
    }

    if (!swapChain) return;

    // Compare against CyberpunkVR_DebugTidPatchCam: equal means the camera write and the frame
    // that carries it are serialised on one thread, so PoseFrameLag 0 is right by
    // construction. Different means a simulation thread runs ahead of this one and the lag is
    // whatever that depth is.
    CyberpunkVR_DebugTidPresent = GetCurrentThreadId();

    uint64_t s_presentCount = m_presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool monoEnabled = m_monoSubmitEnabled.load(std::memory_order_relaxed);

    // ===== POSE PAIR LOCKING — publish point (pipeline shift) =====
    // Snapshot the tracking state + write the VRIK shared slots HERE, BEFORE the
    // next animation/render pass (the plugin reads it during anim eval, which
    // precedes render/LocateCamera).
    //
    // Publish EVERY present. Publishing only on the pair boundary updated VRIK at HALF
    // the present rate -> hands "teleport" at ~20-45 Hz while the world rendered at 90.
    {
        UpdatePairLock();
        FlushHandsToShared();
    }

    // VRIK RATE CENSUS. The skeleton's update rate is not the present rate: the solve is clocked
    // by shared[99], the entity push seq, which the CET mod bumps from its own onUpdate. Everything
    // downstream of that -- arms, body, the lot -- moves at whatever rate that lands on. Printed
    // against this thread's present count so the two are directly comparable.
    if (s_pSharedHands && CyberpunkVR_VrikRateLog) {
        static uint64_t s_last = 0, s_presPrev = 0;
        static float s_prev[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        static float s_prevAge = 0.0f, s_prevSeq = 0.0f, s_prevView = 0.0f;
        static float s_prevGap = 0.0f, s_prevCoh = 0.0f;
        static unsigned long long s_prevExact = 0, s_prevEst = 0, s_prevMiss = 0, s_prevReuse = 0;
        const uint64_t now = GetTickCount64();
        if (!s_last) { s_last = now; s_presPrev = s_presentCount; }
        else if (now - s_last >= 2000) {
            const float dt = static_cast<float>(now - s_last) / 1000.0f;
            const float cur[4] = { s_pSharedHands[208], s_pSharedHands[209],
                                   s_pSharedHands[210], s_pSharedHands[99] };
            // Age of the controller pose the arms were solved from, and how many distinct
            // publishes those solves actually consumed. The peak matters more than the mean --
            // a head turn is where it is felt -- so it is cleared each window rather than averaged
            // away.
            const float dSolve = cur[1] - s_prev[1];
            const float ageMean = (dSolve > 0.5f) ? (s_pSharedHands[211] - s_prevAge) / dSolve : 0.0f;
            const float agePeak = s_pSharedHands[213];
            s_prevAge = s_pSharedHands[211];
            s_pSharedHands[213] = 0.0f;
            const float dHandSeq = s_pSharedHands[214] - s_prevSeq;
            s_prevSeq = s_pSharedHands[214];
            // The ANCHOR's age, separately: the hand offset is built to be head-rotation
            // independent, so what is left to shake the arms is the view pose they hang off
            // being older than the camera the frame is actually drawn with.
            const float viewMean = (dSolve > 0.5f) ? (s_pSharedHands[215] - s_prevView) / dSolve : 0.0f;
            const float viewPeak = s_pSharedHands[216];
            const float turnPeak = s_pSharedHands[217];
            s_prevView = s_pSharedHands[215];
            s_pSharedHands[216] = 0.0f;
            s_pSharedHands[217] = 0.0f;
            // The gap between the point the arms hang off and the point the frame is rendered
            // from, in millimetres, plus whether the coherent anchor is the one being used.
            const float gapMean = (dSolve > 0.5f) ? (s_pSharedHands[221] - s_prevGap) / dSolve : 0.0f;
            const float gapPeak = s_pSharedHands[222];
            const float cohPct  = (dSolve > 0.5f)
                ? 100.0f * (s_pSharedHands[223] - s_prevCoh) / dSolve : 0.0f;
            s_prevGap = s_pSharedHands[221];
            s_prevCoh = s_pSharedHands[223];
            s_pSharedHands[222] = 0.0f;
            // SUBMIT-POSE FIDELITY. A frame submitted under a pose other than the one it was
            // rendered from is re-warped by the compositor, and that error grows the nearer the
            // content is -- so the hands take the worst of it while the world still looks fine.
            // These counters already existed; nobody had put them next to the hand numbers.
            const unsigned long long ex = CyberpunkVR_DebugPoseExact;
            const unsigned long long es = CyberpunkVR_DebugPoseEstimated;
            const unsigned long long mi = CyberpunkVR_DebugSlotMiss;
            const unsigned long long ru = CyberpunkVR_DebugSlotReused;
            const double dEx = static_cast<double>(ex - s_prevExact);
            const double dEs = static_cast<double>(es - s_prevEst);
            const float exactPct = (dEx + dEs > 0.5) ? static_cast<float>(100.0 * dEx / (dEx + dEs)) : 0.0f;
            const float missRate = static_cast<float>(static_cast<double>(mi - s_prevMiss) / dt);
            const float reuseRate = static_cast<float>(static_cast<double>(ru - s_prevReuse) / dt);
            s_prevExact = ex; s_prevEst = es; s_prevMiss = mi; s_prevReuse = ru;
            // Peak second difference at three stages of the hand pipeline: raw controller,
            // anchor, final target. Smooth motion has a small second difference. Whichever stage
            // is the FIRST to be large is where the shake is added -- and if the controller
            // itself is already large, the noise is the runtime's and not ours.
            const float shk[3] = { s_pSharedHands[224], s_pSharedHands[225], s_pSharedHands[226] };
            s_pSharedHands[224] = 0.0f; s_pSharedHands[225] = 0.0f; s_pSharedHands[226] = 0.0f;
            s_pSharedHands[231] = 0.0f; s_pSharedHands[232] = 0.0f;
            Log("[vrik] present=%.1f/s  poseApply=%.1f/s  freshSolve=%.1f/s  replay=%.1f/s  "
                "entPush=%.1f/s  handPub=%.1f/s  hand age=%.1f/%.1fms  view age=%.1f/%.1fms  "
                "headTurn peak=%.2fdeg/solve  anchorGap=%.1f/%.1fmm  coherent=%.0f%%  "
                "poseExact=%.0f%%  slotMiss=%.1f/s  reused=%.1f/s  "
                "shake ctrl=%.1f anchor=%.1f target=%.1f shoulder=%.1f bodyCam=%.1f mm\n",
                static_cast<double>(s_presentCount - s_presPrev) / dt,
                (cur[0] - s_prev[0]) / dt, (cur[1] - s_prev[1]) / dt,
                (cur[2] - s_prev[2]) / dt, (cur[3] - s_prev[3]) / dt,
                dHandSeq / dt, ageMean, agePeak, viewMean, viewPeak, turnPeak,
                gapMean, gapPeak, cohPct, exactPct, missRate, reuseRate,
                shk[0], shk[1], shk[2], shk[3], shk[4]);
            for (int i = 0; i < 4; ++i) s_prev[i] = cur[i];
            s_last = now;
            s_presPrev = s_presentCount;
        }
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc))) {
        Log("OpenXRManager: Present hook could not read swapchain desc.\n");
        return;
    }

    IDXGISwapChain3* swapChain3 = nullptr;
    UINT backBufferIndex = 0;
    if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
        backBufferIndex = swapChain3->GetCurrentBackBufferIndex();
        swapChain3->Release();
    }

    ID3D12Resource* backBuffer = nullptr;
    D3D12_RESOURCE_DESC resourceDesc{};
    if (SUCCEEDED(swapChain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&backBuffer)))) {
        resourceDesc = backBuffer->GetDesc();
    }

    XrPosef monoCapturedPoses[2]{};
    XrFovf monoCapturedFovs[2]{};
    bool monoCapturedViews[2] = {};
    if (monoEnabled) {
        std::lock_guard<std::mutex> viewLock(m_viewMutex);
        if (m_views.size() >= 2) {
            float fovWidth = static_cast<float>(desc.BufferDesc.Width);
            float fovHeight = static_cast<float>(desc.BufferDesc.Height);
            if ((fovWidth <= 1.0f || fovHeight <= 1.0f) && resourceDesc.Width != 0 && resourceDesc.Height != 0) {
                fovWidth = static_cast<float>(resourceDesc.Width);
                fovHeight = static_cast<float>(resourceDesc.Height);
            }

            bool hasRenderHeadPose = false;
            XrPosef renderHeadPose{};
            renderHeadPose.orientation.w = 1.0f;
            {
                std::lock_guard<std::mutex> renderLock(m_renderPoseMutex);
                uint32_t renderedSeq = GetRenderedCameraSeq();
                int idx = renderedSeq % 256;
                
                if (renderedSeq > 0 && m_poseQueueFrame[idx] == renderedSeq) {
                    renderHeadPose = m_poseQueue[idx];
                    hasRenderHeadPose = true;
                } else if (m_renderEyeHeadPoseValid[0]) {
                    renderHeadPose = m_renderEyeHeadPose[0];
                    hasRenderHeadPose = true;
                }
            }

            // THE FRAME'S OWN SLOT, not "the current values".
            //
            // Everything below -- head centre, per-eye offset, per-eye FOV -- comes from the slot
            // published when this frame's pose was located, keyed by this present's serial. That
            // is the whole point: one identity per frame. `m_views` is only the fallback for a
            // frame whose slot is missing, and a miss is counted rather than hidden, exactly like
            // RealVR's "Rendering pose entry is invalid".
            OpenXRManager::XrFrameSlot slot{};
            bool slotExact = false;
            const bool haveSlot = GetFrameSlot(s_presentCount, &slot, &slotExact);
            if (!haveSlot)      ++CyberpunkVR_DebugSlotMiss;   // no slot at all -- live fallback
            else if (slotExact) ++CyberpunkVR_DebugSlotHit;    // this frame's own locate
            else                ++CyberpunkVR_DebugSlotReused; // no locate this interval; correct

            const XrPosef* srcViewPose = haveSlot ? slot.viewPose : nullptr;
            const XrFovf*  srcViewFov  = haveSlot ? slot.viewFov  : nullptr;
            const XrVector3f headCenter =
                srcViewPose
                    ? XrVector3f{ (srcViewPose[0].position.x + srcViewPose[1].position.x) * 0.5f,
                                  (srcViewPose[0].position.y + srcViewPose[1].position.y) * 0.5f,
                                  (srcViewPose[0].position.z + srcViewPose[1].position.z) * 0.5f }
                    : XrVector3f{ (m_views[0].pose.position.x + m_views[1].pose.position.x) * 0.5f,
                                  (m_views[0].pose.position.y + m_views[1].pose.position.y) * 0.5f,
                                  (m_views[0].pose.position.z + m_views[1].pose.position.z) * 0.5f };
            XrPosef monoCenterPose{};
            if (haveSlot) {
                // The located head pose of THIS frame, already in the layer's space -- no
                // recenter round-trip needed, and no chance of the two disagreeing.
                monoCenterPose = slot.headPoseLocal;
            } else {
                monoCenterPose.orientation = m_views[0].pose.orientation;
                monoCenterPose.position = headCenter;
            }
            // BIND THE POSE TO THE IMAGE (default).
            //
            // m_views is the pose cache as of NOW; the snapshot below was rendered from the
            // sample the camera injection took while the engine built it, one frame ago.
            // Labelling the image with the newer value makes the compositor place it ahead
            // of where it was drawn, and the next frame snaps back. The error is one frame
            // period, so it hides above ~80 fps and shows plainly at 50-60 -- and no amount
            // of SpaceWarp helps, because the pose is wrong rather than missing.
            //
            // The eye offsets still come from m_views: only the head ORIENTATION and centre
            // must belong to the image; the interpupillary offsets are static geometry.
            // Exact binding first: the pose recorded inside this very frame, stamped with
            // this present. Only if that is missing (the depth barrier did not fire -- menus,
            // loading, a frame the engine composed differently) does the interval-and-lag
            // estimate below stand in.
            OpenXRHeadPose pending{};
            // ONE SOURCE OF TRUTH, AND THE SLOT IS IT.
            //
            // These older paths (GetFramePoseForSerial, then the interval-and-lag ring) predate
            // the frame slot and are keyed differently: m_framePoseSerial is stamped
            // `presentCount + 1` inside PushRenderHeadPose with no knowledge of the engine
            // pipeline depth, while the slot is published under `presentCount + 1 + depth`. With
            // depth at 0 they agree by accident; the moment it is anything else they name
            // different frames -- and since this path takes precedence, part of the stream gets
            // labelled with another frame's pose.
            //
            // Measured, that is not a subtle error: over 120 frames of head motion the gap
            // between the submitted pose and the live pose ran min 0.00 deg, max 55.63, avg 3.10.
            // A real render-to-photon latency is large but STEADY and reprojects away cleanly; a
            // gap that swings from nothing to fifty degrees is two mechanisms disagreeing, and it
            // is exactly what judder looks like from the inside.
            //
            // THE WRITTEN SAMPLE WINS OVER THE SLOT. This ordering is the point.
            //
            // slot.headPoseLocal is the frame loop's OWN xrLocateSpace. The pixels were built
            // from the sample the camera injection took (AcquireFrameHeadSample), which is a
            // different call -- same target instant, but made at a different wall-clock moment
            // and therefore a different answer, and until now also the only one that carried the
            // head POSITION the view was actually placed at. Submitting the slot meant labelling
            // the image with a pose it was never rendered from, which is exactly the thing the
            // OpenXR guide names as the cause of artifacts: the runtime cannot know which pose a
            // frame used, so what is submitted must BE that pose.
            //
            // The slot is still where the per-eye offsets and the FOV come from -- those are
            // geometry, not a sample -- and it remains the fallback for a frame with no write.
            bool haveExactPose = false;
            if (CyberpunkVR_BindPoseToImage) {
                // FIRST: the pose read back out of the engine at frame-open for THIS frame.
                // It is not derived from any assumption about render-ahead depth -- the render
                // side recognised the quaternion it was about to draw with, so this is the pose
                // in the pixels by identification rather than by arithmetic. The serial-keyed
                // paths below remain for frames where the read-back found no match (a menu, a
                // load, a frame the engine composed without our write).
                haveExactPose =
                    (CyberpunkVR_PoseReadBack &&
                     OpenXRManager::Get().PopRenderedFramePose(&pending) && pending.valid);
                if (haveExactPose) ++CyberpunkVR_DebugPoseReadBack;
                if (!haveExactPose) {
                    haveExactPose =
                        (OpenXRManager::Get().GetFramePoseForSerial(s_presentCount, &pending) &&
                         pending.valid) ||
                        (OpenXRManager::Get().GetRenderHeadPoseForPresent(
                             s_presentCount, CyberpunkVR_PoseFrameLag, &pending) &&
                         pending.valid);
                }
            }
            if (haveExactPose) {
                ++CyberpunkVR_DebugPoseExact;
                ++CyberpunkVR_DebugPoseFromWrite;
            } else if (haveSlot && slotExact) {
                // monoCenterPose already holds the slot's headPoseLocal, in the layer's space.
                ++CyberpunkVR_DebugPoseEstimated;
                ++CyberpunkVR_DebugPoseFromSlot;
            }
            if (haveExactPose) {
                // THE WHOLE POSE, AND IN THE LAYER'S SPACE. Both halves of that mattered.
                //
                // Only the orientation used to be taken from the rendered pose; the position
                // stayed the head centre as of NOW, from m_views. So the image carried the head
                // position it was drawn at while the label said where the head is at submit
                // time, and the compositor duly re-projected away a translation that was
                // already in the pixels. On head motion that is a lag-then-snap of exactly one
                // frame -- which is what "дрожь" is. The OpenXR guide states the invariant
                // plainly: the runtime has no way to know which pose a frame was rendered with,
                // so what is submitted must BE that pose. Crysis VR does the same thing the
                // simple way -- FinishFrame submits m_renderViews[eye].pose whole, the very
                // struct AwaitFrame filled.
                //
                // And the space: GetHeadPose() is recenter-relative, the layer is m_localSpace.
                // Undo the base here rather than shipping a pose from the wrong frame of
                // reference (identity base hides it; a recenter does not).
                XrPosef base{};
                OpenXRManager::Get().GetRecenterBase(&base);
                const XrQuaternionf relOri{ pending.oriX, pending.oriY, pending.oriZ, pending.oriW };
                const XrVector3f relPos{ pending.posX, pending.posY, pending.posZ };
                const XrVector3f rotated = RotateVector(base.orientation, relPos);
                monoCenterPose.orientation = MultiplyQuat(base.orientation, relOri);
                monoCenterPose.position = XrVector3f{
                    base.position.x + rotated.x,
                    base.position.y + rotated.y,
                    base.position.z + rotated.z };
            } else if (GetRenderPoseSubmit() != 0 && hasRenderHeadPose) {
                monoCenterPose = renderHeadPose;
            }
            // The eye offset is STATIC GEOMETRY IN HEAD SPACE, so it has to be rotated by the
            // orientation the frame was RENDERED with -- not by the head orientation as of now.
            //
            // m_views gives the offsets already rotated into local space by the CURRENT head
            // orientation. Adding those to a centre taken from the rendered pose mixes two
            // orientations again: the error is (current - rendered) applied to a half-IPD lever,
            // so it is zero when still and swings back and forth in step with head rotation --
            // small, but exactly the kind of oscillation that reads as swim. Take the offset
            // back into head space with the current orientation, then out again with the
            // rendered one.
            // Source geometry from the FRAME'S slot when we have it -- the eye offsets and the
            // orientation they were located with belong to the same instant as the image. Only a
            // slot miss falls back to `m_views` as of now, which is the old mixed-timeline case.
            const XrQuaternionf curHeadOri =
                srcViewPose ? srcViewPose[0].orientation : m_views[0].pose.orientation;
            const XrQuaternionf curHeadOriInv = ConjugateQuat(curHeadOri);
            for (int eye = 0; eye < 2; ++eye) {
                const XrPosef& srcPose = srcViewPose ? srcViewPose[eye] : m_views[eye].pose;
                const XrVector3f eyeOffsetLocal{
                    srcPose.position.x - headCenter.x,
                    srcPose.position.y - headCenter.y,
                    srcPose.position.z - headCenter.z};
                const XrVector3f eyeOffsetHead = RotateVector(curHeadOriInv, eyeOffsetLocal);
                const XrVector3f eyeOffset =
                    RotateVector(monoCenterPose.orientation, eyeOffsetHead);
                monoCapturedPoses[eye] = monoCenterPose;
                monoCapturedPoses[eye].position.x += eyeOffset.x;
                monoCapturedPoses[eye].position.y += eyeOffset.y;
                monoCapturedPoses[eye].position.z += eyeOffset.z;
                const XrFovf srcFov0 = srcViewFov ? srcViewFov[0] : m_views[0].fov;
                const XrFovf srcFov1 = srcViewFov ? srcViewFov[1] : m_views[1].fov;
                XrFovf monoPairFovs[2] = { srcFov0, srcFov1 };
                monoCapturedFovs[eye] = ApplyForcedProjectionFov(
                    srcViewFov ? srcViewFov[eye] : m_views[eye].fov,
                    monoPairFovs, eye, fovWidth, fovHeight);
                // No cant pose rotation (removed): in mono both eyes derive from ONE
                // frame, so a per-eye cant delta doubled the whole image.
                monoCapturedViews[eye] = true;
            }
        }
    }
    bool monoCaptureOk = false;
    if (monoEnabled && backBuffer) {
        monoCaptureOk = CaptureMonoPresentedFrame(backBuffer, resourceDesc, s_presentCount,
            monoCapturedPoses, monoCapturedFovs, monoCapturedViews);
        if (!monoCaptureOk && (s_presentCount % 300) == 1) {
            Log("OpenXRManager: Mono capture failed. serial=%llu views=(%d,%d)\n",
                static_cast<unsigned long long>(s_presentCount),
                monoCapturedViews[0] ? 1 : 0,
                monoCapturedViews[1] ? 1 : 0);
        }
    }

    std::unique_lock<std::mutex> presentLock(m_presentMutex);
        if (m_lastPresentedBackBuffer) {
            m_lastPresentedBackBuffer->Release();
            m_lastPresentedBackBuffer = nullptr;
        }

        m_lastPresentedWidth = resourceDesc.Width != 0 ? static_cast<uint32_t>(resourceDesc.Width) : desc.BufferDesc.Width;
        m_lastPresentedHeight = resourceDesc.Height != 0 ? resourceDesc.Height : desc.BufferDesc.Height;
        m_lastPresentedFormat = resourceDesc.Format != DXGI_FORMAT_UNKNOWN ? static_cast<uint32_t>(resourceDesc.Format) : static_cast<uint32_t>(desc.BufferDesc.Format);
        m_lastPresentedBufferIndex = backBufferIndex;
        m_lastPresentSerial = s_presentCount;
    if (backBuffer) {
        backBuffer->Release();
        backBuffer = nullptr;
    }

    // [HMD-Paced Frame Sync] Lock the game engine to the OpenXR compositor rate.
    // By waiting for the compositor to finish xrEndFrame, we ensure the game
    // never runs ahead, and the next GetHeadPose will have the absolutely
    // fresh predicted display time for the subsequent frame.
    //
    // Inline submit runs the XR frame loop directly from the Present hook, so there is
    // no separate frame thread to wait for here.

    if ((s_presentCount % 300) != 1) return;

    Log("OpenXRManager: Present observed. hwnd=%p size=%ux%u format=%u backbufferIndex=%u resourceWidth=%llu resourceHeight=%u sessionRunning=%d\n",
        desc.OutputWindow,
        desc.BufferDesc.Width,
        desc.BufferDesc.Height,
        static_cast<unsigned>(desc.BufferDesc.Format),
        backBufferIndex,
        static_cast<unsigned long long>(resourceDesc.Width),
        resourceDesc.Height,
        IsSessionRunning() ? 1 : 0);
}
