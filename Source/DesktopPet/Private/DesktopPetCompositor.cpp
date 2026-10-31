#include "DesktopPetCompositor.h"
#include "Async/ParallelFor.h"

/** 颜色始终取自引擎最终后处理，不再包含旧版 CPU 色调映射公式。 */
void DesktopPetCompositor::Composite(const TArray<FFloat16>& Opacity,
                                     const TArray<FColor>& UI,
                                     int32 Width,
                                     int32 Height,
                                     const FDesktopPetConfig& Config,
                                     TArray<FColor>& Out,
                                     TArray<uint8>& SceneAlpha,
                                     const TArray<FFloat16Color>& FinalColor,
                                     TArray<FColor>& Pet)
{
    const int32 S = Config.SupersampleScale, SW = Width * S;
    const float Inv = 1.f / (S * S);
    Pet.SetNumUninitialized(Width * Height);
    Out.SetNumUninitialized(Width * Height);
    SceneAlpha.SetNumUninitialized(Width * Height);
    // 小画面最多约 8 个批次，减少短任务唤醒；放大后保持充分并行，避免拉长帧时间。
    // 只改变任务划分，像素采样和浮点运算顺序保持不变。
    const int32 BatchRows =
        int64(Width) * Height * S * S <= 500000 ? FMath::Max(32, FMath::DivideAndRoundUp(Height, 8)) : 1;
    ParallelFor(TEXT("DesktopPet.Resolve"),
                Height,
                BatchRows,
                [&](int32 Y)
                {
                    for (int32 X = 0; X < Width; ++X)
                    {
                        FLinearColor Sum(0, 0, 0, 0);
                        float Coverage = 0;
                        for (int32 V = 0; V < S; ++V)
                        {
                            for (int32 U = 0; U < S; ++U)
                            {
                                const int32 J = (Y * S + V) * SW + X * S + U;
                                Sum += FLinearColor(FinalColor[J]);
                                // 几何深度补齐不写 Alpha 的不透明 Pass；半透明继续保留原始覆盖率。
                                Coverage += FMath::Clamp(1.f - float(Opacity[J]), 0.f, 1.f);
                            }
                        }
                        Sum *= Inv * Config.Exposure;
                        float A = FMath::Clamp(Coverage * Inv, 0.f, 1.f);
                        // 两种模式共用最终后处理颜色；不除小 Alpha，避免放大误差及逐通道失色。
                        FColor Pixel = Sum.ToFColorSRGB();
                        const int32 Peak = FMath::Max3(int32(Pixel.R), int32(Pixel.G), int32(Pixel.B));
                        if (Config.bPreserveAdditiveEffects)
                        {
                            // 发光优先：补足 Windows 预乘 BGRA 所需的覆盖率。
                            A = FMath::Max(A, FMath::Clamp(Peak / 255.f * Config.AdditiveAlphaStrength, 0.f, 1.f));
                        }
                        // 原始透明度模式直接使用捕获 Alpha，包括 0；亮度不会让背景被更多遮挡。
                        Pixel.A = FMath::RoundToInt(A * 255);
                        if (Peak > Pixel.A)
                        {
                            // 保持 Alpha 时的颜色上限，按同一比例减弱 RGB，保持编码后颜色的通道比例。
                            const float Weight = float(Pixel.A) / Peak;
                            Pixel.R = FMath::RoundToInt(Pixel.R * Weight);
                            Pixel.G = FMath::RoundToInt(Pixel.G * Weight);
                            Pixel.B = FMath::RoundToInt(Pixel.B * Weight);
                        }
                        const int32 I = Y * Width + X;
                        SceneAlpha[I] = Pixel.A;
                        Pet[I] = Pixel;
                    }
                });
    // 没有锐化和 UI 时，第一遍已经给出最终像素，直接复制即可。
    if (Config.Sharpness <= 0.f && UI.IsEmpty())
    {
        Out = Pet;
        return;
    }
    ParallelFor(
        TEXT("DesktopPet.SharpenAndUI"),
        Height,
        BatchRows,
        [&](int32 Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const int32 I = Y * Width + X;
                FColor C = Pet[I];
                // 受限锐化只发生在完全不透明内部；不锐化透明边缘、发光或 UI，避免黑边和白边。
                if (Config.Sharpness > 0 && C.A == 255 && X > 0 && Y > 0 && X < Width - 1 && Y < Height - 1)
                {
                    const FColor L = Pet[I - 1], R = Pet[I + 1], T = Pet[I - Width], B = Pet[I + Width];
                    if (L.A == 255 && R.A == 255 && T.A == 255 && B.A == 255)
                    {
                        auto Sharpen = [&](int V, int A, int D, int E, int F)
                        {
                            const float Detail = V - (A + D + E + F) * 0.25f;
                            return uint8(FMath::Clamp(
                                FMath::RoundToInt(V + FMath::Clamp(Detail, -24.f, 24.f) * Config.Sharpness), 0, 255));
                        };
                        C.R = Sharpen(C.R, L.R, R.R, T.R, B.R);
                        C.G = Sharpen(C.G, L.G, R.G, T.G, B.G);
                        C.B = Sharpen(C.B, L.B, R.B, T.B, B.B);
                    }
                }
                // 无 UI（或此处 UI 完全透明）时跳过恒等的混合运算，保留原始字节。
                if (UI.IsEmpty() || UI[I] == FColor(0, 0, 0, 0))
                {
                    Out[I] = C;
                    continue;
                }
                const FColor U = UI[I];
                const float Remain = 1.f - U.A / 255.f;
                FColor Result;
                Result.A = FMath::Clamp(FMath::RoundToInt(U.A + C.A * Remain), 0, 255);
                Result.R = FMath::Clamp(FMath::RoundToInt(U.R + C.R * Remain), 0, int32(Result.A));
                Result.G = FMath::Clamp(FMath::RoundToInt(U.G + C.G * Remain), 0, int32(Result.A));
                Result.B = FMath::Clamp(FMath::RoundToInt(U.B + C.B * Remain), 0, int32(Result.A));
                Out[I] = Result;
            }
        });
}
