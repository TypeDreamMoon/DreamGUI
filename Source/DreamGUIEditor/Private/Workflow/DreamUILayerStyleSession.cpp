// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Workflow/DreamUILayerStyleDialog.h"

#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "DreamWidgetBlueprint.h"
#include "Workflow/DreamUILayerStyleClipboard.h"
#include "Workflow/DreamUILayerStyleSnippet.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStylePreset.h"
#include "Core/DreamWidgetTree.h"

#include "Algo/Find.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamUILayerStyleSession"

namespace DreamUILayerStyleSessionLocal
{
	UDreamLayerStyle* FindStyle(UDreamWidget* InWidget)
	{
		if (InWidget != nullptr)
		{
			for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
			{
				if (UDreamLayerStyle* Style = Cast<UDreamLayerStyle>(Behaviour))
				{
					return Style;
				}
			}
		}
		return nullptr;
	}

	/** The element's blend mode, read by reflection: any visual that blends by a mode has a BlendMode of this enum. */
	bool ReadBlendMode(const UDreamWidget* InWidget, EDreamUIBlendMode& OutMode)
	{
		const UDreamVisual* Visual = InWidget != nullptr ? InWidget->GetVisual() : nullptr;
		const FEnumProperty* Mode = Visual != nullptr ? FindFProperty<FEnumProperty>(Visual->GetClass(), TEXT("BlendMode")) : nullptr;
		if (Mode == nullptr || Mode->GetEnum() != StaticEnum<EDreamUIBlendMode>())
		{
			return false;
		}
		OutMode = static_cast<EDreamUIBlendMode>(Mode->GetUnderlyingProperty()->GetSignedIntPropertyValue(Mode->ContainerPtrToValuePtr<void>(Visual)));
		return true;
	}

	void WriteBlendMode(UDreamWidget* InWidget, EDreamUIBlendMode InMode)
	{
		if (UDreamVisualBatchMesh* Visual = InWidget != nullptr ? Cast<UDreamVisualBatchMesh>(InWidget->GetVisual()) : nullptr)
		{
			Visual->SetBlendMode(InMode);
		}
	}

	/** The arrays of the multi-instance pages, as one switch. */
	template<typename FunctorType>
	auto WithArray(FDreamLayerEffects& InStyle, EDreamUIStyleDialogPage InPage, FunctorType&& InFunctor)
	{
		switch (InPage)
		{
		case EDreamUIStyleDialogPage::Stroke: return InFunctor(InStyle.Strokes);
		case EDreamUIStyleDialogPage::InnerShadow: return InFunctor(InStyle.InnerShadows);
		case EDreamUIStyleDialogPage::ColorOverlay: return InFunctor(InStyle.ColorOverlays);
		case EDreamUIStyleDialogPage::GradientOverlay: return InFunctor(InStyle.GradientOverlays);
		case EDreamUIStyleDialogPage::DropShadow: return InFunctor(InStyle.DropShadows);
		default: return InFunctor(InStyle.DropShadows);
		}
	}

	const TCHAR* ContourFields[] = { TEXT("Contour"), TEXT("bContourAntiAliased"), TEXT("ContourRange") };
	const TCHAR* TextureFields[] = { TEXT("Texture"), TEXT("TextureDepth"), TEXT("bTextureInvert") };
}

