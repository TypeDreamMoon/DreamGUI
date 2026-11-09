// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Workflow/DreamUILayerStyleSnippet.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStylePreset.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIReflectionPolicy.h"
#include "Text/DreamUITextBuilder.h"
#include "Text/DreamUITextWriteBack.h"

#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamUILayerStyleSnippet"

const TCHAR* FDreamUILayerStyleSnippet::HeaderLine = TEXT("// DreamGUI layer style");

namespace DreamUILayerStyleSnippetLocal
{
	/** The id of the one node a snippet is built into. Never written anywhere. */
	const TCHAR* SnippetNodeId = TEXT("DreamLayerStyleSnippet");

	const FName MaskNames[] = {
		TEXT("Clipping"), TEXT("ClippingCornerRadius"), TEXT("ClippingMargin"), TEXT("ClipMask"), TEXT("bClipToBelow") };
	const FName StyleNames[] = { TEXT("Preset"), TEXT("Style") };
	const FName BlendModeName = TEXT("BlendMode");
	const FName OpacityName = TEXT("RenderOpacity");

	FString HeadOf(const FString& InPath)
	{
		int32 Dot = INDEX_NONE;
		return InPath.FindChar(TEXT('.'), Dot) ? InPath.Left(Dot) : InPath;
	}

	bool HeadIsOneOf(const FString& InPath, TConstArrayView<FName> InHeads)
	{
		const FName Head(*HeadOf(InPath));
		return InHeads.Contains(Head);
	}

	/** A dotted path inside InScope, from InContainer (an object for a class, the struct's memory for a struct). */
	const FProperty* ResolveLeaf(const UStruct* InScope, const void* InContainer, const FString& InPath, const void*& OutValue)
	{
		TArray<FString> Segments;
		InPath.ParseIntoArray(Segments, TEXT("."));
		const UStruct* Scope = InScope;
		const void* Container = InContainer;
		const FProperty* Leaf = nullptr;
		for (const FString& Segment : Segments)
		{
			if (Scope == nullptr || Container == nullptr)
			{
				return nullptr;
			}
			Leaf = FindFProperty<FProperty>(Scope, *Segment);
			if (Leaf == nullptr)
			{
				return nullptr;
			}
			Container = Leaf->ContainerPtrToValuePtr<void>(Container);
			const FStructProperty* AsStruct = CastField<FStructProperty>(Leaf);
			Scope = AsStruct != nullptr ? AsStruct->Struct : nullptr;
		}
		OutValue = Container;
		return Leaf;
	}

	/**
	 * `Prefix.Path = value` for every writable leaf of InScope under one of InHeads (all of them when empty) whose value on
	 * InLive prints differently from its value on InDefaults; and for the paths of InAlways whatever they hold.
	 */
	void AppendLeaves(const UStruct* InScope, const void* InLive, const void* InDefaults, TConstArrayView<FName> InHeads,
		const FString& InPrefix, const FString& InIndent, TConstArrayView<FString> InAlways, TArray<FString>& OutLines)
	{
		for (const FString& Path : DreamUIReflection::GetWritableLeafPaths(InScope))
		{
			if (!InHeads.IsEmpty() && !HeadIsOneOf(Path, InHeads))
			{
				continue;
			}
			const void* LiveValue = nullptr;
			const void* DefaultValue = nullptr;
			const FProperty* Leaf = ResolveLeaf(InScope, InLive, Path, LiveValue);
			const FProperty* DefaultLeaf = ResolveLeaf(InScope, InDefaults, Path, DefaultValue);
			if (Leaf == nullptr || DefaultLeaf != Leaf)
			{
				continue;
			}
			FString LiveText;
			if (!FDreamUITextWriteBack::PrintValueLiteral(Leaf, LiveValue, LiveText))
			{
				continue;
			}
			FString DefaultText;
			const bool bDefaultPrinted = FDreamUITextWriteBack::PrintValueLiteral(Leaf, DefaultValue, DefaultText);
			if (bDefaultPrinted && DefaultText == LiveText && !InAlways.Contains(Path))
			{
				continue;
			}
			OutLines.Add(FString::Printf(TEXT("%s%s%s = %s"), *InIndent, *InPrefix, *Path, *LiveText));
		}
	}

