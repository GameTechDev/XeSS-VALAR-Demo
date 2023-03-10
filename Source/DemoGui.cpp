/*******************************************************************************
 * Copyright 2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files(the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions :
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "pch.h"
#include "..\Model\Model.h"
#include "DemoGui.h"
#include "DemoApp.h"
#include "ImGuiModule.h"
#include "GraphicsCore.h"
#include "Display.h"
#include "TemporalEffects.h"
#include "ParticleEffectManager.h"
#include "PostEffects.h"
#include "SSAO.h"
#include "MotionBlur.h"
#include "BufferManager.h"
#include "XeSS/XeSSProcess.h"
#include "XeSS/XeSSDebug.h"
#include "imgui.h"
#include "DemoCameraController.h"
#include "Renderer.h"

using namespace Graphics;
using namespace XeSS;

namespace DemoGui
{
    static const ImVec4 DEF_TEXT_COLOR(0.94f, 0.94f, 0.94f, 1.00f);

    bool m_ShowUI = true;
    bool m_ShowLog = false;
    bool m_ShowInspectInput = false;
    bool m_ShowInspectOutput = false;
    bool m_ShowInspectMotionVectors = false;
    bool m_ShowInspectDepth = false;
    bool m_ShowInspectResponsiveMask = false;

    float m_DPIScale = 1.0f;

    inline float DESIGN_SIZE(float size);
    inline ImVec2 DESIGN_SIZE(const ImVec2& size);
    void DPISetNextWindowRect(bool& adjust, const ImVec4& winRect);
    void DPIGetWindowRect(bool& resize, ImVec4& winRect);

    void OnGUI_XeSS();
    void OnGUI_Debug();
    void OnGUI_Rendering();
    void OnGUI_Camera(DemoApp& App);
    void OnGUI_VRS();
    void OnGUI_VRS_Debug();
    void OnGUI_DebugBufferWindows();
    void OnGUI_LogWindow(DemoApp& Log);

    class BufferViewer
    {
    public:
        BufferViewer(const std::string& title)
            : m_Title(title) { }
        void OnGUI(bool& Show, ColorBuffer& Buffer);

    private:
        std::string m_Title;
        int m_ZoomIndex = 0;
        ImVec2 m_ImagePos;
        ImVec2 m_LastMousePos;
        bool m_ScrollDirty = false;
        bool m_DPIAdjust = false;
        ImVec4 m_DPIWinRect;
    };
} // namespace DemoGui

float DemoGui::DESIGN_SIZE(float size)
{
    // We use DPI scale of 1.5 for the design.
    float scale = Display::GetDPIScale() / 1.5f;
    return size * scale;
}

ImVec2 DemoGui::DESIGN_SIZE(const ImVec2& size)
{
    // We use DPI scale of 1.5 for the design.
    float scale = Display::GetDPIScale() / 1.5f;
    return ImVec2(size.x * scale, size.y * scale);
}

void DemoGui::DPISetNextWindowRect(bool& adjust, const ImVec4& winRect)
{
    if (!adjust)
        return;

    ImGui::SetNextWindowPos(ImVec2(winRect.x, winRect.y));
    ImGui::SetNextWindowSize(ImVec2(winRect.z, winRect.w));

    adjust = false;

    LOG_DEBUG("DPI Set next GUI window rect for DPI change.");
}

void DemoGui::DPIGetWindowRect(bool& adjust, ImVec4& winRect)
{
    float newDPIScale = Display::GetDPIScale();
    adjust = newDPIScale != m_DPIScale;
    if (!adjust)
        return;

    float scale = newDPIScale / m_DPIScale;
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    winRect = ImVec4(pos.x * scale, pos.y * scale, size.x * scale, size.y * scale);

    LOG_DEBUG("Get next GUI window rect for DPI change.");
}

bool DemoGui::Initialize()
{
    ImGui::GetIO().IniFilename = nullptr;

    std::wstring programDir = Utility::GetProgramDirectory();
    std::wstring fontPath = programDir + L"DroidSans.ttf";
    ImGuiModule::AddFont(fontPath, 15.0f, false);

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.06f, 0.08f, 0.77f);
    colors[ImGuiCol_Header] = ImVec4(0.13f, 0.35f, 0.55f, 0.87f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.20f, 0.29f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.46f, 0.80f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.94f);
    colors[ImGuiCol_Text] = DEF_TEXT_COLOR;

    m_DPIScale = Display::GetDPIScale();

    return true;
}

void DemoGui::Shutdown()
{
}

void DemoGui::OnGUI(DemoApp& App)
{
    // F1 key to toggle GUI.
    if (ImGui::IsKeyPressed(VK_F1))
    {
        m_ShowUI = !m_ShowUI;
    }

    if (!m_ShowUI)
        return;

    ImGui::SetNextWindowPos(DESIGN_SIZE(ImVec2(10, 10)), ImGuiCond_Once);
    ImGui::SetNextWindowSize(DESIGN_SIZE(ImVec2(800, 1550)), ImGuiCond_Once);

    static bool adjust = false;
    static ImVec4 winRect;
    DPISetNextWindowRect(adjust, winRect);

    if (ImGui::Begin("Options (F1 to toggle)", &m_ShowUI))
    {
        DPIGetWindowRect(adjust, winRect);

        ImGui::Text("Adapter: %s", g_AdapterName.c_str());

        ImGui::Text("CPU %7.3f ms, GPU %7.3f ms, %3u Hz",
            EngineProfiling::GetTotalCpuTime(), EngineProfiling::GetTotalGpuTime(), static_cast<uint32_t>(EngineProfiling::GetFrameRate() + 0.5f));

        ImGui::Separator();

        ImGui::Text("Output: %ux%u", g_DisplayWidth, g_DisplayHeight);
        ImGui::Text("Input: %ux%u", g_NativeWidth, g_NativeHeight);

        ImGui::Separator();

        bool fullscreen = Display::IsFullscreen();
        if (ImGui::Checkbox(Display::g_SupportTearing ? "Fullscreen" : "Borderless", &fullscreen))
        {
            Display::SetFullscreen(fullscreen);
        }

        ImGui::SameLine();

        bool vsync = Display::s_EnableVSync;
        if (ImGui::Checkbox("VSync", &vsync))
        {
            Display::s_EnableVSync = vsync;
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Technique##Header", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static const char* TECHNIQUE_NAMES[] = { "XeSS", "TAA Without Upscaling", "TAA With Simple Upscaling" };

            int upscalingIndex = App.GetTechnique();
            if (ImGui::Combo("Technique", &upscalingIndex, TECHNIQUE_NAMES, IM_ARRAYSIZE(TECHNIQUE_NAMES)))
            {
                App.SetTechnique(static_cast<eDemoTechnique>(upscalingIndex));
            }

            // XeSS selected
            if (upscalingIndex == kDemoTech_XeSS)
            {
                OnGUI_XeSS();
            }
            else
            {
                float sharpness = TemporalEffects::Sharpness;
                if (ImGui::DragFloat("Sharpness", &sharpness, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    TemporalEffects::Sharpness = sharpness;
                }
            }
        }

        if (ImGui::CollapsingHeader("Scene Rendering", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Scene rendering GUI.
            OnGUI_Rendering();
        }

        if (ImGui::CollapsingHeader("Camera"))
        {
            OnGUI_Camera(App);
        }

        if (ImGui::CollapsingHeader("VRS##Header", ImGuiTreeNodeFlags_DefaultOpen))
        {
            OnGUI_VRS();
        }

        /*if (ImGui::CollapsingHeader("VRS Debug##Header", ImGuiTreeNodeFlags_DefaultOpen))
        {
            OnGUI_VRS_Debug();
        }*/

        if (ImGui::CollapsingHeader("Other##Header", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show Log", &m_ShowLog);
        }

        ImGui::Separator();

        ImGui::Text("SDK Version: %s", XeSS::g_XeSSRuntime.GetVersionString().c_str());
    }

    ImGui::End();

    // Show buffer viewers only when XeSS technique is used.
    if (App.GetTechnique() == kDemoTech_XeSS)
    {
        // Buffer viewer windows
        OnGUI_DebugBufferWindows();
    }

    // Log window.
    OnGUI_LogWindow(App);

    // Update DPI scale for this frame.
    m_DPIScale = Display::GetDPIScale();
}

