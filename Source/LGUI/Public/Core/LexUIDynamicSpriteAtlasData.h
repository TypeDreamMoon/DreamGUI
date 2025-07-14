// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Utils/MaxRectsBinPack/MaxRectsBinPack.h"
#include "Engine/Texture2D.h"
#include "LexUIDynamicSpriteAtlasData.generated.h"


class ULexUISpriteData;
class ILexUISpriteRenderInterface;

/** Data container for dynamically generated Sprite atlas */
USTRUCT()
struct LGUI_API FLexUIDynamicSpriteAtlasData
{
	GENERATED_BODY()
	/** AtlasTexture is the real texture for render */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI")
	TObjectPtr<UTexture2D> AtlasTexture = nullptr;
	/** information needed when insert a Sprite */
	rbp::MaxRectsBinPack AtlasBinPack;
	/** sprites belong to this atlas */
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	TArray<TObjectPtr<ULexUISpriteData>> SpriteDataArray;
	/** collection of all objects that use this atlas to render. Object must implement IUISpriteRenderableInterface. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI", AdvancedDisplay)
	TArray<TWeakObjectPtr<UObject>> RenderSpriteArray;

	void EnsureAtlasTexture(const FName& packingTag);
	void CreateAtlasTexture(const FName& packingTag, int oldTextureSize, int newTextureSize);
	/** create a new texture with size * 2 */
	int32 ExpendTextureSize(const FName& packingTag);
	int32 GetWillExpendTextureSize()const;
	void CheckSprite(const FName& packingTag);

	class FLGUIAtlasTextureExpandEvent : public TMulticastDelegate<void(UTexture2D*, int32)>//why not use DECLARE_EVENT here? because DECLARE_EVENT use "friend class XXX", but I need "friend struct"
	{
		friend struct FLexUIDynamicSpriteAtlasData;
	};
	/** atlas texture size may change when dynamic packing, this event will be called when that happen. */
	FLGUIAtlasTextureExpandEvent OnTextureSizeExpanded;
};

UCLASS(NotBlueprintable, NotBlueprintType)
class LGUI_API ULexUIDynamicSpriteAtlasManager :public UObject
{
	GENERATED_BODY()
public:
	static ULexUIDynamicSpriteAtlasManager* Instance;
private:
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI")
		TMap<FName, FLexUIDynamicSpriteAtlasData> AtlasMap;
protected:
	virtual void BeginDestroy()override;
public:
	static bool InitCheck();
	const TMap<FName, FLexUIDynamicSpriteAtlasData>& GetAtlasMap() { return AtlasMap; }
	static FLexUIDynamicSpriteAtlasData* FindOrAdd(const FName& packingTag);
	static FLexUIDynamicSpriteAtlasData* Find(const FName& packingTag);
	static void ResetAtlasMap();

	/**
	 * Dispose and release atlas by PackingTag.
	 * This will not dispose the LexUISpriteData.
	 * Default "Main" tag is not allowed to be disposed.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		static void DisposeAtlasByPackingTag(FName inPackingTag);

	DECLARE_EVENT(ULexUIDynamicSpriteAtlasManager, FLexUIAtlasMapChangeEvent);

	FLexUIAtlasMapChangeEvent OnAtlasMapChanged;
};