TSharedPtr<FDreamUILayerStyleSession> FDreamUILayerStyleSession::Begin(FDreamWidgetBlueprintEditor& InDesigner,
	TConstArrayView<UDreamWidget*> InPreviewWidgets)
{
	using namespace DreamUILayerStyleSessionLocal;
	TSharedRef<FDreamUILayerStyleSession> Session = MakeShareable(new FDreamUILayerStyleSession());
	Session->Designer = StaticCastSharedRef<FDreamWidgetBlueprintEditor>(InDesigner.AsShared());
	UDreamWidget* FirstTemplate = nullptr;
	for (UDreamWidget* Preview : InPreviewWidgets)
	{
		UDreamWidget* Template = IsValid(Preview) ? InDesigner.GetTemplateWidget(Preview) : nullptr;
		if (Template != nullptr && !Session->TargetNames.Contains(Template->GetDisplayName()))
		{
			Session->TargetNames.Add(Template->GetDisplayName());
			FirstTemplate = FirstTemplate != nullptr ? FirstTemplate : Template;
		}
	}
	if (FirstTemplate == nullptr)
	{
		return nullptr;
	}
	// The first widget's look is where the working copy starts: the style it draws (its preset's while it follows one).
	if (const UDreamLayerStyle* Existing = FindStyle(FirstTemplate))
	{
		Session->Model.Style = Existing->GetEffectiveStyle();
	}
	else
	{
		Session->Model.Style = FDreamLayerEffects();
	}
	EDreamUIBlendMode Mode = EDreamUIBlendMode::Alpha;
	ReadBlendMode(FirstTemplate, Mode);
	Session->Model.Blending.BlendMode = Mode;
	Session->Model.Blending.Opacity = FirstTemplate->GetRenderOpacity();
	Session->InitialBlending = Session->Model.Blending;

	Session->FindPreviews();
	if (const TSharedPtr<FDreamWidgetPreviewHost> Host = InDesigner.GetPreviewHost())
	{
		Session->PreviewRebuiltHandle = Host->OnPreviewRebuilt.AddSP(Session, &FDreamUILayerStyleSession::OnPreviewRebuilt);
	}
	return Session;
}

FDreamUILayerStyleSession::~FDreamUILayerStyleSession()
{
	if (!bFinished)
	{
		Cancel();
	}
	if (const TSharedPtr<FDreamWidgetBlueprintEditor> Pinned = Designer.Pin())
	{
		if (const TSharedPtr<FDreamWidgetPreviewHost> Host = Pinned->GetPreviewHost())
		{
			Host->OnPreviewRebuilt.Remove(PreviewRebuiltHandle);
		}
	}
}

FDreamWidgetBlueprintEditor* FDreamUILayerStyleSession::GetDesigner() const
{
	return Designer.Pin().Get();
}

void FDreamUILayerStyleSession::FindPreviews()
{
	using namespace DreamUILayerStyleSessionLocal;
	Previews.Reset();
	const TSharedPtr<FDreamWidgetBlueprintEditor> Pinned = Designer.Pin();
	const TSharedPtr<FDreamWidgetPreviewHost> Host = Pinned.IsValid() ? Pinned->GetPreviewHost() : nullptr;
	UDreamWidgetBlueprint* Blueprint = Pinned.IsValid() ? Pinned->GetWidgetBlueprint() : nullptr;
	if (!Host.IsValid() || Blueprint == nullptr || !IsValid(Blueprint->WidgetTree))
	{
		return;
	}
	// By name, in the CURRENT authoring tree: a compile of a .dui-backed Blueprint builds a new tree, and a rebuild a new preview.
	for (const FString& Name : TargetNames)
	{
		UDreamWidget* Template = nullptr;
		Blueprint->WidgetTree->ForEachWidget([&Template, &Name](UDreamWidget* Widget)
		{
			if (Template == nullptr && Widget->GetDisplayName() == Name)
			{
				Template = Widget;
			}
		});
		UDreamWidget* Preview = Template != nullptr ? Host->FindPreviewForTemplate(Template) : nullptr;
		if (Preview == nullptr)
		{
			continue;
		}
		FPreviewTarget& Target = Previews.AddDefaulted_GetRef();
		Target.Widget = Preview;
		if (UDreamLayerStyle* Existing = FindStyle(Preview))
		{
			Target.Style = Existing;
			Target.OriginalStyle = Existing->GetStyle();
			Target.OriginalPreset = Existing->GetPreset();
		}
		Target.bHadBlendMode = ReadBlendMode(Preview, Target.OriginalBlendMode);
		Target.OriginalOpacity = Preview->GetRenderOpacity();
	}
}