void DemoGui::OnGUI_XeSS()
{
    if (!XeSS::IsSupported())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
        ImGui::Text("XeSS is not supported on this device.");
        ImGui::PopStyleColor();
        return;
    }

    float sharpness = XeSS::Sharpness;
    if (ImGui::DragFloat("Sharpness", &sharpness, 0.01f, 0.0f, 1.0f, "%.2f"))
    {
        XeSS::Sharpness = sharpness;
    }

    static const char* QUALITY_NAMES[] = { "Performance", "Balanced", "Quality", "Ultra Quality" };
    int32_t quality = XeSS::GetQuality();
    if (ImGui::Combo("Quality", &quality, QUALITY_NAMES, IM_ARRAYSIZE(QUALITY_NAMES)))
    {
        XeSS::SetQuality(static_cast<XeSS::eQualityLevel>(quality));
    }

    static const char* VELOCITY_MODE_NAMES[] = { "High-Res", "Low-Res" };
    int MVMode = XeSS::GetMotionVectorsMode();
    if (ImGui::Combo("Motion Vectors", &MVMode, VELOCITY_MODE_NAMES, IM_ARRAYSIZE(VELOCITY_MODE_NAMES)))
    {
        XeSS::SetMotionVectorsMode(static_cast<XeSS::eMotionVectorsMode>(MVMode));
        VRS::UseHighResVelocityBuffer = MVMode == kMotionVectorsHighRes;
    }

    bool jitteredMV = XeSS::IsMotionVectorsJittered();
    if (ImGui::Checkbox("Jittered Motion Vectors", &jitteredMV))
    {
        XeSS::SetMotionVectorsJittered(jitteredMV);
    }

    bool MVinNDC = XeSS::IsMotionVectorsInNDC();
    if (ImGui::Checkbox("NDC Motion Vectors", &MVinNDC))
    {
        XeSS::SetMotionVectorsInNDC(MVinNDC);
    }

    int mipBiasMode = XeSS::GetMipBiasMode();
    static const char* MIPBIAS_METHOD_NAMES[] = { "Recommended", "Customized" };
    if (ImGui::Combo("Mip Bias", &mipBiasMode, MIPBIAS_METHOD_NAMES, IM_ARRAYSIZE(MIPBIAS_METHOD_NAMES)))
    {
        XeSS::SetMipBiasMode(static_cast<eMipBiasMode>(mipBiasMode));
    }

    if (mipBiasMode == 0) // Recommended
        ImGui::BeginDisabled(true);

    float mipBias = XeSS::GetMipBias();
    if (ImGui::DragFloat("Mip Bias Value", &mipBias, 0.02f, -16.0f, 15.99f, "%.03f"))
    {
        XeSS::SetMipBias(mipBias);
    }
    if (mipBiasMode == 0) // Recommended
        ImGui::EndDisabled();

    bool isResponsiveMaskEnabled = XeSS::IsResponsiveMaskEnabled();
    if (ImGui::Checkbox("Responsive Mask", &isResponsiveMaskEnabled))
    {
        XeSS::SetResponsiveMaskEnabled(isResponsiveMaskEnabled);
    }

    if (ImGui::TreeNodeEx("Debug" /*, ImGuiTreeNodeFlags_DefaultOpen*/))
    {
        OnGUI_Debug();
        ImGui::TreePop();
    }
}

