// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI/Public/MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithEase.h"
#include "DreamGUI.h"
#include "Core/Components/DreamText.h"
#include "Curves/CurveFloat.h"
#include "Utils/DreamUIUtils.h"

const FDreamTweenFunction& UDreamMeshModifierTextAnimation_PropertyWithEase::GetEaseFunction()
{
	if (EaseFunc.IsBound())return EaseFunc;
	switch (EaseType)
	{
	case EDreamTweenEase::Linear:
		EaseFunc.BindStatic(&UDreamTweener::Linear);
		break;
	case EDreamTweenEase::InQuad:
		EaseFunc.BindStatic(&UDreamTweener::InQuad);
		break;
	case EDreamTweenEase::OutQuad:
		EaseFunc.BindStatic(&UDreamTweener::OutQuad);
		break;
	case EDreamTweenEase::InOutQuad:
		EaseFunc.BindStatic(&UDreamTweener::InOutQuad);
		break;
	case EDreamTweenEase::InCubic:
		EaseFunc.BindStatic(&UDreamTweener::InCubic);
		break;
	case EDreamTweenEase::OutCubic:
		EaseFunc.BindStatic(&UDreamTweener::OutCubic);
		break;
	case EDreamTweenEase::InOutCubic:
		EaseFunc.BindStatic(&UDreamTweener::InOutCubic);
		break;
	case EDreamTweenEase::InQuart:
		EaseFunc.BindStatic(&UDreamTweener::InQuart);
		break;
	case EDreamTweenEase::OutQuart:
		EaseFunc.BindStatic(&UDreamTweener::OutQuart);
		break;
	case EDreamTweenEase::InOutQuart:
		EaseFunc.BindStatic(&UDreamTweener::InOutQuart);
		break;
	case EDreamTweenEase::InSine:
		EaseFunc.BindStatic(&UDreamTweener::InSine);
		break;
	case EDreamTweenEase::OutSine:
		EaseFunc.BindStatic(&UDreamTweener::OutSine);
		break;
	default:
	case EDreamTweenEase::InOutSine:
		EaseFunc.BindStatic(&UDreamTweener::InOutSine);
		break;
	case EDreamTweenEase::InExpo:
		EaseFunc.BindStatic(&UDreamTweener::InExpo);
		break;
	case EDreamTweenEase::OutExpo:
		EaseFunc.BindStatic(&UDreamTweener::OutExpo);
		break;
	case EDreamTweenEase::InOutExpo:
		EaseFunc.BindStatic(&UDreamTweener::InOutExpo);
		break;
	case EDreamTweenEase::InCirc:
		EaseFunc.BindStatic(&UDreamTweener::InCirc);
		break;
	case EDreamTweenEase::OutCirc:
		EaseFunc.BindStatic(&UDreamTweener::OutCirc);
		break;
	case EDreamTweenEase::InOutCirc:
		EaseFunc.BindStatic(&UDreamTweener::InOutCirc);
		break;
	case EDreamTweenEase::InElastic:
		EaseFunc.BindStatic(&UDreamTweener::InElastic);
		break;
	case EDreamTweenEase::OutElastic:
		EaseFunc.BindStatic(&UDreamTweener::OutElastic);
		break;
	case EDreamTweenEase::InOutElastic:
		EaseFunc.BindStatic(&UDreamTweener::InOutElastic);
		break;
	case EDreamTweenEase::InBack:
		EaseFunc.BindStatic(&UDreamTweener::InBack);
		break;
	case EDreamTweenEase::OutBack:
		EaseFunc.BindStatic(&UDreamTweener::OutBack);
		break;
	case EDreamTweenEase::InOutBack:
		EaseFunc.BindStatic(&UDreamTweener::InOutBack);
		break;
	case EDreamTweenEase::InBounce:
		EaseFunc.BindStatic(&UDreamTweener::InBounce);
		break;
	case EDreamTweenEase::OutBounce:
		EaseFunc.BindStatic(&UDreamTweener::OutBounce);
		break;
	case EDreamTweenEase::InOutBounce:
		EaseFunc.BindStatic(&UDreamTweener::InOutBounce);
		break;
	case EDreamTweenEase::CurveFloat:
		EaseFunc.BindUObject(this, &UDreamMeshModifierTextAnimation_PropertyWithEase::EaseCurveFunction);
		break;
	}
	return EaseFunc;
}
float UDreamMeshModifierTextAnimation_PropertyWithEase::EaseCurveFunction(float c, float b, float t, float d)
{
	if (EaseCurve != nullptr)
	{
		return EaseCurve->GetFloatValue(t / d) * c + b;
	}
	else
	{
		return UDreamTweener::Linear(c, b, t, d);
	}
}
#if WITH_EDITOR
void UDreamMeshModifierTextAnimation_PropertyWithEase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto propertyName = Property->GetFName();
		if (propertyName == GET_MEMBER_NAME_CHECKED(UDreamMeshModifierTextAnimation_PropertyWithEase, EaseType))
		{
			EaseFunc.Unbind();
		}
	}
}
#endif