void FDreamUILayerStyleSession::ApplyPreview()
{
	using namespace DreamUILayerStyleSessionLocal;
	if (!bPreview || bFinished)
	{
		return;
	}
	const bool bModeEdited = bBlendingChanged || Model.Blending.BlendMode != InitialBlending.BlendMode;
	const bool bOpacityEdited = bBlendingChanged || Model.Blending.Opacity != InitialBlending.Opacity;
	for (FPreviewTarget& Target : Previews)
	{
		UDreamWidget* Widget = Target.Widget.Get();
		if (Widget == nullptr)
		{
			continue;
		}
		UDreamLayerStyle* PreviewStyle = Target.Style.Get();
		if (PreviewStyle == nullptr)
		{
			// The preview's own layer style, made for it and taken off again: the authoring tree never sees it.
			PreviewStyle = Cast<UDreamLayerStyle>(Widget->AddComponent(UDreamLayerStyle::StaticClass()));
			Target.Style = PreviewStyle;
			Target.bAdded = PreviewStyle != nullptr;
		}
		if (PreviewStyle != nullptr)
		{
			PreviewStyle->SetPreset(nullptr);
			PreviewStyle->SetStyle(Model.Style);
		}
		// Each widget keeps its own blending until the working copy's is changed, or is being tried.
		if (Target.bHadBlendMode)
		{
			WriteBlendMode(Widget, bModeEdited ? Model.Blending.BlendMode : Target.OriginalBlendMode);
		}
		Widget->SetRenderOpacity(bOpacityEdited ? Model.Blending.Opacity : Target.OriginalOpacity);
	}
}

void FDreamUILayerStyleSession::RestorePreview()
{
	using namespace DreamUILayerStyleSessionLocal;
	for (FPreviewTarget& Target : Previews)
	{
		UDreamWidget* Widget = Target.Widget.Get();
		UDreamLayerStyle* PreviewStyle = Target.Style.Get();
		if (Widget == nullptr)
		{
			continue;
		}
		if (PreviewStyle != nullptr)
		{
			if (Target.bAdded)
			{
				Widget->RemoveComponent(PreviewStyle);
				Target.Style.Reset();
				Target.bAdded = false;
			}
			else
			{
				PreviewStyle->SetStyle(Target.OriginalStyle);
				PreviewStyle->SetPreset(Target.OriginalPreset.Get());
			}
		}
		if (Target.bHadBlendMode)
		{
			WriteBlendMode(Widget, Target.OriginalBlendMode);
		}
		Widget->SetRenderOpacity(Target.OriginalOpacity);
	}
}

void FDreamUILayerStyleSession::OnPreviewRebuilt()
{
	// The preview widgets this session put its changes on are gone, with the changes; the new ones are as authored.
	if (bFinished)
	{
		return;
	}
	FindPreviews();
	if (bDirty)
	{
		ApplyPreview();
	}
}

void FDreamUILayerStyleSession::SetPreviewEnabled(bool bInEnabled)
{
	if (bPreview == bInEnabled || bFinished)
	{
		return;
	}
	bPreview = bInEnabled;
	if (bPreview)
	{
		if (bDirty)
		{
			ApplyPreview();
		}
	}
	else
	{
		RestorePreview();
	}
}

void FDreamUILayerStyleSession::PreviewChanged(EDreamUIStyleDialogPage InPage, FName InProperty)
{
	if (!bFinished)
	{
		ApplyPreview();
	}
}

void FDreamUILayerStyleSession::NotifyChanged(EDreamUIStyleDialogPage InPage, FName InProperty)
{
	if (bFinished)
	{
		return;
	}
	bDirty = true;
	if (InPage == EDreamUIStyleDialogPage::Blending)
	{
		if (InProperty.IsNone() || InProperty == GET_MEMBER_NAME_CHECKED(FDreamUIStyleDialogBlending, BlendMode)
			|| InProperty == GET_MEMBER_NAME_CHECKED(FDreamUIStyleDialogBlending, Opacity))
		{
			bBlendingChanged = true;
		}
	}
	ApplyPreview();
}

// ---------------------------------------------------------------------------------------------------------------------------
// Rows

bool FDreamUILayerStyleSession::IsMultiInstance(EDreamUIStyleDialogPage InPage)
{
	return InPage == EDreamUIStyleDialogPage::Stroke || InPage == EDreamUIStyleDialogPage::InnerShadow
		|| InPage == EDreamUIStyleDialogPage::ColorOverlay || InPage == EDreamUIStyleDialogPage::GradientOverlay
		|| InPage == EDreamUIStyleDialogPage::DropShadow;
}

