// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Workflow/DreamUILayerStyleClipboard.h"

#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"
#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "DreamUILayerStyleClipboard"

namespace DreamUILayerStyleClipboardLocal
{
	using namespace DreamUILayerStyleClipboard;

	const TCHAR* ConfigSection = TEXT("DreamGUI.LayerStyleClipboard");

	/** The clipboard's text the snippet below was read from: a menu asks on every frame it is open. */
	FString CachedText;
	bool bCachedValid = false;
	FDreamUILayerStyleSnippet CachedSnippet;

	UDreamLayerStyle* FindOwnStyle(UDreamWidget* InTemplate)
	{
		for (UDreamUIBehaviour* Behaviour : InTemplate->GetAllComponents())
		{
			if (UDreamLayerStyle* Style = Cast<UDreamLayerStyle>(Behaviour))
			{
				return Style;
			}
		}
		return nullptr;
	}

	/** One property of InClass, copied from InSource to InTarget whole. False when either object does not have it. */
	bool CopyProperty(const UClass* InClass, FName InName, UObject* InTarget, const UObject* InSource)
	{
		const FProperty* Property = FindFProperty<FProperty>(InClass, InName);
		if (Property == nullptr || !InTarget->IsA(InClass) || !InSource->IsA(InClass))
		{
			return false;
		}
		Property->CopyCompleteValue_InContainer(InTarget, InSource);
		return true;
	}

	/** The template's layer style the snippet's is written into: its own, or one added now. Null when none could be. */
	UDreamLayerStyle* PrepareStyle(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InTemplate, bool bInTextAuthored)
	{
		if (UDreamLayerStyle* Existing = FindOwnStyle(InTemplate))
		{
			Existing->SetFlags(RF_Transactional);
			Existing->Modify();
			return Existing;
		}
		if (bInTextAuthored)
		{
			// A `+` line the file takes: checked against the file before the template is touched, as an added behaviour is.
			FDreamUIStructuralEdit Planned;
			Planned.Kind = EDreamUIStructuralEditKind::InsertComponent;
			Planned.NodeId = InTemplate->GetDisplayName();
			Planned.ComponentClassName = FDreamUITextWriteBack::DescribeComponentForText(UDreamLayerStyle::StaticClass());
			FText Reason = LOCTEXT("NoSpelling", "the .dui has no name for a layer style");
			if (Planned.ComponentClassName.IsEmpty()
				|| !DreamUITextAuthoring::CheckStructuralEdit(InBlueprint, Planned, /*bInVerifyBuild*/false, Reason))
			{
				DreamUITextAuthoring::ReportStructuralRefusal(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
					FString::Printf(TEXT("paste a layer style onto '%s'"), *InTemplate->GetDisplayName()), Reason);
				return nullptr;
			}
		}
		UDreamLayerStyle* Added = Cast<UDreamLayerStyle>(InTemplate->AddComponent(UDreamLayerStyle::StaticClass()));
		if (Added != nullptr)
		{
			Added->SetFlags(RF_Transactional);
		}
		return Added;
	}

