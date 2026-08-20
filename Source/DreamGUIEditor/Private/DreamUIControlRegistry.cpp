// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIControlRegistry.h"

#include "Core/Components/DreamBackgroundBlur.h"
#include "Core/Components/DreamBackgroundPixelate.h"
#include "Core/Components/DreamCustomMesh.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPixelSort.h"
#include "Core/Components/DreamLayoutSelfAspectRatio.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamVisual.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUISpriteData.h"
#include "Extensions/2DLineRenderer/Dream2DLineChildrenAsPoints.h"
#include "Extensions/2DLineRenderer/Dream2DLineRaw.h"
#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "Extensions/DreamPolygon.h"
#include "Extensions/DreamPolygonLine.h"
#include "Extensions/DreamPostProcessRenderElement.h"
#include "Extensions/DreamPostProcessRenderElement_Text.h"
#include "Extensions/DreamRing.h"
#include "Extensions/DreamStaticMesh.h"
#include "Extensions/DreamUMGWidget.h"
#include "Extensions/DreamUMGWidgetInteraction.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/DreamResponsiveBinding.h"
#include "Interaction/UIEventBlocker.h"
#include "Interaction/UIEventTrigger.h"
#include "Interaction/UIListView.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UIStandardControls.h"
#include "MeshModifier/DreamMeshModifierGradientColor.h"
#include "MeshModifier/DreamMeshModifierLongShadow.h"
#include "MeshModifier/DreamMeshModifierOutline.h"
#include "MeshModifier/DreamMeshModifierPositionAsUV.h"
#include "MeshModifier/DreamMeshModifierShadow.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DreamGUIEditorModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Styling/SlateIconFinder.h"
#include "UMGStyle.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DreamUIControlRegistry"

