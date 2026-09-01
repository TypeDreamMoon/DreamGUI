// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/Components/DreamVisualDirectMesh.h"
#include "DreamStaticMesh.generated.h"

USTRUCT(BlueprintType)
struct FDreamUIStaticMeshVertex
{
	GENERATED_BODY()

	FDreamUIStaticMeshVertex()
		:
		Position(FVector::ZeroVector)
		, TangentX(FVector::RightVector)
		, TangentZ(FVector::UpVector)
		, Color(ForceInitToZero)
		, UV0(FVector2D::ZeroVector)
		, UV1(FVector2D::ZeroVector)
		, UV2(FVector2D::ZeroVector)
		, UV3(FVector2D::ZeroVector)
	{
	}

	FDreamUIStaticMeshVertex(
		FVector InPos
		, FVector InTangentX
		, FVector InTangentZ
		, FColor InColor
		, FVector2D InUV0
		, FVector2D InUV1
		, FVector2D InUV2
		, FVector2D InUV3
	)
		: Position(InPos)
		, TangentX(InTangentX)
		, TangentZ(InTangentZ)
		, Color(InColor)
		, UV0(InUV0)
		, UV1(InUV1)
		, UV2(InUV2)
		, UV3(InUV3)
	{
	}

	UPROPERTY()
		FVector Position;
	UPROPERTY()
		FVector TangentX;
	UPROPERTY()
		FVector TangentZ;
	UPROPERTY()
		FColor Color;
	UPROPERTY()
		FVector2D UV0;
	UPROPERTY()
		FVector2D UV1;
	UPROPERTY()
		FVector2D UV2;
	UPROPERTY()
		FVector2D UV3;
};

/** Cache StaticMesh for use in DreamUI's DreamStaticMesh. Since we cannot read StaticMesh data in runtime, we must create this object and assign 'MeshAsset' property in editor. */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIStaticMeshCacheData : public UObject
{
	GENERATED_BODY()

public:
	/** Access the slate vertexes. */
	const TArray<FDreamUIStaticMeshVertex>& GetVertexData() const;

	/** Access the indexes for the order in which to draw the vertexes. */
	const TArray<uint32>& GetIndexData() const;

	/** Material to be used with the specified vector art data. */
	UMaterialInterface* GetMaterial() const;

	const FBox& GetMeshBounds() const { return MeshBounds; }

	/** Convert the static mesh data into slate vector art on demand. Does nothing in a cooked build. */
	void EnsureValidData();

#if WITH_EDITORONLY_DATA
	DECLARE_EVENT(UDreamUIStaticMeshCacheData, FDreamUIStaticMeshDataChangeEvent);
	FDreamUIStaticMeshDataChangeEvent OnMeshDataChange;
#endif
private:
	// ~ UObject Interface
	virtual void PreSave(class FObjectPreSaveContext SaveContext) override;
	// ~ UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
	/** Does the actual work of converting mesh data into slate vector art */
	void InitFromStaticMesh(const UStaticMesh* InSourceMesh);
	void ClearMeshData();
#endif

#if WITH_EDITORONLY_DATA
	/** The mesh data asset from which the vector art is sourced */
	UPROPERTY(EditAnywhere, Category = "Vector Art")
		TObjectPtr<UStaticMesh> MeshAsset;

	/** The material which we are using, or the material from with the MIC was constructed. */
	UPROPERTY(Transient)
		TObjectPtr<UMaterialInterface> SourceMaterial;
#endif

	/** @see GetVertexData() */
	UPROPERTY()
		TArray<FDreamUIStaticMeshVertex> VertexData;

	/** @see GetIndexData() */
	UPROPERTY()
		TArray<uint32> IndexData;

	/** @see GetMaterial() */
	UPROPERTY()
		TObjectPtr<UMaterialInterface> Material;
	
	UPROPERTY()
	FBox MeshBounds;
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamStaticMeshVertexColorType :uint8
{
	//Multiply mesh's vertex color with DreamGUI's color parameter
	MultiplyWithUIColor,
	//Replace mesh's vertex color by DreamGUI's color parameter
	ReplaceByUIColor,
	//Use mesh's vertex color only, not consider DreamGUI's color parameter
	NotAffectByUIColor,
};

/**
 * render a StaticMesh as UI element
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, Experimental, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamStaticMesh : public UDreamVisualDirectMesh
{
	GENERATED_BODY()

public:	
	UDreamStaticMesh(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UDreamUIStaticMeshCacheData> MeshCache;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamStaticMeshVertexColorType VertexColorType = EDreamStaticMeshVertexColorType::NotAffectByUIColor;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UMaterialInterface> ReplaceMaterial;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange)override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
	virtual void PostInitProperties() override;
	void OnStaticMeshDataChange();
#endif
#if WITH_EDITORONLY_DATA
	FDelegateHandle OnMeshDataChangeDelegateHandle;
#endif
	
	virtual void OnSupplyMeshSection(TWeakObjectPtr<UDreamUIMeshComponent> InMesh, TWeakPtr<FDreamUIRenderSection_DirectMesh> InSection)override;
	void CreateGeometry();
	virtual void UpdateGeometry()override;
	virtual bool HaveValidData()const override;
	virtual UMaterialInterface* GetMaterial()const override;

	/**
	 * The cached mesh bounds flattened onto the UI plane, or a negative pair when the cache has no
	 * bounds to give. Both measure functions read it; they differ only in which component they take.
	 */
	FVector2f MeasureMeshBounds()const;
public:
	virtual float GetPreferredWidth()const override;
	virtual float GetPreferredHeight()const override;

	/** return 'MeshCache' property */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") 
		UDreamUIStaticMeshCacheData* GetMeshCache()const { return MeshCache; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamStaticMeshVertexColorType GetVertexColorType()const { return VertexColorType; }
	/** Get the 'ReplaceMaterial' property */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		class UMaterialInterface* GetReplaceMaterial()const { return ReplaceMaterial; }
	/** Get actual rendering material. If 'ReplaceMaterial' is valid then return 'ReplaceMaterial', or return the mesh's default material. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		class UMaterialInterface* GetRenderMaterial()const { return GetMaterial(); }
	/** return current rendering DynamicMaterialInstance, or create one if not valid. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		class UMaterialInstanceDynamic* GetOrCreateDynamicMaterialInstance();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMesh(UDreamUIStaticMeshCacheData* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetVertexColorType(EDreamStaticMeshVertexColorType Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetReplaceMaterial(UMaterialInterface* Value);
};

