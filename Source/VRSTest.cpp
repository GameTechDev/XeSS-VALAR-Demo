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
#include "..\Model\Model.h"
#include "VRSTest.h"
#include "VRS.h"
#include "CameraController.h"
#include "GameInput.h"
#include "VRSScreenshot.h"

#include <conio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>

#include <iostream>
#include <stdexcept>
#include <stdio.h>
#include <string>

#include <direct.h>
#include "XeSS/XeSSProcess.h"
#include "XeSS/XeSSDebug.h"
#include "Display.h"
#include "ImageScaling.h"
using namespace std;

#include <iostream>
#include <cstdarg>
#include <string>
#include <fstream>
#include <memory>
#include <cstdio>

#include "DemoApp.h"
#include "FXAA.h"
#include "TemporalEffects.h"
#include "EngineProfiling.h"
#include "ParticleEffectManager.h"
#include "PostEffects.h"
#include "SystemTime.h"

#define ACCUMULATE_FRAMES 1000

std::string exec(const char* command) {
    char tmpname[L_tmpnam];
    std::tmpnam(tmpname);
    std::string scommand = command;
    std::string cmd = scommand + " > " + tmpname + " 2>&1";
    std::system(cmd.c_str());
    std::ifstream file(tmpname, std::ios::in | std::ios::binary);
    std::string result;
    if (file) {
        while (!file.eof()) result.push_back(file.get())
            ;
        file.close();
    }
    remove(tmpname);
    result.pop_back();
    return result;
}

namespace VRSTest
{
    bool RunningTest = false;
    bool takeScreenshot = false;
    float countdownTimer = 5.0f;
    int frameCount = 0;
    double gpuTimeSum = 0;
    double cpuTimeSum = 0;
    Math::Vector3 targetPosition;
    float targetHeading;
    float targetPitch;
    float cameraFlySpeed = 0.0025f;
    float cameraRotateSpeed = 0.0025f;
    float flyingTime = 0.0f;
    int flyCameraIndex = 0;
    int flythroughCount = 0;
    UnitTest* Test = nullptr;
    std::list<Experiment*>::iterator NextExperiment;
    UnitTestMode TestMode = UnitTestMode::TestModeNone;
    UnitTestState TestState = UnitTestState::TestStateNone;

    DemoApp* m_App;

    const Location locales[3] = { Location(1.55f, 0.0f, Math::Vector3(100.0f, 150.0f, -40.0f)), //lion head
                              Location(4.70f, 0.0f, Math::Vector3(-1200.0f, 200.0f, -40.0f)), //first floor view
                              Location(0.0f, 0.0f, Math::Vector3(-600.0f, 160.0f, 300.0f)), //cloth
    };

    const Location flyLocales[] = {
        Location(XM_PIDIV2, 0.0f, Vector3(-559.038208f, 169.621399f, -214.290222f)), //chain
        Location(1.59339249f, -0.00380240404f, Vector3(-1357.49060f, 187.460464f, -63.5717163)), //lion close up
        Location(-2.50639725f, 0.0735977143f, Vector3(645.763733f, 167.056641f, 156.868149f)), //cloth angle
        Location(-3.042f, -0.214f, Vector3(-784.827f, 588.880f, -126.787f)),
        Location(-2.735f, 0.020f, Vector3(-528.387f, 577.721f, 173.991f)),   // lighting
        Location(0.415f, -0.273f, Vector3(-1052.304f, 226.259f, 59.130f)),   // particle fountain
        Location(-0.586f, -0.032f, Vector3(959.399f, 174.945f, -159.308f)),  // particle smoke
        Location(-2.875f, -0.168f, Vector3(982.348f, 226.593f, -113.359f)),  // particle fire
    };

}

void VRSTest::Init(DemoApp* App)
{
    m_App = App;
}

