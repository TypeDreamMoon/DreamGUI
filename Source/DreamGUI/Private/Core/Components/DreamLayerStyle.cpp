// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStylePreset.h"
#include "Core/DreamUILayerStyleMath.h"

void UDreamLayerStylePreset::SetStyle(const FDreamLayerEffects& InStyle)
{
	Style = InStyle;
	StyleChangedEvent.Broadcast();
}

#if WITH_EDITOR
void UDreamLayerStylePreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	StyleChangedEvent.Broadcast();
}

void UDreamLayerStylePreset::PostEditUndo()
{
	Super::PostEditUndo();
	StyleChangedEvent.Broadcast();
}
#endif

UDreamLayerStyle::UDreamLayerStyle()
{
	bCanExecuteTick = false;
	DeclareTickUnused(UDreamLayerStyle::StaticClass());
	DeclareTransformChangedUnused(UDreamLayerStyle::StaticClass());
}

const FDreamLayerEffects& UDreamLayerStyle::GetEffectiveStyle() const
{
	return Preset != nullptr ? Preset->GetStyle() : Style;
}

void UDreamLayerStyle::SetStyle(const FDreamLayerEffects& InStyle)
{
	if (Style != InStyle)
	{
		Style = InStyle;
		NoteChanged();
	}
}

void UDreamLayerStyle::SetPreset(UDreamLayerStylePreset* InPreset)
{
	if (Preset != InPreset)
	{
		UnbindPreset();
		Preset = InPreset;
		BindPreset();
		NoteChanged();
	}
}

void UDreamLayerStyle::DetachFromPreset()
{
	if (Preset != nullptr)
	{
		Style = Preset->GetStyle();
		SetPreset(nullptr);
	}
}

bool UDreamLayerStyle::StylesSubtree() const
{
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return false;
	}
	// A widget that draws nothing of its own is the layer its subtree makes: one with no visual, one whose visual only takes
	// clicks, one whose visual is an effect on its own children (the style is round what the effect made of them). So is a
	// layer group drawn apart.
	const UDreamVisual* Visual = Widget->GetVisual();
	const UDreamLayerGroup* Group = Widget->GetLayerGroup();
	return Visual == nullptr || Visual->IsA<UDreamVisualEmpty>() || Visual->DrawsChildrenThroughEffect() || (Group != nullptr && Group->IsIsolated());
}

EDreamLayerStylePath UDreamLayerStyle::GetPath() const
{
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr || GetRecord().IsEmpty())
	{
		return EDreamLayerStylePath::None;
	}
	// Round a shape blend group's melted shape, in the pixels of the member that draws it.
	if (const UDreamShapeBlendGroup* Blend = Widget->GetComponent<UDreamShapeBlendGroup>(); Blend != nullptr && Blend->IsActive() && !GetRecord().UsesPatterns())
	{
		return EDreamLayerStylePath::Element;
	}
	if (StylesSubtree())
	{
		return EDreamLayerStylePath::Group;
	}
	// A rect block's own shader measures its shape exactly, and draws every effect but the ones that read a pattern.
	if (Cast<UDreamRectBlock>(Widget->GetVisual()) != nullptr && !GetRecord().UsesPatterns())
	{
		return EDreamLayerStylePath::Element;
	}
	return EDreamLayerStylePath::Group;
}

const FDreamUILayerStyleRecord& UDreamLayerStyle::GetRecord() const
{
	if (!bPacked)
	{
		UDreamLayerStyle* Self = const_cast<UDreamLayerStyle*>(this);
		Self->bPacked = true;
		if (FDreamUILayerStyleMath::Pack(GetEffectiveStyle(), Self->Record, PointLight.IsSet() ? &PointLight.GetValue() : nullptr))
		{
			FDreamUILayerStyleRecord Copy = Self->Record;
			Self->Composite = MakeShared<const FDreamUILayerStyleComposite, ESPMode::ThreadSafe>(MoveTemp(Copy));
		}
		else
		{
			Self->Composite.Reset();
		}
	}
	return Record;
}

