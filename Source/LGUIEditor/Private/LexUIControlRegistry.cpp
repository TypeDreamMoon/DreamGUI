// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "LexUIControlRegistry.h"

#include "Core/Components/LexImage.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexVisual.h"
#include "Core/LexUIBehaviour.h"
#include "Core/LexUISpriteData.h"
#include "Interaction/LexContentWidget.h"
#include "Interaction/UIListView.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UIStandardControls.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "LexUIControlRegistry"

namespace LexUIControlRegistryLocal
{
	static ULexWidget* CreateChild(ULexWidget* Parent, const TCHAR* Name, UClass* VisualClass = nullptr)
	{
		ULexWidget* Child = NewObject<ULexWidget>(Parent->GetOuter(), ULexWidget::StaticClass(), NAME_None, RF_Public | RF_Transactional);
		Child->SetDisplayName(Name);
		Child->OnRegister();
		Child->SetParent(Parent, false);
		Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D::UnitVector, false, false);
		Child->SetAnchorOffset(FMargin(0));
		if (VisualClass)
		{
			Child->CreateNewVisual(VisualClass);
		}
		return Child;
	}

	static void ConfigureProgressBar(ULexWidget* Root)
	{
		if (ULexImage* Background = Cast<ULexImage>(Root->GetVisual()))
		{
			Background->SetBrush_LexUISprite(ULexUISpriteData::GetDefaultFrameRect());
		}
		UUIProgressBar* Progress = Root->GetComponent<UUIProgressBar>();
		ULexWidget* Fill = CreateChild(Root, TEXT("Fill"), ULexImage::StaticClass());
		CastChecked<ULexImage>(Fill->GetVisual())->SetBrush_LexUISprite(ULexUISpriteData::GetDefaultFrameRect());
		Progress->SetFillWidget(Fill);
		Progress->SetPercent(0.5f);
	}

	static void ConfigureListView(ULexWidget* Root)
	{
		UUIRecyclableScrollView* List = Cast<UUIRecyclableScrollView>(Root->GetComponent(UUIRecyclableScrollView::StaticClass()));
		if (!List)
		{
			return;
		}
		Root->SetClipping(ELexWidgetClipping::ClipToBounds);
		if (!Root->GetVisual())
		{
			Root->CreateNewVisual<ULexImage>();
		}
		CastChecked<ULexImage>(Root->GetVisual())->SetBrush_LexUISprite(ULexUISpriteData::GetDefaultFrameRect());
		ULexWidget* Viewport = CreateChild(Root, TEXT("Viewport"));
		Viewport->SetClipping(ELexWidgetClipping::ClipToBounds);
		ULexWidget* Content = CreateChild(Viewport, TEXT("Content"));
		Content->SetHorizontalAnchorMinMax(FVector2D(0.0, 1.0), false, false);
		Content->SetVerticalAnchorMinMax(FVector2D(1.0, 1.0), false, false);
		Content->SetHeight(500.0f);
		ULexWidget* EntryTemplate = CreateChild(Content, TEXT("EntryTemplate"), ULexImage::StaticClass());
		EntryTemplate->SetHorizontalAnchorMinMax(FVector2D(0.0, 1.0), false, false);
		EntryTemplate->SetVerticalAnchorMinMax(FVector2D(1.0, 1.0), false, false);
		EntryTemplate->SetHeight(32.0f);
		EntryTemplate->AddComponent<UUIListEntry>();
		EntryTemplate->SetWidgetActive(false);
		List->SetContent(Content);
		List->SetCellTemplate(EntryTemplate);
		List->SetHorizontal(false);
		List->SetVertical(true);
	}

	static void ConfigureScrollBox(ULexWidget* Root)
	{
		UUIScrollView* ScrollView = Root->GetComponent<UUIScrollView>();
		if (!ScrollView)
		{
			return;
		}
		Root->SetClipping(ELexWidgetClipping::ClipToBounds);
		Root->CreateNewVisual<ULexImage>()->SetBrush_LexUISprite(ULexUISpriteData::GetDefaultFrameRect());
		ULexWidget* Content = CreateChild(Root, TEXT("Content"));
		Content->SetHorizontalAnchorMinMax(FVector2D(0.0, 1.0), false, false);
		Content->SetVerticalAnchorMinMax(FVector2D(1.0, 1.0), false, false);
		Content->SetHeight(500.0f);
		Content->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
		ScrollView->SetContent(Content);
		ScrollView->SetHorizontal(false);
		ScrollView->SetVertical(true);
	}

	static void ConfigureImage(ULexWidget* Root)
	{
		if (ULexImage* Image = Cast<ULexImage>(Root->GetVisual()))
		{
			Image->SetBrush_LexUISprite(ULexUISpriteData::GetDefaultFrameRect());
		}
	}

	static FLexUIControlDescriptor MakePrefab(const TCHAR* Name, const TCHAR* DisplayName, const TCHAR* AssetName)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Controls");
		Result.CreationKind = ELexUIControlCreationKind::Prefab;
		Result.PrefabPath = FString::Printf(TEXT("/LGUI/Prefabs/%s"), AssetName);
		return Result;
	}

	static FLexUIControlDescriptor MakePanel(const TCHAR* Name, UClass* LayoutClass)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(Name);
		Result.Category = TEXT("Panels");
		Result.LayoutContainerClass = LayoutClass;
		return Result;
	}

	static FLexUIControlDescriptor MakeBehaviour(const TCHAR* Name, UClass* BehaviourClass, TFunction<void(ULexWidget*)> Configure = nullptr)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(Name);
		Result.Category = TEXT("Controls");
		Result.BehaviourClass = BehaviourClass;
		Result.NativeConfigure = MoveTemp(Configure);
		return Result;
	}
}

