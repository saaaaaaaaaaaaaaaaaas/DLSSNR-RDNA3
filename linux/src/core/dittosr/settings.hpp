// From DittoSR (MIT); see linux/shaders/dittosr/LICENSE.
#pragma once
struct DittoSettings {
    float alphaRest = 0.035f;  // a stable pixel's share of each new frame at rest
    float conf = 0.05f;        // the confidence the narrow estimate is taken alone from (originally 0.3)
    float gammaRest = 0.0f;    // the history clip width at rest (0: 1.5)
    float width = 0.8f;        // the narrow Gaussian's width from 2x (x 0.875 at 1.5x and below; originally 1.4 - at 2.3x a blur as wide as a grass blade)
    float react = 1.0f;        // the reaction to changes
    float reactiveScale = 1.0f;
    float exposureScale = 1.0f;
    float jitterSignX = 1.0f, jitterSignY = 1.0f;
    float mvSign = 1.0f;
    float motionGamma = 1.5f;  // the history clip width while moving (originally 0.9)
    float motionShare = 0.15f;  // the new frame's base share while moving (originally 0.3)
    float motionMinMax = 1.0f; // how far the box is held within the samples' min / max while moving
    float sharpness = 0.6f;    // at rest 0 soft, 0.6 as reconstructed, 1 sharpened; moving, sharpened by it (sharpen.hlsl)
    float frameWeight = 2.0f;  // x the new frame's share, 0.25 - 4: lower, more persistence (smoother, blurrier in motion)
    float changeScale = 1.0f;  // how far the share of the image that changed drives the reaction
    // the new frame's share x shareFar where no jittered sample landed close to the pixel, x shareNear where one did
    // (originally 0.6 / 1.4 - at 2.3x most frames have no sample close: their blurred estimate blurred the history)
    float shareFar = 0.05f;
    float shareNear = 3.0f;
    float encodePower = 0.5f;  // the encoding's power after Reinhard (0.5: sqrt)
    // while moving (from 4 output pixels a frame) shareNear falls to motionNear: each frame's pattern of pixels a sample
    // landed near shimmers
    float motionNear = 1.5f;
    // the history's clip on its low frequencies alone (the 5 taps' mean): the current samples' aliasing in fine detail
    // no longer drags the history's accumulated detail toward it - only a real change, far out of the box, drops it
    float splitClip = 1.0f;
    // the history RGB10A2 (half the bandwidth of RGBA16F: the upscale reads and writes it at output size, the sharpening
    // reads it again - what an integrated GPU's shared memory feels most), its stores dithered; 0: RGBA16F
    int history10 = 1;
    // the FP16 shaders (the colour work in half precision), where the GPU has native 16-bit math: 1 always, -1 on AMD
    // only, 0 never - the default: on the ROG Ally they were slower than FP32 (2x 0.1 ms), on an RTX 5070 Ti ~7 %
    int fp16 = 0;
    // the reactive mask derived where the game gives none (particles, transparencies, flashes: more of the new frame),
    // its strength; 0: off
    float autoReactive = 0.5f;  // (1 until 0.3.0: at 0.5 the image gets sharp again a little sooner after a turn)
    // the history's stability level: the step a pixel climbs each frame a sample lands near it (1: stable). D3D12 keeps it
    // in its own R8 texture (the RGB10A2 history's alpha has 2 bits), Vulkan in the RGBA16F history's alpha
    float stabStep = 0.34f;
    // the anti-shimmer (D3D12; 0 off; -1, the default: 0.4 up to 2x, 0.2 from 3x): where the sign of the new frame's
    // difference from the history keeps alternating
    // (the jittered samples' aliasing), up to this share less of the new frame. Less shimmer (-10..-25 %), a little
    // softer fine detail in motion. The stability texture becomes R8G8 (the sign's running mean in .y)
    float antiShimmer = -1.0f;
    // the history's detail made up for its resampling blur in motion (upscale.hlsl): x its default strength (0.03 up to
    // 2x, 0.05 from 3x); 0 off
    float historySharpen = 1.0f;
    // the sharpening in the upscale, its own pass skipped (D3D12, up to 2.5x - above it the separate pass): the gain
    // of the detail term; 0 the separate pass. On the ROG Ally 0.3 ms less at 1080p 2x
    float fusedSharpen = 1.0f;
    bool indicator = false;
    int debugView = 0;  // 1: the motion; 2: the new frame's share; 3: the stability level; 4: clip / cut / confidence
};

