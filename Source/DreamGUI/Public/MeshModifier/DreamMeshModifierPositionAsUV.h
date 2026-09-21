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
	/**
	 * Which texture coordinate the vertex position is written into. Not every channel is free. The
	 * canvas owns UV1 -- X carries the widget property data coordinate and Y the texture array slice
	 * that font rendering looks glyphs up by -- and it stamps X back over this modifier's work later
	 * in the same rebuild, so a position written there loses half of itself and breaks glyph lookup
	 * with the other half. UV2 and UV3 are claimed per visual rather than globally: DreamText uses
	 * both and DreamRectBlock uses UV3, while a plain image leaves them alone. Only UV0 is
	 * unconditionally the caller's, and it is the texture coordinate, so writing it replaces the
	 * image. Pick the channel against the visual this sits on.
	 *
	 * UV1 is refused outright -- by the setter and by the write -- rather than merely discouraged,
	 * and the default is UV0 for the same reason. The canvas-owned channel used to be the default,
	 * which meant the out-of-the-box configuration of this modifier corrupted glyph lookup.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta=(UIMin=0, UIMax=3))
	uint8 UVChannel = 0;
	/** The texture coordinate the canvas writes into during the rebuild; see UVChannel. */
	static constexpr uint8 CanvasOwnedUVChannel = 1;
	/** Transient, and not a property: one complaint per modifier is enough for a per-rebuild path. */
	bool bHasWarnedAboutCanvasChannel = false;
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
		// The batch mesh turns these four answers into the dirty flags it hands the emitter before it
		// runs the modifier list, so a modifier that writes a UV while declaring none can be handed a
		// vertex buffer that was cleared and then refilled with the UV pass skipped. Over-declaring
		// only costs a recompute; under-declaring loses the write.
		OutUV = true;
		OutColor = false;
	};

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		uint8 GetUVChannel()const { return UVChannel; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector2f GetScale()const { return Scale; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetUVChannel(uint8 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetScale(FVector2f Value);
};