void UDreamMeshModifierTextAnimation_PropertyWithEase::SetEaseType(EDreamTweenEase Value)
{
	if (EaseType != Value)
	{
		EaseType = Value;
		EaseFunc.Unbind();
		if (auto DreamText = GetDreamText())
		{
			DreamText->MarkVertexPositionDirty();
		}
	}
}
void UDreamMeshModifierTextAnimation_PropertyWithEase::SetEaseCurve(UCurveFloat* Value)
{
	if (EaseCurve != Value)
	{
		EaseCurve = Value;
		if (EaseType == EDreamTweenEase::CurveFloat)
		{
			if (auto DreamText = GetDreamText())
			{
				DreamText->MarkVertexPositionDirty();
			}
		}
	}
}

void UDreamMeshModifierTextAnimation_PositionProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	auto easeFunction = GetEaseFunction();
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
	for (int charIndex = InSelection.StartCharIndex; charIndex < InSelection.EndCharCount; charIndex++)
	{
		auto charPropertyItem = charProperties[charIndex];
		int startVertIndex = charPropertyItem.StartVertIndex;
		int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
		float lerpValue = FMath::Clamp(InSelection.LerpValueArray[charIndex - InSelection.StartCharIndex], 0.0f, 1.0f);
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			pos = FMath::Lerp(pos, pos + (FVector3f)Position, lerpValue);
		}
	}
}

void UDreamMeshModifierTextAnimation_PositionRandomProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	FMath::RandInit(Seed);
	auto easeFunction = GetEaseFunction();
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
	for (int charIndex = InSelection.StartCharIndex; charIndex < InSelection.EndCharCount; charIndex++)
	{
		auto charPropertyItem = charProperties[charIndex];
		int startVertIndex = charPropertyItem.StartVertIndex;
		int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
		float lerpValue = FMath::Clamp(InSelection.LerpValueArray[charIndex - InSelection.StartCharIndex], 0.0f, 1.0f);
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		auto position = FVector3f(FMath::FRandRange(Min.X, Max.X), FMath::FRandRange(Min.Y, Max.Y), FMath::FRandRange(Min.Z, Max.Z));
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			pos = FMath::Lerp(pos, pos + position, lerpValue);
		}
	}
}

void UDreamMeshModifierTextAnimation_RotationProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	auto easeFunction = GetEaseFunction();
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
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
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		auto calcRotationMatrix = FRotationMatrix44f(((FRotator3f)rotator) * lerpValue);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			auto vector = pos - charCenterPos;
			pos = charCenterPos + calcRotationMatrix.TransformPosition(vector);
		}
	}
}

void UDreamMeshModifierTextAnimation_RotationRandomProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	FMath::RandInit(Seed);
	auto easeFunction = GetEaseFunction();
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
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
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		auto rotator = FRotator3f(FMath::FRandRange(Min.Pitch, Max.Pitch), FMath::FRandRange(Min.Yaw, Max.Yaw), FMath::FRandRange(Min.Roll, Max.Roll));
		auto calcRotationMatrix = FRotationMatrix44f(rotator * lerpValue);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			auto vector = pos - charCenterPos;
			pos = charCenterPos + calcRotationMatrix.TransformPosition(vector);
		}
	}
}