namespace DreamUIControlRegistryLocal
{
	static UDreamWidget* CreateChild(UDreamWidget* Parent, const TCHAR* Name, UClass* VisualClass = nullptr)
	{
		UDreamWidget* Child = NewObject<UDreamWidget>(Parent->GetOuter(), UDreamWidget::StaticClass(), NAME_None, RF_Public | RF_Transactional);
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

	static void ConfigureProgressBar(UDreamWidget* Root)
	{
		if (UDreamImage* Background = Cast<UDreamImage>(Root->GetVisual()))
		{
			Background->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultFrameRect());
		}
		UUIProgressBar* Progress = Root->GetComponent<UUIProgressBar>();
		UDreamWidget* Fill = CreateChild(Root, TEXT("Fill"), UDreamImage::StaticClass());
		CastChecked<UDreamImage>(Fill->GetVisual())->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultFrameRect());
		Progress->SetFillWidget(Fill);
		Progress->SetPercent(0.5f);
	}

	static void ConfigureListView(UDreamWidget* Root)
	{
		UUIRecyclableScrollView* List = Cast<UUIRecyclableScrollView>(Root->GetComponent(UUIRecyclableScrollView::StaticClass()));
		if (!List)
		{
			return;
		}
		Root->SetClipping(EDreamWidgetClipping::ClipToBounds);
		if (!Root->GetVisual())
		{
			Root->CreateNewVisual<UDreamImage>();
		}
		CastChecked<UDreamImage>(Root->GetVisual())->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultFrameRect());
		UDreamWidget* Viewport = CreateChild(Root, TEXT("Viewport"));
		Viewport->SetClipping(EDreamWidgetClipping::ClipToBounds);
		UDreamWidget* Content = CreateChild(Viewport, TEXT("Content"));
		Content->SetHorizontalAnchorMinMax(FVector2D(0.0, 1.0), false, false);
		Content->SetVerticalAnchorMinMax(FVector2D(1.0, 1.0), false, false);
		Content->SetHeight(500.0f);
		UDreamWidget* EntryTemplate = CreateChild(Content, TEXT("EntryTemplate"), UDreamImage::StaticClass());
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

	static void ConfigureScrollBox(UDreamWidget* Root)
	{
		UUIScrollView* ScrollView = Root->GetComponent<UUIScrollView>();
		if (!ScrollView)
		{
			return;
		}
		Root->SetClipping(EDreamWidgetClipping::ClipToBounds);
		Root->CreateNewVisual<UDreamImage>()->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultFrameRect());
		UDreamWidget* Content = CreateChild(Root, TEXT("Content"));
		Content->SetHorizontalAnchorMinMax(FVector2D(0.0, 1.0), false, false);
		Content->SetVerticalAnchorMinMax(FVector2D(1.0, 1.0), false, false);
		Content->SetHeight(500.0f);
		Content->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
		ScrollView->SetContent(Content);
		ScrollView->SetHorizontal(false);
		ScrollView->SetVertical(true);
	}

	static void ConfigureImage(UDreamWidget* Root)
	{
		if (UDreamImage* Image = Cast<UDreamImage>(Root->GetVisual()))
		{
			Image->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultFrameRect());
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

	static FDreamUIControlDescriptor MakePrefab(const TCHAR* Name, const TCHAR* DisplayName, const TCHAR* AssetName, const TCHAR* IconStyleName)
	{
		FDreamUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Controls");
		Result.CreationKind = EDreamUIControlCreationKind::Prefab;
		Result.PrefabPath = FString::Printf(TEXT("/DreamGUI/Prefabs/%s"), AssetName);
		Result.Icon = MakeUMGIcon(IconStyleName);
		return Result;
	}

	static FDreamUIControlDescriptor MakePanel(const TCHAR* Name, UClass* LayoutClass, const TCHAR* IconStyleName,
		const TCHAR* DisplayName = nullptr)
	{
		FDreamUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName ? DisplayName : Name);
		Result.Category = TEXT("Panels");
		Result.LayoutContainerClass = LayoutClass;
		Result.Icon = MakeUMGIcon(IconStyleName);
		return Result;
	}

	static FDreamUIControlDescriptor MakeBehaviour(const TCHAR* Name, UClass* BehaviourClass, const TCHAR* IconStyleName,
		TFunction<void(UDreamWidget*)> Configure = nullptr)
	{
		FDreamUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(Name);
		Result.Category = TEXT("Controls");
		Result.BehaviourClass = BehaviourClass;
		Result.Icon = MakeUMGIcon(IconStyleName);
		Result.NativeConfigure = MoveTemp(Configure);
		return Result;
	}

	static FDreamUIControlDescriptor MakeVisual(const TCHAR* Name, const TCHAR* DisplayName, const TCHAR* Category, UClass* VisualClass)
	{
		FDreamUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = Category;
		Result.VisualClass = VisualClass;
		Result.Icon = MakeClassIcon(VisualClass);
		return Result;
	}

	static FDreamUIControlDescriptor MakeComponent(const TCHAR* Name, const TCHAR* DisplayName, UClass* BehaviourClass,
		UClass* VisualClass = nullptr)
	{
		FDreamUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Components");
		Result.VisualClass = VisualClass;
		Result.BehaviourClass = BehaviourClass;
		Result.Icon = MakeClassIcon(BehaviourClass);
		return Result;
	}

	static FDreamUIControlDescriptor MakeLayoutSelf(const TCHAR* Name, const TCHAR* DisplayName, UClass* LayoutClass)
	{
		FDreamUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Layout Modifiers");
		Result.LayoutSelfClass = LayoutClass;
		Result.Icon = MakeClassIcon(LayoutClass);
		return Result;
	}

	static FDreamUIControlDescriptor MakeMeshModifier(const TCHAR* Name, const TCHAR* DisplayName, UClass* ModifierClass,
		UClass* VisualClass, TFunction<void(UDreamWidget*)> Configure = nullptr)
	{
		FDreamUIControlDescriptor Result;
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

FDreamUIControlRegistry& FDreamUIControlRegistry::Get()
{
	static FDreamUIControlRegistry Instance;
	return Instance;
}

FDreamUIControlRegistry::FDreamUIControlRegistry()
{
	RegisterDefaults();
}

void FDreamUIControlRegistry::InitializeDynamicDiscovery()
{
	RefreshDynamicClasses();
	if (GEditor && !BlueprintCompiledHandle.IsValid())
	{
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FDreamUIControlRegistry::RefreshDynamicClasses);
	}
	if (!AssetLoadedHandle.IsValid())
	{
		AssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FDreamUIControlRegistry::HandleAssetLoaded);
	}
}

void FDreamUIControlRegistry::ShutdownDynamicDiscovery()
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

void FDreamUIControlRegistry::HandleAssetLoaded(UObject* Asset)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UDreamVisualPostProcess::StaticClass()))
	{
		RefreshDynamicClasses();
	}
}

