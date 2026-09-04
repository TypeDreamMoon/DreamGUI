// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamMeshModifierBase.h"
#include "DreamMeshModifierOutline.generated.h"


UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName="Outline", meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamMeshModifierOutline : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:	
	UDreamMeshModifierOutline();

protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FColor OutlineColor = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FVector2f OutlineSize = FVector2f(1, 1);
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bMultiplySourceAlpha = true;
	/** Default is 4 direction. 8 direction will get nicer look. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (DisplayName = "Use 8 Direction"))
		bool bUse8Direction = false;
	FORCEINLINE void ApplyColorAndAlpha(FColor& InOutColor, uint8 InSourceAlpha);
	/** Set once the direction count has been reported as beyond what the index buffer can address, so a
	 * rebuild every frame does not repeat the message. */
	bool bLoggedVertexLimitWarning = false;
public:
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	)override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetOutlineColor()const { return OutlineColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector2f GetOutlineSize()const { return OutlineSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetUse8Direction()const { return bUse8Direction; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetMultiplySourceAlpha()const { return bMultiplySourceAlpha; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOutlineColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOutlineSize(FVector2f Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetUse8Direction(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMultiplySourceAlpha(bool Value);
};
