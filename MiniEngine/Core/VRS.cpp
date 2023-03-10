// Copyright (C) 2022 Intel Corporation

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom
// the Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
// OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

#include "pch.h"
#include "VRS.h"
#include "Display.h"
#include "Camera.h"
#include "GraphicsCore.h"
#include "BufferManager.h"
#include "ReadbackBuffer.h"
#include "CommandContext.h"
#include "CommandListManager.h"
#include "Util/CommandLineArg.h"
#include "Utility.h"
#include "DepthOfField.h"
#include "GpuResource.h"

#include "CompiledShaders/VRSScreenSpace_RGB_CS.h"
#include "CompiledShaders/VRSScreenSpace_RGB2_CS.h"

#include "CompiledShaders/VRSContrastAdaptive8x8_RGB_CS.h"
#include "CompiledShaders/VRSContrastAdaptive8x8_RGB2_CS.h"

#include "CompiledShaders/VRSContrastAdaptive16x16_RGB_CS.h"
#include "CompiledShaders/VRSContrastAdaptive16x16_RGB2_CS.h"

using namespace Graphics;

namespace VRS
{
    D3D12_QUERY_DATA_PIPELINE_STATISTICS PipelineStatistics;
    ShadingRatePercents Percents;
    ReadbackBuffer VRSReadbackBuffer;
    BoolVar CalculatePercents("VRS/VRS Debug/Calculate %s", true);
    BoolVar UseHighResVelocityBuffer("VRS/Use High Res Velocity Buffer", true);

    const char* VRSLabels[] = { "1X1", "1X2", "2X1", "2X2", "2X4", "4X2", "4X4" };
    const char* combiners[] = { "Passthrough", "Override", "Min", "Max", "Sum" };
    const char* modes[] = { "Contrast Adaptive (GPU)" };

    RootSignature Debug_RootSig;
    RootSignature ContrastAdaptive_RootSig;

    ComputePSO VRSDebugScreenSpaceCS(L"VRS: Debug Screen Space");
    ComputePSO VRSContrastAdaptiveCS(L"VRS: Contrast Adaptive");

    D3D12_VARIABLE_SHADING_RATE_TIER ShadingRateTier = {};
    UINT ShadingRateTileSize = 16;
    BOOL ShadingRateAdditionalShadingRatesSupported = {};

    BoolVar Enable("VRS/Enable", true);
    BoolVar RelaxedMode("VRS/Relaxed Mode", false); 

    EnumVar VRSShadingRate("VRS/Tier 1 Shading Rate", 0, 7, VRSLabels);
    EnumVar ShadingRateCombiners1("VRS/1st Combiner", 0, 5, combiners);
    EnumVar ShadingRateCombiners2("VRS/2nd Combiner", 1, 5, combiners);
    
    EnumVar ShadingModes("VRS/Shading Mode", 0, 1, modes);

    BoolVar DebugDraw("VRS/VRS Debug/Debug", false);
    BoolVar DebugDrawBlendMask("VRS/VRS Debug/Blend Mask", true);
    BoolVar DebugDrawDrawGrid("VRS/VRS Debug/Draw Grid", false);

    BoolVar ConstrastAdaptiveDynamic("VRS/VRS Contrast Adaptive/Dynamic Threshold", false);
    NumVar ConstrastAdaptiveDynamicFPS("VRS/VRS Contrast Adaptive/Dynamic Threshold FPS", 30, 15, 60, 1);
    NumVar ContrastAdaptiveK("VRS/VRS Contrast Adaptive/Quarter Rate Sensitivity", 2.13f, 0.0f, 10.0f, 0.01f);
    NumVar ContrastAdaptiveSensitivityThreshold("VRS/VRS Contrast Adaptive/Sensitivity Threshold", 0.5f, 0.0f, 1.0f, 0.01f);
    NumVar ContrastAdaptiveEnvLuma("VRS/VRS Contrast Adaptive/Env. Luma", 0.05f, 0.0f, 10.0f, 0.001f);
    NumVar ContrastAdaptiveWeberFechnerConstant("VRS/VRS Contrast Adaptive/Weber-Fechner Constant", 1.0f, 0.0f, 10.0f, 0.01f);
    BoolVar ContrastAdaptiveUseWeberFechner("VRS/VRS Contrast Adaptive/Use Weber-Fechner", false);
    BoolVar ContrastAdaptiveUseMotionVectors("VRS/VRS Contrast Adaptive/Use Motion Vectors", false);
    BoolVar ContrastAdaptiveAllowQuarterRate("VRS/VRS Contrast Adaptive/Allow Quarter Rate", true);
}

