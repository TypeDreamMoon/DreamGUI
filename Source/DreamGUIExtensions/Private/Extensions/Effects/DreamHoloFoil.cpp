// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamHoloFoil.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamSpriteBase.h"
#include "Core/Components/DreamTextureBase.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamGUI.h"
#include "DreamPointerOnPlane.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Extensions/Effects/DreamTiltCard.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureResource.h"

namespace DreamHoloFoilLocal
{
	const TCHAR* ImageMaterialPath = TEXT("/DreamGUI/Materials/DreamUI_HoloFoil.DreamUI_HoloFoil");
	const TCHAR* RectBlockMaterialPath = TEXT("/DreamGUI/Materials/DreamUI_HoloFoilRect.DreamUI_HoloFoilRect");
	const FName MaskParameter(TEXT("DreamUI_FoilMask"));
	const FName RainbowParameter(TEXT("DreamUI_FoilRainbow"));
	const FName ViewParameter(TEXT("DreamUI_FoilView"));
	const FName PatternParameter(TEXT("DreamUI_FoilPattern"));
	constexpr int32 RainbowWidth = 256;
}

UDreamHoloFoil::UDreamHoloFoil()
{
	DeclareTransformChangedUnused(UDreamHoloFoil::StaticClass());
	// A spectrum that comes back round to where it started, so the bands repeat without a seam.
	Rainbow.ColorStops = {
		FDreamStyleGradientColorStop(0.0f, FColor(255, 90, 150)), FDreamStyleGradientColorStop(0.17f, FColor(255, 200, 80)),
		FDreamStyleGradientColorStop(0.33f, FColor(140, 255, 120)), FDreamStyleGradientColorStop(0.5f, FColor(80, 230, 255)),
		FDreamStyleGradientColorStop(0.67f, FColor(110, 120, 255)), FDreamStyleGradientColorStop(0.83f, FColor(220, 100, 255)),
		FDreamStyleGradientColorStop(1.0f, FColor(255, 90, 150)) };
}

void UDreamHoloFoil::SetManualView(FVector2D Value)
{
	ManualView = Value;
}

void UDreamHoloFoil::SetViewSource(EDreamHoloFoilView Value)
{
	ViewSource = Value;
}

void UDreamHoloFoil::SetIntensity(float Value)
{
	Intensity = FMath::Max(Value, 0.0f);
	Update(0.0f);
}

void UDreamHoloFoil::BakeRainbow(const FDreamStyleGradient& InGradient, TArray<FColor>& OutTexels, int32 InWidth)
{
	OutTexels.SetNumUninitialized(FMath::Max(InWidth, 2));
	for (int32 Index = 0; Index < OutTexels.Num(); ++Index)
	{
		const FVector4f Color = InGradient.Evaluate(static_cast<float>(Index) / static_cast<float>(OutTexels.Num() - 1));
		OutTexels[Index] = FColor(
			static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Color.X, 0.0f, 1.0f) * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Color.Y, 0.0f, 1.0f) * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Color.Z, 0.0f, 1.0f) * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Color.W, 0.0f, 1.0f) * 255.0f)));
	}
}

void UDreamHoloFoil::BakeRainbowTexture()
{
	using namespace DreamHoloFoilLocal;
	if (RainbowTexture == nullptr)
	{
		RainbowTexture = UTexture2D::CreateTransient(RainbowWidth, 1, PF_B8G8R8A8);
		if (RainbowTexture == nullptr)
		{
			return;
		}
		RainbowTexture->SRGB = true;
		RainbowTexture->AddressX = TA_Wrap;
		RainbowTexture->AddressY = TA_Clamp;
		RainbowTexture->Filter = TF_Bilinear;
		RainbowTexture->LODGroup = TEXTUREGROUP_UI;
	}
	TArray<FColor> Texels;
	BakeRainbow(Rainbow, Texels, RainbowWidth);
	if (FTexturePlatformData* Data = RainbowTexture->GetPlatformData(); Data != nullptr && Data->Mips.Num() > 0)
	{
		void* Pixels = Data->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Pixels, Texels.GetData(), Texels.Num() * sizeof(FColor));
		Data->Mips[0].BulkData.Unlock();
	}
	RainbowTexture->UpdateResource();
}

