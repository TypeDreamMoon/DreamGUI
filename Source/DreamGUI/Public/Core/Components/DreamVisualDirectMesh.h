// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamVisual.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamVisualDirectMesh.generated.h"

struct FDreamUIRenderSection_DirectMesh;
class UDreamUIMeshComponent;
/** 
 * UI element that can update mesh data directly to DreamCanvas's mesh section. Each DreamVisualDirectMesh is considered as a draw-call.
 * This is mainly for custom mesh which have huge vertex data and change frequently, so that we can avoid the overhead of updating each visual element from DreamCanvas.
 * Officially use it for DreamStaticMesh and particle system.
 */
UCLASS(Abstract, NotBlueprintable)
class DREAMGUI_API UDreamVisualDirectMesh : public UDreamVisual
{
	GENERATED_BODY()

public:	
	UDreamVisualDirectMesh(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** enable properties for material */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamVisualPropertiesForMaterial"))
	int8 PropertiesForMaterial = 0;
	
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;
	virtual void MarkAllDirty()override;
	virtual bool LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const override;
	void PostFillMeshData();
public:
	FORCEINLINE bool GetRequirePropertiesForMaterial_Size()const{ return PropertiesForMaterial & (1 << (int)EDreamVisualPropertiesForMaterial::Size); }
	FORCEINLINE bool GetRequirePropertiesForMaterial_CenterPosition()const{ return PropertiesForMaterial & (1 << (int)EDreamVisualPropertiesForMaterial::CenterPosition); }
	
	/** Called by DreamUIMesh when apply mesh data to this UI element. */
	virtual void OnSupplyMeshSection(TWeakObjectPtr<UDreamUIMeshComponent> InMesh, TWeakPtr<FDreamUIRenderSection_DirectMesh> InSection);
	virtual void ClearMeshData();
	virtual bool HaveValidData()const PURE_VIRTUAL(UUIDirectMeshRenderable::HaveValidData, return true;);
	virtual UMaterialInterface* GetMaterial()const PURE_VIRTUAL(UUIDirectMeshRenderable::GetMaterial, return nullptr;);
protected:
	uint8 bLocalVertexPositionChanged : 1;
	TWeakObjectPtr<UDreamUIMeshComponent> Mesh;
	TWeakPtr<FDreamUIRenderSection_DirectMesh> MeshSection;
};