void VRS::ParseCommandLine()
{
    // VRS on or off
    std::wstring enableVRS = {};
    bool foundArg = CommandLineArgs::GetString(L"vrs", enableVRS);
    if (foundArg && enableVRS.compare(L"off") == 0)
    {
        Enable = false;
    }

    // Debug draw
    std::wstring debugVRS = {};
    foundArg = CommandLineArgs::GetString(L"overlay", debugVRS);
    if (foundArg && debugVRS.compare(L"on") == 0)
    {
        DebugDraw = true;
    }

    // Tier 1 Shading Rate
    std::wstring shadingRateVRS = {};
    foundArg = CommandLineArgs::GetString(L"rate", shadingRateVRS);
    if (foundArg)
    {
        if (shadingRateVRS.compare(L"1X2") == 0)
        {
            VRSShadingRate = ShadingRates::OneXTwo;
        }
        else if (shadingRateVRS.compare(L"2X1") == 0)
        {
            VRSShadingRate = ShadingRates::TwoXOne;
        }
        else if (shadingRateVRS.compare(L"2X2") == 0)
        {
            VRSShadingRate = ShadingRates::TwoXTwo;
        }
        else if (shadingRateVRS.compare(L"2X4") == 0 && ShadingRateAdditionalShadingRatesSupported)
        {
            VRSShadingRate = ShadingRates::TwoXFour;
        }
        else if (shadingRateVRS.compare(L"4X2") == 0 && ShadingRateAdditionalShadingRatesSupported)
        {
            VRSShadingRate = ShadingRates::FourXTwo;
        }
        else if (shadingRateVRS.compare(L"4X4") == 0 && ShadingRateAdditionalShadingRatesSupported)
        {
            VRSShadingRate = ShadingRates::FourXFour;
        }
    }

    // Tier 2 Shading Rate Combiners
    std::wstring combiner1 = {};
    std::wstring combiner2 = {};
    foundArg = CommandLineArgs::GetString(L"combiner1", combiner1);
    if (foundArg)
    {
        ShadingRateCombiners1 = SetCombinerUI(combiner1);
    }
    foundArg = CommandLineArgs::GetString(L"combiner2", combiner2);
    if (foundArg)
    {
        ShadingRateCombiners2 = SetCombinerUI(combiner2);
    }
} 

void VRS::Initialize()
{
    if (!ShadingRateAdditionalShadingRatesSupported)
    {
        VRSShadingRate.SetListLength(4);
    }
    ParseCommandLine();
    
    Debug_RootSig.Reset(2, 0);
    Debug_RootSig[0].InitAsConstants(0, 7);
    Debug_RootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
    Debug_RootSig.Finalize(L"Debug_VRS");

    ContrastAdaptive_RootSig.Reset(2, 0);
    ContrastAdaptive_RootSig[0].InitAsConstants(0, 15);
    ContrastAdaptive_RootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 4);
    ContrastAdaptive_RootSig.Finalize(L"ContrastAdaptive_VRS");

#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetRootSignature(Debug_RootSig); \
    ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
    ObjName.Finalize();

    if (g_bTypedUAVLoadSupport_R11G11B10_FLOAT)
    {
        CreatePSO(VRSDebugScreenSpaceCS, g_pVRSScreenSpace_RGB2_CS);
    }
    else
    {
        CreatePSO(VRSDebugScreenSpaceCS, g_pVRSScreenSpace_RGB_CS);
    }

#undef CreatePSO

#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetRootSignature(ContrastAdaptive_RootSig); \
    ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
    ObjName.Finalize();

    if (IsVRSTierSupported(D3D12_VARIABLE_SHADING_RATE_TIER_2))
    {
        if (ShadingRateTileSize == 8)
        {
            if (g_bTypedUAVLoadSupport_R11G11B10_FLOAT)
            {
                CreatePSO(VRSContrastAdaptiveCS, g_pVRSContrastAdaptive8x8_RGB2_CS);
            }
            else
            {
                CreatePSO(VRSContrastAdaptiveCS, g_pVRSContrastAdaptive8x8_RGB_CS);
            }
        }
        else
        {
            if (g_bTypedUAVLoadSupport_R11G11B10_FLOAT)
            {
                CreatePSO(VRSContrastAdaptiveCS, g_pVRSContrastAdaptive16x16_RGB2_CS);
            }
            else
            {
                CreatePSO(VRSContrastAdaptiveCS, g_pVRSContrastAdaptive16x16_RGB_CS);
            }
        }
    }
