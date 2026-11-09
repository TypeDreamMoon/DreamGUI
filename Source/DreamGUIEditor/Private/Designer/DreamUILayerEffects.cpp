// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamUILayerEffects.h"

#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStylePreset.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamGUIEditorStyle.h"
#include "Extensions/DreamMaskTransition.h"
#include "Extensions/Effects/DreamHoloFoil.h"
#include "Extensions/Effects/DreamPointerLight.h"
#include "Extensions/Effects/DreamTiltCard.h"
#include "MeshModifier/DreamMeshModifierBase.h"
#include "MeshModifier/DreamMeshModifierJelly.h"
#include "MeshModifier/DreamMeshModifierTextOnPath.h"
#include "MeshModifier/DreamMeshModifierWarp.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamUILayerEffects"

namespace DreamUILayerEffectsLocal
{
	/** The classes of the plugin's own modules this one does not link against, by path. */
	UClass* FindScriptClass(const TCHAR* InPath)
	{
		return FindObject<UClass>(nullptr, InPath);
	}

	bool IsA(const UObject* InObject, const TCHAR* InClassPath)
	{
		const UClass* Class = InObject != nullptr ? FindScriptClass(InClassPath) : nullptr;
		return Class != nullptr && InObject->IsA(Class);
	}

	/** "Dream Layer Style" reads "Layer Style": every one of these classes carries the family's name. */
	FText NameOf(const UObject* InObject)
	{
		FString Name = InObject->GetClass()->GetDisplayNameText().ToString();
		Name.RemoveFromStart(TEXT("Dream "));
		return FText::FromString(Name);
	}

	FText BlendModeName(EDreamUIBlendMode InMode)
	{
		return StaticEnum<EDreamUIBlendMode>()->GetDisplayNameTextByValue(static_cast<int64>(InMode));
	}

	FText StyleDetail(const FDreamLayerEffects& InStyle)
	{
		TArray<FString> Parts;
		auto CountEnabled = [](const auto& InArray)
		{
			int32 Count = 0;
			for (const auto& Item : InArray)
			{
				Count += Item.bEnabled ? 1 : 0;
			}
			return Count;
		};
		auto AddMany = [&Parts](int32 InCount, const TCHAR* InName)
		{
			if (InCount == 1)
			{
				Parts.Add(InName);
			}
			else if (InCount > 1)
			{
				Parts.Add(FString::Printf(TEXT("%s x%d"), InName, InCount));
			}
		};
		AddMany(CountEnabled(InStyle.DropShadows), TEXT("Drop Shadow"));
		AddMany(InStyle.OuterGlow.bEnabled ? 1 : 0, TEXT("Outer Glow"));
		AddMany(CountEnabled(InStyle.InnerShadows), TEXT("Inner Shadow"));
		AddMany(InStyle.InnerGlow.bEnabled ? 1 : 0, TEXT("Inner Glow"));
		AddMany(InStyle.BevelEmboss.bEnabled ? 1 : 0, TEXT("Bevel & Emboss"));
		AddMany(InStyle.Satin.bEnabled ? 1 : 0, TEXT("Satin"));
		AddMany(CountEnabled(InStyle.ColorOverlays), TEXT("Color Overlay"));
		AddMany(CountEnabled(InStyle.GradientOverlays), TEXT("Gradient Overlay"));
		AddMany(InStyle.PatternOverlay.bEnabled ? 1 : 0, TEXT("Pattern Overlay"));
		AddMany(CountEnabled(InStyle.Strokes), TEXT("Stroke"));
		return Parts.Num() ? FText::FromString(FString::Join(Parts, TEXT(", "))) : LOCTEXT("StyleEmpty", "No effect turned on");
	}

	/** Each property along InPath, from the object's own down to the leaf; empty when one is missing. */
	TArray<FProperty*> ResolvePath(const UObject* InObject, const FString& InPath, FString* OutWhyNot)
	{
		TArray<FString> Parts;
		InPath.ParseIntoArray(Parts, TEXT("."));
		TArray<FProperty*> Chain;
		const UStruct* Scope = InObject != nullptr ? InObject->GetClass() : nullptr;
		for (const FString& Part : Parts)
		{
			FProperty* Property = Scope != nullptr ? FindFProperty<FProperty>(Scope, *Part) : nullptr;
			if (Property == nullptr)
			{
				if (OutWhyNot)
				{
					*OutWhyNot = FString::Printf(TEXT("%s has no %s"), Scope != nullptr ? *Scope->GetName() : TEXT("(nothing)"), *Part);
				}
				return {};
			}
			Chain.Add(Property);
			const FStructProperty* AsStruct = CastField<FStructProperty>(Property);
			Scope = AsStruct != nullptr ? AsStruct->Struct : nullptr;
		}
		return Chain;
	}
}

