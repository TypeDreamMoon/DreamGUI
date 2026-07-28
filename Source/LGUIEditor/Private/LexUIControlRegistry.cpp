// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "LexUIControlRegistry.h"

#include "Core/Components/LexBackgroundBlur.h"
#include "Core/Components/LexBackgroundPixelate.h"
#include "Core/Components/LexCustomMesh.h"
#include "Core/Components/LexImage.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexPixelSort.h"
#include "Core/Components/LexLayoutContainerGrid.h"
#include "Core/Components/LexLayoutSelfAspectRatio.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexLayoutSelfGrid.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexSprite.h"
#include "Core/Components/LexText.h"
#include "Core/Components/LexTexture.h"
#include "Core/Components/LexVisualBatchMesh.h"
#include "Core/Components/LexVisualEmpty.h"
#include "Core/Components/LexVisualPostProcess.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexVisual.h"
#include "Core/LexUIBehaviour.h"
#include "Core/LexUISettings.h"
#include "Core/LexUISpriteData.h"
#include "Extensions/2DLineRenderer/Lex2DLineChildrenAsPoints.h"
#include "Extensions/2DLineRenderer/Lex2DLineRaw.h"
#include "Extensions/LexCanvasRenderTargetPreviewer.h"
#include "Extensions/LexPolygon.h"
#include "Extensions/LexPolygonLine.h"
#include "Extensions/LexPostProcessRenderElement.h"
#include "Extensions/LexPostProcessRenderElement_Text.h"
#include "Extensions/LexRing.h"
#include "Extensions/LexStaticMesh.h"
#include "Extensions/LexUMGWidget.h"
#include "Extensions/LexUMGWidgetInteraction.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "Interaction/LexContentWidget.h"
#include "Interaction/LexResponsiveBinding.h"
#include "Interaction/UIEventBlocker.h"
#include "Interaction/UIEventTrigger.h"
#include "Interaction/UIListView.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UIStandardControls.h"
#include "MeshModifier/LexMeshModifierGradientColor.h"
#include "MeshModifier/LexMeshModifierLongShadow.h"
#include "MeshModifier/LexMeshModifierOutline.h"
#include "MeshModifier/LexMeshModifierPositionAsUV.h"
#include "MeshModifier/LexMeshModifierShadow.h"
#include "MeshModifier/LexMeshModifierTextAnimation.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Styling/SlateIconFinder.h"
#include "UMGStyle.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"

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
		if (ULexUISettings::GetLayoutMode() == ELexUILayoutMode::UMGCompatible)
		{
			Content->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
		}
		else if (ULexLayoutContainerFlexBox* FlexBox = Content->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>())
		{
			FlexBox->SetDirection(ELexLayoutFlexBoxDirectionType::Vertical);
		}
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

	static FSlateIcon MakeUMGIcon(const TCHAR* StyleName)
	{
		return FSlateIcon(FUMGStyle::GetStyleSetName(), FName(StyleName));
	}

	static FSlateIcon MakeClassIcon(UClass* Class)
	{
		return FSlateIconFinder::FindIconForClass(Class);
	}

	static FLexUIControlDescriptor MakePrefab(const TCHAR* Name, const TCHAR* DisplayName, const TCHAR* AssetName, const TCHAR* IconStyleName)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Controls");
		Result.CreationKind = ELexUIControlCreationKind::Prefab;
		Result.PrefabPath = FString::Printf(TEXT("/LGUI/Prefabs/%s"), AssetName);
		Result.Icon = MakeUMGIcon(IconStyleName);
		return Result;
	}

	static FLexUIControlDescriptor MakePanel(const TCHAR* Name, UClass* LayoutClass, const TCHAR* IconStyleName,
		const TCHAR* DisplayName = nullptr)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName ? DisplayName : Name);
		Result.Category = TEXT("Panels");
		Result.LayoutContainerClass = LayoutClass;
		Result.Icon = MakeUMGIcon(IconStyleName);
		return Result;
	}

	static FLexUIControlDescriptor MakeBehaviour(const TCHAR* Name, UClass* BehaviourClass, const TCHAR* IconStyleName,
		TFunction<void(ULexWidget*)> Configure = nullptr)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(Name);
		Result.Category = TEXT("Controls");
		Result.BehaviourClass = BehaviourClass;
		Result.Icon = MakeUMGIcon(IconStyleName);
		Result.NativeConfigure = MoveTemp(Configure);
		return Result;
	}

	static FLexUIControlDescriptor MakeVisual(const TCHAR* Name, const TCHAR* DisplayName, const TCHAR* Category, UClass* VisualClass)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = Category;
		Result.VisualClass = VisualClass;
		Result.Icon = MakeClassIcon(VisualClass);
		return Result;
	}

	static FLexUIControlDescriptor MakeComponent(const TCHAR* Name, const TCHAR* DisplayName, UClass* BehaviourClass,
		UClass* VisualClass = nullptr)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Components");
		Result.VisualClass = VisualClass;
		Result.BehaviourClass = BehaviourClass;
		Result.Icon = MakeClassIcon(BehaviourClass);
		return Result;
	}

	static FLexUIControlDescriptor MakeLayoutSelf(const TCHAR* Name, const TCHAR* DisplayName, UClass* LayoutClass)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Layout Modifiers");
		Result.LayoutSelfClass = LayoutClass;
		Result.Icon = MakeClassIcon(LayoutClass);
		return Result;
	}

	static FLexUIControlDescriptor MakeMeshModifier(const TCHAR* Name, const TCHAR* DisplayName, UClass* ModifierClass,
		UClass* VisualClass, TFunction<void(ULexWidget*)> Configure = nullptr)
	{
		FLexUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Mesh Modifiers");
		Result.VisualClass = VisualClass;
		Result.MeshModifierClass = ModifierClass;
		Result.Icon = MakeClassIcon(ModifierClass);
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

void FLexUIControlRegistry::InitializeDynamicDiscovery()
{
	RefreshDynamicClasses();
	if (GEditor && !BlueprintCompiledHandle.IsValid())
	{
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FLexUIControlRegistry::RefreshDynamicClasses);
	}
	if (!AssetLoadedHandle.IsValid())
	{
		AssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FLexUIControlRegistry::HandleAssetLoaded);
	}
}