#undef CreatePSO

    g_VRSTier2Buffer.SetClearColor(Color(D3D12_SHADING_RATE_1X1));
    VRSReadbackBuffer.Create(L"VRS Readback", g_VRSTier2Buffer.GetWidth() * g_VRSTier2Buffer.GetHeight(), sizeof(uint8_t));
}

void VRS::CheckHardwareSupport()
{
    // Check for VRS hardware support
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options = {};
    if (SUCCEEDED(g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options))))
    {
        ShadingRateTier = options.VariableShadingRateTier;

        if (ShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_1)
        {
            ShadingRateAdditionalShadingRatesSupported = options.AdditionalShadingRatesSupported;
        }
        if (ShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2)
        {
            printf("Tier 2 VRS supported\n");
            ShadingRateTileSize = options.ShadingRateImageTileSize;
            printf("Tile size: %u\n", ShadingRateTileSize);
        }
    }
    else
    {
        // These values should be already set from the call above, but I set them again here just for clarification :)
        ShadingRateTier = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
        printf("VRS not supported on this hardware!\n");
        ShadingRateAdditionalShadingRatesSupported = 0;
        ShadingRateTileSize = 0;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS10 options2 = {};
    if (SUCCEEDED(g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS10, &options2, sizeof(options2))))
    {
        printf("VariableRateShadingSumCombiner: %d\nMeshShaderPerPrimitiveShading %d\n", options2.VariableRateShadingSumCombinerSupported, options2.MeshShaderPerPrimitiveShadingRateSupported);
    }
}

VRS::Combiners VRS::SetCombinerUI(std::wstring combiner )
{
    combiner = Utility::ToLower(combiner);
    if (combiner.compare(L"passthrough") == 0)
    {
        return Combiners::Passthrough;
    }
    else if (combiner.compare(L"override") == 0)
    {
        return Combiners::Override;
    }
    else if (combiner.compare(L"min") == 0)
    {
        return Combiners::Min;
    }
    else if (combiner.compare(L"max") == 0)
    {
        return Combiners::Max;
    }
    else if (combiner.compare(L"sum") == 0)
    {
        return Combiners::Sum;
    }
    else
    {
        printf("User did not enter valid shading rate combiner. Defaulting to passthrough.\n");
        return Combiners::Passthrough;
    }
}

VRS::ShadingMode VRS::GetShadingMode(const char* mode)
{
    VRS::ShadingMode selectedMode = VRS::ShadingMode::ContrastAdaptiveGPU;

    return selectedMode;
}

D3D12_SHADING_RATE_COMBINER VRS::GetCombiner(const char* combiner)
{
    if (strcmp(combiner, "Passthrough") == 0)
    {
        return D3D12_SHADING_RATE_COMBINER_PASSTHROUGH;
    }
    else if (strcmp(combiner, "Override") == 0)
    {
        return D3D12_SHADING_RATE_COMBINER_OVERRIDE;
    }
    else if (strcmp(combiner, "Min") == 0)
    {
        return D3D12_SHADING_RATE_COMBINER_MIN;
    }
    else if (strcmp(combiner, "Max") == 0)
    {
        return D3D12_SHADING_RATE_COMBINER_MAX;
    }
    else if (strcmp(combiner, "Sum") == 0)
    {
        return D3D12_SHADING_RATE_COMBINER_SUM;
    }
    else
    {
        printf("Could not retrieve shading rate combiner. Setting to passthrough.\n");
        return D3D12_SHADING_RATE_COMBINER_PASSTHROUGH;
    }
}

D3D12_SHADING_RATE VRS::GetCurrentTier1ShadingRate(int shadingRate)
{
    D3D12_SHADING_RATE rate = (D3D12_SHADING_RATE)-1;

    //Map to UI values in VRSShadingRate
    switch (shadingRate) {
    case 0:
        rate = D3D12_SHADING_RATE_1X1;
        break;
    case 1:
        rate = D3D12_SHADING_RATE_1X2;
        break;
    case 2:
        rate = D3D12_SHADING_RATE_2X1;
        break;
    case 3:
        rate = D3D12_SHADING_RATE_2X2;
        break;
    case 4:
        if(ShadingRateAdditionalShadingRatesSupported)
            rate = D3D12_SHADING_RATE_2X4;
        break;
    case 5:
        if (ShadingRateAdditionalShadingRatesSupported)
            rate = D3D12_SHADING_RATE_4X2;
        break;
    case 6:
        if (ShadingRateAdditionalShadingRatesSupported)
            rate = D3D12_SHADING_RATE_4X4;
        break;
    }

    if (rate == ((D3D12_SHADING_RATE)-1))
    {
        printf("User did not enter valid shading rate input or additional shading rates not supported. Defaulting to 1X1.\n");
        rate = D3D12_SHADING_RATE_1X1;
    }

    return rate;
}