TArray<FDreamUIStyleDialogRow> FDreamUILayerStyleSession::GetRows() const
{
	using namespace DreamUILayerStyleSessionLocal;
	TArray<FDreamUIStyleDialogRow> Rows;
	FDreamLayerEffects& Mutable = const_cast<FDreamLayerEffects&>(Model.Style);
	for (const EDreamUIStyleDialogPage Page : {
		EDreamUIStyleDialogPage::Blending, EDreamUIStyleDialogPage::BevelEmboss, EDreamUIStyleDialogPage::BevelContour,
		EDreamUIStyleDialogPage::BevelTexture, EDreamUIStyleDialogPage::Stroke, EDreamUIStyleDialogPage::InnerShadow,
		EDreamUIStyleDialogPage::InnerGlow, EDreamUIStyleDialogPage::Satin, EDreamUIStyleDialogPage::ColorOverlay,
		EDreamUIStyleDialogPage::GradientOverlay, EDreamUIStyleDialogPage::PatternOverlay, EDreamUIStyleDialogPage::OuterGlow,
		EDreamUIStyleDialogPage::DropShadow })
	{
		if (!IsMultiInstance(Page))
		{
			Rows.Add({ Page, INDEX_NONE });
			continue;
		}
		const int32 Count = WithArray(Mutable, Page, [](auto& InArray) { return InArray.Num(); });
		if (Count == 0)
		{
			Rows.Add({ Page, INDEX_NONE });
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Rows.Add({ Page, Index });
		}
	}
	return Rows;
}

FText FDreamUILayerStyleSession::GetRowLabel(const FDreamUIStyleDialogRow& InRow) const
{
	FText Name;
	switch (InRow.Page)
	{
	case EDreamUIStyleDialogPage::Blending: Name = LOCTEXT("Blending", "Blending Options"); break;
	case EDreamUIStyleDialogPage::BevelEmboss: Name = LOCTEXT("BevelEmboss", "Bevel & Emboss"); break;
	case EDreamUIStyleDialogPage::BevelContour: Name = LOCTEXT("BevelContour", "Contour"); break;
	case EDreamUIStyleDialogPage::BevelTexture: Name = LOCTEXT("BevelTexture", "Texture"); break;
	case EDreamUIStyleDialogPage::Stroke: Name = LOCTEXT("Stroke", "Stroke"); break;
	case EDreamUIStyleDialogPage::InnerShadow: Name = LOCTEXT("InnerShadow", "Inner Shadow"); break;
	case EDreamUIStyleDialogPage::InnerGlow: Name = LOCTEXT("InnerGlow", "Inner Glow"); break;
	case EDreamUIStyleDialogPage::Satin: Name = LOCTEXT("Satin", "Satin"); break;
	case EDreamUIStyleDialogPage::ColorOverlay: Name = LOCTEXT("ColorOverlay", "Color Overlay"); break;
	case EDreamUIStyleDialogPage::GradientOverlay: Name = LOCTEXT("GradientOverlay", "Gradient Overlay"); break;
	case EDreamUIStyleDialogPage::PatternOverlay: Name = LOCTEXT("PatternOverlay", "Pattern Overlay"); break;
	case EDreamUIStyleDialogPage::OuterGlow: Name = LOCTEXT("OuterGlow", "Outer Glow"); break;
	case EDreamUIStyleDialogPage::DropShadow: Name = LOCTEXT("DropShadow", "Drop Shadow"); break;
	}
	return InRow.Index > 0 ? FText::Format(LOCTEXT("Numbered", "{0} {1}"), Name, FText::AsNumber(InRow.Index + 1)) : Name;
}

bool FDreamUILayerStyleSession::HasCheckbox(const FDreamUIStyleDialogRow& InRow) const
{
	return InRow.Page != EDreamUIStyleDialogPage::Blending;
}