void UDreamMeshModifierTextAnimation_ScaleProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	auto easeFunction = GetEaseFunction();
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
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
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		auto calcScale = FMath::Lerp(FVector3f::OneVector, (FVector3f)Scale, lerpValue);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			auto vector = pos - charCenterPos;
			pos = charCenterPos + vector * calcScale;
		}
	}
}

void UDreamMeshModifierTextAnimation_ScaleRandomProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	FMath::RandInit(Seed);
	auto easeFunction = GetEaseFunction();
	auto& originVertices = InGeometry->OriginVertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
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
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		auto scale = FVector3f(FMath::FRandRange(Min.X, Max.X), FMath::FRandRange(Min.Y, Max.Y), FMath::FRandRange(Min.Z, Max.Z));
		auto calcScale = FMath::Lerp(FVector3f::OneVector, scale, lerpValue);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			auto vector = pos - charCenterPos;
			pos = charCenterPos + vector * calcScale;
		}
	}
}

void UDreamMeshModifierTextAnimation_AlphaProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	auto easeFunction = GetEaseFunction();
	auto& vertices = InGeometry->Vertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
	for (int charIndex = InSelection.StartCharIndex; charIndex < InSelection.EndCharCount; charIndex++)
	{
		auto charPropertyItem = charProperties[charIndex];
		int startVertIndex = charPropertyItem.StartVertIndex;
		int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
		float lerpValue = FMath::Clamp(InSelection.LerpValueArray[charIndex - InSelection.StartCharIndex], 0.0f, 1.0f);
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& vert = vertices[vertIndex];
			vert.Color.A = FMath::Lerp(vert.Color.A, (uint8)(vert.Color.A * Alpha), lerpValue);
		}
	}
}

void UDreamMeshModifierTextAnimation_ColorProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	auto easeFunction = GetEaseFunction();
	auto& vertices = InGeometry->Vertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
	FVector colorHsv;
	if (bUseHSV)
	{
		colorHsv = FDreamUIUtils::ColorRGBToColorHSVData(Color);
	}
	for (int charIndex = InSelection.StartCharIndex; charIndex < InSelection.EndCharCount; charIndex++)
	{
		auto charPropertyItem = charProperties[charIndex];
		int startVertIndex = charPropertyItem.StartVertIndex;
		int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
		float lerpValue = FMath::Clamp(InSelection.LerpValueArray[charIndex - InSelection.StartCharIndex], 0.0f, 1.0f);
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& vert = vertices[vertIndex];
			if (bUseHSV)
			{
				auto vertColorHsv = FDreamUIUtils::ColorRGBToColorHSVData(vert.Color);
				vertColorHsv = FMath::Lerp(vertColorHsv, colorHsv, lerpValue);
				auto vertColor = FDreamUIUtils::ColorHSVDataToColorRGB(vertColorHsv);
				vert.Color.R = vertColor.R;
				vert.Color.G = vertColor.G;
				vert.Color.B = vertColor.B;
			}
			else
			{
				vert.Color.R = FMath::Lerp(vert.Color.R, Color.R, lerpValue);
				vert.Color.G = FMath::Lerp(vert.Color.G, Color.G, lerpValue);
				vert.Color.B = FMath::Lerp(vert.Color.B, Color.B, lerpValue);
			}
			vert.Color.A = FMath::Lerp(vert.Color.A, Color.A, lerpValue);
		}
	}
}
void UDreamMeshModifierTextAnimation_ColorProperty::SetUseHSV(bool Value)
{
	if (bUseHSV != Value)
	{
		bUseHSV = Value;
		if (auto DreamText = GetDreamText())
		{
			DreamText->MarkColorDirty();
		}
	}
}