FLexUIControlRegistry& FLexUIControlRegistry::Get()
{
	static FLexUIControlRegistry Instance;
	return Instance;
}

FLexUIControlRegistry::FLexUIControlRegistry()
{
	RegisterDefaults();
}

bool FLexUIControlRegistry::Register(const FLexUIControlDescriptor& Descriptor)
{
	if (Descriptor.Name.IsNone() || Descriptors.ContainsByPredicate([&Descriptor](const FLexUIControlDescriptor& Existing)
	{
		return Existing.Name == Descriptor.Name;
	}))
	{
		return false;
	}
	Descriptors.Add(Descriptor);
	RegistryChanged.Broadcast();
	return true;
}

bool FLexUIControlRegistry::Unregister(FName Name)
{
	const bool bRemoved = Descriptors.RemoveAll([Name](const FLexUIControlDescriptor& Descriptor) { return Descriptor.Name == Name; }) > 0;
	if (bRemoved)
	{
		RegistryChanged.Broadcast();
	}
	return bRemoved;
}

bool FLexUIControlRegistry::Validate(const FLexUIControlDescriptor& Descriptor, FText& OutError) const
{
	if (Descriptor.CreationKind == ELexUIControlCreationKind::Prefab)
	{
		if (Descriptor.PrefabPath.IsEmpty() || !FPackageName::DoesPackageExist(Descriptor.PrefabPath))
		{
			OutError = FText::Format(LOCTEXT("MissingPrefab", "Missing control prefab: {0}"), FText::FromString(Descriptor.PrefabPath));
			return false;
		}
		TArray<FAssetData> PackageAssets;
		IAssetRegistry::GetChecked().GetAssetsByPackageName(FName(Descriptor.PrefabPath), PackageAssets, true);
		const bool bContainsPrefab = PackageAssets.ContainsByPredicate([](const FAssetData& Asset)
		{
			return Asset.AssetClassPath == ULexUIPrefab::StaticClass()->GetClassPathName();
		});
		if (!bContainsPrefab)
		{
			OutError = FText::Format(LOCTEXT("WrongPrefabType", "Control resource is not a LexUI Prefab: {0}"), FText::FromString(Descriptor.PrefabPath));
			return false;
		}
	}
	else if (!Descriptor.VisualClass.IsValid() && !Descriptor.LayoutContainerClass.IsValid()
		&& !Descriptor.LayoutSelfClass.IsValid() && !Descriptor.BehaviourClass.IsValid() && !Descriptor.NativeConfigure)
	{
		OutError = LOCTEXT("EmptyRecipe", "The native control has no creation recipe.");
		return false;
	}
	if (Descriptor.LayoutContainerClass.IsValid() && !Descriptor.LayoutContainerClass->IsChildOf(ULexLayoutContainer::StaticClass()))
	{
		OutError = LOCTEXT("InvalidContainerClass", "LayoutContainerClass must derive from ULexLayoutContainer.");
		return false;
	}
	if (Descriptor.VisualClass.IsValid() && !Descriptor.VisualClass->IsChildOf(ULexVisual::StaticClass()))
	{
		OutError = LOCTEXT("InvalidVisualClass", "VisualClass must derive from ULexVisual.");
		return false;
	}
	if (Descriptor.LayoutSelfClass.IsValid() && !Descriptor.LayoutSelfClass->IsChildOf(ULexLayoutSelf::StaticClass()))
	{
		OutError = LOCTEXT("InvalidLayoutSelfClass", "LayoutSelfClass must derive from ULexLayoutSelf.");
		return false;
	}
	if (Descriptor.BehaviourClass.IsValid() && !Descriptor.BehaviourClass->IsChildOf(ULexUIBehaviour::StaticClass()))
	{
		OutError = LOCTEXT("InvalidBehaviourClass", "BehaviourClass must derive from ULexUIBehaviour.");
		return false;
	}
	OutError = FText::GetEmpty();
	return true;
}

