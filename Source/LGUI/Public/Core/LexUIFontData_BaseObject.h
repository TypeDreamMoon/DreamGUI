// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIGeometry.h"
#include "Core/FRichTextParser.h"
#include "LexUIFontData_BaseObject.generated.h"


USTRUCT(BlueprintType)
struct FLexUICharData
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	uint16 Width = 0;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	uint16 Height = 0;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	int16 XOffset = 0;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	int16 YOffset = 0;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	int16 XAdvance = 0;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	FVector2f MinUV;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	FVector2f MaxUV;
};
struct FLexUICharData_HighPrecision
{
	FLexUICharData_HighPrecision() {}
	FLexUICharData_HighPrecision(const FLexUICharData& charData)
	{
		Width = charData.Width;
		Height = charData.Height;
		XOffset = charData.XOffset;
		YOffset = charData.YOffset;
		XAdvance = charData.XAdvance;
		MinUV = charData.MinUV;
		MaxUV = charData.MaxUV;
	}
	float Width = 0;
	float Height = 0;
	float XOffset = 0;
	float YOffset = 0;
	float XAdvance = 0;
	FVector2f MinUV;
	FVector2f MaxUV;

	FVector2f GetUV0()const
	{
		return FVector2f(MinUV.X, MaxUV.Y);
	}
	FVector2f GetUV3()const
	{
		return FVector2f(MaxUV.X, MinUV.Y);
	}
	FVector2f GetUV2()const
	{
		return FVector2f(MinUV.X, MinUV.Y);
	}
	FVector2f GetUV1()const
	{
		return FVector2f(MaxUV.X, MaxUV.Y);
	}
	FVector2f GetUVRange()const
	{
		return FVector2f(MaxUV.X - MinUV.X, MinUV.Y - MaxUV.Y);
	}
};

class UTexture2D;
class ULexText;

/**
 * base font class, UIText can use a implemented asset object to render text
 */
UCLASS(Abstract, BlueprintType)
class LGUI_API ULexUIFontData_BaseObject : public UObject
{
	GENERATED_BODY()
public:
	virtual void InitFont()PURE_VIRTUAL(ULGUISpriteData_BaseObject::InitFont, );

	virtual UMaterialInterface* GetFontMaterial()PURE_VIRTUAL(ULGUISpriteData_BaseObject::GetFontMaterial, return nullptr;);
	virtual UTexture2D* GetFontTexture()PURE_VIRTUAL(ULGUISpriteData_BaseObject::GetFontTexture, return nullptr;);
	virtual FLexUICharData_HighPrecision GetCharData(const TCHAR& charCode, const float& charSize) PURE_VIRTUAL(ULGUIFontData_BaseObject::GetCharData, return FLexUICharData_HighPrecision(););
	virtual bool HasKerning() { return false; }
	virtual float GetKerning(const TCHAR& leftCharIndex, const TCHAR& rightCharIndex, const float& charSize) { return 0; }
	virtual float GetLineHeight(const float& fontSize) { return fontSize; }
	virtual float GetVerticalOffset(const float& fontSize) { return 0; }
	virtual float GetFontSizeLimit() { return MAX_FLT; }
	virtual bool GetRequireNormalAndTangent() { return false; }
	virtual bool GetShouldAffectByPixelPerfect() { return true; }
	virtual bool GetNeedObjectScale() { return false; }
	virtual bool GetSupportDynamicPixelsPerUnit() { return false; }
	virtual float GetFontTextureMark() { return 0; }

	/** this is called every time before create a string of char geometry */
	virtual void PrepareForPushCharData(ULexText* InText) {};
	/** create char geometry and push to vertices & triangleIndices array */
	virtual void PushCharData(
		TCHAR charCode, const FVector2f& lineOffset, const FVector2f& fontSpace, const FLexUICharData_HighPrecision& charData,
		const LexUIRichTextParser::FRichTextParseResult& richTextProperty,
		int verticesStartIndex, int indicesStartIndex,
		int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
		TArray<FLexUIOriginVertexData>& originVertices, TArray<FLexUIMeshVertex>& vertices, TArray<FLexUIMeshIndexBufferType>& triangleIndices
	) {};

	virtual void AddUIText(ULexText* InText) {}
	virtual void RemoveUIText(ULexText* InText) {}

	static ULexUIFontData_BaseObject* GetDefaultFont();
};
