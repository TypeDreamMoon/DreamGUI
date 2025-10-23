// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexFrameCapture.h"
#include "LGUI.h"
#include "LTweenBPLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "Core/LexVisualPostProcessRenderProxy.h"
#include "GameFramework/PlayerController.h"
#include "RenderTargetPool.h"
#include "TextureResource.h"

ULexFrameCapture::ULexFrameCapture(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	
}

void ULexFrameCapture::BeginPlay()
{
	Super::BeginPlay();
	ULTweenBPLibrary::UpdateCall(this, FLTweenUpdateDelegate::CreateUObject(this, &ULexFrameCapture::OnUpdate));
}

void ULexFrameCapture::EndPlay()
{
	Super::EndPlay();
	ULTweenBPLibrary::KillAllTweensOnTarget(this, this);
}

void ULexFrameCapture::OnUpdate(float DeltaTime)
{
	if (CaptureMode == ECaptureMode::Continuous)
	{
		if (bIsFrameReady)
		{
			OnFrameReady.Broadcast(CapturedFrame);
		}
	}
	else if (CaptureMode == ECaptureMode::OneShot)
	{
		if (bIsFrameReady)
		{
			OnFrameReady.Broadcast(CapturedFrame);
			bIsFrameReady = false;
			OnFrameReady.Clear();
			CapturedFrame = nullptr;
		}
	}
}


#if WITH_EDITOR
void ULexFrameCapture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		
	}
}
bool ULexFrameCapture::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULexFrameCapture, MaskTexture))
		{
			return false;//mask texture not needed here
		}
	}
	return Super::CanEditChange(InProperty);
}
#endif
void ULexFrameCapture::MarkAllDirty()
{
	Super::MarkAllDirty();
}

DECLARE_CYCLE_STAT(TEXT("PostProcess_FrameCapture"), STAT_FrameGrabber, STATGROUP_LGUI);
class FUIFrameCaptureRenderProxy :public FLexVisualPostProcessRenderProxy
{
public:
	bool bCaptureFullScreen = true;
	FTextureRenderTargetResource* CapturedFrameResource = nullptr;
	ULexFrameCapture::ECaptureMode CaptureMode = ULexFrameCapture::ECaptureMode::None;
	/**
	 * This is a pointer to LexFrameCapture's IsFrameReady.
	 * Why it is safe to use? Check PrimitiveSceneInfo.h OwnerLastRenderTime
	 */
	bool* OwnerIsFrameReady = nullptr;
public:
	FUIFrameCaptureRenderProxy()
		:FLexVisualPostProcessRenderProxy()
	{

	}
	virtual bool CanRender()const override
	{
		return CaptureMode != ULexFrameCapture::ECaptureMode::None;
	}
	virtual void OnRenderPostProcess_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FLexUIRenderer* Renderer,
		FTextureRHIRef ScreenTargetTexture,
		FGlobalShaderMap* GlobalShaderMap,
		const FMatrix44f& ViewProjectionMatrix,
		bool bIsWorldSpace,
		bool bIsRenderTarget,
		float BlendDepthForWorld,
		int DepthFadeForWorld,
		const FIntRect& ViewRect,
		const FVector4f& DepthTextureScaleOffset,
		const FVector4f& ViewTextureScaleOffset
	)override
	{
		if (CapturedFrameResource == nullptr || CapturedFrameResource->GetRenderTargetTexture() == nullptr)return;
		if (CaptureMode == ULexFrameCapture::ECaptureMode::None)return;
		SCOPE_CYCLE_COUNTER(STAT_FrameGrabber);
		auto& RHICmdList = GraphBuilder.RHICmdList;

		TRefCountPtr<IPooledRenderTarget> ScreenResolvedTexture;
		uint8 NumSamples = ScreenTargetTexture->GetNumSamples();
		if (NumSamples > 1)
		{
			auto Size = ScreenTargetTexture->GetSizeXY();
			FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(Size, ScreenTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, desc, ScreenResolvedTexture, TEXT("LGUIBlurEffectResolveTarget"));
			if (!ScreenResolvedTexture.IsValid())
				return;
			auto ResolveSrc = RegisterExternalTexture(GraphBuilder, ScreenTargetTexture, TEXT("LGUIBlurEffectResolveSource"));
			auto ResolveDst = RegisterExternalTexture(GraphBuilder, ScreenResolvedTexture->GetRHI(), TEXT("LGUIBlurEffectResolveTarget"));
			Renderer->AddResolvePass(GraphBuilder, FRDGTextureMSAA(ResolveSrc, ResolveDst), FIntRect(0, 0, Size.X, Size.Y), NumSamples, GlobalShaderMap);
		}

		auto CapturedFrameTexture = (FTextureRHIRef)CapturedFrameResource->GetRenderTargetTexture();
		if (bCaptureFullScreen)
		{
			Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap
				, NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference()
				, CapturedFrameTexture, true);
		}
		else
		{
			//copy rect area from screen image to a render target
			auto modelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
			Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder
				, RegisterExternalTexture(GraphBuilder, CapturedFrameTexture, TEXT("LGUI_FrameCaptureTargetTexture"))
				, NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference()
				, GlobalShaderMap
				, RenderScreenToMeshRegionVertexArray
				, modelViewProjectionMatrix
				, bIsRenderTarget
				, FIntRect(0, 0, CapturedFrameTexture->GetSizeXYZ().X, CapturedFrameTexture->GetSizeXYZ().Y)
				, ViewTextureScaleOffset
				, true
			);
		}
		*OwnerIsFrameReady = true;
		
		if (ScreenResolvedTexture.IsValid())
		{
			ScreenResolvedTexture.SafeRelease();
		}
		if (CaptureMode == ULexFrameCapture::ECaptureMode::OneShot)
			CaptureMode = ULexFrameCapture::ECaptureMode::None;
	}
};

