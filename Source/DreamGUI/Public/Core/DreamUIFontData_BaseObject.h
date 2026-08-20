// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIGeometry.h"
#include "Core/FRichTextParser.h"
#include "DreamUIFontData_BaseObject.generated.h"


struct FDreamUICharData
{
	float Width = 0;
	float Height = 0;
	float XOffset = 0;
	float YOffset = 0;
	float XAdvance = 0;
	FVector2f MinUV;
	FVector2f MaxUV;
	int32 SliceIndex = 0;//texture index in Texture2DArray

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

enum class EDreamUIFontTextureMark : uint8
{
	None = 0, Bitmap = 1, DistanceField = 2,
};

class UTexture2D;
class UDreamText;
class UDreamUIFontEmojiData;

/**
 * base font class, UIText can use a implemented asset object to render text
 */
UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamUIFontData_BaseObject : public UObject
{
	GENERATED_BODY()
public:
	virtual void InitFont()PURE_VIRTUAL(UDreamGUISpriteData_BaseObject::InitFont, );

	virtual UMaterialInterface* GetFontMaterial()PURE_VIRTUAL(UDreamUIFontData_BaseObject::GetFontMaterial, return nullptr;);
	virtual UTexture2DArray* GetFontTexture()PURE_VIRTUAL(UDreamUIFontData_BaseObject::GetFontTexture, return nullptr;);
	virtual FDreamUICharData GetCharData(uint32 CharCode, float CharSize, bool IsBold) PURE_VIRTUAL(UDreamUIFontData::GetCharData, return FDreamUICharData(););
	virtual bool HasKerning() { return false; }
	virtual float GetKerning(uint32 LeftCharIndex, uint32 RightCharIndex, float CharSize) { return 0; }
	virtual float GetLineHeight(float FontSize) { return FontSize; }
	virtual float GetVerticalOffset(float FontSize) { return 0; }
	virtual float GetFontSizeLimit() { return MAX_FLT; }
	virtual bool GetRequireNormalAndTangent() { return false; }
	virtual bool GetShouldAffectByPixelPerfect() { return true; }
	virtual bool GetNeedObjectScale() { return false; }
	virtual bool GetSupportDynamicPixelsPerUnit() { return false; }
	virtual EDreamUIFontTextureMark GetFontTextureMark() { return EDreamUIFontTextureMark::None; }
	virtual float GetBoldRatio() { return 0; }

	/** this is called every time before create a string of char geometry */
	virtual void PrepareForPushCharData(UDreamText* InText) {};
	/** create char geometry and push to vertices & triangleIndices array */
	virtual void PushCharData(
		uint32 charCode, FVector2f lineOffset, FVector2f fontSpace, const FDreamUICharData& charData,
		const DreamUIRichTextParser::FRichTextParseResult& richTextProperty,
		int verticesStartIndex, int indicesStartIndex,
		int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
		TArray<FDreamUIOriginVertexData>& originVertices, TArray<FDreamUIMeshVertex>& vertices, TArray<FDreamUIMeshIndex>& triangleIndices
	) {};

	virtual void AddUIText(UDreamText* InText) {}
	virtual void RemoveUIText(UDreamText* InText) {}

	UDreamUIFontEmojiData* GetEmojiData()const{return EmojiData;}
	const TArray<TObjectPtr<UMaterialInterface>>& GetPresetMaterials()const{return PresetMaterials;}

	static UDreamUIFontData_BaseObject* GetDefaultFont();

	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
#endif

	DECLARE_EVENT(UDreamUIFontData_BaseObject, FDreamUIFontEmojiDataRefreshEvent);
	/** Called when emoji data changed, and need DreamText to refresh. */
	FDreamUIFontEmojiDataRefreshEvent OnEmojiDataChanged;
protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TObjectPtr<UDreamUIFontEmojiData> EmojiData;

	/**
	 * Put materials here so DreamText can easily select OverrideMaterial from this array.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TArray<TObjectPtr<UMaterialInterface>> PresetMaterials;
};
