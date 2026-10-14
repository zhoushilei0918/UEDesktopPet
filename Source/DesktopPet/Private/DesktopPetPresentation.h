#pragma once
#include "CoreMinimal.h"
#include "Async/ParallelFor.h"

/** 显示层只缩放预乘 Alpha 图像；不操作相机、Actor、材质或 RHI 渲染设置。 */
namespace DesktopPetPresentation
{
    /**
     * 使用相同的像素中心变换缩放颜色和命中 Alpha。
     * Source - 预乘 BGRA 源图像；
     * SourceAlpha - 源图像对应的场景命中 Alpha；
     * From、To - 源尺寸和目标物理像素尺寸；
     * Pixels - 写入目标 DIB 的颜色缓冲区；
     * Alpha - 接收缩放后的场景命中 Alpha。
     */
    inline void Resample(const TArray<FColor>& Source,
                         const TArray<uint8>& SourceAlpha,
                         FIntPoint From,
                         FIntPoint To,
                         FColor* Pixels,
                         TArray<uint8>& Alpha)
    {
        // 直接写入 Windows DIB；正常尺寸只复制一次颜色，缩放时也不再复制中间整幅图。
        if (From == To)
        {
            FMemory::Memcpy(Pixels, Source.GetData(), Source.Num() * sizeof(FColor));
            Alpha = SourceAlpha;
            return;
        }
        Alpha.SetNumUninitialized(To.X * To.Y);
        // 横向采样位置对每行相同，只计算一遍；保持原像素中心及取整公式，颜色结果不变。
        struct FColumn
        {
            int32 X0, X1;
            float Weight;
        };
        TArray<FColumn> Columns;
        Columns.SetNumUninitialized(To.X);
        for (int32 X = 0; X < To.X; ++X)
        {
            const double SX = FMath::Clamp((X + .5) * From.X / To.X - .5, 0., double(From.X - 1));
            const int32 X0 = FMath::FloorToInt(SX);
            Columns[X] = {X0, FMath::Min(X0 + 1, From.X - 1), float(SX - X0)};
        }
        // 和合成器一致，缩放也采用较大批次，避免一次小窗口缩放唤醒全部 CPU 核心。
        ParallelFor(TEXT("DesktopPet.Resample"),
                    To.Y,
                    FMath::Max(32, FMath::DivideAndRoundUp(To.Y, 4)),
                    [&](int32 Y)
                    {
                        const double SY = FMath::Clamp((Y + .5) * From.Y / To.Y - .5, 0., double(From.Y - 1));
                        const int32 Y0 = FMath::FloorToInt(SY), Y1 = FMath::Min(Y0 + 1, From.Y - 1);
                        const float FY = SY - Y0;
                        for (int32 X = 0; X < To.X; ++X)
                        {
                            const int32 X0 = Columns[X].X0, X1 = Columns[X].X1;
                            const float FX = Columns[X].Weight;
                            const int32 A = Y0 * From.X + X0, B = Y0 * From.X + X1, C = Y1 * From.X + X0,
                                        D = Y1 * From.X + X1, I = Y * To.X + X;
                            auto Sample = [&](float V0, float V1, float V2, float V3) -> uint8
                            {
                                return uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(
                                                              FMath::Lerp(V0, V1, FX), FMath::Lerp(V2, V3, FX), FY)),
                                                          0,
                                                          255));
                            };
                            Pixels[I] = FColor(Sample(Source[A].R, Source[B].R, Source[C].R, Source[D].R),
                                               Sample(Source[A].G, Source[B].G, Source[C].G, Source[D].G),
                                               Sample(Source[A].B, Source[B].B, Source[C].B, Source[D].B),
                                               Sample(Source[A].A, Source[B].A, Source[C].A, Source[D].A));
                            Alpha[I] = Sample(SourceAlpha[A], SourceAlpha[B], SourceAlpha[C], SourceAlpha[D]);
                        }
                    });
    }
} // namespace DesktopPetPresentation