	bool ApplyToTemplate(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InTemplate, const FDreamUILayerStyleSnippet& InSnippet,
		EDreamUIStyleParts InParts, bool bInTextAuthored)
	{
		const UDreamWidget* SourceWidget = InSnippet.GetWidget();
		const UDreamWidgetTree* Tree = InTemplate->GetTypedOuter<UDreamWidgetTree>();
		const FString NodeId = InTemplate->GetDisplayName();
		InTemplate->SetFlags(RF_Transactional);
		InTemplate->Modify();

		auto NoteNode = [Tree, &NodeId](FName InName)
		{
			FDreamUITextWriteBack::NoteDirtyProperty(Tree, NodeId, EDreamUIPatchTarget::Node, INDEX_NONE, InName.ToString());
		};

		bool bChanged = false;
		if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Style))
		{
			const UDreamLayerStyle* Source = InSnippet.GetLayerStyle();
			UDreamLayerStyle* Target = Source != nullptr ? PrepareStyle(InBlueprint, InTemplate, bInTextAuthored) : nullptr;
			if (Target != nullptr)
			{
				const int32 Index = InTemplate->GetAllComponents().Find(Target);
				for (const FName Name : FDreamUILayerStyleSnippet::GetStylePropertyNames())
				{
					if (CopyProperty(UDreamLayerStyle::StaticClass(), Name, Target, Source))
					{
						FDreamUITextWriteBack::NoteDirtyProperty(Tree, NodeId, EDreamUIPatchTarget::Component, Index, Name.ToString());
					}
				}
				bChanged = true;
			}
		}
		if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Blending))
		{
			const FName OpacityName = FDreamUILayerStyleSnippet::GetOpacityPropertyName();
			if (CopyProperty(UDreamWidget::StaticClass(), OpacityName, InTemplate, SourceWidget))
			{
				NoteNode(OpacityName);
				bChanged = true;
			}
			// Only onto an element that blends by a mode of its own, by the same enum: a plain widget has no element.
			UDreamVisual* TargetVisual = InTemplate->GetVisual();
			const UDreamVisual* SourceVisual = InSnippet.GetVisual();
			const FName ModeName = FDreamUILayerStyleSnippet::GetBlendModePropertyName();
			const FEnumProperty* TargetMode = TargetVisual != nullptr ? FindFProperty<FEnumProperty>(TargetVisual->GetClass(), ModeName) : nullptr;
			const FEnumProperty* SourceMode = SourceVisual != nullptr ? FindFProperty<FEnumProperty>(SourceVisual->GetClass(), ModeName) : nullptr;
			if (TargetMode != nullptr && SourceMode != nullptr && TargetMode->GetEnum() == SourceMode->GetEnum())
			{
				TargetVisual->SetFlags(RF_Transactional);
				TargetVisual->Modify();
				const int64 Mode = SourceMode->GetUnderlyingProperty()->GetSignedIntPropertyValue(SourceMode->ContainerPtrToValuePtr<void>(SourceVisual));
				TargetMode->GetUnderlyingProperty()->SetIntPropertyValue(TargetMode->ContainerPtrToValuePtr<void>(TargetVisual), Mode);
				NoteNode(ModeName);
				bChanged = true;
			}
		}
		if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Mask))
		{
			for (const FName Name : FDreamUILayerStyleSnippet::GetMaskPropertyNames())
			{
				if (CopyProperty(UDreamWidget::StaticClass(), Name, InTemplate, SourceWidget))
				{
					NoteNode(Name);
					bChanged = true;
				}
			}
		}
		return bChanged;
	}

	FText DescribeParts(EDreamUIStyleParts InParts)
	{
		TArray<FText> Names;
		if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Style))
		{
			Names.Add(LOCTEXT("PartStyle", "style"));
		}
		if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Blending))
		{
			Names.Add(LOCTEXT("PartBlending", "blend mode"));
		}
		if (EnumHasAnyFlags(InParts, EDreamUIStyleParts::Mask))
		{
			Names.Add(LOCTEXT("PartMask", "mask"));
		}
		return FText::Join(LOCTEXT("PartSeparator", ", "), Names);
	}

	TArray<UDreamWidget*> ValidSelection(FDreamWidgetBlueprintEditor& InDesigner)
	{
		TArray<UDreamWidget*> Widgets;
		for (const TWeakObjectPtr<UDreamWidget>& Widget : InDesigner.GetSelectedWidgets())
		{
			if (Widget.IsValid())
			{
				Widgets.Add(Widget.Get());
			}
		}
		return Widgets;
	}

	/** What the selected widget's copy would hold: its template's values, or the preview's when it has no template. */
	const UDreamWidget* CopySource(FDreamWidgetBlueprintEditor& InDesigner, UDreamWidget* InPreview)
	{
		const UDreamWidget* Template = InDesigner.GetTemplateWidget(InPreview);
		return Template != nullptr ? Template : InPreview;
	}

	class FMenuExtension : public IDreamUIWidgetContextMenuExtension
	{
	public:
		virtual void ExtendWidgetContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FDreamWidgetBlueprintEditor> InDesigner) const override
		{
			if (ValidSelection(*InDesigner).IsEmpty())
			{
				return;
			}
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakDesigner = InDesigner;
			MenuBuilder.BeginSection("DreamUILayerStyle", LOCTEXT("LayerStyleSection", "Layer Style"));
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CopyLayerStyle", "Copy Layer Style"),
					LOCTEXT("CopyLayerStyleTooltip", "Copy the selected widget's layer style -- and its blend mode and mask, as Copy Includes says -- as lines of .dui. Paste them onto other widgets, in this Blueprint or another, or into a .dui."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Copy")),
					FUIAction(
						FExecuteAction::CreateLambda([WeakDesigner]()
						{
							if (const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = WeakDesigner.Pin())
							{
								const TArray<UDreamWidget*> Selection = ValidSelection(*Designer);
								if (Selection.Num() == 1)
								{
									Copy(CopySource(*Designer, Selection[0]), GetCopyParts());
								}
							}
						}),
						FCanExecuteAction::CreateLambda([WeakDesigner]()
						{
							const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = WeakDesigner.Pin();
							if (!Designer.IsValid())
							{
								return false;
							}
							const TArray<UDreamWidget*> Selection = ValidSelection(*Designer);
							return Selection.Num() == 1
								&& !FDreamUILayerStyleSnippet::FromWidget(CopySource(*Designer, Selection[0]), GetCopyParts()).IsEmpty();
						})));
				MenuBuilder.AddMenuEntry(
					TAttribute<FText>::CreateLambda([]()
					{
						FDreamUILayerStyleSnippet Snippet;
						return ReadClipboard(Snippet)
							? FText::Format(LOCTEXT("PasteLayerStyleParts", "Paste Layer Style ({0})"), DescribeParts(Snippet.GetParts()))
							: LOCTEXT("PasteLayerStyle", "Paste Layer Style");
					}),
					LOCTEXT("PasteLayerStyleTooltip", "Write the copied layer style onto every selected widget, in one undo step: each widget's own layer style, added where it has none, and the blend mode and mask when the copy carries them."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Paste")),
					FUIAction(
						FExecuteAction::CreateLambda([WeakDesigner]()
						{
							if (const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = WeakDesigner.Pin())
							{
								Paste(*Designer, ValidSelection(*Designer));
							}
						}),
						FCanExecuteAction::CreateLambda([]()
						{
							FDreamUILayerStyleSnippet Snippet;
							return ReadClipboard(Snippet);
						})));
				MenuBuilder.AddSubMenu(
					LOCTEXT("CopyIncludes", "Copy Includes"),
					LOCTEXT("CopyIncludesTooltip", "What Copy Layer Style takes along with the style. Remembered for next time."),
					FNewMenuDelegate::CreateLambda([](FMenuBuilder& SubMenu)
					{
						auto AddToggle = [&SubMenu](const FText& InLabel, const FText& InTooltip, EDreamUIStyleParts InPart)
						{
							SubMenu.AddMenuEntry(InLabel, InTooltip, FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([InPart]()
									{
										SetCopyParts(GetCopyParts() ^ InPart);
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([InPart]()
									{
										return EnumHasAnyFlags(GetCopyParts(), InPart);
									})),
								NAME_None, EUserInterfaceActionType::ToggleButton);
						};
						AddToggle(LOCTEXT("IncludeBlending", "Blend Mode and Opacity"),
							LOCTEXT("IncludeBlendingTooltip", "The element's blend mode and the widget's opacity."), EDreamUIStyleParts::Blending);
						AddToggle(LOCTEXT("IncludeMask", "Mask and Clipping"),
							LOCTEXT("IncludeMaskTooltip", "The widget's clip, its corner radius and margin, its clip mask, and clip-to-layer-below."), EDreamUIStyleParts::Mask);
					}));
			}
			MenuBuilder.EndSection();
		}
	};

	TSharedPtr<FMenuExtension> GMenuExtension;
}

