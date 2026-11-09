// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamPointerLight.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamPointerOnPlane.h"
#include "Engine/World.h"

UDreamPointerLight::UDreamPointerLight()
{
	DeclareTransformChangedUnused(UDreamPointerLight::StaticClass());
}

void UDreamPointerLight::SetLightPosition(FVector2D Value)
{
	LightPosition = Value;
	if (!bFollowPointer)
	{
		Update(0.0f);
	}
}

void UDreamPointerLight::SetFollowPointer(bool Value)
{
	bFollowPointer = Value;
}

void UDreamPointerLight::SetLightColor(FColor Value)
{
	LightColor = Value;
}

void UDreamPointerLight::SetIntensity(float Value)
{
	Intensity = FMath::Max(Value, 0.0f);
}

void UDreamPointerLight::SetRadius(float Value)
{
	Radius = FMath::Max(Value, 1.0f);
}

void UDreamPointerLight::SetHeight(float Value)
{
	Height = FMath::Max(Value, 1.0f);
}

void UDreamPointerLight::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (World != nullptr && !World->IsGameWorld())
	{
		// No behaviour ticks in a world that is not playing: a light that stands where it is put is shown in the designer too.
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World))
		{
			EditorTickHandle = Manager->GetEditorTickDelegate().AddWeakLambda(this, [this](float InDeltaTime) { Update(InDeltaTime); });
		}
	}
#endif
}

void UDreamPointerLight::OnUnregister()
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
	ReleaseAll();
	Super::OnUnregister();
}

void UDreamPointerLight::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Update(DeltaTime);
}

#if WITH_EDITOR
void UDreamPointerLight::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Update(0.0f);
}
#endif

void UDreamPointerLight::SetEnabled(bool Value)
{
	if (bEnabled != Value)
	{
		bEnabled = Value;
		Update(0.0f);
	}
}

void UDreamPointerLight::Update(float DeltaTime)
{
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return;
	}
	if (!bEnabled)
	{
		ReleaseAll();
		return;
	}
	const FVector2D Centre = Widget->GetLocalSpaceCenter();
	FVector2D Target = LightPosition;
	bool bPresent = true;
	if (bFollowPointer)
	{
		FVector Local;
		bPresent = DreamPointerOnPlane::Find(Widget, UserIndex, PointerId, Local);
		if (bPresent)
		{
			Target = FVector2D(Local.Y - Centre.X, Local.Z - Centre.Y);
			// Over the plane within reach of the widget's rect: past that it lights nothing of it.
			const FVector2D Half(Widget->GetWidth() * 0.5, Widget->GetHeight() * 0.5);
			bPresent = FMath::Abs(Target.X) <= Half.X + Radius && FMath::Abs(Target.Y) <= Half.Y + Radius;
		}
		// A world that is not playing has no pointer to follow -- the designer's preview is one -- so the light stands where it
		// stands when it does not follow: an author sees what it lights, and moves it by its Light Position.
		const UWorld* World = GetWorld();
		if (!bPresent && World != nullptr && !World->IsGameWorld())
		{
			Target = LightPosition;
			bPresent = true;
		}
	}
	const float WantedIntensity = bPresent ? Intensity : 0.0f;
	if (DeltaTime <= 0.0f || !bFollowPointer)
	{
		CurrentIntensity = WantedIntensity;
	}
	else
	{
		CurrentIntensity = FMath::FInterpConstantTo(CurrentIntensity, WantedIntensity, DeltaTime, FadeSpeed * FMath::Max(Intensity, 0.1f));
	}
	if (bPresent)
	{
		// Where the pointer comes in, the light starts there rather than sliding in from where it last was.
		const bool bSnap = !bHasPosition || CurrentIntensity <= 0.0f || FollowSharpness <= 0.0f || DeltaTime <= 0.0f || !bFollowPointer;
		CurrentPosition = bSnap ? Target : FMath::Lerp(CurrentPosition, Target, 1.0 - FMath::Exp(-FollowSharpness * DeltaTime));
		bHasPosition = true;
	}
	Apply();
}

