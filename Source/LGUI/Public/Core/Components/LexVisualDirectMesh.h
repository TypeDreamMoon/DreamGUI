// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexVisual.h"
#include "Core/Components/LexCanvas.h"
#include "LexVisualDirectMesh.generated.h"

struct FLexUIRenderSection_DirectMesh;
class ULexUIMeshComponent;
/** 
 * UI element that can update mesh data directly to LexCanvas's mesh section. Each LexVisualDirectMesh is considered as a draw-call.
 * This is mainly for custom mesh which have huge vertex data and change frequently, so that we can avoid the overhead of updating each visual element from LexCanvas.
 * Officially use it for LexStaticMesh and particle system.
 */
UCLASS(Abstract, NotBlueprintable)
class LGUI_API ULexVisualDirectMesh : public ULexVisual
{
	GENERATED_BODY()

public:	
	ULexVisualDirectMesh(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** enable properties for material */
	UPROPERTY(EditAnywhere, Category = LGUI, meta = (Bitmask, BitmaskEnum = "/Script/LGUI.ELexVisualPropertiesForMaterial"))
	int8 PropertiesForMaterial = 0;
	
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;
	virtual void MarkAllDirty()override;
	virtual bool LineTraceUI(FLexUIHitResult& OutHit, const FVector& Start, const FVector& End)const override;
	void PostFillMeshData();
public:
	FORCEINLINE bool GetRequirePropertiesForMaterial_Size()const{ return PropertiesForMaterial & (1 << (int)ELexVisualPropertiesForMaterial::Size); }
	FORCEINLINE bool GetRequirePropertiesForMaterial_CenterPosition()const{ return PropertiesForMaterial & (1 << (int)ELexVisualPropertiesForMaterial::CenterPosition); }
	
	/** Called by LexUIMesh when apply mesh data to this UI element. */
	virtual void OnSupplyMeshSection(TWeakObjectPtr<ULexUIMeshComponent> InMesh, TWeakPtr<FLexUIRenderSection_DirectMesh> InSection);
	virtual void ClearMeshData();
	virtual bool HaveValidData()const PURE_VIRTUAL(UUIDirectMeshRenderable::HaveValidData, return true;);
	virtual UMaterialInterface* GetMaterial()const PURE_VIRTUAL(UUIDirectMeshRenderable::GetMaterial, return nullptr;);
protected:
	uint8 bLocalVertexPositionChanged : 1;
	TWeakObjectPtr<ULexUIMeshComponent> Mesh;
	TWeakPtr<FLexUIRenderSection_DirectMesh> MeshSection;
};
