#include "DesktopPetCompositor.h"
#include "Async/ParallelFor.h"

// 颜色始终取自引擎最终后处理，不再包含旧版 CPU 色调映射公式。
void DesktopPetCompositor::Composite(const TArray<FFloat16>& Opacity,const TArray<FColor>& UI,
    int32 Width,int32 Height,const FDesktopPetConfig& Config,TArray<FColor>& Out,TArray<uint8>& SceneAlpha,
    const TArray<FFloat16Color>& FinalColor,TArray<FColor>& Pet)
{
    const int32 S=Config.SupersampleScale,SW=Width*S;
    const float Inv=1.f/(S*S);
    Pet.SetNumUninitialized(Width*Height);
    Out.SetNumUninitialized(Width*Height);SceneAlpha.SetNumUninitialized(Width*Height);
    ParallelFor(Height,[&](int32 Y)
    {
        for(int32 X=0;X<Width;++X)
        {
            FLinearColor Sum(0,0,0,0);float Coverage=0;
            for(int32 V=0;V<S;++V)for(int32 U=0;U<S;++U)
            {
                const int32 J=(Y*S+V)*SW+X*S+U;
                Sum+=FLinearColor(FinalColor[J]);
                // 几何深度补齐不写 Alpha 的不透明 Pass；半透明继续保留原始覆盖率。
                Coverage+=FMath::Clamp(1.f-float(Opacity[J]),0.f,1.f);
            }
            Sum*=Inv*Config.Exposure;float A=FMath::Clamp(Coverage*Inv,0.f,1.f);
            FColor RGB=FColor::Black;
            if(A>0.00001f)RGB=(Sum/A).ToFColorSRGB();
            else if(Config.bPreserveAdditiveEffects)
            {
                // Windows source-over 无法还原任意桌面上的真实加法混合，发光使用覆盖率近似。
                const FColor Glow=Sum.ToFColorSRGB();
                A=FMath::Clamp(FMath::Max3(Glow.R,Glow.G,Glow.B)/255.f*Config.AdditiveAlphaStrength,0.f,1.f);
                if(A>0.00001f)RGB=FColor(FMath::Min(255,FMath::RoundToInt(Glow.R/A)),FMath::Min(255,FMath::RoundToInt(Glow.G/A)),FMath::Min(255,FMath::RoundToInt(Glow.B/A)),255);
            }
            const int32 I=Y*Width+X;const uint8 Alpha=FMath::RoundToInt(A*255);
            SceneAlpha[I]=Alpha;
            Pet[I]=FColor(FMath::Min(int32(Alpha),FMath::RoundToInt(RGB.R*A)),FMath::Min(int32(Alpha),FMath::RoundToInt(RGB.G*A)),FMath::Min(int32(Alpha),FMath::RoundToInt(RGB.B*A)),Alpha);
        }
    });
    ParallelFor(Height,[&](int32 Y)
    {
        for(int32 X=0;X<Width;++X)
        {
            const int32 I=Y*Width+X;FColor C=Pet[I];
            // 受限锐化只发生在完全不透明内部；不锐化透明边缘、发光或 UI，避免黑边和白边。
            if(Config.Sharpness>0&&C.A==255&&X>0&&Y>0&&X<Width-1&&Y<Height-1)
            {
                const FColor L=Pet[I-1],R=Pet[I+1],T=Pet[I-Width],B=Pet[I+Width];
                if(L.A==255&&R.A==255&&T.A==255&&B.A==255)
                {
                    auto Sharpen=[&](int V,int A,int D,int E,int F)
                    {
                        const float Detail=V-(A+D+E+F)*0.25f;
                        return uint8(FMath::Clamp(FMath::RoundToInt(V+FMath::Clamp(Detail,-24.f,24.f)*Config.Sharpness),0,255));
                    };
                    C.R=Sharpen(C.R,L.R,R.R,T.R,B.R);C.G=Sharpen(C.G,L.G,R.G,T.G,B.G);C.B=Sharpen(C.B,L.B,R.B,T.B,B.B);
                }
            }
            const FColor U=UI.IsEmpty()?FColor(0,0,0,0):UI[I];const float Remain=1.f-U.A/255.f;
            FColor Result;Result.A=FMath::Clamp(FMath::RoundToInt(U.A+C.A*Remain),0,255);
            Result.R=FMath::Clamp(FMath::RoundToInt(U.R+C.R*Remain),0,int32(Result.A));
            Result.G=FMath::Clamp(FMath::RoundToInt(U.G+C.G*Remain),0,int32(Result.A));
            Result.B=FMath::Clamp(FMath::RoundToInt(U.B+C.B*Remain),0,int32(Result.A));
            Out[I]=Result;
        }
    });
}