void UDreamHoloFoil::Attach()
{
	using namespace DreamHoloFoilLocal;
	UDreamWidget* Widget = GetWidget();
	UDreamVisual* Visual = Widget != nullptr ? Widget->GetVisual() : nullptr;
	UDreamRectBlock* Block = Cast<UDreamRectBlock>(Visual);
	const bool bImage = Visual != nullptr && (Visual->IsA<UDreamImage>() || Visual->IsA<UDreamSpriteBase>() || Visual->IsA<UDreamTextureBase>());
	UDreamVisualBatchMesh* Element = Block != nullptr || bImage ? Cast<UDreamVisualBatchMesh>(Visual) : nullptr;
	if (Element == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("Holo foil on '%s' does nothing: it goes on an image, a sprite, a texture or a rect block."), Widget != nullptr ? *Widget->GetDisplayName() : TEXT("none"));
		return;
	}
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, Block != nullptr ? RectBlockMaterialPath : ImageMaterialPath);
	if (Parent == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("Holo foil on '%s' found no foil material (%s)."), *Widget->GetDisplayName(), Block != nullptr ? RectBlockMaterialPath : ImageMaterialPath);
		return;
	}
	BakeRainbowTexture();
	FoilMaterial = UMaterialInstanceDynamic::Create(Parent, this);
	if (FoilMask != nullptr)
	{
		FoilMaterial->SetTextureParameterValue(MaskParameter, FoilMask);
	}
	if (RainbowTexture != nullptr)
	{
		FoilMaterial->SetTextureParameterValue(RainbowParameter, RainbowTexture);
	}
	// In the element's transient override, so its own material properties -- saved, copied -- never hold the foil's instance.
	Element->SetMaterialOverride(FoilMaterial);
	Update(0.0f);
}

void UDreamHoloFoil::Detach()
{
	UDreamWidget* Widget = GetWidget();
	if (UDreamVisualBatchMesh* Element = Cast<UDreamVisualBatchMesh>(Widget != nullptr ? Widget->GetVisual() : nullptr);
		Element != nullptr && FoilMaterial != nullptr && Element->GetMaterialOverride() == FoilMaterial)
	{
		Element->SetMaterialOverride(nullptr);
	}
	FoilMaterial = nullptr;
}

void UDreamHoloFoil::OnRegister()
{
	Super::OnRegister();
	Attach();
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (World != nullptr && !World->IsGameWorld())
	{
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World))
		{
			EditorTickHandle = Manager->GetEditorTickDelegate().AddWeakLambda(this, [this](float InDeltaTime) { Update(InDeltaTime); });
		}
	}
#endif
}

void UDreamHoloFoil::OnUnregister()
{
#if WITH_EDITOR
	if (EditorTickHandle.IsValid())
	{
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			Manager->GetEditorTickDelegate().Remove(EditorTickHandle);
		}
		EditorTickHandle.Reset();
	}
#endif
	Detach();
	Super::OnUnregister();
}

void UDreamHoloFoil::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Update(DeltaTime);
}

#if WITH_EDITOR
void UDreamHoloFoil::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (FoilMaterial != nullptr)
	{
		BakeRainbowTexture();
		FoilMaterial->SetTextureParameterValue(DreamHoloFoilLocal::MaskParameter, FoilMask);
	}
	Update(0.0f);
}
#endif

void UDreamHoloFoil::Update(float InDeltaTime)
{
	using namespace DreamHoloFoilLocal;
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return;
	}
	FVector2D Target = FVector2D::ZeroVector;
	switch (ViewSource)
	{
	case EDreamHoloFoilView::Pointer:
	{
		FVector Local;
		if (DreamPointerOnPlane::Find(Widget, UserIndex, PointerId, Local))
		{
			const FVector2D Centre = Widget->GetLocalSpaceCenter();
			const FVector2D Half(FMath::Max(Widget->GetWidth() * 0.5, 1.0), FMath::Max(Widget->GetHeight() * 0.5, 1.0));
			Target = FVector2D(FMath::Clamp((Local.Y - Centre.X) / Half.X, -1.5, 1.5), FMath::Clamp((Local.Z - Centre.Y) / Half.Y, -1.5, 1.5));
		}
		break;
	}
	case EDreamHoloFoilView::Tilt:
		if (const UDreamTiltCard* Card = Widget->GetComponent<UDreamTiltCard>())
		{
			Target = Card->GetTilt();
		}
		break;
	default:
		Target = ManualView;
		break;
	}
	const bool bSnap = InDeltaTime <= 0.0f || ViewSharpness <= 0.0f || ViewSource == EDreamHoloFoilView::Manual;
	CurrentView = bSnap ? Target : FMath::Lerp(CurrentView, Target, 1.0 - FMath::Exp(-ViewSharpness * InDeltaTime));
	if (FoilMaterial != nullptr)
	{
		FoilMaterial->SetVectorParameterValue(ViewParameter, FLinearColor(static_cast<float>(CurrentView.X), static_cast<float>(CurrentView.Y), Intensity, SparkleIntensity));
		FoilMaterial->SetVectorParameterValue(PatternParameter, FLinearColor(BandFrequency, FMath::DegreesToRadians(BandAngle), SparkleCells, SparkleDensity));
	}
}
