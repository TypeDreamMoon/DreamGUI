// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamMeshModifierBase.h"
#include "DreamMeshModifierShadow.generated.h"


UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName="Shadow", meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamMeshModifierShadow : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:	
	UDreamMeshModifierShadow();

protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FColor ShadowColor = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bMultiplySourceAlpha = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FVector3f ShadowOffset = FVector3f(0, 1, -1);
public:
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	)override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetShadowColor()const { return ShadowColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector3f GetShadowOffset()const { return ShadowOffset; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetShadowColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetShadowOffset(FVector3f Value);
};
