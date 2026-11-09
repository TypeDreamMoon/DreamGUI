// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamVisual.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamVisualDirectMesh.generated.h"

struct FDreamUIRenderSection_DirectMesh;
class UDreamUIMeshComponent;
class UTexture;

/**
 * How the canvas shades one segment of a direct mesh when the element leaves it to the canvas (UDreamVisualDirectMesh::
 * GetSegmentShading): as a batched element is shaded, with the clip data, the clip masks' textures and the blend mode.
 */
struct FDreamUIDirectMeshShading
{
	/** The built-in UI shader draws the segment with Texture (white where there is none); otherwise Material does. */
	bool bBuiltIn = true;
	TWeakObjectPtr<UTexture> Texture;
	/** A material DreamGUI answers its parameters for (the clip data, the masks' textures, the main texture), as for a batched element's. */
	TWeakObjectPtr<UMaterialInterface> Material;
	/** The built-in shader's: a material blends as it says. */
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
};

/**
 * UI element that can update mesh data directly to DreamCanvas's mesh section. Each DreamVisualDirectMesh is considered as a draw-call.
 * This is mainly for custom mesh which have huge vertex data and change frequently, so that we can avoid the overhead of updating each visual element from DreamCanvas.
 * Officially use it for DreamStaticMesh and particle system.
 *
 * An element may draw in several segments (GetSegmentCount): each is a draw call and a section of its own, in segment order,
 * supplied through OnSupplyMeshSegment. Segment 0 is also MeshSection, as it was when there was only one.
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
	/** The widget property record: its mark (GetWidgetPropertyMark), clip coordinate and, when asked, size and centre. The vertices carry its row in UV1.x. */
	void UpdateWidgetPropertyRecord();
public:
	FORCEINLINE bool GetRequirePropertiesForMaterial_Size()const{ return PropertiesForMaterial & (1 << (int)EDreamVisualPropertiesForMaterial::Size); }
	FORCEINLINE bool GetRequirePropertiesForMaterial_CenterPosition()const{ return PropertiesForMaterial & (1 << (int)EDreamVisualPropertiesForMaterial::CenterPosition); }
	
	/** Called by DreamUIMesh when apply mesh data to this UI element. */
	virtual void OnSupplyMeshSection(TWeakObjectPtr<UDreamUIMeshComponent> InMesh, TWeakPtr<FDreamUIRenderSection_DirectMesh> InSection);
	/**
	 * Called by DreamUIMesh with each segment's section. Keeps it (GetMeshSegment) and, for segment 0, calls
	 * OnSupplyMeshSection, which an element drawing in one segment overrides.
	 */
	virtual void OnSupplyMeshSegment(TWeakObjectPtr<UDreamUIMeshComponent> InMesh, TWeakPtr<FDreamUIRenderSection_DirectMesh> InSection, int32 InSegment);
	virtual void ClearMeshData();
	virtual bool HaveValidData()const PURE_VIRTUAL(UUIDirectMeshRenderable::HaveValidData, return true;);
	virtual UMaterialInterface* GetMaterial()const PURE_VIRTUAL(UUIDirectMeshRenderable::GetMaterial, return nullptr;);

	/**
	 * How many segments the element draws in, each a draw call and a section of its own, drawn in segment order. Asked on the
	 * game thread as the canvas prepares its draw calls: an element whose count changes asks for a canvas update. One by default.
	 */
	virtual int32 GetSegmentCount()const { return 1; }
	/**
	 * Whether the canvas shades segment InSegment itself, and how: the built-in shader or a material, given the clip data,
	 * the clip masks' textures and the blend mode as a batched element is. False, the default, leaves the section's material to
	 * the element (UDreamUIMeshComponent::SetupDirectMeshRenderSection), and the clip to its material. Game thread, when the
	 * canvas builds its draw calls; an element whose shading changes asks for a canvas update.
	 */
	virtual bool GetSegmentShading(int32 InSegment, FDreamUIDirectMeshShading& OutShading)const { return false; }
	/**
	 * Whether the canvas calls RefreshMeshSegments every frame it sends its sections, which an element whose vertices change
	 * every frame (particles) wants instead of a canvas update for each. Asked as the draw calls are built.
	 */
	virtual bool WantsFrameRefresh()const { return false; }
	/**
	 * Every frame the canvas sends its sections to the render thread, just before: write the segments' vertices and send
	 * them (UDreamUIMeshComponent::UpdateDirectMeshSectionGeometry). True when a segment's box changed, so that the mesh's
	 * bounds follow. Game thread.
	 */
	virtual bool RefreshMeshSegments() { return false; }
	/** The section the canvas last supplied segment InSegment with, while it is the current canvas's; null otherwise. */
	TSharedPtr<FDreamUIRenderSection_DirectMesh> GetMeshSegment(int32 InSegment)const;
	/** The mark the widget property record carries for this element (UDreamVisualBatchMesh::GetFontMark_WidgetPropertyDataForMaterial). */
	virtual uint8 GetWidgetPropertyMark()const { return 0; }
protected:
	virtual void OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas)override;
	uint8 bLocalVertexPositionChanged : 1;
	TWeakObjectPtr<UDreamUIMeshComponent> Mesh;
	TWeakPtr<FDreamUIRenderSection_DirectMesh> MeshSection;
	/** Each segment's section, by segment: what OnSupplyMeshSegment was last given. */
	TArray<TWeakPtr<FDreamUIRenderSection_DirectMesh>> MeshSegments;
};