void VRSTest::Update(CameraController* camera, float deltaT)
{
    int64_t flyCameraStartTime = 0;

    switch (TestState)
    {
        case UnitTestState::TestStateNone:
        {
            TestMode = CheckIfChangeLocationKeyPressed();

            if (TestMode != UnitTestMode::TestModeNone)
            {
                RunningTest = true;
                TestState = UnitTestState::Setup;
                break;
            }

            if (GameInput::IsFirstPressed(GameInput::kKey_4))
            {
                flyCameraStartTime = SystemTime::GetCurrentTick();
                RunningTest = true;
                TestState = UnitTestState::FlyCamera;
                break;
            }

            // auto set FlyCamera for bench:
            //TestState = UnitTestState::FlyCamera;
        }
        break;
        case UnitTestState::Setup:
        {
            frameCount = 0;
            gpuTimeSum = 0;
            cpuTimeSum = 0;

            // 8k - TAA - No Upscale - No VRS.
            Experiment* TAANativeValarOff = 
                new Experiment(std::string("Control"), true, true, true);

            Experiment* XeSSUltraValarOff =
                new Experiment(std::string("XeSSUltraValarOff"), true, true, false);
            Experiment* XeSSQualityValarOff =
                new Experiment(std::string("XeSSQualityValarOff"), true, true, false);
            Experiment* XeSSBalancedValarOff =
                new Experiment(std::string("XeSSBalancedValarOff"), true, true, false);
            Experiment* XeSSPerformanceValarOff =
                new Experiment(std::string("XeSSPerformanceValarOff"), true, true, false);

             // 8k - XeSS - Upscale - Medium VRS.
            Experiment* XeSSUltraValarQuality =
                new Experiment(std::string("XeSSUltraValarQuality"), true, true, false);
            Experiment* XeSSQualityValarQuality =
                new Experiment(std::string("XeSSQualityValarQuality"), true, true, false);
            Experiment* XeSSBalancedValarQuality =
                new Experiment(std::string("XeSSBalancedValarQuality"), true, true, false);
            Experiment* XeSSPerformanceValarQuality =
                new Experiment(std::string("XeSSPerformanceValarQuality"), true, true, false);

            // 8k - XeSS - Upscale - Medium VRS.
            
            Experiment* XeSSUltraValarBalanced =
                new Experiment(std::string("XeSSUltraValarBalanced"), true, true, false);
            Experiment* XeSSQualityValarBalanced =
                new Experiment(std::string("XeSSQualityValarBalanced"), true, true, false);
            Experiment* XeSSBalancedValarBalanced =
                new Experiment(std::string("XeSSBalancedValarBalanced"), true, true, false);
            Experiment* XeSSPerformanceValarBalanced =
                new Experiment(std::string("XeSSPerformanceValarBalanced"), true, true, false);

           

            // 8k - XeSS - Upscale - Medium VRS.
            Experiment* XeSSUltraValarPerformance =
                new Experiment(std::string("XeSSUltraValarPerformance"), true, true, false);
            Experiment* XeSSQualityValarPerformance =
                new Experiment(std::string("XeSSQualityValarPerformance"), true, true, false);
            Experiment* XeSSBalancedValarPerformance =
                new Experiment(std::string("XeSSBalancedValarPerformance"), true, true, false);
            Experiment* XeSSPerformanceValarPerformance =
                new Experiment(std::string("XeSSPerformanceValarPerformance"), true, true, false);


            TAANativeValarOff->ExperimentFunction = []() -> void
            {
                VRS::Enable = false;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;

                ParticleEffectManager::Enable = false;

                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_TAANative);
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityQuality);
                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };


             XeSSUltraValarOff->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.0f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityUltraQuality);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

              XeSSQualityValarOff->ExperimentFunction = []() -> void
             {
                 VRS::Enable = true;
                 VRS::DebugDraw = false;
                 VRS::DebugDrawDrawGrid = false;
                 VRS::DebugDrawBlendMask = false;
                 VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                 VRS::ContrastAdaptiveUseWeberFechner = false;
                 VRS::ContrastAdaptiveSensitivityThreshold = 0.0f;

                 ParticleEffectManager::Enable = false;
                 XeSS::SetQuality(XeSS::eQualityLevel::kQualityQuality);
                 VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                 PostEffects::EnableHDR = false;
                 Display::SetFullscreen(true);
             };

              XeSSBalancedValarOff->ExperimentFunction = []() -> void
              {
                  VRS::Enable = true;
                  VRS::DebugDraw = false;
                  VRS::DebugDrawDrawGrid = false;
                  VRS::DebugDrawBlendMask = false;
                  VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                  VRS::ContrastAdaptiveUseWeberFechner = false;
                  VRS::ContrastAdaptiveSensitivityThreshold = 0.0f;

                  ParticleEffectManager::Enable = false;
                  XeSS::SetQuality(XeSS::eQualityLevel::kQualityBalanced);
                  VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                  PostEffects::EnableHDR = false;
                  Display::SetFullscreen(true);
              };

               XeSSPerformanceValarOff->ExperimentFunction = []() -> void
              {
                  VRS::Enable = true;
                  VRS::DebugDraw = false;
                  VRS::DebugDrawDrawGrid = false;
                  VRS::DebugDrawBlendMask = false;
                  VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                  VRS::ContrastAdaptiveUseWeberFechner = false;
                  VRS::ContrastAdaptiveSensitivityThreshold = 0.0f;

                  ParticleEffectManager::Enable = false;
                  XeSS::SetQuality(XeSS::eQualityLevel::kQualityPerformance);
                  VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                  PostEffects::EnableHDR = false;
                  Display::SetFullscreen(true);
              };




            XeSSUltraValarQuality->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.25f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityUltraQuality);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSQualityValarQuality->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.25f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityQuality);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSBalancedValarQuality->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.25f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityBalanced);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSPerformanceValarQuality->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.25f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityPerformance);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };




            XeSSUltraValarBalanced->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.50f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityUltraQuality);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSQualityValarBalanced->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.50f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityQuality);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSBalancedValarBalanced->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.50f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityBalanced);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSPerformanceValarBalanced->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.50f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityPerformance);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };



            XeSSUltraValarPerformance->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.75f;

 
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityUltraQuality);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSQualityValarPerformance->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.75f;
                

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityQuality);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSBalancedValarPerformance->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.75f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityBalanced);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };

            XeSSPerformanceValarPerformance->ExperimentFunction = []() -> void
            {
                VRS::Enable = true;
                VRS::DebugDraw = false;
                VRS::DebugDrawDrawGrid = false;
                VRS::DebugDrawBlendMask = false;
                VRS::ShadingModes = VRS::ShadingMode::ContrastAdaptiveGPU;
                VRS::ContrastAdaptiveUseWeberFechner = false;
                VRS::ContrastAdaptiveSensitivityThreshold = 0.75f;

                ParticleEffectManager::Enable = false;
                XeSS::SetQuality(XeSS::eQualityLevel::kQualityPerformance);
                VRSTest::m_App->SetTechnique(eDemoTechnique::kDemoTech_XeSS);

                PostEffects::EnableHDR = false;
                Display::SetFullscreen(true);
            };




            switch (TestMode)
            {
                case UnitTestMode::LionHead:
                {
                    Test = new UnitTest(std::string("SponzaLionHead"), UnitTestMode::LionHead);
                    
                    Test->AddExperiment(TAANativeValarOff);

                    Test->AddExperiment(XeSSUltraValarOff);
                    Test->AddExperiment(XeSSUltraValarQuality);
                    Test->AddExperiment(XeSSUltraValarBalanced);
                    Test->AddExperiment(XeSSUltraValarPerformance);

                    Test->AddExperiment(XeSSQualityValarOff);
                    Test->AddExperiment(XeSSQualityValarQuality);
                    Test->AddExperiment(XeSSQualityValarBalanced);
                    Test->AddExperiment(XeSSQualityValarPerformance);

                    Test->AddExperiment(XeSSBalancedValarOff);
                    Test->AddExperiment(XeSSBalancedValarQuality);
                    Test->AddExperiment(XeSSBalancedValarBalanced);
                    Test->AddExperiment(XeSSBalancedValarPerformance);

                    Test->AddExperiment(XeSSPerformanceValarOff);
                    Test->AddExperiment(XeSSPerformanceValarQuality);
                    Test->AddExperiment(XeSSPerformanceValarBalanced);
                    Test->AddExperiment(XeSSPerformanceValarPerformance);

                    Test->Setup();

                    NextExperiment = Test->m_experiments.begin();
                }
                break;
                case UnitTestMode::FirstFloor:
                {
                    Test = new UnitTest(std::string("SponzaFirstFloor"), UnitTestMode::FirstFloor);

                    Test->AddExperiment(TAANativeValarOff);

                    Test->AddExperiment(XeSSUltraValarOff);
                    Test->AddExperiment(XeSSUltraValarQuality);
                    Test->AddExperiment(XeSSUltraValarBalanced);
                    Test->AddExperiment(XeSSUltraValarPerformance);

                    Test->AddExperiment(XeSSQualityValarOff);
                    Test->AddExperiment(XeSSQualityValarQuality);
                    Test->AddExperiment(XeSSQualityValarBalanced);
                    Test->AddExperiment(XeSSQualityValarPerformance);

                    Test->AddExperiment(XeSSBalancedValarOff);
                    Test->AddExperiment(XeSSBalancedValarQuality);
                    Test->AddExperiment(XeSSBalancedValarBalanced);
                    Test->AddExperiment(XeSSBalancedValarPerformance);

                    Test->AddExperiment(XeSSPerformanceValarOff);
                    Test->AddExperiment(XeSSPerformanceValarQuality);
                    Test->AddExperiment(XeSSPerformanceValarBalanced);
                    Test->AddExperiment(XeSSPerformanceValarPerformance);

                    Test->Setup();

                    NextExperiment = Test->m_experiments.begin();
                }
                break;
                case UnitTestMode::Tapestry:
                {
                    Test = new UnitTest(std::string("SponzaTapestry"), UnitTestMode::Tapestry);

                    Test->AddExperiment(TAANativeValarOff);

                    Test->AddExperiment(XeSSUltraValarOff);
                    Test->AddExperiment(XeSSUltraValarQuality);
                    Test->AddExperiment(XeSSUltraValarBalanced);
                    Test->AddExperiment(XeSSUltraValarPerformance);

                    Test->AddExperiment(XeSSQualityValarOff);
                    Test->AddExperiment(XeSSQualityValarQuality);
                    Test->AddExperiment(XeSSQualityValarBalanced);
                    Test->AddExperiment(XeSSQualityValarPerformance);

                    Test->AddExperiment(XeSSBalancedValarOff);
                    Test->AddExperiment(XeSSBalancedValarQuality);
                    Test->AddExperiment(XeSSBalancedValarBalanced);
                    Test->AddExperiment(XeSSBalancedValarPerformance);

                    Test->AddExperiment(XeSSPerformanceValarOff);
                    Test->AddExperiment(XeSSPerformanceValarQuality);
                    Test->AddExperiment(XeSSPerformanceValarBalanced);
                    Test->AddExperiment(XeSSPerformanceValarPerformance);

                    Test->Setup();

                    NextExperiment = Test->m_experiments.begin();
                }
                break;
            }

            ResetExperimentData();
            TestState = UnitTestState::MoveCamera;
        }
        break;
        case UnitTestState::MoveCamera:
        {
            MoveCamera(camera, Test->m_testMode);
            TestState = UnitTestState::RunExperiment;
        }
        break;
        case UnitTestState::RunExperiment:
        {
            gpuTimeSum = 0;
            cpuTimeSum = 0;
            frameCount = 0;

            if (NextExperiment != Test->m_experiments.end())
            {
                (*NextExperiment)->ExperimentFunction();
                TestState = UnitTestState::Wait;
            }
            else
            {
                TestState = UnitTestState::Teardown;
            }
        }
        break;

        case UnitTestState::Wait:
        {
            countdownTimer -= deltaT;
            if (countdownTimer <= 0.0f)
            {
                countdownTimer = 5.0f;
                TestState = UnitTestState::AccumulateFrametime;
            }
        }
        break;
        case UnitTestState::AccumulateFrametime:
        {
            if (!(*NextExperiment)->CaptureStats())
            {
                TestState = UnitTestState::TakeScreenshot;
                break;
            }

            gpuTimeSum += EngineProfiling::GetTotalGpuTime();
            cpuTimeSum += EngineProfiling::GetTotalCpuTime();
            frameCount++;

            if (frameCount == ACCUMULATE_FRAMES)
            {
                TestState = UnitTestState::TakeScreenshot;
            }
        }
        break;
        case UnitTestState::TakeScreenshot:
        {
            takeScreenshot = true;

            TestState = UnitTestState::RunExperiment;
        }
        break;
        case UnitTestState::Teardown:
        {
            while (Test->m_experiments.size() > 0)
            {
                Experiment* exp = (*Test->m_experiments.begin());
                Test->m_experiments.pop_front();
                delete exp;
            }

            delete Test;
            Test = nullptr;

            RunningTest = false;
            TestState = UnitTestState::TestStateNone;
        }
        break;
        case UnitTestState::FlyCamera:
        {
            //FlyingFPSCamera* const fpsCamera = dynamic_cast<FlyingFPSCamera*> (camera);
            //Vector3 p = fpsCamera->GetPosition();

            targetPosition = flyLocales[flyCameraIndex].GetPosition();
            targetHeading = flyLocales[flyCameraIndex].GetHeading();
            targetPitch = flyLocales[flyCameraIndex].GetPitch();

            flyCameraIndex++;
            if (flyCameraIndex >= _countof(flyLocales))
            {
                flyCameraIndex = 0;
                VRS::DebugDraw = !VRS::DebugDraw;
                // exit after 3 loops for bench
                static int loops = 0;
                if (loops++ >= 3)
                {
                    int64_t flyCameraEndTime = SystemTime::GetCurrentTick();
                    double duration =
                        SystemTime::TimeBetweenTicks(flyCameraEndTime, flyCameraStartTime);
                    LOG_INFOF("Fly Camera duration: %d", duration);
                    exit(0);
                }
            }

            flyingTime = 0.0f;
            TestState = UnitTestState::WaitFlyCamera;
        }
        break;
        case UnitTestState::WaitFlyCamera:
        {
            FlyingFPSCamera* const fpsCamera = dynamic_cast<FlyingFPSCamera*> (camera);
            if (fpsCamera)
            {
                Vector3 currentPosition = fpsCamera->GetPosition();
                float currentHeading = fpsCamera->GetCurrentHeading();
                float currentPitch = fpsCamera->GetCurrentPitch();
                
                flyingTime += deltaT;
                
                Vector3 newPosition = Math::Lerp(currentPosition, targetPosition, flyingTime * cameraFlySpeed);
                float newHeading = Math::Lerp(currentHeading, targetHeading, flyingTime * cameraRotateSpeed);
                float newPitch = Math::Lerp(currentPitch, targetPitch, flyingTime * cameraRotateSpeed);

                fpsCamera->SetHeadingPitchAndPosition(newHeading, newPitch, newPosition);
                if (fabs(newPosition.GetX() - targetPosition.GetX()) < 1.0f &&
                    fabs(newPosition.GetY() - targetPosition.GetY()) < 1.0f &&
                    fabs(newPosition.GetZ() - targetPosition.GetZ()) < 1.0f)
                {
                    TestState = UnitTestState::FlyCamera;
                }
            }
            else
            {
                TestState = UnitTestState::TestStateNone;
            }
        }
        break;
    }
}