void FLexUIControlRegistry::RegisterDefaults()
{
	using namespace LexUIControlRegistryLocal;
	Register(MakePrefab(TEXT("Button"), TEXT("Button"), TEXT("Button")));
	Register(MakePrefab(TEXT("CheckBox"), TEXT("Check Box"), TEXT("Toggle")));
	Register(MakePrefab(TEXT("ToggleGroup"), TEXT("Toggle Group"), TEXT("ToggleGroup")));
	Register(MakePrefab(TEXT("HorizontalSlider"), TEXT("Horizontal Slider"), TEXT("HorizontalSlider")));
	Register(MakePrefab(TEXT("VerticalSlider"), TEXT("Vertical Slider"), TEXT("VerticalSlider")));
	Register(MakePrefab(TEXT("HorizontalScrollbar"), TEXT("Horizontal Scrollbar"), TEXT("HorizontalScrollbar")));
	Register(MakePrefab(TEXT("VerticalScrollbar"), TEXT("Vertical Scrollbar"), TEXT("VerticalScrollbar")));
	Register(MakePrefab(TEXT("ComboBox"), TEXT("Combo Box"), TEXT("Dropdown")));
	Register(MakePrefab(TEXT("TextInput"), TEXT("Text Input"), TEXT("TextInput")));
	Register(MakePrefab(TEXT("TextInputMultiline"), TEXT("Text Input (Multiline)"), TEXT("TextInput_Multiline")));
	Register(MakePrefab(TEXT("HorizontalScrollView"), TEXT("Horizontal Scroll Box"), TEXT("HorizontalScrollView")));
	Register(MakePrefab(TEXT("VerticalScrollView"), TEXT("Vertical Scroll Box"), TEXT("VerticalScrollView")));

	Register(MakePanel(TEXT("CanvasPanel"), ULexLayoutContainerCanvasPanel::StaticClass()));
	Register(MakePanel(TEXT("Overlay"), ULexLayoutContainerOverlay::StaticClass()));
	Register(MakePanel(TEXT("HorizontalBox"), ULexLayoutContainerHorizontalBox::StaticClass()));
	Register(MakePanel(TEXT("VerticalBox"), ULexLayoutContainerVerticalBox::StaticClass()));
	Register(MakePanel(TEXT("StackBox"), ULexLayoutContainerStackBox::StaticClass()));
	Register(MakePanel(TEXT("WrapBox"), ULexLayoutContainerWrapBox::StaticClass()));
	Register(MakePanel(TEXT("GridPanel"), ULexLayoutContainerGridPanel::StaticClass()));
	Register(MakePanel(TEXT("UniformGridPanel"), ULexLayoutContainerUniformGridPanel::StaticClass()));
	Register(MakePanel(TEXT("SafeZone"), ULexLayoutContainerSafeZone::StaticClass()));
	Register(MakePanel(TEXT("ScaleBox"), ULexLayoutContainerScaleBox::StaticClass()));
	Register(MakePanel(TEXT("SizeBox"), ULexLayoutContainerSizeBox::StaticClass()));
	Register(MakePanel(TEXT("WidgetSwitcher"), ULexLayoutContainerWidgetSwitcher::StaticClass()));

	FLexUIControlDescriptor ScrollBox = MakeBehaviour(TEXT("ScrollBox"), UUIScrollView::StaticClass(), ConfigureScrollBox);
	ScrollBox.Category = TEXT("Panels");
	Register(ScrollBox);
	FLexUIControlDescriptor Border = MakePanel(TEXT("Border"), ULexLayoutContainerOverlay::StaticClass());
	Border.VisualClass = ULexImage::StaticClass();
	Border.NativeConfigure = ConfigureImage;
	Register(Border);

	FLexUIControlDescriptor Spacer;
	Spacer.Name = TEXT("Spacer");
	Spacer.DisplayName = FText::FromString(TEXT("Spacer"));
	Spacer.Category = TEXT("Primitive");
	Spacer.LayoutSelfClass = ULexLayoutSelfSpacer::StaticClass();
	Register(Spacer);

	FLexUIControlDescriptor Progress = MakeBehaviour(TEXT("ProgressBar"), UUIProgressBar::StaticClass(), ConfigureProgressBar);
	Progress.VisualClass = ULexImage::StaticClass();
	Register(Progress);
	Register(MakeBehaviour(TEXT("ContentWidget"), ULexContentWidget::StaticClass()));
	Register(MakeBehaviour(TEXT("NamedSlot"), ULexNamedSlotHost::StaticClass()));
	Register(MakeBehaviour(TEXT("ListView"), UUIListView::StaticClass(), ConfigureListView));
	Register(MakeBehaviour(TEXT("TileView"), UUITileView::StaticClass(), ConfigureListView));
	Register(MakeBehaviour(TEXT("TreeView"), UUITreeView::StaticClass(), ConfigureListView));
}

#undef LOCTEXT_NAMESPACE