void DemoGui::OnGUI_Debug()
{
    static const char* NETWORK_NAMES[] = {
        "KPSS"
    };

    int networkModel = XeSSDebug::GetNetworkModel();
    if (ImGui::Combo("Network Model", &networkModel, NETWORK_NAMES, IM_ARRAYSIZE(NETWORK_NAMES)))
    {
        XeSSDebug::SelectNetworkModel(networkModel);
    }

    ImGui::PushItemWidth(DESIGN_SIZE(150.0f));

    static float JitterScaleX = 1.0f;
    static float JitterScaleY = 1.0f;

    bool jitterScaleDirty = false;
    if (ImGui::DragFloat("Jitter Scale X", &JitterScaleX, 0.1f, -16.0f, 16.0f, "%.1f"))
        jitterScaleDirty = true;

    //ImGui::SameLine();
    if (ImGui::DragFloat("Jitter Scale Y", &JitterScaleY, 0.1f, -16.0f, 16.0f, "%.1f"))
        jitterScaleDirty = true;

    if (jitterScaleDirty)
    {
        XeSS::g_XeSSRuntime.SetJitterScale(JitterScaleX, JitterScaleY);
    }

    static float VelocityScaleX = 1.0f;
    static float VelocityScaleY = 1.0f;

    bool velocityScaleDirty = false;
    if (ImGui::DragFloat("Velocity Scale X", &VelocityScaleX, 0.1f, -16.0f, 16.0f, "%.1f"))
        velocityScaleDirty = true;

    //ImGui::SameLine();
    if (ImGui::DragFloat("Velocity Scale Y", &VelocityScaleY, 0.1f, -16.0f, 16.0f, "%.1f"))
        velocityScaleDirty = true;

    if (velocityScaleDirty)
    {
        XeSS::g_XeSSRuntime.SetVelocityScale(VelocityScaleX, VelocityScaleY);
    }

    ImGui::PopItemWidth();

    // Buffer view options

    ImGui::Checkbox("Inspect Input", &m_ShowInspectInput);
    ImGui::Checkbox("Inspect Output", &m_ShowInspectOutput);
    ImGui::Checkbox("Inspect Motion Vectors", &m_ShowInspectMotionVectors);

    // Update the XeSS buffer debugging.
    if (m_ShowInspectOutput != XeSSDebug::IsBufferDebugEnabled())
    {
        XeSSDebug::SetBufferDebugEnabled(m_ShowInspectOutput);
    }

    if (XeSS::GetMotionVectorsMode() == XeSS::kMotionVectorsLowRes)
    {
        ImGui::Checkbox("Inspect Depth", &m_ShowInspectDepth);
    }
    else
    {
        m_ShowInspectDepth = false;
    }

    if (XeSS::IsResponsiveMaskEnabled())
    {
        ImGui::Checkbox("Inspect Responsive Mask", &m_ShowInspectResponsiveMask);
    }
    else
    {
        m_ShowInspectResponsiveMask = false;
    }

    if (ImGui::TreeNode("Frame Dump"))
    {
        bool isDumping = XeSSDebug::IsFrameDumpOn();

        if (isDumping)
            ImGui::BeginDisabled(true);
        if (ImGui::Button("Dump Static Frames"))
        {
            XeSSDebug::BeginFrameDump(false);
        }
        if (isDumping)
            ImGui::EndDisabled();

        if (XeSSDebug::IsFrameDumpOn() && !XeSSDebug::IsDumpDynamic())
        {
            ImGui::SameLine();
            ImGui::Text("Dumping Frame: %u/32...", XeSSDebug::GetDumpFrameIndex());
        }

        if (isDumping)
            ImGui::BeginDisabled(true);
        if (ImGui::Button("Dump Dynamic Frames"))
        {
            XeSSDebug::BeginFrameDump(true);
        }
        if (isDumping)
            ImGui::EndDisabled();

        if (XeSSDebug::IsFrameDumpOn() && XeSSDebug::IsDumpDynamic())
        {
            ImGui::SameLine();
            ImGui::Text("Dumping Frame: %u/32...", XeSSDebug::GetDumpFrameIndex());
        }

        ImGui::TreePop();
    }
}