void VRSTest::ResetExperimentData()
{
    std::ofstream outfile;
    std::string filename = std::string("c:\\VRSExperiments\\").append(Test->GetName()).append("\\").append(Test->GetName()).append("-Results.csv");
    outfile.open(filename.c_str());
    outfile << "UnitTest,Experiment,Threshold,K,Env. Luma,Weber-Fechner Constant,PSInvocations,CPUTime,GPUTime,FrameRate,1x1,1x2,2x1,2x2,2x4,4x2,4x4,"
            << "AE,DSSIM,FUZZ,MAE,MEPP,MSE,NCC,PAE,PHASH,RMSE,SSIM,PSNR,Path" << std::endl;
    outfile.close();

    filename = std::string("c:\\VRSExperiments\\").append(Test->GetName()).append("\\").append(Test->GetName()).append("-FLIP.txt");
    outfile.open(filename.c_str());
    outfile << "";
    outfile.close();

    filename = std::string("c:\\VRSExperiments\\").append(Test->GetName()).append("\\").append("FLIP.csv");
    remove(filename.c_str());
}

void VRSTest::WriteExperimentData(std::string& AE, std::string& DSSIM, std::string& FUZZ, std::string& MAE, std::string& MEPP, std::string& MSE, std::string& NCC, std::string& PAE, std::string& PHASH, std::string& RMSE, std::string& SSIM, std::string& PSNR, std::string& FLIP)
{
    float frameRate = 1.0f / EngineProfiling::GetFrameRate();

    std::ofstream outfile;
    std::string filename = std::string("c:\\VRSExperiments\\").append(Test->GetName()).append("\\").append(Test->GetName()).append("-Results.csv");
    std::string imagePath = std::string("c:\\VRSExperiments\\").append(Test->GetName()).append("\\").append((*NextExperiment)->GetName()).append(".png");
    outfile.open(filename.c_str(), std::ios_base::app);
    outfile << Test->GetName() << ","
        << (*NextExperiment)->GetName() << ","
        << (float)VRS::ContrastAdaptiveSensitivityThreshold << ","
        << (float)VRS::ContrastAdaptiveK << ","
        << (float)VRS::ContrastAdaptiveEnvLuma << ","
        << (float)VRS::ContrastAdaptiveWeberFechnerConstant << ","
        << VRS::PipelineStatistics.PSInvocations << ","
        << (float)cpuTimeSum / (float)ACCUMULATE_FRAMES << ","
        << (float)gpuTimeSum / (float)ACCUMULATE_FRAMES << ","
        << frameRate << ","
        << VRS::Percents.num1x1 << ","
        << VRS::Percents.num1x2 << ","
        << VRS::Percents.num2x1 << ","
        << VRS::Percents.num2x2 << ","
        << VRS::Percents.num2x4 << ","
        << VRS::Percents.num4x2 << ","
        << VRS::Percents.num4x4 << ","
        << AE << ","
        << DSSIM << ","
        << FUZZ << ","
        << MAE << ","
        << MEPP << ","
        << MSE << ","
        << NCC << ","
        << PAE << ","
        << PHASH << ","
        << RMSE << ","
        << SSIM << ","
        << PSNR << ","
        << imagePath << std::endl;
    outfile.close();

    filename = std::string("c:\\VRSExperiments\\").append(Test->GetName()).append("\\").append(Test->GetName()).append("-FLIP.txt");
    outfile.open(filename.c_str(), std::ios_base::app);
    outfile << "[Unit Test: " << Test->GetName() << " Experiment: " << (*NextExperiment)->GetName() << "]\n";
    outfile << FLIP;
    outfile.close();
}