bool FDreamUILayerStyleSession::IsChecked(const FDreamUIStyleDialogRow& InRow) const
{
	using namespace DreamUILayerStyleSessionLocal;
	switch (InRow.Page)
	{
	case EDreamUIStyleDialogPage::Blending: return false;
	case EDreamUIStyleDialogPage::BevelEmboss: return Model.Style.BevelEmboss.bEnabled;
	case EDreamUIStyleDialogPage::BevelContour: return Model.Style.BevelEmboss.bUseContour;
	case EDreamUIStyleDialogPage::BevelTexture: return Model.Style.BevelEmboss.bUseTexture;
	case EDreamUIStyleDialogPage::InnerGlow: return Model.Style.InnerGlow.bEnabled;
	case EDreamUIStyleDialogPage::Satin: return Model.Style.Satin.bEnabled;
	case EDreamUIStyleDialogPage::PatternOverlay: return Model.Style.PatternOverlay.bEnabled;
	case EDreamUIStyleDialogPage::OuterGlow: return Model.Style.OuterGlow.bEnabled;
	default:
		break;
	}
	FDreamLayerEffects& Mutable = const_cast<FDreamLayerEffects&>(Model.Style);
	return WithArray(Mutable, InRow.Page, [&InRow](auto& InArray) { return InArray.IsValidIndex(InRow.Index) && InArray[InRow.Index].bEnabled; });
}

void FDreamUILayerStyleSession::SetChecked(const FDreamUIStyleDialogRow& InRow, bool bInChecked)
{
	using namespace DreamUILayerStyleSessionLocal;
	switch (InRow.Page)
	{
	case EDreamUIStyleDialogPage::Blending: return;
	case EDreamUIStyleDialogPage::BevelEmboss: Model.Style.BevelEmboss.bEnabled = bInChecked; break;
	case EDreamUIStyleDialogPage::BevelContour:
		Model.Style.BevelEmboss.bUseContour = bInChecked;
		// A contour or a texture belongs to the bevel: ticking one is asking for the bevel too, as an image editor has it.
		Model.Style.BevelEmboss.bEnabled |= bInChecked;
		break;
	case EDreamUIStyleDialogPage::BevelTexture:
		Model.Style.BevelEmboss.bUseTexture = bInChecked;
		Model.Style.BevelEmboss.bEnabled |= bInChecked;
		break;
	case EDreamUIStyleDialogPage::InnerGlow: Model.Style.InnerGlow.bEnabled = bInChecked; break;
	case EDreamUIStyleDialogPage::Satin: Model.Style.Satin.bEnabled = bInChecked; break;
	case EDreamUIStyleDialogPage::PatternOverlay: Model.Style.PatternOverlay.bEnabled = bInChecked; break;
	case EDreamUIStyleDialogPage::OuterGlow: Model.Style.OuterGlow.bEnabled = bInChecked; break;
	default:
		WithArray(Model.Style, InRow.Page, [&InRow, bInChecked](auto& InArray)
		{
			if (InArray.IsValidIndex(InRow.Index))
			{
				InArray[InRow.Index].bEnabled = bInChecked;
			}
			else if (bInChecked)
			{
				// Ticking a kind with none is asking for one.
				InArray.AddDefaulted_GetRef().bEnabled = true;
			}
			return 0;
		});
		break;
	}
	if (bInChecked)
	{
		Model.Style.bEnabled = true;
	}
	NotifyChanged(InRow.Page);
}

FDreamUIStyleDialogRow FDreamUILayerStyleSession::AddInstance(const FDreamUIStyleDialogRow& InRow)
{
	using namespace DreamUILayerStyleSessionLocal;
	if (!IsMultiInstance(InRow.Page))
	{
		return InRow;
	}
	const int32 Added = WithArray(Model.Style, InRow.Page, [&InRow](auto& InArray)
	{
		using FElement = typename TDecay<decltype(InArray[0])>::Type;
		FElement Element = InArray.IsValidIndex(InRow.Index) ? InArray[InRow.Index] : FElement();
		Element.bEnabled = true;
		const int32 At = InArray.IsValidIndex(InRow.Index) ? InRow.Index + 1 : InArray.Num();
		InArray.Insert(Element, At);
		return At;
	});
	Model.Style.bEnabled = true;
	NotifyChanged(InRow.Page);
	return { InRow.Page, Added };
}

