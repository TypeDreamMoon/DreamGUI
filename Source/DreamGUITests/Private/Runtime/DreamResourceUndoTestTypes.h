// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamText.h"
#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "DreamResourceUndoTestTypes.generated.h"

/** Counts the repaint requests made by the previewer's actual render-target listener. */
UCLASS()
class UDreamRenderTargetPreviewerUndoProbe : public UDreamCanvasRenderTargetPreviewer
{
	GENERATED_BODY()

public:
	int32 TextureDirtyCalls = 0;

	virtual void MarkTextureDirty() override
	{
		++TextureDirtyCalls;
		Super::MarkTextureDirty();
	}

	void CheckResourceBindingBeforeGeometry()
	{
		OnBeforeCreateOrUpdateGeometry();
	}
};

/** Counts the geometry invalidations made by a text's actual rich-text resource listeners. */
UCLASS()
class UDreamRichTextResourceUndoProbe : public UDreamText
{
	GENERATED_BODY()

public:
	int32 VertexDirtyCalls = 0;

	virtual void MarkVerticesDirty(bool InTriangleDirty, bool InVertexPositionDirty, bool InVertexUVDirty, bool InVertexColorDirty) override
	{
		++VertexDirtyCalls;
		Super::MarkVerticesDirty(InTriangleDirty, InVertexPositionDirty, InVertexUVDirty, InVertexColorDirty);
	}

	void CheckResourceBindingBeforeGeometry()
	{
		OnBeforeCreateOrUpdateGeometry();
	}
};