void FDreamUIControlRegistry::RefreshDynamicClasses()
{
	TMap<FName, TWeakObjectPtr<UClass>> FoundClasses;
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (!Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
			|| !Class->IsChildOf(UDreamVisualPostProcess::StaticClass())
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

	Descriptors.RemoveAll([this](const FDreamUIControlDescriptor& Descriptor)
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
			FDreamUIControlDescriptor Descriptor = DreamUIControlRegistryLocal::MakeVisual(
				*Pair.Key.ToString(), *Class->GetDisplayNameText().ToString(), TEXT("Post Process"), Class);
			Descriptors.Add(MoveTemp(Descriptor));
		}
	}
	RegistryChanged.Broadcast();
}

bool FDreamUIControlRegistry::Register(const FDreamUIControlDescriptor& Descriptor)
{
	// A refusal used to be a discarded bool, so an extension registering a name the defaults already
	// take simply never appeared in the Palette and nothing anywhere said why. Name it, and name what
	// it collided with -- that pair is the whole diagnosis.
	if (Descriptor.Name.IsNone())
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d Refused a control descriptor with no Name (display name \"%s\"): the registry keys entries by Name.")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Descriptor.DisplayName.ToString());
		return false;
	}
	if (const FDreamUIControlDescriptor* Existing = Descriptors.FindByPredicate([&Descriptor](const FDreamUIControlDescriptor& Item)
	{
		return Item.Name == Descriptor.Name;
	}))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d Refused control \"%s\": that name is already registered by \"%s\". Unregister it first, or register under another name.")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Descriptor.Name.ToString(), *Existing->DisplayName.ToString());
		return false;
	}
	// Validated here as well as in the Palette so a broken recipe is reported where the call stack
	// still names whoever registered it. Not a rejection: an invalid entry appears disabled carrying
	// this same reason, which beats vanishing. A prefab recipe is a question for the asset registry,
	// and asking it mid-scan -- which is where the defaults register from the constructor -- answers
	// "missing" about every one of them.
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (Descriptor.CreationKind != EDreamUIControlCreationKind::Prefab || (AssetRegistry != nullptr && !AssetRegistry->IsLoadingAssets()))
	{
		FText ValidationError;
		if (!Validate(Descriptor, ValidationError))
		{
			UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d Control \"%s\" will appear disabled in the Palette: %s")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Descriptor.Name.ToString(), *ValidationError.ToString());
		}
	}
	Descriptors.Add(Descriptor);
	RegistryChanged.Broadcast();
	return true;
}

bool FDreamUIControlRegistry::Unregister(FName Name)
{
	const bool bRemoved = Descriptors.RemoveAll([Name](const FDreamUIControlDescriptor& Descriptor) { return Descriptor.Name == Name; }) > 0;
	if (bRemoved)
	{
		RegistryChanged.Broadcast();
	}
	return bRemoved;
}