void UDreamPointerLight::Apply()
{
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return;
	}
	const FVector2D Centre = Widget->GetLocalSpaceCenter();
	const FVector World = Widget->GetWorldMatrix().TransformPosition(FVector(0.0, Centre.X + CurrentPosition.X, Centre.Y + CurrentPosition.Y));
	const bool bLit = CurrentIntensity > 0.0f && bHasPosition;
	TArray<TWeakObjectPtr<UDreamLayerStyle>> Styles;
	TArray<TWeakObjectPtr<UDreamRectBlock>> Blocks;
	TArray<const UDreamWidget*, TInlineAllocator<64>> Stack;
	Stack.Add(Widget);
	while (Stack.Num() > 0)
	{
		const UDreamWidget* Here = Stack.Pop(EAllowShrinking::No);
		for (const UDreamWidget* Child : Here->GetChildren())
		{
			if (IsValid(Child))
			{
				Stack.Add(Child);
			}
		}
		if (!bLit)
		{
			continue;
		}
		if (bLightBevels)
		{
			UDreamLayerStyle* Style = UDreamLayerStyle::Find(Here);
			if (Style != nullptr && Style->GetEffectiveStyle().BevelEmboss.bEnabled)
			{
				// Measured in the frame the style is drawn in: a shape blend group's leader's, else the widget's own rect.
				const UDreamShapeBlendGroup* Blend = Here->GetComponent<UDreamShapeBlendGroup>();
				const UDreamWidget* Frame = Blend != nullptr && Blend->GetLeader() != nullptr ? Blend->GetLeader()->GetWidget() : Here;
				const FVector Local = Frame->GetInverseWorldMatrix().TransformPosition(World);
				const FVector2D FrameCentre = Frame->GetLocalSpaceCenter();
				FDreamUILayerStyleLight Light;
				Light.Position = FVector2f(static_cast<float>(Local.Y - FrameCentre.X), static_cast<float>(FrameCentre.Y - Local.Z));
				Light.Height = Height;
				Light.Radius = Radius;
				Light.Color = LightColor;
				Light.Intensity = CurrentIntensity;
				Style->SetPointLight(&Light);
				Styles.Add(Style);
			}
		}
		if (bRevealBorders)
		{
			UDreamRectBlock* Block = Cast<UDreamRectBlock>(Here->GetVisual());
			if (Block != nullptr && Block->GetEnableBorder())
			{
				const FVector Local = Here->GetInverseWorldMatrix().TransformPosition(World);
				const FVector2D LeftBottom = Here->GetLocalSpaceLeftBottomPoint();
				FColor Color = LightColor;
				Color.A = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(CurrentIntensity, 0.0f, 1.0f) * 255.0f));
				Block->SetRevealHighlight(true, FVector2f(static_cast<float>(Local.Y - LeftBottom.X), static_cast<float>(Local.Z - LeftBottom.Y)), Radius, Color);
				Blocks.Add(Block);
			}
		}
	}
	// What it lit before and no longer does goes back to its own light.
	for (const TWeakObjectPtr<UDreamLayerStyle>& Old : LitStyles)
	{
		if (UDreamLayerStyle* Style = Old.Get(); Style != nullptr && !Styles.Contains(Old))
		{
			Style->SetPointLight(nullptr);
		}
	}
	for (const TWeakObjectPtr<UDreamRectBlock>& Old : RevealedBlocks)
	{
		if (UDreamRectBlock* Block = Old.Get(); Block != nullptr && !Blocks.Contains(Old))
		{
			Block->SetRevealHighlight(false, FVector2f::ZeroVector, 0.0f, FColor::Transparent);
		}
	}
	LitStyles = MoveTemp(Styles);
	RevealedBlocks = MoveTemp(Blocks);
}

void UDreamPointerLight::ReleaseAll()
{
	for (const TWeakObjectPtr<UDreamLayerStyle>& Old : LitStyles)
	{
		if (UDreamLayerStyle* Style = Old.Get())
		{
			Style->SetPointLight(nullptr);
		}
	}
	for (const TWeakObjectPtr<UDreamRectBlock>& Old : RevealedBlocks)
	{
		if (UDreamRectBlock* Block = Old.Get())
		{
			Block->SetRevealHighlight(false, FVector2f::ZeroVector, 0.0f, FColor::Transparent);
		}
	}
	LitStyles.Reset();
	RevealedBlocks.Reset();
	CurrentIntensity = 0.0f;
	bHasPosition = false;
}
