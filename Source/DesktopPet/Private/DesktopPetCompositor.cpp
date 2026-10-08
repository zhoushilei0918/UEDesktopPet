#include "DesktopPetCompositor.h"
#include "Async/ParallelFor.h"

namespace
{
    // 简单固定色调映射；避免依赖主视口自动曝光和不透明背景的后处理链。
    float Tone(float X)
    {
        X=FMath::Max(0.f,X);
        return FMath::Clamp((X*(2.51f*X+.03f))/(X*(2.43f*X+.59f)+.14f),0.f,1.f);
    }
}

// 在线性空间同时缩小颜色和覆盖率，之后才做色调映射与 sRGB 转换。
void DesktopPetCompositor::Composite(const TArray<FFloat16Color>& Scene,const TArray<FColor>& UI,
    int32 Width,int32 Height,const FDesktopPetConfig& Config,TArray<FColor>& Out,TArray<uint8>& SceneAlpha,const TArray<FFloat16Color>* FinalColor)
{
    const int32 S=Config.SupersampleScale,SW=Width*S;
    const float Inv=1.f/(S*S);
    Out.SetNumUninitialized(Width*Height);
    SceneAlpha.SetNumUninitialized(Width*Height);
    ParallelFor(Height,[&](int32 Y)
    {
        for(int32 X=0;X<Width;++X)
        {
            FLinearColor Sum(0,0,0,0),FinalSum(0,0,0,0);
            for(int32 V=0;V<S;++V)for(int32 U=0;U<S;++U)
            {
                const FLinearColor C(Scene[(Y*S+V)*SW+X*S+U]);
                // 不能因 Alpha=0 就丢弃 RGB：Niagara Additive 的亮度正是这种情况。
                Sum.R+=C.R;Sum.G+=C.G;Sum.B+=C.B;
                Sum.A+=FMath::Clamp(1.f-C.A,0.f,1.f);
                if(FinalColor)FinalSum+=FLinearColor((*FinalColor)[(Y*S+V)*SW+X*S+U]);
            }
            Sum*=Inv;FinalSum*=Inv;
            float A=FMath::Clamp(Sum.A,0.f,1.f);
            FColor RGB=FColor::Black;
            if(A>.00001f)
            {
                // 标准半透明：先还原颜色，再输出预乘 Alpha，保留材质本来的透明度。
                // FinalColor 已经通过引擎色调映射，不能再套一次 Tone，否则会二次调色。
                const FLinearColor Color=FinalColor?FinalSum*(Config.Exposure/A):
                    FLinearColor(Tone(Sum.R*Config.Exposure/A),Tone(Sum.G*Config.Exposure/A),Tone(Sum.B*Config.Exposure/A),1);
                RGB=Color.ToFColorSRGB();
            }
            else if(Config.bPreserveAdditiveEffects)
            {
                // Windows 不支持对任意桌面做真正的加法混合；以发光颜色重建覆盖率作近似。
                const FLinearColor GlowLinear=FinalColor?FinalSum*Config.Exposure:
                    FLinearColor(Tone(Sum.R*Config.Exposure),Tone(Sum.G*Config.Exposure),Tone(Sum.B*Config.Exposure),1);
                FColor Glow=GlowLinear.ToFColorSRGB();
                A=FMath::Clamp(FMath::Max3(Glow.R,Glow.G,Glow.B)/255.f*Config.AdditiveAlphaStrength,0.f,1.f);
                if(A>.00001f)RGB=FColor(FMath::Min(255,FMath::RoundToInt(Glow.R/A)),FMath::Min(255,FMath::RoundToInt(Glow.G/A)),FMath::Min(255,FMath::RoundToInt(Glow.B/A)),255);
            }
            const int32 I=Y*Width+X;
            SceneAlpha[I]=FMath::RoundToInt(A*255);
            const FColor U=UI[I];
            const float Remain=1.f-U.A/255.f;
            FColor Result;
            // UI 与场景分别保留 Alpha，按 source-over 叠加，最后约束预乘颜色不超过 Alpha。
            Result.A=FMath::Clamp(FMath::RoundToInt(U.A+255*A*Remain),0,255);
            Result.R=FMath::Clamp(FMath::RoundToInt(U.R+RGB.R*A*Remain),0,int32(Result.A));
            Result.G=FMath::Clamp(FMath::RoundToInt(U.G+RGB.G*A*Remain),0,int32(Result.A));
            Result.B=FMath::Clamp(FMath::RoundToInt(U.B+RGB.B*A*Remain),0,int32(Result.A));
            Out[I]=Result;
        }
    });
}