void DreamUILayerEffects::Collect(const UDreamWidget* InWidget, TArray<FDreamUILayerEffect>& OutEffects)
{
	using namespace DreamUILayerEffectsLocal;
	if (!IsValid(InWidget))
	{
		return;
	}
	auto Add = [&OutEffects](EDreamUILayerEffectKind InKind, const UObject* InObject, FText InName, FText InDetail, const TCHAR* InSwitch) -> FDreamUILayerEffect&
	{
		FDreamUILayerEffect& Effect = OutEffects.AddDefaulted_GetRef();
		Effect.Kind = InKind;
		Effect.Object = const_cast<UObject*>(InObject);
		Effect.Name = MoveTemp(InName);
		Effect.Detail = MoveTemp(InDetail);
		Effect.SwitchPath = InSwitch;
		return Effect;
	};

	// What the widget itself carries.
	const EDreamWidgetClipping Clipping = InWidget->GetClipping();
	const bool bClipsToBounds = Clipping == EDreamWidgetClipping::ClipToBounds || Clipping == EDreamWidgetClipping::ClipToBoundsWithoutIntersecting;
	if (bClipsToBounds && InWidget->GetClipMask().IsActive())
	{
		Add(EDreamUILayerEffectKind::Mask, InWidget, LOCTEXT("Mask", "Mask"),
			FText::Format(LOCTEXT("MaskDetail", "{0} mask on its clip"), StaticEnum<EDreamClipMaskShape>()->GetDisplayNameTextByValue(static_cast<int64>(InWidget->GetClipMask().Shape))),
			TEXT(""));
	}
	if (InWidget->GetClipToBelow())
	{
		Add(EDreamUILayerEffectKind::Mask, InWidget, LOCTEXT("ClipToBelow", "Clip to Layer Below"),
			LOCTEXT("ClipToBelowDetail", "Shown only where the layer below is drawn"), TEXT(""));
	}

	// What its visual is.
	if (const UDreamVisual* Visual = InWidget->GetVisual())
	{
		if (const UDreamVisualBatchMesh* Element = Cast<UDreamVisualBatchMesh>(Visual); Element != nullptr && Element->GetBlendMode() != EDreamUIBlendMode::Alpha)
		{
			Add(EDreamUILayerEffectKind::Blend, Visual, FText::Format(LOCTEXT("BlendName", "Blend: {0}"), BlendModeName(Element->GetBlendMode())),
				LOCTEXT("BlendDetail", "How this element blends with what is drawn behind it"), TEXT(""));
		}
		if (Visual->IsA<UDreamVisualPostProcess>())
		{
			Add(EDreamUILayerEffectKind::Effect, Visual, NameOf(Visual), LOCTEXT("PostProcessDetail", "Changes what is drawn behind it"), TEXT("bEnabled"));
		}
		else if (IsA(Visual, TEXT("/Script/DreamGUINiagara.DreamNiagaraSystem")))
		{
			Add(EDreamUILayerEffectKind::Particles, Visual, NameOf(Visual), LOCTEXT("ParticlesDetail", "A Niagara system drawn on the canvas"), TEXT(""));
		}
	}

	// What hangs off it.
	for (const UDreamUIBehaviour* Component : InWidget->GetAllComponents())
	{
		if (!IsValid(Component))
		{
			continue;
		}
		if (const UDreamLayerStyle* Style = Cast<UDreamLayerStyle>(Component))
		{
			FDreamUILayerEffect& Effect = Add(EDreamUILayerEffectKind::Style, Style, LOCTEXT("LayerStyle", "Layer Style"), StyleDetail(Style->GetEffectiveStyle()), TEXT("Style.bEnabled"));
			if (const UDreamLayerStylePreset* Preset = Style->GetPreset())
			{
				Effect.SwitchUnavailable = FText::Format(LOCTEXT("StyleFollowsPreset", "Follows the preset {0}: turn its effects off on the preset, or detach the style from it."), FText::FromString(Preset->GetName()));
			}
		}
		else if (const UDreamLayerGroup* Group = Cast<UDreamLayerGroup>(Component))
		{
			Add(EDreamUILayerEffectKind::Group, Group, LOCTEXT("LayerGroup", "Layer Group"),
				FText::Format(LOCTEXT("GroupDetail", "{0}, {1}, opacity {2}"),
					StaticEnum<EDreamLayerGroupIsolation>()->GetDisplayNameTextByValue(static_cast<int64>(Group->GetIsolation())),
					BlendModeName(Group->GetGroupBlendMode()), FText::AsPercent(Group->GetGroupOpacity())),
				TEXT("bEnabled"));
		}
		else if (Component->IsA<UDreamShapeBlendGroup>())
		{
			Add(EDreamUILayerEffectKind::Group, Component, NameOf(Component), LOCTEXT("ShapeBlendDetail", "Its children's shapes melt into one"), TEXT("bBlendEnabled"));
		}
		else if (const UDreamMaskTransition* Transition = Cast<UDreamMaskTransition>(Component))
		{
			Add(EDreamUILayerEffectKind::Mask, Transition, NameOf(Transition),
				FText::Format(LOCTEXT("TransitionDetail", "{0} at {1}"), StaticEnum<EDreamMaskTransitionPreset>()->GetDisplayNameTextByValue(static_cast<int64>(Transition->GetPreset())),
					FText::AsPercent(Transition->GetProgress())),
				TEXT("bEnabled"));
		}
		else if (Component->IsA<UDreamHoloFoil>() || Component->IsA<UDreamPointerLight>() || Component->IsA<UDreamTiltCard>())
		{
			Add(EDreamUILayerEffectKind::Effect, Component, NameOf(Component), Component->GetClass()->GetToolTipText(/*bShortTooltip*/true), TEXT("bEnabled"));
		}
		else if (Component->IsA<UDreamMeshModifierBase>())
		{
			const bool bBends = Component->IsA<UDreamMeshModifierWarp>() || Component->IsA<UDreamMeshModifierJelly>() || Component->IsA<UDreamMeshModifierTextOnPath>();
			Add(bBends ? EDreamUILayerEffectKind::Warp : EDreamUILayerEffectKind::Effect, Component, NameOf(Component),
				Component->GetClass()->GetToolTipText(/*bShortTooltip*/true), TEXT("bEnable"));
		}
		else if (IsA(Component, TEXT("/Script/DreamGUINiagara.DreamNiagaraTrigger")))
		{
			Add(EDreamUILayerEffectKind::Particles, Component, NameOf(Component), LOCTEXT("TriggerDetail", "Bursts particles when the widget is clicked or entered"), TEXT(""));
		}
	}
}

