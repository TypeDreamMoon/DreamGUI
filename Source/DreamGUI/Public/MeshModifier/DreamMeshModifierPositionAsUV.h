// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamMeshModifierBase.h"
#include "DreamMeshModifierPositionAsUV.generated.h"


UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName="PositionAsUV", meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamMeshModifierPositionAsUV : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:	
	UDreamMeshModifierPositionAsUV();

protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta=(UIMin=0, UIMax=3))
	uint8 UVChannel = 1;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	FVector2f Scale = FVector2f::One();
public:
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	)override;
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)override
	{
		OutTriangleIndices = false;
		OutVertexPosition = false;
		OutUV = false;
		OutColor = false;
	};
};