bool FDreamUIControlRegistry::Validate(const FDreamUIControlDescriptor& Descriptor, FText& OutError) const
{
	if (Descriptor.CreationKind == EDreamUIControlCreationKind::Prefab)
	{
		if (Descriptor.PrefabPath.IsEmpty() || !FPackageName::DoesPackageExist(Descriptor.PrefabPath))
		{
			OutError = FText::Format(LOCTEXT("MissingPrefab", "Missing control prefab: {0}"), FText::FromString(Descriptor.PrefabPath));
			return false;
		}
		TArray<FAssetData> PackageAssets;
		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		AssetRegistry.GetAssetsByPackageName(FName(Descriptor.PrefabPath), PackageAssets, true);
		const bool bContainsPrefab = PackageAssets.ContainsByPredicate([](const FAssetData& Asset)
		{
			return Asset.AssetClassPath == UDreamUIPrefab::StaticClass()->GetClassPathName();
		});
		// A registry that has not finished scanning knows nothing about the package's contents, and
		// answering "wrong type" to a question it cannot answer yet disabled every prefab-backed
		// control for the rest of the session. The package exists on disk; that is all we can say
		// until OnFilesLoaded, which the Palette re-validates on.
		if (!bContainsPrefab && !AssetRegistry.IsLoadingAssets())
		{
			OutError = FText::Format(LOCTEXT("WrongPrefabType", "Control resource is not a DreamUI Prefab: {0}"), FText::FromString(Descriptor.PrefabPath));
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
	if (Descriptor.LayoutContainerClass.IsValid() && !Descriptor.LayoutContainerClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
	{
		OutError = LOCTEXT("InvalidContainerClass", "LayoutContainerClass must derive from UDreamLayoutContainer.");
		return false;
	}
	if (Descriptor.VisualClass.IsValid() && !Descriptor.VisualClass->IsChildOf(UDreamVisual::StaticClass()))
	{
		OutError = LOCTEXT("InvalidVisualClass", "VisualClass must derive from UDreamVisual.");
		return false;
	}
	if (Descriptor.LayoutSelfClass.IsValid() && !Descriptor.LayoutSelfClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
	{
		OutError = LOCTEXT("InvalidLayoutSelfClass", "LayoutSelfClass must derive from UDreamLayoutSelf.");
		return false;
	}
	if (Descriptor.BehaviourClass.IsValid() && !Descriptor.BehaviourClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		OutError = LOCTEXT("InvalidBehaviourClass", "BehaviourClass must derive from UDreamUIBehaviour.");
		return false;
	}
	if (Descriptor.MeshModifierClass.IsValid() && !Descriptor.MeshModifierClass->IsChildOf(UDreamMeshModifierBase::StaticClass()))
	{
		OutError = LOCTEXT("InvalidMeshModifierClass", "MeshModifierClass must derive from UDreamMeshModifierBase.");
		return false;
	}
	if (Descriptor.MeshModifierClass.IsValid()
		&& (!Descriptor.VisualClass.IsValid() || !Descriptor.VisualClass->IsChildOf(UDreamVisualBatchMesh::StaticClass())))
	{
		OutError = LOCTEXT("MeshModifierRequiresVisual", "A mesh modifier requires a UDreamVisualBatchMesh visual class.");
		return false;
	}
	OutError = FText::GetEmpty();
	return true;
}

void FDreamUIControlRegistry::RegisterDefaults()
{
	using namespace DreamUIControlRegistryLocal;
	auto MakeFrameworkPanel = [](const TCHAR* Name, UClass* LayoutClass, const TCHAR* IconStyleName,
		bool /*bUMGPanel*/ = true, const TCHAR* DisplayName = nullptr)
	{
		FDreamUIControlDescriptor Descriptor = MakePanel(Name, LayoutClass, IconStyleName, DisplayName);
		if (!DisplayName)
		{
			// The class DisplayName carries the family prefix, so the palette label follows it
			// instead of the spaceless registry name.
			Descriptor.DisplayName = LayoutClass->GetDisplayNameText();
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

	Register(MakeFrameworkPanel(TEXT("CanvasPanel"), UDreamLayoutContainerCanvasPanel::StaticClass(), TEXT("ClassIcon.CanvasPanel"), true));
	Register(MakeFrameworkPanel(TEXT("Overlay"), UDreamLayoutContainerOverlay::StaticClass(), TEXT("ClassIcon.Overlay"), true));
	Register(MakeFrameworkPanel(TEXT("HorizontalBox"), UDreamLayoutContainerHorizontalBox::StaticClass(), TEXT("ClassIcon.HorizontalBox"), true));
	Register(MakeFrameworkPanel(TEXT("VerticalBox"), UDreamLayoutContainerVerticalBox::StaticClass(), TEXT("ClassIcon.VerticalBox"), true));
	Register(MakeFrameworkPanel(TEXT("StackBox"), UDreamLayoutContainerStackBox::StaticClass(), TEXT("ClassIcon.StackBox"), true));
	Register(MakeFrameworkPanel(TEXT("WrapBox"), UDreamLayoutContainerWrapBox::StaticClass(), TEXT("ClassIcon.WrapBox"), true));
	Register(MakeFrameworkPanel(TEXT("GridPanel"), UDreamLayoutContainerGridPanel::StaticClass(), TEXT("ClassIcon.GridPanel"), true));
	Register(MakeFrameworkPanel(TEXT("UniformGridPanel"), UDreamLayoutContainerUniformGridPanel::StaticClass(), TEXT("ClassIcon.UniformGridPanel"), true));
	FDreamUIControlDescriptor SafeZone = MakeFrameworkPanel(TEXT("SafeZone"), UDreamLayoutContainerSafeZone::StaticClass(), TEXT("ClassIcon.SafeZone"), true);
	SafeZone.BehaviourClass = UDreamContentWidget::StaticClass();
	Register(SafeZone);
	FDreamUIControlDescriptor ScaleBox = MakeFrameworkPanel(TEXT("ScaleBox"), UDreamLayoutContainerScaleBox::StaticClass(), TEXT("ClassIcon.ScaleBox"), true);
	ScaleBox.BehaviourClass = UDreamContentWidget::StaticClass();
	Register(ScaleBox);
	FDreamUIControlDescriptor SizeBox = MakeFrameworkPanel(TEXT("SizeBox"), UDreamLayoutContainerSizeBox::StaticClass(), TEXT("ClassIcon.Sizebox"), true);
	SizeBox.BehaviourClass = UDreamContentWidget::StaticClass();
	Register(SizeBox);
	Register(MakeFrameworkPanel(TEXT("WidgetSwitcher"), UDreamLayoutContainerWidgetSwitcher::StaticClass(), TEXT("ClassIcon.WidgetSwitcher"), true));
	Register(MakeFrameworkPanel(TEXT("LayoutScrollBox"), UDreamLayoutContainerScrollBox::StaticClass(), TEXT("ClassIcon.Scrollbox"), true));

	FDreamUIControlDescriptor ScrollBox = MakeBehaviour(TEXT("ScrollBox"), UUIScrollView::StaticClass(), TEXT("ClassIcon.Scrollbox"), ConfigureScrollBox);
	ScrollBox.DisplayName = FText::FromString(TEXT("Dream Scroll Box"));
	// The Dream scroll view is a behaviour, not a panel layout; keep it out of the panel list.
	ScrollBox.Category = TEXT("Legacy DreamGUI Panels");
	Register(ScrollBox);
	FDreamUIControlDescriptor Border = MakePanel(TEXT("Border"), UDreamLayoutContainerOverlay::StaticClass(), TEXT("ClassIcon.Border"));
	Border.VisualClass = UDreamImage::StaticClass();
	Border.BehaviourClass = UDreamContentWidget::StaticClass();
	Border.NativeConfigure = ConfigureImage;
	Register(Border);

	FDreamUIControlDescriptor Spacer;
	Spacer.Name = TEXT("Spacer");
	Spacer.DisplayName = FText::FromString(TEXT("Spacer"));
	Spacer.Category = TEXT("Primitive");
	Spacer.LayoutSelfClass = UDreamLayoutSelfSpacer::StaticClass();
	Spacer.Icon = MakeUMGIcon(TEXT("ClassIcon.Spacer"));
	Register(Spacer);

	FDreamUIControlDescriptor Progress = MakeBehaviour(TEXT("ProgressBar"), UUIProgressBar::StaticClass(), TEXT("ClassIcon.ProgressBar"), ConfigureProgressBar);
	Progress.VisualClass = UDreamImage::StaticClass();
	Register(Progress);
	Register(MakeBehaviour(TEXT("ContentWidget"), UDreamContentWidget::StaticClass(), TEXT("ClassIcon.NativeWidgetHost")));
	Register(MakeBehaviour(TEXT("NamedSlot"), UDreamNamedSlotHost::StaticClass(), TEXT("ClassIcon.NamedSlot")));
	Register(MakeBehaviour(TEXT("ListView"), UUIListView::StaticClass(), TEXT("ClassIcon.ListView"), ConfigureListView));
	Register(MakeBehaviour(TEXT("TileView"), UUITileView::StaticClass(), TEXT("ClassIcon.TileView"), ConfigureListView));
	Register(MakeBehaviour(TEXT("TreeView"), UUITreeView::StaticClass(), TEXT("ClassIcon.TreeView"), ConfigureListView));

	Register(MakeVisual(TEXT("Polygon"), TEXT("Polygon"), TEXT("Extensions"), UDreamPolygon::StaticClass()));
	Register(MakeVisual(TEXT("PolygonLine"), TEXT("Polygon Line"), TEXT("Extensions"), UDreamPolygonLine::StaticClass()));
	Register(MakeVisual(TEXT("Ring"), TEXT("Ring"), TEXT("Extensions"), UDreamRing::StaticClass()));
	Register(MakeVisual(TEXT("Line2DRaw"), TEXT("2D Line"), TEXT("Extensions"), UDream2DLineRaw::StaticClass()));
	Register(MakeVisual(TEXT("Line2DChildren"), TEXT("2D Line (Children as Points)"), TEXT("Extensions"), UDream2DLineChildrenAsPoints::StaticClass()));

	Register(MakeVisual(TEXT("BackgroundBlur"), TEXT("Background Blur"), TEXT("Post Process"), UDreamBackgroundBlur::StaticClass()));
	Register(MakeVisual(TEXT("BackgroundPixelate"), TEXT("Background Pixelate"), TEXT("Post Process"), UDreamBackgroundPixelate::StaticClass()));
	Register(MakeVisual(TEXT("PixelSort"), TEXT("Pixel Sort"), TEXT("Post Process"), UDreamPixelSort::StaticClass()));

	Register(MakeVisual(TEXT("VisualEmpty"), TEXT("Empty Visual"), TEXT("Advanced Visuals"), UDreamVisualEmpty::StaticClass()));
	Register(MakeVisual(TEXT("Texture"), TEXT("Texture"), TEXT("Advanced Visuals"), UDreamTexture::StaticClass()));
	Register(MakeVisual(TEXT("Sprite"), TEXT("Sprite"), TEXT("Advanced Visuals"), UDreamSprite::StaticClass()));
	Register(MakeVisual(TEXT("CustomMesh"), TEXT("Custom Mesh"), TEXT("Advanced Visuals"), UDreamCustomMesh::StaticClass()));
	Register(MakeVisual(TEXT("UMGWidget"), TEXT("UMG Widget"), TEXT("Advanced Visuals"), UDreamUMGWidget::StaticClass()));
	Register(MakeVisual(TEXT("CanvasRenderTargetPreviewer"), TEXT("Canvas Render Target Previewer"), TEXT("Advanced Rendering"), UDreamCanvasRenderTargetPreviewer::StaticClass()));
	Register(MakeVisual(TEXT("PostProcessRenderElement"), TEXT("Post Process Render Element"), TEXT("Advanced Rendering"), UDreamPostProcessRenderElement::StaticClass()));
	Register(MakeVisual(TEXT("PostProcessRenderElementText"), TEXT("Post Process Render Element (Text)"), TEXT("Advanced Rendering"), UDreamPostProcessRenderElement_Text::StaticClass()));
	Register(MakeVisual(TEXT("StaticMeshExperimental"), TEXT("Static Mesh (Experimental)"), TEXT("Experimental"), UDreamStaticMesh::StaticClass()));

	Register(MakeComponent(TEXT("DataBinding"), TEXT("Data Binding"), UDreamDataBinding::StaticClass()));
	Register(MakeComponent(TEXT("ResponsiveBehaviour"), TEXT("Responsive Behaviour"), UDreamResponsiveBehaviour::StaticClass()));
	Register(MakeComponent(TEXT("EventBlocker"), TEXT("Event Blocker"), UUIEventBlocker::StaticClass()));
	Register(MakeComponent(TEXT("EventTrigger"), TEXT("Event Trigger"), UUIEventTrigger::StaticClass()));
	Register(MakeComponent(TEXT("NavigationInputSelection"), TEXT("Navigation Input Selection"), UUINavigationInputSelectionHandler::StaticClass()));
	Register(MakeComponent(TEXT("SpriteSequencePlayer"), TEXT("Sprite Sequence Player"), UUISpriteSequencePlayer::StaticClass(), UDreamSprite::StaticClass()));
	Register(MakeComponent(TEXT("SpriteSheetTexturePlayer"), TEXT("Sprite Sheet Texture Player"), UUISpriteSheetTexturePlayer::StaticClass(), UDreamTexture::StaticClass()));
	Register(MakeComponent(TEXT("UMGWidgetInteraction"), TEXT("UMG Widget Interaction"), UDreamUMGWidgetInteraction::StaticClass(), UDreamUMGWidget::StaticClass()));

	Register(MakeLayoutSelf(TEXT("AspectRatioLayout"), TEXT("Aspect Ratio"), UDreamLayoutSelfAspectRatio::StaticClass()));

	Register(MakeMeshModifier(TEXT("GradientColorModifier"), TEXT("Gradient Color"), UDreamMeshModifierGradientColor::StaticClass(), UDreamImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("LongShadowModifier"), TEXT("Long Shadow"), UDreamMeshModifierLongShadow::StaticClass(), UDreamImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("OutlineModifier"), TEXT("Outline"), UDreamMeshModifierOutline::StaticClass(), UDreamImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("PositionAsUVModifier"), TEXT("Position as UV"), UDreamMeshModifierPositionAsUV::StaticClass(), UDreamImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("ShadowModifier"), TEXT("Shadow"), UDreamMeshModifierShadow::StaticClass(), UDreamImage::StaticClass(), ConfigureImage));
	Register(MakeMeshModifier(TEXT("TextAnimationModifier"), TEXT("Text Animation"), UDreamMeshModifierTextAnimation::StaticClass(), UDreamText::StaticClass()));
}

#undef LOCTEXT_NAMESPACE