bool DreamUILayerEffects::HasSwitch(const FDreamUILayerEffect& InEffect)
{
	return !InEffect.SwitchPath.IsEmpty();
}

bool DreamUILayerEffects::IsOn(const FDreamUILayerEffect& InEffect)
{
	if (!HasSwitch(InEffect))
	{
		return true;
	}
	const TOptional<FString> Value = ReadProperty(InEffect.Object.Get(), InEffect.SwitchPath);
	return !Value.IsSet() || Value.GetValue() == TEXT("True");
}

bool DreamUILayerEffects::SetOn(const FDreamUILayerEffect& InEffect, bool bInOn)
{
	if (!HasSwitch(InEffect) || !InEffect.SwitchUnavailable.IsEmpty())
	{
		return false;
	}
	const FScopedTransaction Transaction(bInOn ? LOCTEXT("TurnOn", "Turn Layer Effect On") : LOCTEXT("TurnOff", "Turn Layer Effect Off"));
	return EditAsDetailsPanel(InEffect.Object.Get(), InEffect.SwitchPath, bInOn ? TEXT("True") : TEXT("False"));
}

const FSlateBrush* DreamUILayerEffects::KindIcon(EDreamUILayerEffectKind InKind)
{
	static const TCHAR* const Names[] = { TEXT("Style"), TEXT("Group"), TEXT("Mask"), TEXT("Blend"), TEXT("Effect"), TEXT("Warp"), TEXT("Particles") };
	static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EDreamUILayerEffectKind::Count), "one mark per kind");
	return FDreamGUIEditorStyle::Get().GetBrush(*FString::Printf(TEXT("DreamGUIEditor.LayerMark.%s"), Names[static_cast<int32>(InKind)]));
}