EDreamUIStyleParts DreamUILayerStyleClipboard::GetCopyParts()
{
	using namespace DreamUILayerStyleClipboardLocal;
	bool bBlending = false;
	bool bMask = false;
	if (GConfig != nullptr)
	{
		GConfig->GetBool(ConfigSection, TEXT("bIncludeBlending"), bBlending, GEditorPerProjectIni);
		GConfig->GetBool(ConfigSection, TEXT("bIncludeMask"), bMask, GEditorPerProjectIni);
	}
	EDreamUIStyleParts Parts = EDreamUIStyleParts::Style;
	if (bBlending)
	{
		Parts |= EDreamUIStyleParts::Blending;
	}
	if (bMask)
	{
		Parts |= EDreamUIStyleParts::Mask;
	}
	return Parts;
}

void DreamUILayerStyleClipboard::SetCopyParts(EDreamUIStyleParts InParts)
{
	using namespace DreamUILayerStyleClipboardLocal;
	if (GConfig != nullptr)
	{
		GConfig->SetBool(ConfigSection, TEXT("bIncludeBlending"), EnumHasAnyFlags(InParts, EDreamUIStyleParts::Blending), GEditorPerProjectIni);
		GConfig->SetBool(ConfigSection, TEXT("bIncludeMask"), EnumHasAnyFlags(InParts, EDreamUIStyleParts::Mask), GEditorPerProjectIni);
	}
}

bool DreamUILayerStyleClipboard::Copy(const UDreamWidget* InWidget, EDreamUIStyleParts InParts)
{
	const FDreamUILayerStyleSnippet Snippet = FDreamUILayerStyleSnippet::FromWidget(InWidget, InParts);
	if (Snippet.IsEmpty())
	{
		return false;
	}
	FPlatformApplicationMisc::ClipboardCopy(*Snippet.GetText());
	return true;
}

