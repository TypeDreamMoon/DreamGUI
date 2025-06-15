// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "UIBaseRenderable.h"
#include "LGUI/Public/Core/Components/LGUICanvas.h"
#include "UIDirectMeshRenderable.generated.h"

struct FLexUIRenderSection;
class ULexUIMeshComponent;
/** 
 * UI element that render directly to LGUICanvas's mesh section. Each UIDirectMeshRenderable is considered as a drawcall.
 */
UCLASS(Abstract, NotBlueprintable)
class LGUI_API UUIDirectMeshRenderable : public UUIBaseRenderable
{
	GENERATED_BODY()

public:	
	UUIDirectMeshRenderable(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction ) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnUnregister()override;

	virtual void UpdateGeometry()override;

	virtual void OnAnchorChange(bool InPivotChange, bool InWidthChange, bool InHeightChange, bool InDiscardCache = true)override;

	void MarkVertexPositionDirty();

	virtual void MarkAllDirty()override;

	virtual bool LineTraceUI(FHitResult& OutHit, const FVector& Start, const FVector& End)override;
public:
	/** Called by LGUICanvas when this UI element have valid mesh data. */
	virtual void OnMeshDataReady();
	virtual TWeakPtr<FLexUIRenderSection> GetMeshSection()const;
	virtual TWeakObjectPtr<ULexUIMeshComponent> GetUIMesh()const;
	virtual void ClearMeshData();
	virtual bool HaveValidData()const PURE_VIRTUAL(UUIDirectMeshRenderable::HaveValidData, return true;);
	virtual UMaterialInterface* GetMaterial()const PURE_VIRTUAL(UUIDirectMeshRenderable::GetMaterial, return nullptr;);
protected:
	uint8 bLocalVertexPositionChanged : 1;
};
