#if WITH_DEV_AUTOMATION_TESTS
#include "DesktopPetCompositor.h"
#include "DesktopPetGPUCompositor.h"
#include "Misc/AutomationTest.h"
#include "DesktopPetActor.h"
#include "DesktopPetSettings.h"
#include "JsonObjectConverter.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"

namespace DesktopPetCompositorTests
{
    FColor Resolve(FLinearColor Color, float Alpha, bool Preserve = true, float Strength = 1.f)
    {
        FDesktopPetConfig Config;
        Config.SupersampleScale = 1;
        Config.Exposure = 1.f;
        Config.Sharpness = 0.f;
        Config.bPreserveAdditiveEffects = Preserve;
        Config.AdditiveAlphaStrength = Strength;
        TArray<FFloat16> Opacity{FFloat16(1.f - Alpha)};
        TArray<FFloat16Color> Colors{FFloat16Color(Color)};
        TArray<FColor> UI, Out, Scratch;
        TArray<uint8> HitAlpha;
        DesktopPetCompositor::Composite(Opacity, UI, 1, 1, Config, Out, HitAlpha, Colors, Scratch);
        return Out[0];
    }
    bool ValidPremultiplied(FColor C)
    {
        return C.R <= C.A && C.G <= C.A && C.B <= C.A;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPetAlphaContinuityTest, "DesktopPet.Compositor.LowAlphaColorContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPetAlphaContinuityTest::RunTest(const FString&)
{
    using namespace DesktopPetCompositorTests;
    const FLinearColor Blue(.08f, .16f, .30f, 1.f);
    const FColor Expected = FLinearColor(FFloat16Color(Blue)).ToFColorSRGB();
    for (int32 I = 0; I <= 255; ++I)
    {
        const FColor C = Resolve(Blue, I / 255.f);
        TestEqual(TEXT("Red contribution survives overlap"), C.R, Expected.R);
        TestEqual(TEXT("Green contribution survives overlap"), C.G, Expected.G);
        TestEqual(TEXT("Blue contribution survives overlap"), C.B, Expected.B);
        TestTrue(TEXT("Windows premultiplied invariant"), ValidPremultiplied(C));
    }
    TestEqual(TEXT("Empty pixel stays transparent"), Resolve(FLinearColor::Black, 0), FColor(0, 0, 0, 0));
    TestEqual(TEXT("Opaque black outline survives"), Resolve(FLinearColor::Black, 1), FColor::Black);
    TestEqual(TEXT("Opaque color is unchanged"), Resolve(Blue, 1), Expected);
    TestEqual(TEXT("Opt-out still drops zero-alpha glow"), Resolve(Blue, 0, false), FColor(0, 0, 0, 0));
    const FColor Weak = Resolve(Blue, 0, true, .4f);
    TestTrue(TEXT("Weaker glow preserves blue hue"), Weak.B > Weak.G && Weak.G > Weak.R && ValidPremultiplied(Weak));
    const FColor Strong = Resolve(Blue, 0, true, 2.f);
    TestEqual(TEXT("Stronger coverage does not change blue contribution"), Strong.B, Expected.B);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPetOriginalOpacityTest, "DesktopPet.Compositor.OriginalOpacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPetOriginalOpacityTest::RunTest(const FString&)
{
    using namespace DesktopPetCompositorTests;
    for (float Brightness : {0.f, .1f, 1.f, 5.f})
    for (int32 I = 0; I <= 255; ++I)
    {
        const float Alpha = I / 255.f;
        const FLinearColor Color = FLinearColor(.08f, .16f, .30f, 1.f) * Brightness;
        const FColor C = Resolve(Color, Alpha, false);
        const uint8 ExpectedAlpha = FMath::RoundToInt((1.f - float(FFloat16(1.f - Alpha))) * 255.f);
        TestEqual(TEXT("Brightness never changes original Alpha"), C.A, ExpectedAlpha);
        TestTrue(TEXT("Original opacity is valid premultiplied color"), ValidPremultiplied(C));
        TestEqual(TEXT("Glow strength is ignored in original opacity mode"), Resolve(Color, Alpha, false, 4.f), C);
    }
    const FLinearColor Blue(.08f, .16f, .30f, 1.f);
    const FColor Low = Resolve(Blue, .2f, false);
    TestTrue(TEXT("Low opacity keeps blue hue instead of whitening channels"), Low.B > Low.G && Low.G > Low.R);
    TestTrue(TEXT("Only glow priority raises Alpha"), Resolve(Blue, .2f, true).A > Low.A);
    TestEqual(TEXT("Opaque pixels agree in both modes"), Resolve(Blue, 1, false), Resolve(Blue, 1, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPetAlphaModeSettingsTest, "DesktopPet.Compositor.AlphaModeSettingsAndSwitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPetAlphaModeSettingsTest::RunTest(const FString&)
{
    UDesktopPetSettings* Settings = NewObject<UDesktopPetSettings>();
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Isolated test world"), World)) return false;
    ADesktopPetActor* Actor = World->SpawnActor<ADesktopPetActor>();
    if (!TestNotNull(TEXT("Isolated pet actor"), Actor)) { World->DestroyWorld(false); return false; }
    FDesktopPetConfig Initial = Actor->PetGetRuntimeConfig();
    Initial.Exposure = 2.f;
    Initial.DisplayScale = 1.f;
    Actor->PetApplyRuntimeConfig(Initial);
    const FDesktopPetConfig Before = Actor->PetGetRuntimeConfig();
    for (const auto Mode : {EDesktopPetAlphaMode::OriginalOpacity, EDesktopPetAlphaMode::PreserveGlow,
                           EDesktopPetAlphaMode::OriginalOpacity})
    {
        const bool Preserve = Mode == EDesktopPetAlphaMode::PreserveGlow;
        Settings->bPreserveAdditiveEffects = Preserve;
        const FDesktopPetConfig Config = Settings->MakeRuntimeConfig();
        TestEqual(TEXT("Project setting reaches runtime config"), Config.bPreserveAdditiveEffects, Preserve);
        FString Json;
        TestTrue(TEXT("Config can be saved"), FJsonObjectConverter::UStructToJsonObjectString(Config, Json));
        FDesktopPetConfig Loaded;
        TestTrue(TEXT("Config can be loaded"), FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Loaded));
        TestEqual(TEXT("Saved Alpha mode round-trips"), Loaded.bPreserveAdditiveEffects, Preserve);
        Actor->PetSetAlphaMode(Mode);
        TestTrue(TEXT("Blueprint mode switch is readable immediately"), Actor->PetGetAlphaMode() == Mode);
        const FDesktopPetConfig After = Actor->PetGetRuntimeConfig();
        TestEqual(TEXT("Switch preserves exposure"), After.Exposure, Before.Exposure);
        TestEqual(TEXT("Switch preserves scale"), After.DisplayScale, Before.DisplayScale);
        TestEqual(TEXT("Switch preserves position"), After.WindowPosition, Before.WindowPosition);
    }
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPetGPUAlphaParityTest, "DesktopPet.Compositor.GPUParityAndUIOverlap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPetGPUAlphaParityTest::RunTest(const FString&)
{
    if (GUsingNullRHI)
    {
        AddError(TEXT("This regression requires an actual GPU RHI."));
        return false;
    }
    constexpr int32 W = 64, H = 4, S = 2, SW = W * S, SH = H * S;
    TArray<FFloat16Color> Colors;
    TArray<FFloat16> Opacity;
    TArray<FColor> UI;
    Colors.SetNum(SW * SH);
    Opacity.SetNum(SW * SH);
    UI.SetNum(W * H);
    for (int32 I = 0; I < SW * SH; ++I)
    {
        Colors[I] = FFloat16Color(FLinearColor((I % 11) * .021f, (I % 17) * .031f, (I % 23) * .07f, 1.f));
        Opacity[I] = FFloat16(1.f - (I % 64) / 63.f);
    }
    for (int32 I = 0; I < W * H; ++I)
    {
        const uint8 A = I % 4 == 0 ? 0 : (I % 4 == 1 ? 32 : (I % 4 == 2 ? 128 : 255));
        UI[I] = FColor(A / 2, A / 4, A, A);
    }
    for (bool Preserve : {false, true})
    for (float Strength : {.4f, 1.f, 2.f})
    {
        FDesktopPetConfig Config;
        Config.SupersampleScale = S;
        Config.Exposure = 1.3f;
        Config.Sharpness = .2f;
        Config.bPreserveAdditiveEffects = Preserve;
        Config.AdditiveAlphaStrength = Strength;
        TArray<FColor> CPU, Scratch, GPU;
        TArray<uint8> CPUAlpha, GPUAlpha;
        DesktopPetCompositor::Composite(Opacity, UI, W, H, Config, CPU, CPUAlpha, Colors, Scratch);
        bool ReadOK = false;
        ENQUEUE_RENDER_COMMAND(PetAlphaParity)([&](FRHICommandListImmediate& Cmd)
        {
            auto Upload = [&](const TCHAR* Name, int32 Width, int32 Height, EPixelFormat Format, uint32 Pitch, const void* Data)
            {
                auto Desc = FRHITextureCreateDesc::Create2D(Name, Width, Height, Format)
                    .SetFlags(ETextureCreateFlags::ShaderResource).SetInitialState(ERHIAccess::CopyDest);
                FTextureRHIRef Texture = Cmd.CreateTexture(Desc);
                Cmd.UpdateTexture2D(Texture, 0, FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height), Pitch,
                                   static_cast<const uint8*>(Data));
                Cmd.Transition(FRHITransitionInfo(Texture, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
                return Texture;
            };
            const auto C = Upload(TEXT("PetTest.Color"), SW, SH, PF_FloatRGBA, SW * sizeof(FFloat16Color), Colors.GetData());
            const auto A = Upload(TEXT("PetTest.Alpha"), SW, SH, PF_R16F, SW * sizeof(FFloat16), Opacity.GetData());
            const auto U = Upload(TEXT("PetTest.UI"), W, H, PF_B8G8R8A8, W * sizeof(FColor), UI.GetData());
            FRHIGPUBufferReadback Readback(TEXT("PetTest.Readback"));
            DesktopPetGPUCompositor::Enqueue(Cmd, C, A, U, FIntPoint(W, H), Config, Readback);
            Cmd.SubmitAndBlockUntilGPUIdle();
            if (!Readback.IsReady()) return;
            const auto* Data = static_cast<const uint8*>(Readback.Lock(DesktopPetGPUCompositor::GetReadbackBytes(W * H)));
            if (!Data) return;
            GPU.SetNumUninitialized(W * H);
            GPUAlpha.SetNumUninitialized(W * H);
            FMemory::Memcpy(GPU.GetData(), Data, W * H * sizeof(FColor));
            FMemory::Memcpy(GPUAlpha.GetData(), Data + W * H * sizeof(FColor), W * H);
            Readback.Unlock();
            ReadOK = true;
        });
        FlushRenderingCommands();
        if (!TestTrue(TEXT("GPU readback completed"), ReadOK)) return false;
        int32 MaxDifference = 0;
        for (int32 I = 0; I < W * H; ++I)
        {
            const FColor A = CPU[I], B = GPU[I];
            MaxDifference = FMath::Max(MaxDifference, FMath::Max3(FMath::Abs(int(A.R) - B.R), FMath::Abs(int(A.G) - B.G),
                FMath::Abs(int(A.B) - B.B)));
            MaxDifference = FMath::Max(MaxDifference, FMath::Abs(int(A.A) - B.A));
            MaxDifference = FMath::Max(MaxDifference, FMath::Abs(int(CPUAlpha[I]) - GPUAlpha[I]));
            TestTrue(TEXT("UI overlap remains valid premultiplied color"), DesktopPetCompositorTests::ValidPremultiplied(B));
            if (UI[I].A == 255) TestEqual(TEXT("Opaque UI replaces scene once"), B, UI[I]);
            if (!Preserve)
            {
                float Coverage = 0.f;
                for (int32 V = 0; V < S; ++V)
                for (int32 U = 0; U < S; ++U)
                    Coverage += 1.f - float(Opacity[((I / W) * S + V) * SW + (I % W) * S + U]);
                const uint8 ExpectedAlpha = FMath::RoundToInt(Coverage / (S * S) * 255.f);
                TestEqual(TEXT("GPU preserves sampled original scene Alpha"), GPUAlpha[I], ExpectedAlpha);
                const uint8 ExpectedUIAlpha = FMath::RoundToInt(UI[I].A + ExpectedAlpha * (1.f - UI[I].A / 255.f));
                TestEqual(TEXT("UI is composed over original scene Alpha"), B.A, ExpectedUIAlpha);
            }
        }
        AddInfo(FString::Printf(TEXT("Preserve=%d Strength=%.1f CPU/GPU maximum byte difference=%d"), Preserve, Strength, MaxDifference));
        TestTrue(TEXT("CPU/GPU agree within one quantization step"), MaxDifference <= 1);
    }
    return true;
}
#endif
