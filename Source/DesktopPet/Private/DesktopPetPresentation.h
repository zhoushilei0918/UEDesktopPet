#pragma once
#include "CoreMinimal.h"
#include "Async/ParallelFor.h"

/** 显示层只缩放预乘 Alpha 图像；不操作相机、Actor、材质或 RHI 渲染设置。 */
namespace DesktopPetPresentation
{
    // 使用像素中心采样，颜色与命中 Alpha 使用完全相同的变换，防止透明边缘出现黑边。
    inline void Resample(const TArray<FColor>& Source,const TArray<uint8>& SourceAlpha,FIntPoint From,FIntPoint To,TArray<FColor>& Pixels,TArray<uint8>& Alpha)
    {
        if(From==To){Pixels=Source;Alpha=SourceAlpha;return;}
        Pixels.SetNumUninitialized(To.X*To.Y);Alpha.SetNumUninitialized(To.X*To.Y);
        // 和合成器一致，缩放也采用较大批次，避免一次小窗口缩放唤醒全部 CPU 核心。
        ParallelFor(TEXT("DesktopPet.Resample"),To.Y,FMath::Max(32,FMath::DivideAndRoundUp(To.Y,4)),[&](int32 Y)
        {
            const double SY=FMath::Clamp((Y+.5)*From.Y/To.Y-.5,0.,double(From.Y-1));
            const int32 Y0=FMath::FloorToInt(SY),Y1=FMath::Min(Y0+1,From.Y-1);
            const float FY=SY-Y0;
            for(int32 X=0;X<To.X;++X)
            {
                const double SX=FMath::Clamp((X+.5)*From.X/To.X-.5,0.,double(From.X-1));
                const int32 X0=FMath::FloorToInt(SX),X1=FMath::Min(X0+1,From.X-1);
                const float FX=SX-X0;
                const int32 A=Y0*From.X+X0,B=Y0*From.X+X1,C=Y1*From.X+X0,D=Y1*From.X+X1,I=Y*To.X+X;
                auto Sample=[&](float V0,float V1,float V2,float V3)->uint8
                {return uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(FMath::Lerp(V0,V1,FX),FMath::Lerp(V2,V3,FX),FY)),0,255));};
                Pixels[I]=FColor(Sample(Source[A].R,Source[B].R,Source[C].R,Source[D].R),Sample(Source[A].G,Source[B].G,Source[C].G,Source[D].G),Sample(Source[A].B,Source[B].B,Source[C].B,Source[D].B),Sample(Source[A].A,Source[B].A,Source[C].A,Source[D].A));
                Alpha[I]=Sample(SourceAlpha[A],SourceAlpha[B],SourceAlpha[C],SourceAlpha[D]);
            }
        });
    }
}