bool VRSTest::Render(CommandContext& context, ColorBuffer& source, ColorBuffer& vrsBuffer)
{
    if (takeScreenshot)
    {
        if (NextExperiment != Test->m_experiments.end())
        {
            Experiment* exp = (*NextExperiment);

            std::string filename = std::string("c:\\VRSExperiments\\").append(Test->GetName()).append("\\").append(exp->GetName()).append(".png");
            std::string vrsfilename = std::string("c:\\VRSExperiments\\").append(Test->GetName()).append("\\").append(exp->GetName()).append("-VRSBuffer.png");
            
            Screenshot::TakeScreenshotAndExportVRSBuffer(filename.c_str(), source, vrsfilename.c_str(), vrsBuffer, context, exp->CaptureVRSBuffer());

            std::string AE, DSSIM, FUZZ, MAE, MEPP, MSE, NCC, PAE, PHASH, RMSE, SSIM, PSNR, FLIP;

            if (exp->CaptureStats())
            {
                printf("[Unit Test: %s Experiment: %s]\n", Test->GetName().c_str(), exp->GetName().c_str());

                std::string controlPath("\"C:\\VRSExperiments\\");
                controlPath.append(Test->GetName()).append("\\Control.png\"");

                std::string experimentPath("\"C:\\VRSExperiments\\");
                experimentPath.append(Test->GetName()).append("\\").append(exp->GetName()).append(".png\"");

                std::string differencePath("\"C:\\VRSExperiments\\");
                differencePath.append(Test->GetName()).append("\\").append(exp->GetName()).append("-diff.png\"");

                std::string cmd("magick compare -metric AE");
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                AE = exec(cmd.c_str());
                printf("AE: %s\n", AE.c_str());

                cmd = "magick compare -metric DSSIM";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                DSSIM = exec(cmd.c_str());
                printf("DSSIM: %s\n", DSSIM.c_str());

                cmd = "magick compare -metric FUZZ";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                FUZZ = exec(cmd.c_str());
                printf("FUZZ: %s\n", FUZZ.c_str());

                cmd = "magick compare -metric MAE";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                MAE = exec(cmd.c_str());
                printf("MAE: %s\n", MAE.c_str());

                cmd = "magick compare -metric MEPP";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                MEPP = exec(cmd.c_str());
                std::replace(MEPP.begin(), MEPP.end(), ',', ';');
                printf("MEPP: %s\n", MEPP.c_str());

                cmd = "magick compare -metric MSE";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                MSE = exec(cmd.c_str());
                printf("MSE: %s\n", MSE.c_str());

                cmd = "magick compare -metric NCC";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                NCC = exec(cmd.c_str());
                printf("NCC: %s\n", NCC.c_str());

                cmd = "magick compare -metric PAE";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                PAE = exec(cmd.c_str());
                printf("PAE: %s\n", PAE.c_str());

                cmd = "magick compare -metric PHASH";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                PHASH = exec(cmd.c_str());
                printf("PHASH: %s\n", PHASH.c_str());

                cmd = "magick compare -metric RMSE";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                RMSE = exec(cmd.c_str());
                printf("RMSE: %s\n", RMSE.c_str());

                cmd = "magick compare -metric SSIM";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                SSIM = exec(cmd.c_str());
                printf("SSIM: %s\n", SSIM.c_str());

                cmd = "magick compare -metric PSNR";
                cmd.append(" ").append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" ").append(differencePath);
                PSNR = exec(cmd.c_str());
                printf("PSNR: %s\n", PSNR.c_str());

                std::string flipCSV("C:\\VRSExperiments\\");
                flipCSV.append(Test->GetName()).append("\\FLIP.csv");

                cmd = "cd C:\\VRSExperiments\\";
                cmd.append(Test->GetName());
                cmd.append(" & C:\\VRSExperiments\\FLIP\\flip.exe ");
                cmd.append(controlPath);
                cmd.append(" ").append(experimentPath);
                cmd.append(" -heatmap ").append(exp->GetName()).append("-heatmap.png");
                cmd.append(" -histogram ").append(exp->GetName()).append("-FLIP");
                //cmd.append(" --save-ldrflip --save-ldr-images -hist --basename ").append(exp->GetName()).append("-FLIP");
                //cmd.append(" --csv ").append(flipCSV);
                FLIP = exec(cmd.c_str());
                printf("FLIP: %s\n\n", FLIP.c_str());
                
                //std::string createFlipPDF("cd C:\\VRSExperiments\\");
                //createFlipPDF.append(Test->GetName());
                ////C:\Users\Marissa\AppData\Local\Programs\Python\Python310
                //createFlipPDF.append(" & \"C:\\Users\\Marissa\\AppData\\Local\\Programs\\Python\\Python310\\python.exe\" ");
                //createFlipPDF.append(exp->GetName()).append("-FLIP.py");
                ////printf("FLIP PDF CMD: %s\n", createFlipPDF.c_str());
                //std::system(createFlipPDF.c_str());
                ////printf("%s\n", result.c_str());

                WriteExperimentData(AE, DSSIM, FUZZ, MAE, MEPP, MSE, NCC, PAE, PHASH, RMSE, SSIM, PSNR, FLIP);
            }

            VRSTest::takeScreenshot = false;
            
            NextExperiment++;
            
            return true;
        }
    }
    return false;
}