void DemoGui::OnGUI_DebugBufferWindows()
{
    if (m_ShowInspectInput)
    {
        static BufferViewer viewer("Input Buffer");
        viewer.OnGUI(m_ShowInspectInput, g_SceneColorBuffer);
    }

    if (m_ShowInspectOutput)
    {
        ASSERT(XeSSDebug::IsBufferDebugEnabled());
        static BufferViewer viewer("Output Buffer");
        viewer.OnGUI(m_ShowInspectOutput, XeSSDebug::g_DebugBufferOutput);
    }

    if (m_ShowInspectMotionVectors)
    {
        static BufferViewer viewer("Motion Vectors Buffer");
        if (XeSS::GetMotionVectorsMode() == XeSS::kMotionVectorsHighRes)
            viewer.OnGUI(m_ShowInspectMotionVectors, g_UpscaledVelocityBuffer);
        else
            viewer.OnGUI(m_ShowInspectMotionVectors, g_ConvertedVelocityBuffer);
    }

    if (m_ShowInspectDepth)
    {
        static BufferViewer viewer("Depth Buffer");
        ColorBuffer& depthBuffer = g_LinearDepth[XeSS::GetFrameIndexMod2()];
        viewer.OnGUI(m_ShowInspectDepth, depthBuffer);
    }

    if (m_ShowInspectResponsiveMask)
    {
        static BufferViewer viewer("Responsive Mask Buffer");
        viewer.OnGUI(m_ShowInspectResponsiveMask, g_ResponsiveMaskBuffer);
    }
}