void FLexUIControlRegistry::ShutdownDynamicDiscovery()
{
	if (GEditor && BlueprintCompiledHandle.IsValid())
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
	}
	BlueprintCompiledHandle.Reset();
	if (AssetLoadedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnAssetLoaded.Remove(AssetLoadedHandle);
	}
	AssetLoadedHandle.Reset();
}

void FLexUIControlRegistry::HandleAssetLoaded(UObject* Asset)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(ULexVisualPostProcess::StaticClass()))
	{
		RefreshDynamicClasses();
	}
}

void FLexUIControlRegistry::RefreshDynamicClasses()
{
	TMap<FName, TWeakObjectPtr<UClass>> FoundClasses;
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (!Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
			|| !Class->IsChildOf(ULexVisualPostProcess::StaticClass())
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Transient | CLASS_NotPlaceable))
		{
			continue;
		}
		const FString ClassName = Class->GetName();
		if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_"))
			|| ClassName.Contains(TEXT("TRASH_")) || ClassName.Contains(TEXT("_DEPRECATED")))
		{
			continue;
		}
		FoundClasses.Add(FName(*FString::Printf(TEXT("PostProcess.%s"), *Class->GetPathName())), Class);
	}

	bool bChanged = FoundClasses.Num() != DynamicPostProcessClasses.Num();
	if (!bChanged)
	{
		for (const TPair<FName, TWeakObjectPtr<UClass>>& Pair : FoundClasses)
		{
			if (DynamicPostProcessClasses.FindRef(Pair.Key) != Pair.Value)
			{
				bChanged = true;
				break;
			}
		}
	}
	if (!bChanged)
	{
		return;
	}

	Descriptors.RemoveAll([this](const FLexUIControlDescriptor& Descriptor)
	{
		return DynamicPostProcessClasses.Contains(Descriptor.Name);
	});
	DynamicPostProcessClasses = FoundClasses;

	TArray<TPair<FName, TWeakObjectPtr<UClass>>> SortedClasses;
	for (const TPair<FName, TWeakObjectPtr<UClass>>& Pair : FoundClasses)
	{
		SortedClasses.Add(Pair);
	}
	SortedClasses.Sort([](const auto& A, const auto& B)
	{
		const UClass* ClassA = A.Value.Get();
		const UClass* ClassB = B.Value.Get();
		return ClassA && ClassB ? ClassA->GetPathName() < ClassB->GetPathName() : ClassA != nullptr;
	});
	for (const TPair<FName, TWeakObjectPtr<UClass>>& Pair : SortedClasses)
	{
		if (UClass* Class = Pair.Value.Get())
		{
			FLexUIControlDescriptor Descriptor = LexUIControlRegistryLocal::MakeVisual(
				*Pair.Key.ToString(), *Class->GetDisplayNameText().ToString(), TEXT("Post Process"), Class);
			Descriptors.Add(MoveTemp(Descriptor));
		}
	}
	RegistryChanged.Broadcast();
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
		&& !Descriptor.LayoutSelfClass.IsValid() && !Descriptor.BehaviourClass.IsValid()
		&& !Descriptor.MeshModifierClass.IsValid() && !Descriptor.NativeConfigure)
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
	if (Descriptor.MeshModifierClass.IsValid() && !Descriptor.MeshModifierClass->IsChildOf(ULexMeshModifierBase::StaticClass()))
	{
		OutError = LOCTEXT("InvalidMeshModifierClass", "MeshModifierClass must derive from ULexMeshModifierBase.");
		return false;
	}
	if (Descriptor.MeshModifierClass.IsValid()
		&& (!Descriptor.VisualClass.IsValid() || !Descriptor.VisualClass->IsChildOf(ULexVisualBatchMesh::StaticClass())))
	{
		OutError = LOCTEXT("MeshModifierRequiresVisual", "A mesh modifier requires a ULexVisualBatchMesh visual class.");
		return false;
	}
	OutError = FText::GetEmpty();
	return true;
}