bool FDreamUILayerStyleSession::RemoveInstance(const FDreamUIStyleDialogRow& InRow)
{
	using namespace DreamUILayerStyleSessionLocal;
	if (!IsMultiInstance(InRow.Page))
	{
		return false;
	}
	const bool bRemoved = WithArray(Model.Style, InRow.Page, [&InRow](auto& InArray)
	{
		if (!InArray.IsValidIndex(InRow.Index))
		{
			return false;
		}
		InArray.RemoveAt(InRow.Index);
		return true;
	});
	if (bRemoved)
	{
		NotifyChanged(InRow.Page);
	}
	return bRemoved;
}

bool FDreamUILayerStyleSession::MoveInstance(EDreamUIStyleDialogPage InPage, int32 InFrom, int32 InTo)
{
	using namespace DreamUILayerStyleSessionLocal;
	if (!IsMultiInstance(InPage) || InFrom == InTo)
	{
		return false;
	}
	const bool bMoved = WithArray(Model.Style, InPage, [InFrom, InTo](auto& InArray)
	{
		if (!InArray.IsValidIndex(InFrom) || !InArray.IsValidIndex(InTo))
		{
			return false;
		}
		auto Element = InArray[InFrom];
		InArray.RemoveAt(InFrom);
		InArray.Insert(Element, InTo);
		return true;
	});
	if (bMoved)
	{
		NotifyChanged(InPage);
	}
	return bMoved;
}

void* FDreamUILayerStyleSession::GetPageMemory(const FDreamUIStyleDialogRow& InRow, const UScriptStruct*& OutStruct)
{
	using namespace DreamUILayerStyleSessionLocal;
	OutStruct = nullptr;
	switch (InRow.Page)
	{
	case EDreamUIStyleDialogPage::Blending: OutStruct = FDreamUIStyleDialogBlending::StaticStruct(); return &Model.Blending;
	case EDreamUIStyleDialogPage::BevelEmboss:
	case EDreamUIStyleDialogPage::BevelContour:
	case EDreamUIStyleDialogPage::BevelTexture: OutStruct = FDreamStyleBevelEmboss::StaticStruct(); return &Model.Style.BevelEmboss;
	case EDreamUIStyleDialogPage::InnerGlow: OutStruct = FDreamStyleInnerGlow::StaticStruct(); return &Model.Style.InnerGlow;
	case EDreamUIStyleDialogPage::Satin: OutStruct = FDreamStyleSatin::StaticStruct(); return &Model.Style.Satin;
	case EDreamUIStyleDialogPage::PatternOverlay: OutStruct = FDreamStylePatternOverlay::StaticStruct(); return &Model.Style.PatternOverlay;
	case EDreamUIStyleDialogPage::OuterGlow: OutStruct = FDreamStyleOuterGlow::StaticStruct(); return &Model.Style.OuterGlow;
	default:
		break;
	}
	return WithArray(Model.Style, InRow.Page, [&InRow, &OutStruct](auto& InArray) -> void*
	{
		using FElement = typename TDecay<decltype(InArray[0])>::Type;
		if (!InArray.IsValidIndex(InRow.Index))
		{
			return nullptr;
		}
		OutStruct = FElement::StaticStruct();
		return &InArray[InRow.Index];
	});
}

FName FDreamUILayerStyleSession::GetPageMember(EDreamUIStyleDialogPage InPage)
{
	switch (InPage)
	{
	case EDreamUIStyleDialogPage::BevelEmboss:
	case EDreamUIStyleDialogPage::BevelContour:
	case EDreamUIStyleDialogPage::BevelTexture: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, BevelEmboss);
	case EDreamUIStyleDialogPage::Stroke: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, Strokes);
	case EDreamUIStyleDialogPage::InnerShadow: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, InnerShadows);
	case EDreamUIStyleDialogPage::InnerGlow: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, InnerGlow);
	case EDreamUIStyleDialogPage::Satin: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, Satin);
	case EDreamUIStyleDialogPage::ColorOverlay: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, ColorOverlays);
	case EDreamUIStyleDialogPage::GradientOverlay: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, GradientOverlays);
	case EDreamUIStyleDialogPage::PatternOverlay: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, PatternOverlay);
	case EDreamUIStyleDialogPage::OuterGlow: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, OuterGlow);
	case EDreamUIStyleDialogPage::DropShadow: return GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, DropShadows);
	default: return NAME_None;
	}
}