void VRSTest::MoveCamera(CameraController* camera, UnitTestMode testMode)
{
    FlyingFPSCamera* const fpsCamera = dynamic_cast<FlyingFPSCamera*> (camera );
    if (fpsCamera)
    {
        const Location currLocation = VRSTest::locales[((int)testMode) - 1];
        fpsCamera->SetHeadingPitchAndPosition(currLocation.GetHeading(), currLocation.GetPitch(), currLocation.GetPosition());
    }
    else
    {
        printf("Unable to move camera, not FPSCamera.\n");
    }
}

UnitTestMode VRSTest::CheckIfChangeLocationKeyPressed()
{
    if (GameInput::IsFirstPressed(GameInput::kKey_1))
    {
        return UnitTestMode::LionHead;
    }
    else if(GameInput::IsFirstPressed(GameInput::kKey_2))
    {
        return UnitTestMode::FirstFloor;
    }
    else if (GameInput::IsFirstPressed(GameInput::kKey_3))
    {
        return UnitTestMode::Tapestry;
    }
    return UnitTestMode::TestModeNone;
}

Experiment::Experiment(std::string& name, bool captureVRSBuffer, bool captureStats, bool isControl)
{
    m_isControl = isControl;
    m_experimentName = name;
    m_captureStats = captureStats;
    m_captureVRSBuffer = captureVRSBuffer;
}

std::string& Experiment::GetName()
{
    return m_experimentName;
}

bool Experiment::CaptureVRSBuffer()
{
    return m_captureVRSBuffer;
}

bool Experiment::IsControl()
{
    return m_isControl;
}

bool Experiment::CaptureStats()
{
    return m_captureStats;
}

UnitTest::UnitTest(std::string& testName, UnitTestMode testMode)
{
    m_testName = testName;
    m_testMode = testMode;
}

void UnitTest::AddExperiment(Experiment* exp)
{
    m_experiments.push_back(exp);
}

std::string& UnitTest::GetName()
{
    return m_testName;
}

void UnitTest::Setup()
{
    CreateDirectoryA(std::string("C:\\VRSExperiments\\").c_str(), NULL);
    CreateDirectoryA(std::string("C:\\VRSExperiments\\").append(m_testName).c_str(), NULL);
}