bool DreamUILayerStyleClipboard::ReadClipboard(FDreamUILayerStyleSnippet& OutSnippet)
{
	using namespace DreamUILayerStyleClipboardLocal;
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	if (Text != CachedText || (CachedText.IsEmpty() && !bCachedValid))
	{
		CachedText = Text;
		// A clipboard that does not even mention a style, a blend mode or a clip is not built: most of what is ever
		// copied is text or a widget, and a menu asks on every frame it is open.
		const bool bMentionsAPart = Text.Contains(TEXT("LayerStyle")) || Text.Contains(TEXT("BlendMode"))
			|| Text.Contains(TEXT("RenderOpacity")) || Text.Contains(TEXT("Clip"));
		bCachedValid = bMentionsAPart && FDreamUILayerStyleSnippet::Parse(Text, CachedSnippet);
		if (!bCachedValid)
		{
			CachedSnippet = FDreamUILayerStyleSnippet();
		}
	}
	if (bCachedValid)
	{
		OutSnippet = CachedSnippet;
	}
	return bCachedValid;
}

int32 DreamUILayerStyleClipboard::Apply(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets,
	const FDreamUILayerStyleSnippet& InSnippet, EDreamUIStyleParts InParts, const FText& InTransactionName)
{
	using namespace DreamUILayerStyleClipboardLocal;

	const EDreamUIStyleParts Parts = InParts & InSnippet.GetParts();
	UDreamWidgetBlueprint* Blueprint = InDesigner.GetWidgetBlueprint();
	if (Parts == EDreamUIStyleParts::None || !IsValid(Blueprint) || InSnippet.GetWidget() == nullptr)
	{
		return 0;
	}
	TArray<UDreamWidget*> Templates;
	for (UDreamWidget* Preview : InPreviewWidgets)
	{
		if (UDreamWidget* Template = IsValid(Preview) ? InDesigner.GetTemplateWidget(Preview) : nullptr)
		{
			Templates.AddUnique(Template);
		}
	}
	if (Templates.IsEmpty())
	{
		return 0;
	}

	const bool bTextAuthored = DreamUITextAuthoring::IsTextAuthored(Blueprint);
	FScopedTransaction Transaction(InTransactionName);
	Blueprint->Modify();
	int32 Applied = 0;
	{
		// One compile for the whole selection, at the end.
		DreamWidgetTreeEditing::FScopedStructureChangeBatch StructureBatch;
		for (UDreamWidget* Template : Templates)
		{
			if (ApplyToTemplate(Blueprint, Template, InSnippet, Parts, bTextAuthored))
			{
				++Applied;
			}
		}
		if (Applied > 0)
		{
			// Structural, because a layer style may have been added; on a .dui-backed Blueprint this writes the file first.
			DreamWidgetTreeEditing::NotifyStructureChanged(Blueprint);
		}
	}
	if (Applied == 0)
	{
		Transaction.Cancel();
		return 0;
	}
	TArray<UDreamWidget*> Previews;
	InDesigner.RepublishPreviewAndSelect(Templates, Previews);
	return Applied;
}

int32 DreamUILayerStyleClipboard::Paste(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets)
{
	FDreamUILayerStyleSnippet Snippet;
	if (!ReadClipboard(Snippet))
	{
		return 0;
	}
	return Apply(InDesigner, InPreviewWidgets, Snippet, EDreamUIStyleParts::All,
		InPreviewWidgets.Num() > 1 ? LOCTEXT("PasteLayerStyles", "Paste Layer Style onto Widgets") : LOCTEXT("PasteLayerStyleTx", "Paste Layer Style"));
}

void DreamUILayerStyleClipboard::RegisterMenuExtension(FDreamUIWidgetContextMenuExtensibilityManager& InManager)
{
	using namespace DreamUILayerStyleClipboardLocal;
	if (!GMenuExtension.IsValid())
	{
		GMenuExtension = MakeShared<FMenuExtension>();
		InManager.AddExtension(GMenuExtension.ToSharedRef());
	}
}

void DreamUILayerStyleClipboard::UnregisterMenuExtension(FDreamUIWidgetContextMenuExtensibilityManager& InManager)
{
	using namespace DreamUILayerStyleClipboardLocal;
	if (GMenuExtension.IsValid())
	{
		InManager.RemoveExtension(GMenuExtension.ToSharedRef());
		GMenuExtension.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