void VRS::Shutdown(void) {
    VRSReadbackBuffer.Destroy();
}

bool VRS::IsVRSSupported() {
    bool isSupported = true;
    if (ShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
        isSupported = false;
    }

    return isSupported;
}

bool VRS::IsVRSRateSupported(D3D12_SHADING_RATE rate) {
    bool isRateSupported = true;
    if (ShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
        isRateSupported = false;
    }

    if (!ShadingRateAdditionalShadingRatesSupported && rate > D3D12_SHADING_RATE_2X2) {
        isRateSupported = false;
    }

    return isRateSupported;
}

bool VRS::IsVRSTierSupported(D3D12_VARIABLE_SHADING_RATE_TIER tier) {
    return (ShadingRateTier >= tier);
}

void VRS::Update()
{
    static float prevCenterX = 0.0f;
    static float prevCenterY = 0.0f;
    static bool wasTrackMouse = false;

    if (IsVRSTierSupported(D3D12_VARIABLE_SHADING_RATE_TIER_2))
    {
        VRS::ShadingMode mode = (VRS::ShadingMode)((int32_t)VRS::ShadingModes);

        if(mode == VRS::ShadingMode::ContrastAdaptiveGPU)
        {
            if ((bool)ConstrastAdaptiveDynamic)
            {
                ContrastAdaptiveSensitivityThreshold = EngineProfiling::GetTotalGpuTime() / (1000.0f / (float)ConstrastAdaptiveDynamicFPS);
            }
        }
    }
}

