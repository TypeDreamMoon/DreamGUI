// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIGeometry.h"
#include "Core/FRichTextParser.h"
#include "LexUIFontData_BaseObject.generated.h"


struct FLexUICharData
{
	float Width = 0;
	float Height = 0;
	float XOffset = 0;
	float YOffset = 0;
	float XAdvance = 0;
	FVector2f MinUV;
	FVector2f MaxUV;

	bool IsValid()const
	{
		return Width > 0 || Height > 0 || XAdvance > 0;
	}

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
class ULexUIFontEmojiData;

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
	virtual FLexUICharData GetCharData(const uint32& charCode, const float& charSize) PURE_VIRTUAL(ULGUIFontData_BaseObject::GetCharData, return FLexUICharData(););
	virtual bool HasKerning() { return false; }
	virtual float GetKerning(const uint32& leftCharIndex, const uint32& rightCharIndex, const float& charSize) { return 0; }
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
		uint32 charCode, const FVector2f& lineOffset, const FVector2f& fontSpace, const FLexUICharData& charData,
		const LexUIRichTextParser::FRichTextParseResult& richTextProperty,
		int verticesStartIndex, int indicesStartIndex,
		int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
		TArray<FLexUIOriginVertexData>& originVertices, TArray<FLexUIMeshVertex>& vertices, TArray<FLexUIMeshIndexBufferType>& triangleIndices
	) {};

	virtual void AddUIText(ULexText* InText) {}
	virtual void RemoveUIText(ULexText* InText) {}

	ULexUIFontEmojiData* GetEmojiData()const{return EmojiData;}

	static ULexUIFontData_BaseObject* GetDefaultFont();

	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
#endif

	DECLARE_EVENT(ULexUIFontData_BaseObject, FLexUIFontEmojiDataRefreshEvent);
	/** Called when emoji data changed, and need LexText to refresh. */
	FLexUIFontEmojiDataRefreshEvent OnEmojiDataChanged;
private:
	UPROPERTY(EditAnywhere, Category = "LGUI")
	TObjectPtr<ULexUIFontEmojiData> EmojiData;
};
