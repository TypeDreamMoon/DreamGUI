// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI/Public/MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithWave.h"
#include "DreamGUI.h"
#include "Core/Components/DreamText.h"
#include "DreamTweenBPLibrary.h"
#include "Engine/World.h"
#include "Core/DreamUIWorldContext.h"

void UDreamMeshModifierTextAnimation_PropertyWithWave::Init()
{
	TextObject = GetDreamText();
	UpdateTweener = UDreamTweenBPLibrary::UpdateCall(this, FDreamTweenUpdateDelegate::CreateUObject(this, &UDreamMeshModifierTextAnimation_PropertyWithWave::OnUpdate));
}
void UDreamMeshModifierTextAnimation_PropertyWithWave::Deinit()
{
	UDreamTweenBPLibrary::KillIfIsTweening(this, UpdateTweener.Get());
}
void UDreamMeshModifierTextAnimation_PropertyWithWave::SetFrequency(float Value)
{
	if (Frequency != Value)
	{
		Frequency = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_PropertyWithWave::SetSpeed(float Value)
{
	if (Speed != Value)
	{
		Speed = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_PropertyWithWave::OnUpdate(float deltaTime)
{
	if (IsValid(TextObject))
	{
		TextObject->MarkVertexPositionDirty();
	}
}

void UDreamMeshModifierTextAnimation_PositionWaveProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
	// A wave needs a clock, and one of the three places this runs has none: a mesh modifier rebuilds
	// geometry in a Blueprint's authoring tree and in a headless test, and this property is a plain
	// UObject whose GetWorld walks an outer chain that ends at the widget tree rather than at a world.
	// Phase zero is the honest answer there -- the still frame of the animation, which is exactly what
	// a designer wants to look at anyway. See DreamUIWorldContext.h for the rule.
	const UWorld* WaveWorld = DreamUI::GetWorldSafe(this);
	float PIxFreq = (WaveWorld != nullptr ? WaveWorld->TimeSeconds : 0.0f) * PI * Speed;
	PIxFreq = FlipDirection ? -PIxFreq : PIxFreq;
	for (int charIndex = InSelection.StartCharIndex; charIndex < InSelection.EndCharCount; charIndex++)
	{
		auto charPropertyItem = charProperties[charIndex];
		int startVertIndex = charPropertyItem.StartVertIndex;
		int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
		float lerpValue = FMath::Clamp(InSelection.LerpValueArray[charIndex - InSelection.StartCharIndex], 0.0f, 1.0f);
		auto wavePosition = (FVector3f)Position * FMath::Sin(PIxFreq + charIndex * Frequency);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			pos = FMath::Lerp(pos, pos + wavePosition, lerpValue);
		}
	}
}
void UDreamMeshModifierTextAnimation_PositionWaveProperty::SetPosition(FVector Value)
{
	if (Position != Value)
	{
		Position = Value;
		MarkUITextPositionDirty();
	}
}

void UDreamMeshModifierTextAnimation_RotationWaveProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
	// A wave needs a clock, and one of the three places this runs has none: a mesh modifier rebuilds
	// geometry in a Blueprint's authoring tree and in a headless test, and this property is a plain
	// UObject whose GetWorld walks an outer chain that ends at the widget tree rather than at a world.
	// Phase zero is the honest answer there -- the still frame of the animation, which is exactly what
	// a designer wants to look at anyway. See DreamUIWorldContext.h for the rule.
	const UWorld* WaveWorld = DreamUI::GetWorldSafe(this);
	float PIxFreq = (WaveWorld != nullptr ? WaveWorld->TimeSeconds : 0.0f) * PI * Speed;
	PIxFreq = FlipDirection ? -PIxFreq : PIxFreq;
	for (int charIndex = InSelection.StartCharIndex; charIndex < InSelection.EndCharCount; charIndex++)
	{
		auto charPropertyItem = charProperties[charIndex];
		int startVertIndex = charPropertyItem.StartVertIndex;
		int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
		auto charCenterPos = originVertices[startVertIndex].Position;
		for (int vertIndex = startVertIndex + 1; vertIndex < endVertIndex; vertIndex++)
		{
			charCenterPos += originVertices[vertIndex].Position;
		}
		charCenterPos /= charPropertyItem.VertCount;
		float lerpValue = FMath::Clamp(InSelection.LerpValueArray[charIndex - InSelection.StartCharIndex], 0.0f, 1.0f);
		auto waveRotator = (FRotator3f)Rotator * FMath::Sin(PIxFreq + charIndex * Frequency);
		auto calcRotationMatrix = FRotationMatrix44f(waveRotator * lerpValue);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			auto vector = pos - charCenterPos;
			pos = charCenterPos + calcRotationMatrix.TransformPosition(vector);
		}
	}
}
void UDreamMeshModifierTextAnimation_RotationWaveProperty::SetRotator(FRotator Value)
{
	if (Rotator != Value)
	{
		Rotator = Value;
		MarkUITextPositionDirty();
	}
}

void UDreamMeshModifierTextAnimation_ScaleWaveProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
	// A wave needs a clock, and one of the three places this runs has none: a mesh modifier rebuilds
	// geometry in a Blueprint's authoring tree and in a headless test, and this property is a plain
	// UObject whose GetWorld walks an outer chain that ends at the widget tree rather than at a world.
	// Phase zero is the honest answer there -- the still frame of the animation, which is exactly what
	// a designer wants to look at anyway. See DreamUIWorldContext.h for the rule.
	const UWorld* WaveWorld = DreamUI::GetWorldSafe(this);
	float PIxFreq = (WaveWorld != nullptr ? WaveWorld->TimeSeconds : 0.0f) * PI * Speed;
	PIxFreq = FlipDirection ? -PIxFreq : PIxFreq;
	for (int charIndex = InSelection.StartCharIndex; charIndex < InSelection.EndCharCount; charIndex++)
	{
		auto charPropertyItem = charProperties[charIndex];
		int startVertIndex = charPropertyItem.StartVertIndex;
		int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
		auto charCenterPos = originVertices[startVertIndex].Position;
		for (int vertIndex = startVertIndex + 1; vertIndex < endVertIndex; vertIndex++)
		{
			charCenterPos += originVertices[vertIndex].Position;
		}
		charCenterPos /= charPropertyItem.VertCount;
		float lerpValue = FMath::Clamp(InSelection.LerpValueArray[charIndex - InSelection.StartCharIndex], 0.0f, 1.0f);
		auto waveScale = FVector3f::OneVector + ((FVector3f)Scale - FVector3f::OneVector) * FMath::Sin(PIxFreq + charIndex * Frequency);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			auto vector = pos - charCenterPos;
			pos = charCenterPos + vector * waveScale;
		}
	}
}
void UDreamMeshModifierTextAnimation_ScaleWaveProperty::SetScale(FVector Value)
{
	if (Scale != Value)
	{
		Scale = Value;
		MarkUITextPositionDirty();
	}
}