TArray<FName> FDreamUILayerStyleSession::GetPageFields(EDreamUIStyleDialogPage InPage)
{
	using namespace DreamUILayerStyleSessionLocal;
	TArray<FName> Fields;
	if (InPage == EDreamUIStyleDialogPage::Blending)
	{
		return Fields;
	}
	if (InPage == EDreamUIStyleDialogPage::BevelContour)
	{
		for (const TCHAR* Field : ContourFields)
		{
			Fields.Add(Field);
		}
		return Fields;
	}
	if (InPage == EDreamUIStyleDialogPage::BevelTexture)
	{
		for (const TCHAR* Field : TextureFields)
		{
			Fields.Add(Field);
		}
		return Fields;
	}
	const FStructProperty* Member = CastField<FStructProperty>(FDreamLayerEffects::StaticStruct()->FindPropertyByName(GetPageMember(InPage)));
	const FArrayProperty* Array = Member == nullptr ? CastField<FArrayProperty>(FDreamLayerEffects::StaticStruct()->FindPropertyByName(GetPageMember(InPage))) : nullptr;
	const FStructProperty* Element = Array != nullptr ? CastField<FStructProperty>(Array->Inner) : Member;
	if (Element == nullptr)
	{
		return Fields;
	}
	for (TFieldIterator<FProperty> It(Element->Struct); It; ++It)
	{
		const FString Name = It->GetName();
		// The checkbox in the list is the effect's switch; on the bevel's own page, its contour's and texture's switches and
		// fields are their pages'.
		const bool bElsewhere = Name == TEXT("bEnabled")
			|| (InPage == EDreamUIStyleDialogPage::BevelEmboss && (Name == TEXT("bUseContour") || Name == TEXT("bUseTexture")
				|| Algo::Find(ContourFields, Name) != nullptr || Algo::Find(TextureFields, Name) != nullptr));
		if (!bElsewhere && It->HasAnyPropertyFlags(CPF_Edit))
		{
			Fields.Add(It->GetFName());
		}
	}
	return Fields;
}

// ---------------------------------------------------------------------------------------------------------------------------
// Ending

int32 FDreamUILayerStyleSession::Commit()
{
	if (bFinished)
	{
		return 0;
	}
	bFinished = true;
	RestorePreview();
	const TSharedPtr<FDreamWidgetBlueprintEditor> Pinned = Designer.Pin();
	if (!bDirty || !Pinned.IsValid())
	{
		return 0;
	}
	const FDreamUILayerStyleSnippet Snippet = FDreamUILayerStyleSnippet::FromStyle(Model.Style, nullptr,
		bBlendingChanged ? TOptional<EDreamUIBlendMode>(Model.Blending.BlendMode) : TOptional<EDreamUIBlendMode>(), Model.Blending.Opacity);
	TArray<UDreamWidget*> Widgets;
	for (const FPreviewTarget& Target : Previews)
	{
		if (Target.Widget.IsValid())
		{
			Widgets.Add(Target.Widget.Get());
		}
	}
	const EDreamUIStyleParts Parts = bBlendingChanged ? EDreamUIStyleParts::Style | EDreamUIStyleParts::Blending : EDreamUIStyleParts::Style;
	return DreamUILayerStyleClipboard::Apply(*Pinned, Widgets, Snippet, Parts, LOCTEXT("LayerStyle", "Layer Style"));
}

void FDreamUILayerStyleSession::Cancel()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	RestorePreview();
}

FDreamUIStylePresetSaveArgs FDreamUILayerStyleSession::MakeSaveArgs() const
{
	FDreamUIStylePresetSaveArgs Args;
	Args.Style = Model.Style;
	FDreamLayerStyleBlending Layer;
	Layer.BlendMode = Model.Blending.BlendMode;
	Layer.Opacity = Model.Blending.Opacity;
	Args.Blending = Layer;
	Args.Name = TargetNames.IsEmpty() ? FString(TEXT("Style")) : FString::Printf(TEXT("LS_%s"), *TargetNames[0]);
	return Args;
}

#undef LOCTEXT_NAMESPACE