	/** The widget's own layer style: the first, as UDreamLayerStyle::Find answers for a registered one. */
	const UDreamLayerStyle* FindStyle(const UDreamWidget* InWidget)
	{
		if (InWidget != nullptr)
		{
			for (const UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
			{
				if (const UDreamLayerStyle* Style = Cast<UDreamLayerStyle>(Behaviour))
				{
					return Style;
				}
			}
		}
		return nullptr;
	}

	FString JoinLines(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n")) + TEXT("\n");
	}

	void AppendStyleBlock(const UDreamLayerStyle* InStyle, TArray<FString>& OutLines)
	{
		const FString ClassName = FDreamUITextWriteBack::DescribeComponentForText(UDreamLayerStyle::StaticClass());
		OutLines.Add(FString::Printf(TEXT("+ %s {"), ClassName.IsEmpty() ? TEXT("DreamLayerStyle") : *ClassName));
		AppendLeaves(UDreamLayerStyle::StaticClass(), InStyle, GetDefault<UDreamLayerStyle>(), StyleNames, FString(),
			TEXT("    "), {}, OutLines);
		OutLines.Add(TEXT("}"));
	}
}

TConstArrayView<FName> FDreamUILayerStyleSnippet::GetMaskPropertyNames()
{
	return DreamUILayerStyleSnippetLocal::MaskNames;
}

TConstArrayView<FName> FDreamUILayerStyleSnippet::GetStylePropertyNames()
{
	return DreamUILayerStyleSnippetLocal::StyleNames;
}

FName FDreamUILayerStyleSnippet::GetBlendModePropertyName()
{
	return DreamUILayerStyleSnippetLocal::BlendModeName;
}

FName FDreamUILayerStyleSnippet::GetOpacityPropertyName()
{
	return DreamUILayerStyleSnippetLocal::OpacityName;
}

bool FDreamUILayerStyleSnippet::Parse(const FString& InText, FDreamUILayerStyleSnippet& OutSnippet, FText* OutError)
{
	using namespace DreamUILayerStyleSnippetLocal;

	OutSnippet = FDreamUILayerStyleSnippet();
	auto Fail = [OutError](const FText& InReason)
	{
		if (OutError != nullptr)
		{
			*OutError = InReason;
		}
		return false;
	};
	if (InText.TrimStartAndEnd().IsEmpty())
	{
		return Fail(LOCTEXT("Empty", "There is nothing to read."));
	}

	// A rect block, because its element has a blend mode: a bare `BlendMode` line lands on it, as on any visual's node.
	const FString Wrapped = FString::Printf(TEXT("RectBlock %s {\n%s\n}\n"), SnippetNodeId, *InText);
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	Diagnostics.SourceName = TEXT("LayerStyle.dui");
	UDreamWidgetTree* Tree = FDreamUITextWriteBack::BuildReferenceTree(Wrapped, Ast, Diagnostics);
	if (Tree == nullptr || !Ast.bHasRoot || Tree->GetRootWidget() == nullptr)
	{
		for (const FDreamUIDiagnostic& Diagnostic : Diagnostics.Diagnostics)
		{
			if (Diagnostic.IsError())
			{
				return Fail(FText::FromString(Diagnostic.ToString()));
			}
		}
		return Fail(LOCTEXT("DoesNotBuild", "These lines do not build."));
	}
	// Children would be widgets of their own, which a look has nothing to do with: this is a widget, not a style.
	if (!Ast.Root.Children.IsEmpty())
	{
		return Fail(LOCTEXT("HasChildren", "These lines declare widgets; a layer style is a block of values."));
	}

	EDreamUIStyleParts Parts = EDreamUIStyleParts::None;
	for (const FDreamUIProperty& Property : Ast.Root.Properties)
	{
		if (HeadIsOneOf(Property.Name, MaskNames))
		{
			Parts |= EDreamUIStyleParts::Mask;
		}
		else if (Property.Name == BlendModeName.ToString() || Property.Name == OpacityName.ToString())
		{
			Parts |= EDreamUIStyleParts::Blending;
		}
	}
	for (const FDreamUIComponent& Component : Ast.Root.Components)
	{
		const UClass* ComponentClass = FDreamUITextBuilder::ResolveComponentClass(Component.ClassName);
		if (ComponentClass != nullptr && ComponentClass->IsChildOf(UDreamLayerStyle::StaticClass()))
		{
			Parts |= EDreamUIStyleParts::Style;
		}
	}
	if (Parts == EDreamUIStyleParts::None)
	{
		return Fail(LOCTEXT("NoParts", "These lines hold no layer style, blend mode or mask."));
	}

	OutSnippet.Text = InText;
	OutSnippet.Parts = Parts;
	OutSnippet.Built.Reset(Tree);
	return true;
}

FDreamUILayerStyleSnippet FDreamUILayerStyleSnippet::FromWidget(const UDreamWidget* InWidget, EDreamUIStyleParts InParts)
{
	using namespace DreamUILayerStyleSnippetLocal;

	FDreamUILayerStyleSnippet Snippet;
	if (!IsValid(InWidget))
	{
		return Snippet;
	}
	TArray<FString> Lines;
	Lines.Add(HeaderLine);
	bool bAny = false;
	if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Blending))
	{
		const UDreamVisual* Visual = InWidget->GetVisual();
		if (Visual != nullptr)
		{
			const void* Value = nullptr;
			FString Literal;
			if (const FProperty* Leaf = ResolveLeaf(Visual->GetClass(), Visual, BlendModeName.ToString(), Value);
				Leaf != nullptr && FDreamUITextWriteBack::PrintValueLiteral(Leaf, Value, Literal))
			{
				Lines.Add(FString::Printf(TEXT("%s = %s"), *BlendModeName.ToString(), *Literal));
			}
		}
		const void* Value = nullptr;
		FString Literal;
		if (const FProperty* Leaf = ResolveLeaf(UDreamWidget::StaticClass(), InWidget, OpacityName.ToString(), Value);
			Leaf != nullptr && FDreamUITextWriteBack::PrintValueLiteral(Leaf, Value, Literal))
		{
			Lines.Add(FString::Printf(TEXT("%s = %s"), *OpacityName.ToString(), *Literal));
		}
		bAny = true;
	}
	if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Mask))
	{
		const FString Always[] = { MaskNames[0].ToString() };
		AppendLeaves(UDreamWidget::StaticClass(), InWidget, GetDefault<UDreamWidget>(), MaskNames, FString(), FString(),
			Always, Lines);
		bAny = true;
	}
	if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Style))
	{
		if (const UDreamLayerStyle* Style = FindStyle(InWidget))
		{
			AppendStyleBlock(Style, Lines);
			bAny = true;
		}
	}
	if (bAny)
	{
		Parse(JoinLines(Lines), Snippet);
	}
	return Snippet;
}