TSharedPtr<FLexVisualPostProcessRenderProxy> ULexFrameCapture::GetRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		auto Proxy = MakeShared<FUIFrameCaptureRenderProxy>();
		Proxy->OwnerIsFrameReady = &this->bIsFrameReady;
		RenderProxy = Proxy;
		SendRegionVertexDataToRenderProxy();
	}
	return RenderProxy;
}

void ULexFrameCapture::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
}

void ULexFrameCapture::SendCaptureDataToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		auto TempRenderProxy = (FUIFrameCaptureRenderProxy*)(RenderProxy.Get());
		ENQUEUE_RENDER_COMMAND(FLexBackgroundPixelate_UpdateData)
			([TempRenderProxy, bCaptureFullScreen = this->bCaptureFullScreen, RenderTargetResource = CapturedFrame->GameThread_GetRenderTargetResource(), CaptureMode = CaptureMode](FRHICommandListImmediate& RHICmdList)
				{
					TempRenderProxy->bCaptureFullScreen = bCaptureFullScreen;
					TempRenderProxy->CapturedFrameResource = RenderTargetResource;
					TempRenderProxy->CaptureMode = CaptureMode;
				});
	}
}
void ULexFrameCapture::UpdateRenderTarget()
{
	auto Widget = GetWidget();
	FIntPoint DesiredRenderTargetSize(Widget->GetWidth(), Widget->GetHeight());
	if (this->bCaptureFullScreen)
	{
		if (auto pc = this->GetWorld()->GetFirstPlayerController())
		{
			pc->GetViewportSize(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y);
		}
	}
	static const int32 MaxAllowedDrawSize = GetMax2DTextureDimension();
	if (DesiredRenderTargetSize.X <= 0 || DesiredRenderTargetSize.Y <= 0)
	{
		return;
	}
	DesiredRenderTargetSize.X = FMath::Min(DesiredRenderTargetSize.X, MaxAllowedDrawSize);
	DesiredRenderTargetSize.Y = FMath::Min(DesiredRenderTargetSize.Y, MaxAllowedDrawSize);

	if (CapturedFrame == nullptr)
	{
		CapturedFrame = NewObject<UTextureRenderTarget2D>(this, NAME_None, EObjectFlags::RF_Transient);
		CapturedFrame->AddressX = TextureAddress::TA_Clamp;
		CapturedFrame->AddressY = TextureAddress::TA_Clamp;
		CapturedFrame->ClearColor = FLinearColor::Transparent;
		CapturedFrame->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
	}
	else
	{
		if (CapturedFrame->SizeX != DesiredRenderTargetSize.X || CapturedFrame->SizeY != DesiredRenderTargetSize.Y)
		{
			CapturedFrame->ClearColor = FLinearColor::Transparent;
			CapturedFrame->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
			CapturedFrame->UpdateResourceImmediate();
#if WITH_EDITOR
			CapturedFrame->Modify();
#endif
		}
	}
}

void ULexFrameCapture::DoOneFrameCapture(const FLexFrameCapture_OnFrameReady_DynamicDelegate& InDelegate)
{
	if (CaptureMode == ECaptureMode::Continuous)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Already in continuous capture process, can't start one frame capture!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	CaptureMode = ECaptureMode::OneShot;
	MarkOneFrameCapture();
	OnFrameReady.AddLambda([InDelegate](UTextureRenderTarget2D* InCapturedFrame) {
		InDelegate.ExecuteIfBound(InCapturedFrame);
		});
}
void ULexFrameCapture::DoOneFrameCapture(const FLexFrameCapture_OnFrameReady_Delegate& InDelegate)
{
	if (CaptureMode == ECaptureMode::Continuous)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Already in continuous capture process, can't start one frame capture!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	CaptureMode = ECaptureMode::OneShot;
	MarkOneFrameCapture();
	OnFrameReady.Add(InDelegate);
}
void ULexFrameCapture::DoOneFrameCapture(const TFunction<void(UTextureRenderTarget2D*)>& InFunction)
{
	if (CaptureMode == ECaptureMode::Continuous)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Already in continuous capture process, can't start one frame capture!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	CaptureMode = ECaptureMode::OneShot;
	MarkOneFrameCapture();
	OnFrameReady.AddLambda(InFunction);
}

void ULexFrameCapture::StartContinuousCapture()
{
	if (CaptureMode == ECaptureMode::OneShot)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Already in one frame capture process, can't start continuous capture!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	CaptureMode = ECaptureMode::Continuous;
	UpdateRenderTarget();
	SendCaptureDataToRenderProxy();
}
void ULexFrameCapture::StopContinuousCapture()
{
	CaptureMode = ECaptureMode::None;
	SendCaptureDataToRenderProxy();
}

void ULexFrameCapture::MarkOneFrameCapture()
{
	CaptureMode = ECaptureMode::OneShot;
	UpdateRenderTarget();
	SendCaptureDataToRenderProxy();
}
