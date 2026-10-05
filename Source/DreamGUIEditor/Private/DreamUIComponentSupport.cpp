// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIComponentSupport.h"

#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetNavigation.h"
#include "Extensions/DreamRetainerBox.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UITextHyperlink.h"
#include "MeshModifier/DreamMeshModifierBase.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "UMG/DreamUMGWidget.h"
#include "UMG/DreamUMGWidgetInteraction.h"

#define LOCTEXT_NAMESPACE "DreamUIComponentSupport"

FDreamUIComponentSupport& FDreamUIComponentSupport::Get()
{
	static FDreamUIComponentSupport Instance;
	return Instance;
}

FDreamUIComponentSupport::FDreamUIComponentSupport()
{
	RegisterDefaults();
}

void FDreamUIComponentSupport::AddRule(const UClass* InComponentClass, FRule InRule)
{
	if (InComponentClass != nullptr && InRule)
	{
		Entries.Add({ InComponentClass, MoveTemp(InRule) });
	}
}

void FDreamUIComponentSupport::RequireVisual(const UClass* InComponentClass, const UClass* InVisualClass, const FText& InVisualLabel)
{
	const TWeakObjectPtr<const UClass> VisualClass = InVisualClass;
	const FText Reason = FText::Format(LOCTEXT("NeedsVisual", "Needs {0} on this widget."), InVisualLabel);
	AddRule(InComponentClass, [VisualClass, Reason](const UDreamWidget* InWidget, FText& OutReason)
	{
		const UDreamVisual* Visual = InWidget->GetVisual();
		if (IsValid(Visual) && VisualClass.IsValid() && Visual->IsA(VisualClass.Get()))
		{
			return true;
		}
		OutReason = Reason;
		return false;
	});
}

void FDreamUIComponentSupport::RequireComponent(const UClass* InComponentClass, const UClass* InSiblingClass)
{
	if (InSiblingClass == nullptr)
	{
		return;
	}
	const TWeakObjectPtr<const UClass> SiblingClass = InSiblingClass;
	const FText Reason = FText::Format(LOCTEXT("NeedsComponent", "Needs a {0} on this widget first."), InSiblingClass->GetDisplayNameText());
	AddRule(InComponentClass, [SiblingClass, Reason](const UDreamWidget* InWidget, FText& OutReason)
	{
		for (const UDreamUIBehaviour* Component : InWidget->GetAllComponents())
		{
			if (IsValid(Component) && SiblingClass.IsValid() && Component->IsA(SiblingClass.Get()))
			{
				return true;
			}
		}
		OutReason = Reason;
		return false;
	});
}

void FDreamUIComponentSupport::OnePerWidget(const UClass* InComponentClass, const UClass* InFamilyClass)
{
	const UClass* Family = InFamilyClass != nullptr ? InFamilyClass : InComponentClass;
	if (Family == nullptr)
	{
		return;
	}
	const TWeakObjectPtr<const UClass> FamilyClass = Family;
	const FText Reason = FText::Format(LOCTEXT("OnePerWidget", "This widget already has a {0}."), Family->GetDisplayNameText());
	AddRule(InComponentClass, [FamilyClass, Reason](const UDreamWidget* InWidget, FText& OutReason)
	{
		for (const UDreamUIBehaviour* Component : InWidget->GetAllComponents())
		{
			if (IsValid(Component) && FamilyClass.IsValid() && Component->IsA(FamilyClass.Get()))
			{
				OutReason = Reason;
				return false;
			}
		}
		return true;
	});
}

void FDreamUIComponentSupport::RemoveRules(const UClass* InComponentClass)
{
	Entries.RemoveAll([InComponentClass](const FEntry& Entry) { return Entry.Class.Get() == InComponentClass; });
}

bool FDreamUIComponentSupport::IsSupported(const UClass* InComponentClass, const UDreamWidget* InWidget, FText* OutReason) const
{
	if (InComponentClass == nullptr)
	{
		return false;
	}
	return IsSupported([InComponentClass](const UClass* InBase) { return InComponentClass->IsChildOf(InBase); }, InWidget, OutReason);
}