FText DreamUILayerEffects::KindName(EDreamUILayerEffectKind InKind)
{
	switch (InKind)
	{
	case EDreamUILayerEffectKind::Style: return LOCTEXT("KindStyle", "Style");
	case EDreamUILayerEffectKind::Group: return LOCTEXT("KindGroup", "Group");
	case EDreamUILayerEffectKind::Mask: return LOCTEXT("KindMask", "Mask");
	case EDreamUILayerEffectKind::Blend: return LOCTEXT("KindBlend", "Blend");
	case EDreamUILayerEffectKind::Warp: return LOCTEXT("KindWarp", "Warp");
	case EDreamUILayerEffectKind::Particles: return LOCTEXT("KindParticles", "Particles");
	default: return LOCTEXT("KindEffect", "Effect");
	}
}

TOptional<FString> DreamUILayerEffects::ReadProperty(const UObject* InObject, const FString& InPath)
{
	const TArray<FProperty*> Chain = DreamUILayerEffectsLocal::ResolvePath(InObject, InPath, nullptr);
	if (Chain.Num() == 0)
	{
		return {};
	}
	const void* Container = InObject;
	for (int32 Index = 0; Index < Chain.Num() - 1; ++Index)
	{
		Container = Chain[Index]->ContainerPtrToValuePtr<void>(Container);
	}
	FString Text;
	Chain.Last()->ExportText_InContainer(0, Text, Container, nullptr, nullptr, PPF_None);
	return Text;
}

bool DreamUILayerEffects::EditAsDetailsPanel(UObject* InObject, const FString& InPath, const FString& InValueText, FString* OutWhyNot)
{
	if (!IsValid(InObject))
	{
		if (OutWhyNot)
		{
			*OutWhyNot = TEXT("no object");
		}
		return false;
	}
	const TArray<FProperty*> Chain = DreamUILayerEffectsLocal::ResolvePath(InObject, InPath, OutWhyNot);
	if (Chain.Num() == 0)
	{
		return false;
	}
	FEditPropertyChain EditChain;
	for (FProperty* Property : Chain)
	{
		EditChain.AddTail(Property);
	}
	EditChain.SetActiveMemberPropertyNode(Chain[0]);
	EditChain.SetActivePropertyNode(Chain.Last());

	const UDreamWidget* OwnerWidget = Cast<UDreamWidget>(InObject);
	OwnerWidget = OwnerWidget != nullptr ? OwnerWidget : InObject->GetTypedOuter<UDreamWidget>();
	FDreamWidgetBlueprintEditor* Designer = OwnerWidget != nullptr ? FDreamWidgetBlueprintEditor::FindDesignerForWidget(OwnerWidget) : nullptr;
	TArray<UObject*> Edited = { InObject };

	const FScopedTransaction Transaction(LOCTEXT("EditProperty", "Edit Property"));
	// The template is what the undo buffer records, in the mirror's modify pass: a preview object filed there is restored
	// onto by an undo after the next rebuild has destroyed it (SDreamWidgetDesignerDetails::OnEditorSelectionChanged).
	if (Designer != nullptr)
	{
		Designer->MigrateDetailsChangeToTemplate(Edited, EditChain, /*bIsModify*/true);
	}
	else
	{
		InObject->Modify();
	}
	InObject->PreEditChange(EditChain);
	// The whole top-level value, edited in a copy and put back through the property's setter where it has one, as a
	// property row writes it.
	FProperty* Top = Chain[0];
	void* Copy = FMemory::Malloc(Top->GetSize(), Top->GetMinAlignment());
	Top->InitializeValue(Copy);
	Top->GetValue_InContainer(InObject, Copy);
	void* Leaf = Copy;
	for (int32 Index = 1; Index < Chain.Num(); ++Index)
	{
		Leaf = Chain[Index]->ContainerPtrToValuePtr<void>(Leaf);
	}
	const bool bImported = Chain.Last()->ImportText_Direct(*InValueText, Leaf, InObject, PPF_None) != nullptr;
	if (bImported)
	{
		Top->SetValue_InContainer(InObject, Copy);
	}
	Top->DestroyValue(Copy);
	FMemory::Free(Copy);
	if (!bImported)
	{
		if (OutWhyNot)
		{
			*OutWhyNot = FString::Printf(TEXT("'%s' is not a value of %s"), *InValueText, *InPath);
		}
		return false;
	}
	FPropertyChangedEvent Event(Chain.Last(), EPropertyChangeType::ValueSet, MakeArrayView(Edited));
	FPropertyChangedChainEvent ChainEvent(EditChain, Event);
	InObject->PostEditChangeChainProperty(ChainEvent);
	if (Designer != nullptr)
	{
		Designer->MigrateDetailsChangeToTemplate(Edited, EditChain, /*bIsModify*/false);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