void UDreamMeshModifierTextAnimation_ColorRandomProperty::ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry)
{
	FMath::RandInit(Seed);
	auto easeFunction = GetEaseFunction();
	auto& vertices = InGeometry->Vertices;
	auto& charProperties = InUIText->GetCharPropertyArray();
	for (int charIndex = InSelection.StartCharIndex; charIndex < InSelection.EndCharCount; charIndex++)
	{
		auto charPropertyItem = charProperties[charIndex];
		int startVertIndex = charPropertyItem.StartVertIndex;
		int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
		auto color = FColor((uint8)FMath::RandRange(Min.R, Max.R), (uint8)FMath::RandRange(Min.G, Max.G), (uint8)FMath::RandRange(Min.B, Max.B), (uint8)FMath::RandRange(Min.A, Max.A));
		float lerpValue = FMath::Clamp(InSelection.LerpValueArray[charIndex - InSelection.StartCharIndex], 0.0f, 1.0f);
		lerpValue = easeFunction.Execute(1.0f, 0.0f, lerpValue, 1.0f);
		FVector colorHsv;
		if (bUseHSV)
		{
			colorHsv = FDreamUIUtils::ColorRGBToColorHSVData(color);
		}
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& vert = vertices[vertIndex];
			if (bUseHSV)
			{
				auto vertColorHsv = FDreamUIUtils::ColorRGBToColorHSVData(vert.Color);
				vertColorHsv = FMath::Lerp(vertColorHsv, colorHsv, lerpValue);
				auto vertColor = FDreamUIUtils::ColorHSVDataToColorRGB(vertColorHsv);
				vert.Color.R = vertColor.R;
				vert.Color.G = vertColor.G;
				vert.Color.B = vertColor.B;
			}
			else
			{
				vert.Color.R = FMath::Lerp(vert.Color.R, color.R, lerpValue);
				vert.Color.G = FMath::Lerp(vert.Color.G, color.G, lerpValue);
				vert.Color.B = FMath::Lerp(vert.Color.B, color.B, lerpValue);
			}
			vert.Color.A = FMath::Lerp(vert.Color.A, color.A, lerpValue);
		}
	}
}
void UDreamMeshModifierTextAnimation_ColorRandomProperty::SetUseHSV(bool Value)
{
	if (bUseHSV != Value)
	{
		bUseHSV = Value;
		if (auto DreamText = GetDreamText())
		{
			DreamText->MarkColorDirty();
		}
	}
}

void UDreamMeshModifierTextAnimation_PositionProperty::SetPosition(FVector Value)
{
	if (Position != Value)
	{
		Position = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_PositionRandomProperty::SetSeed(int Value)
{
	if (Seed != Value)
	{
		Seed = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_PositionRandomProperty::SetMin(FVector Value)
{
	if (Min != Value)
	{
		Min = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_PositionRandomProperty::SetMax(FVector Value)
{
	if (Max != Value)
	{
		Max = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_RotationProperty::SetRotator(FRotator value)
{
	if (rotator != value)
	{
		rotator = value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_RotationRandomProperty::SetSeed(int Value)
{
	if (Seed != Value)
	{
		Seed = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_RotationRandomProperty::SetMin(FRotator Value)
{
	if (Min != Value)
	{
		Min = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_RotationRandomProperty::SetMax(FRotator Value)
{
	if (Max != Value)
	{
		Max = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_ScaleProperty::SetScale(FVector Value)
{
	if (Scale != Value)
	{
		Scale = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_ScaleRandomProperty::SetSeed(int Value)
{
	if (Seed != Value)
	{
		Seed = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_ScaleRandomProperty::SetMin(FVector Value)
{
	if (Min != Value)
	{
		Min = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_ScaleRandomProperty::SetMax(FVector Value)
{
	if (Max != Value)
	{
		Max = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_AlphaProperty::SetAlpha(float Value)
{
	if (Alpha != Value)
	{
		Alpha = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_ColorProperty::SetColor(FColor value)
{
	if (Color != value)
	{
		Color = value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_ColorRandomProperty::SetSeed(int Value)
{
	if (Seed != Value)
	{
		Seed = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_ColorRandomProperty::SetMin(FColor Value)
{
	if (Min != Value)
	{
		Min = Value;
		MarkUITextPositionDirty();
	}
}
void UDreamMeshModifierTextAnimation_ColorRandomProperty::SetMax(FColor Value)
{
	if (Max != Value)
	{
		Max = Value;
		MarkUITextPositionDirty();
	}
}