void FLexUIControlRegistry::RegisterDefaults()
{
	using namespace LexUIControlRegistryLocal;
	const bool bUseUMGLayout = ULexUISettings::GetLayoutMode() == ELexUILayoutMode::UMGCompatible;
	auto MakeFrameworkPanel = [bUseUMGLayout](const TCHAR* Name, UClass* LayoutClass, const TCHAR* IconStyleName,
		bool bUMGPanel, const TCHAR* DisplayName = nullptr)
	{
		FLexUIControlDescriptor Descriptor = MakePanel(Name, LayoutClass, IconStyleName, DisplayName);
		if (!DisplayName)
		{
			// The class DisplayName carries the family prefix ("UMG Vertical Box" / "Lex Flex Box"),
			// so the palette label follows it instead of the spaceless registry name.
			Descriptor.DisplayName = LayoutClass->GetDisplayNameText();
		}
		if (bUMGPanel != bUseUMGLayout)
		{
			Descriptor.Category = bUMGPanel ? TEXT("UMG Panels") : TEXT("Legacy LGUI Panels");
		}
		return Descriptor;
	};
	Register(MakePrefab(TEXT("Button"), TEXT("Button"), TEXT("Button"), TEXT("ClassIcon.Button")));
	Register(MakePrefab(TEXT("CheckBox"), TEXT("Check Box"), TEXT("Toggle"), TEXT("ClassIcon.CheckBox")));
	Register(MakePrefab(TEXT("ToggleGroup"), TEXT("Toggle Group"), TEXT("ToggleGroup"), TEXT("ClassIcon.CheckBox")));
	Register(MakePrefab(TEXT("HorizontalSlider"), TEXT("Horizontal Slider"), TEXT("HorizontalSlider"), TEXT("ClassIcon.Slider")));
	Register(MakePrefab(TEXT("VerticalSlider"), TEXT("Vertical Slider"), TEXT("VerticalSlider"), TEXT("ClassIcon.Slider")));
	Register(MakePrefab(TEXT("HorizontalScrollbar"), TEXT("Horizontal Scrollbar"), TEXT("HorizontalScrollbar"), TEXT("ClassIcon.ScrollBar")));
	Register(MakePrefab(TEXT("VerticalScrollbar"), TEXT("Vertical Scrollbar"), TEXT("VerticalScrollbar"), TEXT("ClassIcon.ScrollBar")));
	Register(MakePrefab(TEXT("ComboBox"), TEXT("Combo Box"), TEXT("Dropdown"), TEXT("ClassIcon.ComboBox")));
	Register(MakePrefab(TEXT("TextInput"), TEXT("Text Input"), TEXT("TextInput"), TEXT("ClassIcon.EditableTextBox")));
	Register(MakePrefab(TEXT("TextInputMultiline"), TEXT("Text Input (Multiline)"), TEXT("TextInput_Multiline"), TEXT("ClassIcon.MultilineEditableTextBox")));
	Register(MakePrefab(TEXT("HorizontalScrollView"), TEXT("Horizontal Scroll Box"), TEXT("HorizontalScrollView"), TEXT("ClassIcon.Scrollbox")));
	Register(MakePrefab(TEXT("VerticalScrollView"), TEXT("Vertical Scroll Box"), TEXT("VerticalScrollView"), TEXT("ClassIcon.Scrollbox")));

	Register(MakeFrameworkPanel(TEXT("CanvasPanel"), ULexLayoutContainerCanvasPanel::StaticClass(), TEXT("ClassIcon.CanvasPanel"), true));
	Register(MakeFrameworkPanel(TEXT("Overlay"), ULexLayoutContainerOverlay::StaticClass(), TEXT("ClassIcon.Overlay"), true));
	Register(MakeFrameworkPanel(TEXT("HorizontalBox"), ULexLayoutContainerHorizontalBox::StaticClass(), TEXT("ClassIcon.HorizontalBox"), true));
	Register(MakeFrameworkPanel(TEXT("VerticalBox"), ULexLayoutContainerVerticalBox::StaticClass(), TEXT("ClassIcon.VerticalBox"), true));
	Register(MakeFrameworkPanel(TEXT("StackBox"), ULexLayoutContainerStackBox::StaticClass(), TEXT("ClassIcon.StackBox"), true));
	Register(MakeFrameworkPanel(TEXT("WrapBox"), ULexLayoutContainerWrapBox::StaticClass(), TEXT("ClassIcon.WrapBox"), true));
	Register(MakeFrameworkPanel(TEXT("GridPanel"), ULexLayoutContainerGridPanel::StaticClass(), TEXT("ClassIcon.GridPanel"), true));
	Register(MakeFrameworkPanel(TEXT("UniformGridPanel"), ULexLayoutContainerUniformGridPanel::StaticClass(), TEXT("ClassIcon.UniformGridPanel"), true));
	FLexUIControlDescriptor SafeZone = MakeFrameworkPanel(TEXT("SafeZone"), ULexLayoutContainerSafeZone::StaticClass(), TEXT("ClassIcon.SafeZone"), true);
	SafeZone.BehaviourClass = ULexContentWidget::StaticClass();
	Register(SafeZone);
	FLexUIControlDescriptor ScaleBox = MakeFrameworkPanel(TEXT("ScaleBox"), ULexLayoutContainerScaleBox::StaticClass(), TEXT("ClassIcon.ScaleBox"), true);
	ScaleBox.BehaviourClass = ULexContentWidget::StaticClass();
	Register(ScaleBox);
	FLexUIControlDescriptor SizeBox = MakeFrameworkPanel(TEXT("SizeBox"), ULexLayoutContainerSizeBox::StaticClass(), TEXT("ClassIcon.Sizebox"), true);
	SizeBox.BehaviourClass = ULexContentWidget::StaticClass();
	Register(SizeBox);
	Register(MakeFrameworkPanel(TEXT("WidgetSwitcher"), ULexLayoutContainerWidgetSwitcher::StaticClass(), TEXT("ClassIcon.WidgetSwitcher"), true));
	Register(MakeFrameworkPanel(TEXT("FlexBox"), ULexLayoutContainerFlexBox::StaticClass(), TEXT("ClassIcon.WrapBox"), false));
	Register(MakeFrameworkPanel(TEXT("LayoutScrollBox"), ULexLayoutContainerScrollBox::StaticClass(), TEXT("ClassIcon.Scrollbox"), true));
	Register(MakeFrameworkPanel(TEXT("ResponsiveGrid"), ULexLayoutContainerGrid::StaticClass(), TEXT("ClassIcon.GridPanel"), false));

	FLexUIControlDescriptor ScrollBox = MakeBehaviour(TEXT("ScrollBox"), UUIScrollView::StaticClass(), TEXT("ClassIcon.Scrollbox"), ConfigureScrollBox);
	ScrollBox.DisplayName = FText::FromString(TEXT("Lex Scroll Box"));
	// Same category rule as the other Lex-family panels: sidelined when the project runs UMG layout.
	ScrollBox.Category = bUseUMGLayout ? TEXT("Legacy LGUI Panels") : TEXT("Panels");
	Register(ScrollBox);
	FLexUIControlDescriptor Border = MakePanel(TEXT("Border"), ULexLayoutContainerOverlay::StaticClass(), TEXT("ClassIcon.Border"));
	if (!bUseUMGLayout) Border.Category = TEXT("UMG Panels");
	Border.VisualClass = ULexImage::StaticClass();
	Border.BehaviourClass = ULexContentWidget::StaticClass();
	Border.NativeConfigure = ConfigureImage;
	Register(Border);

	FLexUIControlDescriptor Spacer;
	Spacer.Name = TEXT("Spacer");
	Spacer.DisplayName = FText::FromString(TEXT("Spacer"));
	Spacer.Category = TEXT("Primitive");
	Spacer.LayoutSelfClass = ULexLayoutSelfSpacer::StaticClass();
	Spacer.Icon = MakeUMGIcon(TEXT("ClassIcon.Spacer"));
	Register(Spacer);

	FLexUIControlDescriptor Progress = MakeBehaviour(TEXT("ProgressBar"), UUIProgressBar::StaticClass(), TEXT("ClassIcon.ProgressBar"), ConfigureProgressBar);
	Progress.VisualClass = ULexImage::StaticClass();
	Register(Progress);
	Register(MakeBehaviour(TEXT("ContentWidget"), ULexContentWidget::StaticClass(), TEXT("ClassIcon.NativeWidgetHost")));
	Register(MakeBehaviour(TEXT("NamedSlot"), ULexNamedSlotHost::StaticClass(), TEXT("ClassIcon.NamedSlot")));
	Register(MakeBehaviour(TEXT("ListView"), UUIListView::StaticClass(), TEXT("ClassIcon.ListView"), ConfigureListView));
	Register(MakeBehaviour(TEXT("TileView"), UUITileView::StaticClass(), TEXT("ClassIcon.TileView"), ConfigureListView));
	Register(MakeBehaviour(TEXT("TreeView"), UUITreeView::StaticClass(), TEXT("ClassIcon.TreeView"), ConfigureListView));

	Register(MakeVisual(TEXT("Polygon"), TEXT("Polygon"), TEXT("Extensions"), ULexPolygon::StaticClass()));
	Register(MakeVisual(TEXT("PolygonLine"), TEXT("Polygon Line"), TEXT("Extensions"), ULexPolygonLine::StaticClass()));
	Register(MakeVisual(TEXT("Ring"), TEXT("Ring"), TEXT("Extensions"), ULexRing::StaticClass()));
	Register(MakeVisual(TEXT("Line2DRaw"), TEXT("2D Line"), TEXT("Extensions"), ULex2DLineRaw::StaticClass()));
	Register(MakeVisual(TEXT("Line2DChildren"), TEXT("2D Line (Children as Points)"), TEXT("Extensions"), ULex2DLineChildrenAsPoints::StaticClass()));

	Register(MakeVisual(TEXT("BackgroundBlur"), TEXT("Background Blur"), TEXT("Post Process"), ULexBackgroundBlur::StaticClass()));
	Register(MakeVisual(TEXT("BackgroundPixelate"), TEXT("Background Pixelate"), TEXT("Post Process"), ULexBackgroundPixelate::StaticClass()));
	Register(MakeVisual(TEXT("PixelSort"), TEXT("Pixel Sort"), TEXT("Post Process"), ULexPixelSort::StaticClass()));

	Register(MakeVisual(TEXT("VisualEmpty"), TEXT("Empty Visual"), TEXT("Advanced Visuals"), ULexVisualEmpty::StaticClass()));
	Register(MakeVisual(TEXT("Texture"), TEXT("Texture"), TEXT("Advanced Visuals"), ULexTexture::StaticClass()));
	Register(MakeVisual(TEXT("Sprite"), TEXT("Sprite"), TEXT("Advanced Visuals"), ULexSprite::StaticClass()));
	Register(MakeVisual(TEXT("CustomMesh"), TEXT("Custom Mesh"), TEXT("Advanced Visuals"), ULexCustomMesh::StaticClass()));
	Register(MakeVisual(TEXT("UMGWidget"), TEXT("UMG Widget"), TEXT("Advanced Visuals"), ULexUMGWidget::StaticClass()));
	Register(MakeVisual(TEXT("CanvasRenderTargetPreviewer"), TEXT("Canvas Render Target Previewer"), TEXT("Advanced Rendering"), ULexCanvasRenderTargetPreviewer::StaticClass()));
	Register(MakeVisual(TEXT("PostProcessRenderElement"), TEXT("Post Process Render Element"), TEXT("Advanced Rendering"), ULexPostProcessRenderElement::StaticClass()));
	Register(MakeVisual(TEXT("PostProcessRenderElementText"), TEXT("Post Process Render Element (Text)"), TEXT("Advanced Rendering"), ULexPostProcessRenderElement_Text::StaticClass()));
	Register(MakeVisual(TEXT("StaticMeshExperimental"), TEXT("Static Mesh (Experimental)"), TEXT("Experimental"), ULexStaticMesh::StaticClass()));

	Register(MakeComponent(TEXT("DataBinding"), TEXT("Data Binding"), ULexDataBinding::StaticClass()));
	Register(MakeComponent(TEXT("ResponsiveBehaviour"), TEXT("Responsive Behaviour"), ULexResponsiveBehaviour::StaticClass()));
	Register(MakeComponent(TEXT("EventBlocker"), TEXT("Event Blocker"), UUIEventBlocker::StaticClass()));
	Register(MakeComponent(TEXT("EventTrigger"), TEXT("Event Trigger"), UUIEventTrigger::StaticClass()));
	Register(MakeComponent(TEXT("NavigationInputSelection"), TEXT("Navigation Input Selection"), UUINavigationInputSelectionHandler::StaticClass()));
	Register(MakeComponent(TEXT("SpriteSequencePlayer"), TEXT("Sprite Sequence Player"), UUISpriteSequencePlayer::StaticClass(), ULexSprite::StaticClass()));
	Register(MakeComponent(TEXT("SpriteSheetTexturePlayer"), TEXT("Sprite Sheet Texture Player"), UUISpriteSheetTexturePlayer::StaticClass(), ULexTexture::StaticClass()));
	Register(MakeComponent(TEXT("UMGWidgetInteraction"), TEXT("UMG Widget Interaction"), ULexUMGWidgetInteraction::StaticClass(), ULexUMGWidget::StaticClass()));

	Register(MakeLayoutSelf(TEXT("AspectRatioLayout"), TEXT("Aspect Ratio"), ULexLayoutSelfAspectRatio::StaticClass()));
	Register(MakeLayoutSelf(TEXT("FlexBoxItemLayout"), TEXT("Flex Box Item"), ULexLayoutSelfFlexBox::StaticClass()));
	Register(MakeLayoutSelf(TEXT("GridItemLayout"), TEXT("Grid Item (Experimental)"), ULexLayoutSelfGrid::StaticClass()));

	Register(MakeMeshModifier(TEXT("GradientColorModifier"), TEXT("Gradient Color"), ULexMeshModifierGradientColor::StaticClass(), ULexImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("LongShadowModifier"), TEXT("Long Shadow"), ULexMeshModifierLongShadow::StaticClass(), ULexImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("OutlineModifier"), TEXT("Outline"), ULexMeshModifierOutline::StaticClass(), ULexImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("PositionAsUVModifier"), TEXT("Position as UV"), ULexMeshModifierPositionAsUV::StaticClass(), ULexImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("ShadowModifier"), TEXT("Shadow"), ULexMeshModifierShadow::StaticClass(), ULexImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("TextAnimationModifier"), TEXT("Text Animation"), ULexMeshModifierTextAnimation::StaticClass(), ULexText::StaticClass()));
}

#undef LOCTEXT_NAMESPACE
