// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/IDreamUISpriteRenderInterface.h"
#include "DreamSpriteRegistrationTestTypes.generated.h"

/**
 * A sprite that answers like a packed one and counts who registers with it.
 *
 * The real UDreamUISpriteData answers GetAtlasTexture() and GetSpriteInfo() by running
 * InitSpriteData, which waits for texture compilation and packs a runtime atlas -- neither of which
 * a headless test can stand up, and neither of which is what the tests using this probe are about.
 * What they are about is the HAND-OVER: which sprite a visual unregisters from and which one it
 * registers with when the brush changes. That is a pair of virtual calls, so a probe that records
 * them says exactly as much as the real thing would, and says it without an RHI.
 */
UCLASS()
class UDreamSpriteRegistrationProbe : public UDreamUISpriteData_BaseObject
{
	GENERATED_BODY()

public:
	virtual UTexture2D* GetAtlasTexture() override { return AtlasTexture; }
	virtual const FDreamUISpriteInfo& GetSpriteInfo() override { return SpriteInfo; }
	virtual bool IsIndividual() const override { return false; }
	virtual bool ReadPixel(const FVector2D& InUV, FColor& OutPixel) const override { return false; }
	virtual bool SupportReadPixel() const override { return false; }

	virtual void AddUISprite(TScriptInterface<IDreamUISpriteRenderInterface> InUISprite) override
	{
		++AddCount;
		LastAdded = InUISprite.GetObject();
	}
	virtual void RemoveUISprite(TScriptInterface<IDreamUISpriteRenderInterface> InUISprite) override
	{
		++RemoveCount;
		LastRemoved = InUISprite.GetObject();
	}

	/** What GetAtlasTexture answers. Two probes sharing one (or sharing null) are "in the same atlas". */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> AtlasTexture = nullptr;
	/** What GetSpriteInfo answers. Width and Height are the authored pixel size a layout measures by. */
	FDreamUISpriteInfo SpriteInfo;

	int32 AddCount = 0;
	int32 RemoveCount = 0;
	UPROPERTY(Transient)
	TObjectPtr<UObject> LastAdded = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UObject> LastRemoved = nullptr;
};

/**
 * Something that draws a sprite, without being an element that needs a canvas to exist.
 *
 * The dynamic atlas decides what it may evict by asking everything in its render list which sprite it
 * draws, and it tells the survivors of a repack that their rectangle moved. Both of those are calls
 * through IDreamUISpriteRenderInterface, so a probe that answers the first and counts the second is
 * exactly the fixture those decisions need -- and a real UDreamImage is not, because setting its
 * brush initialises the sprite, which packs it into the process-wide atlas for its tag.
 */
UCLASS()
class UDreamSpriteRenderProbe : public UObject, public IDreamUISpriteRenderInterface
{
	GENERATED_BODY()

public:
	virtual UDreamUISpriteData_BaseObject* SpriteRenderGetSprite_Implementation()const override { return DrawnSprite; }
	virtual void ApplyAtlasTextureChange_Implementation() override { ++AtlasChangeCount; }

	/** What this probe reports drawing. Set it directly; nothing here initialises the sprite. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamUISpriteData_BaseObject> DrawnSprite = nullptr;
	int32 AtlasChangeCount = 0;
};
