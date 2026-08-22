// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamVisual.h"
#include "DreamVisualBatchMesh.generated.h"

class UDreamMeshModifierBase;
class FDreamUIGeometry;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIGeometryVertex
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		FVector position = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		FColor color = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		FVector2D uv0 = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", AdvancedDisplay)
		FVector2D uv1 = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", AdvancedDisplay)
		FVector2D uv2 = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", AdvancedDisplay)
		FVector2D uv3 = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", AdvancedDisplay)
		FVector normal = FVector(-1, 0, 0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", AdvancedDisplay)
		FVector tangent = FVector(0, 1, 0);
};
/** a helper class for make DreamGUI geometry */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIGeometryHelper : public UObject
{
	GENERATED_BODY()
public:
	FDreamUIGeometry* UIGeo = nullptr;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void AddVertexSimple(FVector position, FColor color, FVector2D uv0);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void AddVertexFull(FVector position, FColor color, FVector2D uv0, FVector2D uv1, FVector2D uv2, FVector2D uv3, FVector normal, FVector tangent);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void AddVertexStruct(FDreamUIGeometryVertex vertex);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void AddTriangle(int index0, int index1, int index2);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMesh(const TArray<FDreamUIGeometryVertex>& InVertices, const TArray<int>& InIndices);

	/**
	 * Remove vertices and triangle indices data, left the geometry empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void Clear();

	/**
	 * Add a list of triangles.
	 * @param	InVertexTriangleStream	Vertices to add, length should be divisible by 3.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void AddVertexTriangleStream(const TArray<FDreamUIGeometryVertex>& InVertexTriangleStream);
	/**
	 * Get a stream of vertex in triangles.
	 * @param	OutVertexTriangleStream		Vertices stream, length is divisible by 3.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void GetVertexTriangleStream(TArray<FDreamUIGeometryVertex>& OutVertexTriangleStream);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		static FVector2D CalculatePivotOffset(float InWidth, float InHeight, const FVector2D& InPivot);
};

/** Widget's properties which can access in material */
UENUM(BlueprintType, meta = (Bitflags), Category = DreamGUI)
enum class EDreamVisualPropertiesForMaterial :uint8
{
	//width & height
	Size,
	//widget rect center position in canvas space
	CenterPosition,
};
ENUM_CLASS_FLAGS(EDreamVisualPropertiesForMaterial);

/** UI element which have render geometry, and can be batched and renderred by DreamGUICanvas */
UCLASS(Abstract, Blueprintable, ClassGroup=(DreamGUI))
class DREAMGUI_API UDreamVisualBatchMesh : public UDreamVisual
{
	GENERATED_BODY()

public:	
	UDreamVisualBatchMesh(const FObjectInitializer& ObjectInitializer);

protected:
	friend class FDreamVisualBatchMeshCustomization;
	virtual void BeginPlay() override;
	virtual void EndPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	TSharedPtr<FDreamUIGeometry> UIGeometry = nullptr;
	
	/** Will any geometry modifier change these data? */
	void GeometryModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor);
	/** 
	 * use GeometryModifier to modify geometry 
	 */
	void ApplyGeometryModifier(bool triangleChanged, bool uvChanged, bool colorChanged, bool vertexPositionChanged);

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;
public:
	void MarkVertexPositionDirty();
	void MarkVertexUVDirty();
	void MarkCanvasUpdate();
	virtual void MarkTextureDirty();
	virtual void MarkMaterialDirty();
	virtual void MarkVerticesDirty(bool InTriangleDirty, bool InVertexPositionDirty, bool InVertexUVDirty, bool InVertexColorDirty);

	/** 
	 * Mark vertices dirty, then DreamGUI will trigger UpdateGeometry process, and OnUpdateGeometry will execute in next render update.
	 * Call this if you want to update vertex data. 
	 * For blueprint easily use.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void MarkVerticesDirty();

	virtual void MarkAllDirty()override;
	FDreamUIGeometry* GetGeometry()const { return UIGeometry.Get(); }
	UDreamMeshModifierBase* AddMeshModifier(TSubclassOf<UDreamMeshModifierBase> ModifierClass);

	virtual bool LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const override;
	/** is this UI element type support draw-call batching? */
	virtual bool SupportDrawCallBatching()const { return true; }

	FORCEINLINE bool GetRequirePropertiesForMaterial_Size()const{ return PropertiesForMaterial & (1 << (int)EDreamVisualPropertiesForMaterial::Size); }
	FORCEINLINE bool GetRequirePropertiesForMaterial_CenterPosition()const{ return PropertiesForMaterial & (1 << (int)EDreamVisualPropertiesForMaterial::CenterPosition); }

	void AddMeshModifier(UDreamMeshModifierBase* InModifier);
	void RemoveMeshModifier(UDreamMeshModifierBase* InModifier);
	void MarkMeshModifierOrderChanged();
protected:
	virtual bool LineTraceVisiblePixel(float InAlphaThreshold, FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const;
	virtual bool ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const { return false; }
protected:
	friend class FDreamVisualBatchMeshCustomization;

	/** enable properties for material */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamVisualPropertiesForMaterial"))
	int8 PropertiesForMaterial = 0;
	TArray<TWeakObjectPtr<UDreamMeshModifierBase>> MeshModifierArray;

	/** texture for render this UI element */
	virtual UTexture* GetTextureToCreateGeometry();
	/** material to render this UI element. if CustomUIMaterial is not valid, then use this material. */
	virtual UMaterialInterface* GetMaterialToCreateGeometry();

	/** do anything before actually create or update geometry */
	virtual void OnBeforeCreateOrUpdateGeometry();
	/** fill and update ui geometry */
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged);
	virtual uint8 GetFontMark_WidgetPropertyDataForMaterial(){return 0;}
	/** Write anything this visual keeps in its widget property record beyond the common pixels; runs whenever the record is (re)written. */
	virtual void FillWidgetPropertyDataForMaterial_Extra(class UDreamUIDataAsTexture* DataAsTexture) {}
	virtual void OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas) override;
	/** return true means any data dirty, then update geometry (go OnUpdateGeometry), otherwise return false. */
	virtual bool GetAnythingDirty()const;

	virtual void UpdateGeometry()override final;
	virtual void GetGeometryBoundsInLocalSpace(FVector2D& OutMinPoint, FVector2D& OutMaxPoint)const override;
	virtual void GetGeometryBounds3DInLocalSpace(FVector& OutMinPoint, FVector& OutMaxPoint)const override;

	/** texture for render this UI element */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "GetTextureToCreateGeometry"))
		UTexture* ReceiveGetTextureToCreateGeometry();
	/** material to render this UI element. if CustomUIMaterial is not valid, then use this material. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "GetMaterialToCreateGeometry"))
		UMaterialInterface* ReceiveGetMaterialToCreateGeometry();
	/** do anything before actually create or update geometry */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "OnBeforeCreateOrUpdateGeometry"))
		void ReceiveOnBeforeCreateOrUpdateGeometry();
	/** fill and update ui geometry */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "OnUpdateGeometry"))
		void ReceiveOnUpdateGeometry(UDreamUIGeometryHelper* InGeometryHelper, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged);

private:
	/** local space vertex position changed */
	uint8 bLocalVertexPositionChanged : 1;
	/** vertex's uv change */
	uint8 bUVChanged:1;
	/** triangle index change */
	uint8 bTriangleChanged:1;
	uint8 bTextureChanged:1;
	uint8 bMaterialChanged:1;
	uint8 bMeshModifierOrderChanged:1;
	FVector LocalMinPoint3D = FVector::ZeroVector, LocalMaxPoint3D = FVector::ZeroVector;
	void CalculateLocalBounds();
	UPROPERTY(Transient)TObjectPtr<UDreamUIGeometryHelper> GeometryHelper = nullptr;
};
