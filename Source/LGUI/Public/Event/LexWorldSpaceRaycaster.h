// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexBaseRaycaster.h"
#include "LexWorldSpaceRaycaster.generated.h"

class ULexWorldSpaceRaycaster;

/** Interaction target for world space */
UENUM(BlueprintType, Category = LGUI)
enum class ELexUIInteractionTarget :uint8
{
	/** Only hit UI object */
	UI,
	/** Only hit world object */
	World,
	/** Hit UI and world object */
	UIAndWorld		UMETA(DisplayName="UI and World"),
};

/**
 * Interaction source for LGUIWorldSpaceRaycaster
 */
UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexUIWorldSpaceRaycasterSource : public UObject
{
	GENERATED_BODY()
private:
	TWeakObjectPtr<ULexBaseRaycaster> RaycasterObject = nullptr;
public:
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexBaseRaycaster* GetRaycasterObject()const;
	/** Called by LexUIWorldSpaceRaycaster when register, use as initialize. */
	virtual void Init(ULexBaseRaycaster* InRaycaster);
	/** Generate ray for raycast hit test */
	virtual bool GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection);
	/** Should convert press event to drag event? */
	virtual bool ShouldStartDrag(ULexPointerEventData* InPointerEventData);
protected:
	/** Called by LexUIWorldSpaceRaycaster when register, use as initialize. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Init"), Category = "LGUI")
		void ReceiveInit(ULexBaseRaycaster* InRaycaster);
	/** Generate ray for raycast hit test */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "EmitRay"), Category = "LGUI")
		bool ReceiveGenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection);
	/** Should convert press event to drag event? */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ShouldStartDrag"), Category = "LGUI")
		bool ReceiveShouldStartDrag(ULexPointerEventData* InPointerEventData);
};

enum class ELexRenderMode :uint8;

/**
 * Perform a raycaster interaction for WorldSpaceUI and common world space objects.
 * One world can only have one LexWorldSpaceRaycaster. 
 */
UCLASS(ClassGroup = LGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class LGUI_API ULexWorldSpaceRaycaster : public ULexBaseRaycaster
{
	GENERATED_BODY()
	
public:	
	ULexWorldSpaceRaycaster();
	virtual void BeginPlay()override;
	virtual void OnRegister()override;
protected:
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexUIInteractionTarget InteractionTarget = ELexUIInteractionTarget::UIAndWorld;
	/** Will get FaceIndex when line trace world object's mesh. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bRequireFaceIndex = false;

	UPROPERTY(EditAnywhere, Instanced, Category = "LGUI")
		TObjectPtr<ULexUIWorldSpaceRaycasterSource> RaycasterSourceObject = nullptr;
	virtual bool ShouldSkipCanvas(class ULexCanvas* UICanvas)override;
	TArray<ELexRenderMode> RenderModeArray;
public:
	virtual bool GetAffectByGamePause()const override;
	virtual bool GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection) override;
	virtual bool Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray)override;
	virtual bool ShouldStartDrag(ULexPointerEventData* InPointerEventData) override;

	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexUIWorldSpaceRaycasterSource* GetRaycasterSourceObject()const { return RaycasterSourceObject; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRaycasterSourceObject(ULexUIWorldSpaceRaycasterSource* NewSource);
};
