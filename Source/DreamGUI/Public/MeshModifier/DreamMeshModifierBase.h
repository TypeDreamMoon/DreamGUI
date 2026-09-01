// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamMeshModifierBase.generated.h"

class UDreamVisualBatchMesh;
class UDreamText;

UENUM(BlueprintType)
enum class EDreamUIMeshModifierHelper_TextPositionType:uint8
{
	//Relative to character's origin position
	Relative,
	//Direct set character's position, relative to UIText's pivot position
	Absolute,
};
/** a helper class for UIGeometryModifierBase to easily modify ui geometry */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamVisualBatchMeshModifierHelper : public UDreamUIGeometryHelper
{
	GENERATED_BODY()
public:
	/** Get character's center position in UIText's rect range, and convert to 0-1 range (left is 0 and right is 1) */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float UITextHelperFunction_GetCharHorizontalPositionRatio01(UDreamText* InUIText, int InCharIndex)const;
	/**
	 * Modify character's position & rotation & scale
	 * @param	InPositionType		Set position type, relative to origin position or absolute position
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void UITextHelperFunction_ModifyCharGeometry_Transform(UDreamText* InUIText, int InCharIndex
			, EDreamUIMeshModifierHelper_TextPositionType InPositionType
			, const FVector& InPosition
			, const FRotator& InRotator = FRotator::ZeroRotator
			, const FVector& InScale = FVector(1,1,1)
		);
	/**
	 * Get character's pivot position relative to UIText's pivot position
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void UITextHelperFunction_GetCharGeometry_AbsolutePosition(UDreamText* InUIText, int InCharIndex, FVector& OutPosition)const;
	/**
	 * Modify character's position
	 * @param	InPositionType		Set position type, relative to origin position or absolute position
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void UITextHelperFunction_ModifyCharGeometry_Position(UDreamText* InUIText, int InCharIndex
			, const FVector& InPosition, EDreamUIMeshModifierHelper_TextPositionType InPositionType
		);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void UITextHelperFunction_ModifyCharGeometry_Rotate(UDreamText* InUIText, int InCharIndex, const FRotator& InRotator = FRotator::ZeroRotator);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void UITextHelperFunction_ModifyCharGeometry_Scale(UDreamText* InUIText, int InCharIndex, const FVector& InScale = FVector(1, 1, 1));
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void UITextHelperFunction_ModifyCharGeometry_Color(UDreamText* InUIText, int InCharIndex, const FColor& InColor);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void UITextHelperFunction_ModifyCharGeometry_Alpha(UDreamText* InUIText, int InCharIndex, const float& InAlpha);
};

/** 
 * For modify ui mesh, like a filter.
 */
UCLASS(Abstract, Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamMeshModifierBase : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:	
	UDreamMeshModifierBase();

protected:
	friend class UDreamVisualBatchMesh;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	FDelegateHandle ComponentsChangedDelegateHandle;

	/** Enable this mesh modifier */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bEnable = true;

private:
	mutable TWeakObjectPtr<UDreamVisualBatchMesh> CacheVisualBatchMesh;
	/**
	 * Whether this modifier currently occupies a slot in some visual's modifier list. The cached mesh
	 * pointer cannot stand in for this, because it is re-resolved on every access -- including before
	 * the component is ever registered and after the widget has let it go -- and re-adding the
	 * modifier in either of those states would resurrect a registration that no longer exists.
	 */
	uint8 bRegisteredWithVisual : 1 = false;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamVisualBatchMesh* GetVisualBatchMesh()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnable()const { return bEnable; }
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnable(bool Value);
	/**
	 * Modify UI mesh's vertex and triangle.
	 * @param	InTriangleChanged		triangle changed
	 * @param	InUVChanged			vertex uv changed
	 * @param	InColorChanged			vertex color changed
	 * @param	InVertexPositionChanged			vertex position changed
	 */
	virtual void ModifyUIGeometry(
		FDreamUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	);
	/**
	 * Will this modifier affect these geometry data? Save some calculation if not affect.
	 * For blueprint just make all to true, for easier use.
	 */
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)
	{
		OutTriangleIndices = true;
		OutVertexPosition = true;
		OutUV = true;
		OutColor = true;
	}
protected:
	UPROPERTY(Transient) TObjectPtr<UDreamVisualBatchMeshModifierHelper> GeometryModifierHelper = nullptr;
	/**
	 * Modify UI geometry's vertex and triangle.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "ModifyUIGeometry"))
		void ReceiveModifyUIGeometry(UDreamVisualBatchMeshModifierHelper* InGeometryModifierHelper);
};
