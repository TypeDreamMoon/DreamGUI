// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamMeshModifierBase.h"
#include "DreamMeshModifierLongShadow.generated.h"


UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName="LongShadow", meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamMeshModifierLongShadow : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:	
	UDreamMeshModifierLongShadow();

protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FColor ShadowColor = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FVector3f ShadowSize = FVector3f(0, 1, -1);
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		uint8 ShadowSegment = 5;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bUseGradientColor = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FColor GradientColor = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bMultiplySourceAlpha = true;
	FORCEINLINE void ApplyColorAndAlpha(FColor& InOutColor, FColor InTintColor, uint8 InOriginAlpha);
	/** Set once the segment count has been reported as beyond what the index buffer can address, so a
	 * rebuild every frame does not repeat the message. */
	bool bLoggedVertexLimitWarning = false;
public:
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	)override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetShadowColor()const { return ShadowColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector3f GetShadowSize()const { return ShadowSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		uint8 GetShadowSegments()const { return ShadowSegment; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetUseGradientColor()const { return bUseGradientColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetGradientColor()const { return GradientColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetMultiplySourceAlpha()const { return bMultiplySourceAlpha; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetShadowColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetShadowSize(FVector3f Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetShadowSegment(uint8 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetUseGradientColor(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetGradientColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMultiplySourceAlpha(bool Value);
};