FDreamUILayerStyleRef UDreamLayerStyle::GetComposite() const
{
	GetRecord();
	return Composite;
}

void UDreamLayerStyle::SetPointLight(const FDreamUILayerStyleLight* InLight)
{
	if (InLight == nullptr ? !PointLight.IsSet() : (PointLight.IsSet() && PointLight.GetValue() == *InLight))
	{
		return;
	}
	PointLight.Reset();
	if (InLight != nullptr)
	{
		PointLight = *InLight;
	}
	NoteChanged();
}

UDreamLayerStyle* UDreamLayerStyle::Find(const UDreamWidget* InWidget)
{
	if (InWidget == nullptr)
	{
		return nullptr;
	}
	UDreamLayerStyle* Found = InWidget->GetComponent<UDreamLayerStyle>();
	return Found != nullptr && Found->bIsRegisteredWithWidget ? Found : nullptr;
}

void UDreamLayerStyle::OnRegister()
{
	Super::OnRegister();
	BindPreset();
	NoteChanged();
}

void UDreamLayerStyle::OnUnregister()
{
	UnbindPreset();
	UDreamWidget* Widget = GetWidget();
	Super::OnUnregister();
	// No longer registered: whatever drew the style draws without it.
	if (Widget != nullptr)
	{
		if (UDreamRectBlock* Block = Cast<UDreamRectBlock>(Widget->GetVisual()))
		{
			Block->OnLayerStyleChanged();
		}
		if (const UDreamShapeBlendGroup* Blend = Widget->GetComponent<UDreamShapeBlendGroup>())
		{
			if (UDreamRectBlock* Leader = Blend->GetLeader())
			{
				Leader->OnLayerStyleChanged();
			}
		}
		if (UDreamCanvas* Canvas = Widget->GetRenderCanvas())
		{
			Canvas->MarkCanvasUpdate(LastPath == EDreamLayerStylePath::Group);
		}
	}
	LastPath = EDreamLayerStylePath::None;
}

#if WITH_EDITOR
void UDreamLayerStyle::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (BoundPreset.Get() != Preset)
	{
		UnbindPreset();
		BindPreset();
	}
	NoteChanged();
}

void UDreamLayerStyle::PostEditUndo()
{
	Super::PostEditUndo();
	UnbindPreset();
	BindPreset();
	NoteChanged();
}
#endif

void UDreamLayerStyle::NoteChanged()
{
	bPacked = false;
	GetRecord();
	UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr || !bIsRegisteredWithWidget)
	{
		return;
	}
	if (UDreamRectBlock* Block = Cast<UDreamRectBlock>(Widget->GetVisual()))
	{
		Block->OnLayerStyleChanged();
	}
	if (const UDreamShapeBlendGroup* Blend = Widget->GetComponent<UDreamShapeBlendGroup>())
	{
		if (UDreamRectBlock* Leader = Blend->GetLeader())
		{
			Leader->OnLayerStyleChanged();
		}
	}
	const EDreamLayerStylePath Path = GetPath();
	// A group's style is read every frame; only starting or stopping to draw the widget apart makes the draw calls again.
	const bool bRebuild = Path != LastPath && (Path == EDreamLayerStylePath::Group || LastPath == EDreamLayerStylePath::Group);
	LastPath = Path;
	if (UDreamCanvas* Canvas = Widget->GetRenderCanvas())
	{
		Canvas->MarkCanvasUpdate(bRebuild);
	}
}

void UDreamLayerStyle::BindPreset()
{
	if (Preset != nullptr && BoundPreset.Get() != Preset)
	{
		BoundPreset = Preset;
		PresetChangedHandle = Preset->OnStyleChanged().AddUObject(this, &UDreamLayerStyle::NoteChanged);
	}
}

void UDreamLayerStyle::UnbindPreset()
{
	if (UDreamLayerStylePreset* Bound = BoundPreset.Get())
	{
		Bound->OnStyleChanged().Remove(PresetChangedHandle);
	}
	BoundPreset.Reset();
	PresetChangedHandle.Reset();
}