void VRS::Render(ComputeContext& Context, bool upscaledOverlay)
{
    if (IsVRSTierSupported(D3D12_VARIABLE_SHADING_RATE_TIER_2))
    {
       VRS::ShadingMode mode = (VRS::ShadingMode)((int32_t)VRS::ShadingModes);

       if (mode == ShadingMode::ContrastAdaptiveGPU)
        {
            ColorBuffer& Target = g_bTypedUAVLoadSupport_R11G11B10_FLOAT ? g_SceneColorBuffer
                                      : g_PostEffectsBuffer;

            ColorBuffer *velocityBuffer;

            D3D12_CPU_DESCRIPTOR_HANDLE Pass1UAVs[] =
            {g_VRSTier2Buffer.GetUAV(), Target.GetUAV(),
                g_VelocityBuffer.GetUAV(), g_UpscaledVelocityBuffer.GetUAV()
            };

            Context.SetRootSignature(ContrastAdaptive_RootSig);
            Context.SetConstant(0, 0, Target.GetWidth());
            Context.SetConstant(0, 1, Target.GetHeight());
            Context.SetConstant(0, 2, ShadingRateTileSize);
            Context.SetConstant(0, 3, (float)ContrastAdaptiveSensitivityThreshold);
            Context.SetConstant(0, 4, (float)ContrastAdaptiveEnvLuma);
            Context.SetConstant(0, 5, (float)ContrastAdaptiveK);
            Context.SetConstant(0, 6, (float)ContrastAdaptiveWeberFechnerConstant);
            Context.SetConstant(0, 7, (bool)ContrastAdaptiveUseWeberFechner);
            Context.SetConstant(0, 8, (bool)ContrastAdaptiveUseMotionVectors);
            Context.SetConstant(0, 9, (bool)ContrastAdaptiveAllowQuarterRate);
            Context.SetConstant(0, 10, g_NativeWidth);
            Context.SetConstant(0, 11, g_NativeHeight);
            Context.SetConstant(0, 12, g_UpscaledSceneColorBuffer.GetWidth());
            Context.SetConstant(0, 13, g_UpscaledSceneColorBuffer.GetHeight());
            Context.SetConstant(0, 14, (bool)UseHighResVelocityBuffer);
            Context.SetDynamicDescriptors(1, 0, _countof(Pass1UAVs), Pass1UAVs);
            Context.TransitionResource(g_VRSTier2Buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.TransitionResource(Target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.TransitionResource(g_VelocityBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.TransitionResource(g_UpscaledVelocityBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.SetPipelineState(VRSContrastAdaptiveCS);
            Context.Dispatch((UINT)ceil((float)Target.GetWidth() / (float)ShadingRateTileSize),
                             (UINT)ceil((float)Target.GetHeight() / (float)ShadingRateTileSize));
        }

        if (DebugDraw)
        {
            ColorBuffer& Target =
                upscaledOverlay ? g_UpscaledSceneColorBuffer : g_SceneColorBuffer;
            D3D12_CPU_DESCRIPTOR_HANDLE Pass1UAVs[] =
            {
                g_VRSTier2Buffer.GetUAV(),
                Target.GetUAV(),
            };
            Context.SetRootSignature(Debug_RootSig);
            Context.SetConstant(0, 0, ShadingRateTileSize);
            Context.SetConstant(0, 1, (bool)DebugDrawBlendMask);
            Context.SetConstant(0, 2, (bool)DebugDrawDrawGrid);
            Context.SetConstant(0, 3, (uint32_t)g_SceneColorBuffer.GetWidth());
            Context.SetConstant(0, 4, (uint32_t)g_SceneColorBuffer.GetHeight());
            Context.SetConstant(0, 5, (uint32_t)Target.GetWidth());
            Context.SetConstant(0, 6, (uint32_t)Target.GetHeight());
            Context.SetDynamicDescriptors(1, 0, _countof(Pass1UAVs), Pass1UAVs);
            Context.TransitionResource(g_VRSTier2Buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.TransitionResource(Target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.SetPipelineState(VRSDebugScreenSpaceCS);
            Context.Dispatch(Target.GetWidth(), Target.GetHeight());
        }

        Context.TransitionResource(g_VRSTier2Buffer, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, true);
    }
}

void VRS::CalculateShadingRatePercentages(CommandContext& Context)
{
    uint8_t* vrsreadbackptr = nullptr;
    uint32_t vrsRowPitchInBytes = 0;

    int vrsHeight = g_VRSTier2Buffer.GetHeight();
    int vrsWidth = g_VRSTier2Buffer.GetWidth();

    vrsRowPitchInBytes = Context.ReadbackTexture(VRSReadbackBuffer, g_VRSTier2Buffer);
    Context.TransitionResource(g_VRSTier2Buffer, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, true);
    Context.Finish(true);
    
    Percents.num1x1 = 0;
    Percents.num1x2 = 0;
    Percents.num2x1 = 0;
    Percents.num2x2 = 0;
    Percents.num2x4 = 0;
    Percents.num4x2 = 0;
    Percents.num4x4 = 0;

    if (!(bool)VRS::Enable)
    {
        Percents.num1x1 = 100;
        return;
    }

    vrsreadbackptr = (uint8_t*)VRSReadbackBuffer.Map();

    int totalPixels = vrsHeight * vrsWidth;
    for (int yy = 0; yy < vrsHeight; yy++)
    {
        for (int xx = 0; xx < vrsWidth; xx++)
        {
            uint8_t currentPixel = vrsreadbackptr[yy * vrsRowPitchInBytes + xx];
            switch (currentPixel)
            {
            case D3D12_SHADING_RATE_1X1:
                Percents.num1x1++;
                break;
            case D3D12_SHADING_RATE_1X2:
                Percents.num1x2++;
                break;
            case D3D12_SHADING_RATE_2X1:
                Percents.num2x1++;
                break;
            case D3D12_SHADING_RATE_2X2:
                Percents.num2x2++;
                break;
            case D3D12_SHADING_RATE_2X4:
                Percents.num2x4++;
                break;
            case D3D12_SHADING_RATE_4X2:
                Percents.num4x2++;
                break;
            case D3D12_SHADING_RATE_4X4:
                Percents.num4x4++;
            }
        }
    }
    Percents.num1x1 = ((float)Percents.num1x1 / (float)totalPixels) * 100.0f;
    Percents.num1x2 = ((float)Percents.num1x2 / (float)totalPixels) * 100.0f;
    Percents.num2x1 = ((float)Percents.num2x1 / (float)totalPixels) * 100.0f;
    Percents.num2x2 = ((float)Percents.num2x2 / (float)totalPixels) * 100.0f;
    Percents.num2x4 = ((float)Percents.num2x4 / (float)totalPixels) * 100.0f;
    Percents.num4x2 = ((float)Percents.num4x2 / (float)totalPixels) * 100.0f;
    Percents.num4x4 = ((float)Percents.num4x4 / (float)totalPixels) * 100.0f;
    VRSReadbackBuffer.Unmap();
}