bool FDreamUIComponentSupport::IsSupported(TFunctionRef<bool(const UClass*)> InIsChildOf, const UDreamWidget* InWidget, FText* OutReason) const
{
	if (!IsValid(InWidget))
	{
		return true;
	}
	for (const FEntry& Entry : Entries)
	{
		const UClass* RuleClass = Entry.Class.Get();
		if (RuleClass == nullptr || !InIsChildOf(RuleClass))
		{
			continue;
		}
		FText Reason;
		if (!Entry.Rule(InWidget, Reason))
		{
			if (OutReason != nullptr)
			{
				*OutReason = Reason;
			}
			return false;
		}
	}
	return true;
}

void FDreamUIComponentSupport::RegisterDefaults()
{
	// WHAT EACH COMPONENT READS OFF ITS WIDGET AND DOES NOTHING WITHOUT.
	//
	// Every one of these was found in the component's own code -- a Cast of the visual that returns early, an error
	// logged when it first plays -- and none of them stopped the component from being added: it sat on the widget,
	// inert, until someone wondered why.
	RequireVisual(UDreamMeshModifierBase::StaticClass(), UDreamVisualBatchMesh::StaticClass(),
		LOCTEXT("MeshVisual", "a visual that builds a mesh (Text, Image, Sprite, Texture, Rect Block...)"));
	RequireVisual(UDreamMeshModifierTextAnimation::StaticClass(), UDreamText::StaticClass(), LOCTEXT("TextVisual", "a Text visual"));
	RequireVisual(UUITextHyperlink::StaticClass(), UDreamText::StaticClass(), LOCTEXT("TextVisual", "a Text visual"));
	RequireVisual(UUISpriteSequencePlayer::StaticClass(), UDreamSprite::StaticClass(), LOCTEXT("SpriteVisual", "a Sprite visual"));
	RequireVisual(UUISpriteSheetTexturePlayer::StaticClass(), UDreamTexture::StaticClass(), LOCTEXT("TextureVisual", "a Texture visual"));
	RequireVisual(UDreamUMGWidgetInteraction::StaticClass(), UDreamUMGWidget::StaticClass(), LOCTEXT("UMGWidgetVisual", "a UMG Widget visual"));

	// Both configure the canvas on their own widget rather than making one (DreamRetainerBox.h says so).
	RequireComponent(UDreamRetainerBox::StaticClass(), UDreamCanvas::StaticClass());
	RequireComponent(UDreamInvalidationBox::StaticClass(), UDreamCanvas::StaticClass());

	// ONE PER WIDGET: everything that reads these finds the first with GetComponent, so a second is never read.
	OnePerWidget(UDreamCanvas::StaticClass());
	OnePerWidget(UDreamWidgetNavigation::StaticClass());
	OnePerWidget(UDreamWidgetAnimationComponent::StaticClass());
	OnePerWidget(UDreamNamedSlot::StaticClass());
	OnePerWidget(UDreamContentWidget::StaticClass());
	OnePerWidget(UDreamRetainerBox::StaticClass());
	OnePerWidget(UDreamInvalidationBox::StaticClass());
	// And by family: a widget is one kind of selectable -- a button or a slider, not both -- and one scroll view.
	OnePerWidget(UUISelectable::StaticClass(), UUISelectable::StaticClass());
	OnePerWidget(UUIScrollView::StaticClass(), UUIScrollView::StaticClass());

	// A content widget is a slot for one child. On a widget that already holds several it would refuse them all but
	// the first the next time the hierarchy is arranged.
	AddRule(UDreamContentWidget::StaticClass(), [](const UDreamWidget* InWidget, FText& OutReason)
	{
		if (InWidget->GetChildrenCount() <= 1)
		{
			return true;
		}
		OutReason = FText::Format(LOCTEXT("ContentWidgetChildren", "Holds one child, and this widget has {0}."), InWidget->GetChildrenCount());
		return false;
	});
}

#undef LOCTEXT_NAMESPACE
