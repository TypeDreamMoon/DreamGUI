// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipData.h"
#include "DreamUIRender/DreamUICompositor.h"

UDreamLayerGroup::UDreamLayerGroup()
{
	bCanExecuteTick = false;
}

void UDreamLayerGroup::SetIsolation(EDreamLayerGroupIsolation Value)
{
	if (Isolation != Value)
	{
		Isolation = Value;
		NoteChanged();
	}
}

void UDreamLayerGroup::SetGroupOpacity(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (GroupOpacity != Value)
	{
		GroupOpacity = Value;
		NoteChanged();
	}
}

void UDreamLayerGroup::SetGroupBlendMode(EDreamUIBlendMode Value)
{
	if (GroupBlendMode != Value)
	{
		GroupBlendMode = Value;
		NoteChanged();
	}
}

void UDreamLayerGroup::SetGroupMask(const FDreamClipMask& Value)
{
	GroupMask = Value;
	NoteChanged();
}

bool UDreamLayerGroup::IsIsolated() const
{
	return Isolation == EDreamLayerGroupIsolation::Isolated || GroupOpacity < 1.0f || GroupBlendMode != EDreamUIBlendMode::Alpha || GroupMask.IsActive();
}

void UDreamLayerGroup::FillComposite(FDreamUICompositeDesc& InOutDesc, UDreamCanvas* InCanvas)
{
	InOutDesc.BlendMode = GroupBlendMode;
	InOutDesc.Opacity = GroupOpacity;
	InOutDesc.MaskClipCoordinate = 0.0f;
	InOutDesc.Textures = FDreamUIBuiltInDrawParams();
	if (const FDreamUIClipData* Clip = InCanvas != nullptr ? GetMaskClip(InCanvas) : nullptr)
	{
		InOutDesc.MaskClipCoordinate = static_cast<float>(Clip->GetBufferStartPos());
		InOutDesc.Textures.ClipDataTexture = InCanvas->GetClipDataTexture();
		FDreamUIMaskTextures Textures;
		Clip->GetMaskTextures(Textures);
		InOutDesc.Textures.MaskTexture0 = Textures.Slots[0].Get();
		InOutDesc.Textures.MaskTexture1 = Textures.Slots[1].Get();
	}
}

void UDreamLayerGroup::OnRegister()
{
	Super::OnRegister();
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->SetLayerGroup(this);
		bWasIsolated = IsIsolated();
		if (UDreamCanvas* Canvas = Widget->GetRenderCanvas())
		{
			Canvas->MarkCanvasUpdate(true);
		}
	}
}

void UDreamLayerGroup::OnUnregister()
{
	ReleaseMaskClip();
	if (UDreamWidget* Widget = GetWidget())
	{
		if (Widget->GetLayerGroup() == this)
		{
			Widget->SetLayerGroup(nullptr);
		}
		if (UDreamCanvas* Canvas = Widget->GetRenderCanvas())
		{
			Canvas->MarkCanvasUpdate(true);
		}
	}
	Super::OnUnregister();
}

#if WITH_EDITOR
void UDreamLayerGroup::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	GroupOpacity = FMath::Clamp(GroupOpacity, 0.0f, 1.0f);
	NoteChanged();
}
#endif

void UDreamLayerGroup::NoteChanged()
{
	if (!GroupMask.IsActive())
	{
		ReleaseMaskClip();
	}
	UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr || Widget->GetLayerGroup() != this)
	{
		return;
	}
	const bool bIsolated = IsIsolated();
	const bool bRebuild = bIsolated != bWasIsolated;
	bWasIsolated = bIsolated;
	if (UDreamCanvas* Canvas = Widget->GetRenderCanvas())
	{
		// The composite is read again every frame; only starting or stopping to draw apart makes the draw calls again.
		Canvas->MarkCanvasUpdate(bRebuild);
	}
}

const FDreamUIClipData* UDreamLayerGroup::GetMaskClip(UDreamCanvas* InCanvas)
{
	if (!GroupMask.IsActive())
	{
		ReleaseMaskClip();
		return nullptr;
	}
	UDreamCanvas* Root = InCanvas->GetRootCanvas();
	UDreamWidget* Widget = GetWidget();
	if (Root == nullptr || Widget == nullptr)
	{
		return nullptr;
	}
	if (!MaskClip.IsValid() || MaskClipCanvas.Get() != Root)
	{
		ReleaseMaskClip();
		MaskClip = Root->MakeGroupMaskClipData(Widget);
		MaskClipCanvas = Root;
		if (MaskClip.IsValid())
		{
			// Written now rather than at the next refresh, so the first frame the group is drawn reads its mask.
			MaskClip->UpdateData();
		}
	}
	return MaskClip.Get();
}

void UDreamLayerGroup::ReleaseMaskClip()
{
	if (MaskClip.IsValid())
	{
		if (UDreamCanvas* Canvas = MaskClipCanvas.Get())
		{
			Canvas->RemoveClipData(MaskClip);
		}
	}
	MaskClip.Reset();
	MaskClipCanvas.Reset();
}