void DemoGui::OnGUI_Rendering()
{
    bool enableParticle = ParticleEffectManager::Enable;
    if (ImGui::Checkbox("Particle", &enableParticle))
    {
        ParticleEffectManager::Enable = enableParticle;
    }

    bool enableBloom = PostEffects::BloomEnable;
    if (ImGui::Checkbox("Bloom", &enableBloom))
    {
        PostEffects::BloomEnable = enableBloom;
    }

    bool enableMotionBlur = MotionBlur::Enable;
    if (ImGui::Checkbox("Motion Blur", &enableMotionBlur))
    {
        MotionBlur::Enable = enableMotionBlur;
    }

    bool enableSSAO = SSAO::Enable;
    if (ImGui::Checkbox("SSAO", &enableSSAO))
    {
        SSAO::Enable = enableSSAO;
    }
}

void DemoGui::OnGUI_Camera(DemoApp& App)
{
    DemoCameraController* controller = static_cast<DemoCameraController*>(App.GetCameraController());
    ASSERT(controller);
    if (!controller)
        return;

    float yaw, pitch;
    Vector3 pos;
    controller->GetHeadingPitchAndPosition(yaw, pitch, pos);

    {
        ImGui::PushItemWidth(DESIGN_SIZE(150.0f));
        bool dirty = false;
        if (ImGui::DragFloat("Yaw", &yaw, 0.01f, -FLT_MAX, FLT_MAX))
            dirty = true;

        ImGui::SameLine();
        if (ImGui::DragFloat("Pitch", &pitch, 0.01f, -FLT_MAX, FLT_MAX))
            dirty = true;

        if (dirty)
        {
            controller->SetHeadingAndPitch(yaw, pitch);
        }
    }

    {
        bool dirty = false;
        float x = pos.GetX();
        if (ImGui::DragFloat("X", &x, 0.1f, -FLT_MAX, FLT_MAX))
            dirty = true;

        ImGui::SameLine();

        float y = pos.GetY();
        if (ImGui::DragFloat("Y", &y, 0.1f, -FLT_MAX, FLT_MAX))
            dirty = true;

        ImGui::SameLine();
        float z = pos.GetZ();
        if (ImGui::DragFloat("Z", &z, 0.1f, -FLT_MAX, FLT_MAX))
            dirty = true;

        if (dirty)
        {
            controller->SetPosition(Vector3(x, y, z));
        }

        ImGui::PopItemWidth();
    }
}