FDreamUILayerStyleSnippet FDreamUILayerStyleSnippet::FromStyle(const FDreamLayerEffects& InStyle,
	const UDreamLayerStylePreset* InPreset, TOptional<EDreamUIBlendMode> InBlendMode, float InOpacity)
{
	using namespace DreamUILayerStyleSnippetLocal;

	TArray<FString> Lines;
	Lines.Add(HeaderLine);
	if (InBlendMode.IsSet())
	{
		if (const UEnum* Enum = StaticEnum<EDreamUIBlendMode>())
		{
			Lines.Add(FString::Printf(TEXT("%s = %s"), *BlendModeName.ToString(),
				*Enum->GetNameStringByValue(static_cast<int64>(InBlendMode.GetValue()))));
		}
		const FProperty* OpacityProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), OpacityName);
		FString Literal;
		if (OpacityProperty != nullptr && FDreamUITextWriteBack::PrintValueLiteral(OpacityProperty, &InOpacity, Literal))
		{
			Lines.Add(FString::Printf(TEXT("%s = %s"), *OpacityName.ToString(), *Literal));
		}
	}
	// A scratch style of the class's own defaults with these effects: the lines are then the block's, printed the one way.
	UDreamLayerStyle* Scratch = NewObject<UDreamLayerStyle>(GetTransientPackage());
	if (FProperty* StyleProperty = FindFProperty<FProperty>(UDreamLayerStyle::StaticClass(), StyleNames[1]))
	{
		StyleProperty->CopyCompleteValue(StyleProperty->ContainerPtrToValuePtr<void>(Scratch), &InStyle);
	}
	if (FObjectPropertyBase* PresetProperty = FindFProperty<FObjectPropertyBase>(UDreamLayerStyle::StaticClass(), StyleNames[0]))
	{
		PresetProperty->SetObjectPropertyValue_InContainer(Scratch, const_cast<UDreamLayerStylePreset*>(InPreset));
	}
	AppendStyleBlock(Scratch, Lines);
	Scratch->MarkAsGarbage();

	FDreamUILayerStyleSnippet Snippet;
	Parse(JoinLines(Lines), Snippet);
	return Snippet;
}

const UDreamLayerStyle* FDreamUILayerStyleSnippet::GetLayerStyle() const
{
	return HasPart(EDreamUIStyleParts::Style) ? DreamUILayerStyleSnippetLocal::FindStyle(GetWidget()) : nullptr;
}

const UDreamWidget* FDreamUILayerStyleSnippet::GetWidget() const
{
	return Built.IsValid() ? Built->GetRootWidget() : nullptr;
}

const UDreamVisual* FDreamUILayerStyleSnippet::GetVisual() const
{
	const UDreamWidget* Widget = GetWidget();
	return Widget != nullptr ? Widget->GetVisual() : nullptr;
}

bool FDreamUILayerStyleSnippet::GetBlending(EDreamUIBlendMode& OutBlendMode, float& OutOpacity) const
{
	using namespace DreamUILayerStyleSnippetLocal;
	const UDreamWidget* Widget = GetWidget();
	const UDreamVisual* Visual = GetVisual();
	if (!HasPart(EDreamUIStyleParts::Blending) || Widget == nullptr || Visual == nullptr)
	{
		return false;
	}
	const void* Value = nullptr;
	const FEnumProperty* ModeProperty = CastField<FEnumProperty>(ResolveLeaf(Visual->GetClass(), Visual, BlendModeName.ToString(), Value));
	const void* OpacityValue = nullptr;
	const FFloatProperty* OpacityProperty = CastField<FFloatProperty>(ResolveLeaf(UDreamWidget::StaticClass(), Widget, OpacityName.ToString(), OpacityValue));
	if (ModeProperty == nullptr || OpacityProperty == nullptr)
	{
		return false;
	}
	OutBlendMode = static_cast<EDreamUIBlendMode>(ModeProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
	OutOpacity = OpacityProperty->GetPropertyValue(OpacityValue);
	return true;
}

#undef LOCTEXT_NAMESPACE