void DemoGui::OnGUI_VRS()
{
    bool VRSEnable = static_cast<bool>(VRS::Enable);
    bool relaxedMode = static_cast<bool>(VRS::RelaxedMode);
    if (ImGui::Checkbox("Enable VRS", &VRSEnable))
    {
        VRS::Enable = VRSEnable;
    }

    if (ImGui::Checkbox("Relaxed Mode", &relaxedMode))
    {
        VRS::RelaxedMode = relaxedMode;
    }

    if (VRSEnable)
    {
        int VRSshadingRate = VRS::VRSShadingRate;
        if (ImGui::Combo("Tier 1 Shading Rate", &VRSshadingRate, VRS::VRSLabels, 7))
        {
            VRS::VRSShadingRate = VRSshadingRate;
        }

        int combinerOneIndex = VRS::ShadingRateCombiners1;
        if (ImGui::Combo("First Combiner", &combinerOneIndex, VRS::combiners, 5))
        {
            VRS::ShadingRateCombiners1 = combinerOneIndex;
        }

        int combinerTwoIndex = VRS::ShadingRateCombiners2;
        if (ImGui::Combo("Second Combiner", &combinerTwoIndex, VRS::combiners, 5))
        {
            VRS::ShadingRateCombiners2 = combinerTwoIndex;
        }

        if (ImGui::TreeNodeEx("VRS Tier 2 Contrast Adaptive", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float sensitivityThreshold =
                static_cast<float>(VRS::ContrastAdaptiveSensitivityThreshold);
            if (ImGui::SliderFloat("Sensitivity Threshold", &sensitivityThreshold, 0, 1, "%.2f"))
            {
                VRS::ContrastAdaptiveSensitivityThreshold = sensitivityThreshold;
            }

            bool allowQuarterRate = static_cast<bool>(VRS::ContrastAdaptiveAllowQuarterRate);
            if (ImGui::Checkbox("Allow Quarter Rate", &allowQuarterRate))
            {
                VRS::ContrastAdaptiveAllowQuarterRate = allowQuarterRate;
            }

            float quarterRateSensitivity = static_cast<float>(VRS::ContrastAdaptiveK);
            if (ImGui::SliderFloat(
                    "Quarter Rate Sensitivity", &quarterRateSensitivity, 0, 10, "%.2f"))
            {
                VRS::ContrastAdaptiveK = quarterRateSensitivity;
            }

            float envLuma = static_cast<float>(VRS::ContrastAdaptiveEnvLuma);
            if (ImGui::SliderFloat("Env. Luma", &envLuma, 0, 10, "%.3f"))
            {
                VRS::ContrastAdaptiveEnvLuma = envLuma;
            }
            
            bool useWeberFechner = static_cast<bool>(VRS::ContrastAdaptiveUseWeberFechner);
            if (ImGui::Checkbox("Use Weber-Fechner", &useWeberFechner))
            {
                VRS::ContrastAdaptiveUseWeberFechner = useWeberFechner;
            }

            float weberFechnerConstant =
                static_cast<float>(VRS::ContrastAdaptiveWeberFechnerConstant);
            if (ImGui::SliderFloat("Weber-Fechner Constant", &weberFechnerConstant, 0, 10, "%.2f"))
            {
                VRS::ContrastAdaptiveWeberFechnerConstant = weberFechnerConstant;
            }
            
            bool useDynamicThreshold = static_cast<bool>(VRS::ConstrastAdaptiveDynamic);
            if (ImGui::Checkbox("Dynamic Threshold", &useDynamicThreshold))
            {
                VRS::ConstrastAdaptiveDynamic = useDynamicThreshold;
            }

            int dynamicThresholdFPS = static_cast<int>(VRS::ConstrastAdaptiveDynamicFPS);
            if (ImGui::SliderInt("Dynamic Threshold FPS", &dynamicThresholdFPS, 15, 60, "%d FPS"))
            {
                VRS::ConstrastAdaptiveDynamicFPS = dynamicThresholdFPS;
            }

            bool useMotionVectors = static_cast<bool>(VRS::ContrastAdaptiveUseMotionVectors);
            if (ImGui::Checkbox("Use Motion Vectors", &useMotionVectors))
            {
                VRS::ContrastAdaptiveUseMotionVectors = useMotionVectors;
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("VRS Debug" /*, ImGuiTreeNodeFlags_DefaultOpen*/))
        {
            OnGUI_VRS_Debug();
            ImGui::TreePop();
        }
    }
}

void DemoGui::OnGUI_VRS_Debug()
{
    bool blendMask = static_cast<bool>(VRS::DebugDrawBlendMask);

    if (ImGui::Checkbox("Blend Mask", &blendMask))
    {
        VRS::DebugDrawBlendMask = blendMask;
    }

    bool calculatePercent = static_cast<bool>(VRS::CalculatePercents);
    if (ImGui::Checkbox("Calculate Percentages", &calculatePercent))
    {
        VRS::CalculatePercents = calculatePercent;
    }

    bool VRSDebug = static_cast<bool>(VRS::DebugDraw);
    if (ImGui::Checkbox("Debug ", &VRSDebug))
    {
        VRS::DebugDraw = VRSDebug;
    }

    bool drawGrid = static_cast<bool>(VRS::DebugDrawDrawGrid);
    if (ImGui::Checkbox("Draw Grid", &drawGrid))
    {
        VRS::DebugDrawDrawGrid = drawGrid;
    }
}

void DemoGui::OnGUI_LogWindow(DemoApp& App)
{
    if (!m_ShowLog)
        return;

    static const char* LEVEL_NAMES[] = { "[DEBUG]", "[INFO]", "[WARN]", "[ERROR]" };
    static const ImVec4 LEVEL_COLORS[] = { ImVec4(0, 1, 0, 1), DEF_TEXT_COLOR, ImVec4(1, 1, 0, 1), ImVec4(1, 0, 0, 1) };

    ImGui::SetNextWindowPos(DESIGN_SIZE(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y - 300.0f)), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(DESIGN_SIZE(ImVec2(1000, 300)), ImGuiCond_Once);

    static bool adjust = false;
    static ImVec4 winRect;
    DPISetNextWindowRect(adjust, winRect);

    if (ImGui::Begin("Log", &m_ShowLog))
    {
        DPIGetWindowRect(adjust, winRect);

        auto& messages = App.GetLog().GetMessages();

        ImGui::BeginChild("Log scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(messages.size()));

        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const Log::LogMessage& message = messages[i];
                ImGui::TextColored(LEVEL_COLORS[message.m_Level], "%s %s", LEVEL_NAMES[message.m_Level], message.m_Message.c_str());
            }
        }
        ImGui::PopStyleVar();

        ImGui::EndChild();
    }

    ImGui::End();
}

void DemoGui::BufferViewer::OnGUI(bool& Show, ColorBuffer& Buffer)
{
    DPISetNextWindowRect(m_DPIAdjust, m_DPIWinRect);
    ImGui::SetNextWindowSize(DESIGN_SIZE(ImVec2(Buffer.GetWidth() * 0.25f + 100.0f,
        Buffer.GetHeight() * 0.25f + 100.0f)),
        ImGuiCond_Once);

    if (ImGui::Begin(m_Title.c_str(), &Show))
    {
        DPIGetWindowRect(m_DPIAdjust, m_DPIWinRect);

        ImGui::Text("Resolution: %ux%u", Buffer.GetWidth(), Buffer.GetHeight());

        ImGui::SameLine();
        float left = ImGui::GetContentRegionMax().x - DESIGN_SIZE(300);
        ImGui::SetCursorPosX(left);
        ImGui::PushItemWidth(DESIGN_SIZE(200));

        static const char* ZOOM_NAMES[] = { "1/4", "1/2", "1", "2", "4", "8", "16" };
        static const int MAX_ZOOM_INDEX = IM_ARRAYSIZE(ZOOM_NAMES) - 1;
        ImGui::Combo("Zoom", &m_ZoomIndex, ZOOM_NAMES, IM_ARRAYSIZE(ZOOM_NAMES));
        ImGui::PopItemWidth();

        float scale = Math::Pow(2.0f, static_cast<float>(m_ZoomIndex) - 2.0f);
        const ImVec2 imageSize = ImVec2(static_cast<float>(Buffer.GetWidth())* scale, static_cast<float>(Buffer.GetHeight())* scale);

        ImGui::BeginChild("Image Viewport", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImGuiModule::RegisterImage(Buffer);

            // Image button shows our buffer.
            ImGui::ImageButton(&Buffer, imageSize, ImVec2(0, 0), ImVec2(1, 1), 0);

            if (!m_ScrollDirty)
            {
                m_ImagePos = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
            }
            else
            {
                // We scroll the image to its place here.
                ImGui::SetScrollX(m_ImagePos.x);
                ImGui::SetScrollY(m_ImagePos.y);

                m_ScrollDirty = false;
            }

            if (ImGui::IsItemHovered())
            {
                const ImVec2 cursorPosInImage = ImVec2(ImGui::GetMousePos().x - ImGui::GetCursorScreenPos().x,
                    ImGui::GetMousePos().y - ImGui::GetCursorScreenPos().y + imageSize.y);

                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                ImVec2 mousePos = ImGui::GetMousePos();

                // Handle mouse drag.
                if (ImGui::IsAnyMouseDown())
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    ImVec2 mouseOffset(mousePos.x - m_LastMousePos.x, mousePos.y - m_LastMousePos.y);

                    m_ImagePos.x -= mouseOffset.x;
                    m_ImagePos.y -= mouseOffset.y;

                    m_ScrollDirty = true;
                }

                // Handle mouse wheel.
                float wheel = ImGui::GetIO().MouseWheel;
                int zoom = m_ZoomIndex;
                if (wheel > 0.0f)
                {
                    zoom++;
                    zoom = (zoom > MAX_ZOOM_INDEX) ? MAX_ZOOM_INDEX : zoom;
                }
                else if (wheel < 0.0f)
                {
                    zoom--;
                    zoom = (zoom < 0) ? 0 : zoom;
                }

                if (zoom != m_ZoomIndex)
                {
                    // Here we want to translate the image so the cursor still points to the same place.
                    float ratio = Math::Pow(2.0f, static_cast<float>(zoom - m_ZoomIndex));

                    float cursorPosInViewportX = cursorPosInImage.x - m_ImagePos.x;
                    float cursorPosInViewportY = cursorPosInImage.y - m_ImagePos.y;

                    m_ImagePos.x = cursorPosInImage.x * ratio - cursorPosInViewportX;
                    m_ImagePos.y = cursorPosInImage.y * ratio - cursorPosInViewportY;

                    m_ScrollDirty = true;

                    m_ZoomIndex = zoom;
                }

                m_LastMousePos = ImGui::GetMousePos();
            }
        }

        ImGui::EndChild();
        ImGui::End();
        return;
    }

    ImGui::End();
}