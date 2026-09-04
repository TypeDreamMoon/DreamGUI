// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIControlRegistry.h"
// Explicit: this used to arrive through whichever designer header the unity blob happened to pull
// in first, which is not a dependency, it is a coincidence.
#include "DreamWidgetBlueprint.h"
#include "Core/DreamGUISettings.h"

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
#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamExpandableArea.h"
#include "Controls/DreamInputKeySelector.h"
#include "Controls/DreamRadioButton.h"
#include "Controls/DreamRingMenu.h"
#include "Controls/DreamScrollBar.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamSpinBox.h"
#include "Controls/DreamTabView.h"
#include "Controls/DreamTextInput.h"
#include "Controls/DreamToggle.h"
#include "Interaction/UIToggleGroup.h"
#include "Controls/DreamListView.h"
#include "Controls/DreamProgressBar.h"
#include "Controls/DreamTreeView.h"
#include "Core/DreamUserWidget.h"
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
#include "AssetRegistry/IAssetRegistry.h"
#include "DreamGUIEditorModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Misc/CoreDelegates.h"
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
		// Same guard the other native recipes carry: if the behaviour did not go on, bail before
		// building parts nothing will own -- rather than dereferencing null two lines down.
		if (!Progress)
		{
			return;
		}
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

	/*
	 * The property writes that turn one control class into two palette rows.
	 *
	 * Each of these replaces a Blueprint preset that existed ONLY to hold this one value -- an asset
	 * cannot branch on a property, so "horizontal" and "vertical" had to be two files. They are
	 * still two rows, because that is how an author looks for them, but the difference is now one
	 * line of code instead of a second asset to keep in step.
	 *
	 * Written on the TEMPLATE, before anything observes it -- which is why a bare assignment is
	 * enough and none of these needs a setter. They are plain public UPROPERTYs, so the value
	 * serialises with the widget, and the control reads it in RealizeBuiltIn, which has not run
	 * yet at this point.
	 */
	static void ConfigureSliderHorizontal(UDreamWidget* Root)
	{
		if (UDreamSlider* Slider = Cast<UDreamSlider>(Root))
		{
			Slider->Direction = EUISliderDirectionType::LeftToRight;
		}
	}

	static void ConfigureSliderVertical(UDreamWidget* Root)
	{
		if (UDreamSlider* Slider = Cast<UDreamSlider>(Root))
		{
			Slider->Direction = EUISliderDirectionType::BottomToTop;
		}
	}

	static void ConfigureScrollBarHorizontal(UDreamWidget* Root)
	{
		if (UDreamScrollBar* Bar = Cast<UDreamScrollBar>(Root))
		{
			Bar->Direction = EUIScrollbarDirectionType::LeftToRight;
		}
	}

	static void ConfigureScrollBarVertical(UDreamWidget* Root)
	{
		if (UDreamScrollBar* Bar = Cast<UDreamScrollBar>(Root))
		{
			Bar->Direction = EUIScrollbarDirectionType::TopToBottom;
		}
	}

	static void ConfigureScrollBoxHorizontal(UDreamWidget* Root)
	{
		if (UDreamScrollBox* Box = Cast<UDreamScrollBox>(Root))
		{
			Box->Orientation = EDreamPanelOrientation::Horizontal;
		}
	}

	static void ConfigureScrollBoxVertical(UDreamWidget* Root)
	{
		if (UDreamScrollBox* Box = Cast<UDreamScrollBox>(Root))
		{
			Box->Orientation = EDreamPanelOrientation::Vertical;
		}
	}

	static void ConfigureTextInputMultiline(UDreamWidget* Root)
	{
		if (UDreamTextInput* Input = Cast<UDreamTextInput>(Root))
		{
			Input->bMultiLine = true;
		}
	}

	static void ConfigureImage(UDreamWidget* Root)
	{
		if (UDreamImage* Image = Cast<UDreamImage>(Root->GetVisual()))
		{
			Image->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultFrameRect());
		}
	}

	/**
	 * Where an entry goes when the control library has taken over its job.
	 *
	 * Kept in the palette rather than deleted: existing assets reference these behaviours, and an
	 * entry that vanishes takes with it the only way to understand what an old widget is made of.
	 * A category is the honest amount of discouragement -- findable on purpose, not by accident.
	 * Sibling of "Legacy DreamGUI Panels", which the Dream scroll view already sits in.
	 */
	static const TCHAR* LegacyControlsCategory = TEXT("Legacy DreamGUI Controls");

	static FSlateIcon MakeUMGIcon(const TCHAR* StyleName)
	{
		return FSlateIcon(FUMGStyle::GetStyleSetName(), FName(StyleName));
	}

	static FSlateIcon MakeClassIcon(UClass* Class)
	{
		return FSlateIconFinder::FindIconForClass(Class);
	}

	static FDreamUIControlDescriptor MakeControl(const TCHAR* Name, const TCHAR* DisplayName, const TCHAR* AssetName, const TCHAR* IconStyleName)
	{
		FDreamUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Controls");
		Result.CreationKind = EDreamUIControlCreationKind::WidgetClass;
		Result.WidgetClassPath = UDreamGUISettings::Get()->PresetControlFolder + TEXT("BP_") + AssetName;
		Result.Icon = MakeUMGIcon(IconStyleName);
		return Result;
	}

	/**
	 * A control whose hierarchy is code: the palette entry for a `Native.X` tag.
	 *
	 * The name is the registry key and stays spaceless like every other; DisplayName is what the
	 * row reads. Both are given rather than derived from the class, because the class is called
	 * UDreamListView and the tag is Native.List -- the language's name for a control is the one an
	 * author knows it by, and the palette should agree with the language.
	 */
	static FDreamUIControlDescriptor MakeControlClass(const TCHAR* Name, const TCHAR* DisplayName,
		UClass* ControlClass, const TCHAR* IconStyleName, TFunction<void(UDreamWidget*)> Configure = nullptr)
	{
		FDreamUIControlDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = FText::FromString(DisplayName);
		Result.Category = TEXT("Controls");
		Result.CreationKind = EDreamUIControlCreationKind::ControlClass;
		Result.ControlClass = ControlClass;
		Result.Icon = MakeUMGIcon(IconStyleName);
		Result.NativeConfigure = MoveTemp(Configure);
		return Result;
	}

	/**
	 * The Blueprint preset a native control replaced, kept in the palette and moved out of the way.
	 *
	 * Not deleted, and the asset is not touched either: a project has these dropped in levels and in
	 * prefabs, and an entry that vanishes takes with it the only way to recognise what an existing
	 * widget was made from. The label says what it is so the two rows are never confused.
	 */
	static FDreamUIControlDescriptor MakeLegacyPreset(const TCHAR* Name, const TCHAR* DisplayName,
		const TCHAR* AssetName, const TCHAR* IconStyleName)
	{
		FDreamUIControlDescriptor Result = MakeControl(Name, DisplayName, AssetName, IconStyleName);
		Result.DisplayName = FText::FromString(FString::Printf(TEXT("%s (Prefab)"), DisplayName));
		Result.Category = LegacyControlsCategory;
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
	// This runs from StartupModule, and DreamGUIEditor loads in the Default phase -- which is BEFORE
	// the editor engine object exists. Hooking the compile broadcast here therefore never happened at
	// all, and nothing retried it, so a post-process Blueprint compiled during the session never
	// reached the palette and a recompiled one left the descriptor pointing at the REINST_ class.
	// Wait for the engine when it is not up yet; GEditor is assigned before this fires.
	BindBlueprintCompiled();
	if (!BlueprintCompiledHandle.IsValid() && !PostEngineInitHandle.IsValid())
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FDreamUIControlRegistry::HandlePostEngineInit);
	}
	if (!AssetLoadedHandle.IsValid())
	{
		AssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FDreamUIControlRegistry::HandleAssetLoaded);
	}
}

void FDreamUIControlRegistry::BindBlueprintCompiled()
{
	if (GEditor && !BlueprintCompiledHandle.IsValid())
	{
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FDreamUIControlRegistry::RefreshDynamicClasses);
	}
}

void FDreamUIControlRegistry::HandlePostEngineInit()
{
	BindBlueprintCompiled();
	// Classes that came up while the hook did not exist yet are already in memory, so the palette
	// has to be caught up once rather than waiting for the next compile.
	RefreshDynamicClasses();
}

void FDreamUIControlRegistry::ShutdownDynamicDiscovery()
{
	if (GEditor && BlueprintCompiledHandle.IsValid())
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
	}
	BlueprintCompiledHandle.Reset();
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	}
	PostEngineInitHandle.Reset();
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
	if (Descriptor.CreationKind != EDreamUIControlCreationKind::WidgetClass || (AssetRegistry != nullptr && !AssetRegistry->IsLoadingAssets()))
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
	if (Descriptor.CreationKind == EDreamUIControlCreationKind::WidgetClass)
	{
		if (Descriptor.WidgetClassPath.IsEmpty() || !FPackageName::DoesPackageExist(Descriptor.WidgetClassPath))
		{
			OutError = FText::Format(LOCTEXT("MissingControlClass", "Missing control class: {0}"), FText::FromString(Descriptor.WidgetClassPath));
			return false;
		}
		TArray<FAssetData> PackageAssets;
		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		AssetRegistry.GetAssetsByPackageName(FName(Descriptor.WidgetClassPath), PackageAssets, true);
		const bool bContainsWidgetBlueprint = PackageAssets.ContainsByPredicate([](const FAssetData& Asset)
		{
			return Asset.AssetClassPath == UDreamWidgetBlueprint::StaticClass()->GetClassPathName();
		});
		// A registry that has not finished scanning knows nothing about the package's contents, and
		// answering "wrong type" to a question it cannot answer yet disabled every prefab-backed
		// control for the rest of the session. The package exists on disk; that is all we can say
		// until OnFilesLoaded, which the Palette re-validates on.
		if (!bContainsWidgetBlueprint && !AssetRegistry.IsLoadingAssets())
		{
			OutError = FText::Format(LOCTEXT("WrongControlType", "Control resource is not a DreamUI Widget Blueprint: {0}"), FText::FromString(Descriptor.WidgetClassPath));
			return false;
		}
	}
	else if (Descriptor.CreationKind == EDreamUIControlCreationKind::ControlClass)
	{
		// Everything a placement needs to succeed, asked here rather than discovered by
		// CreateRegisteredControlClassAndReturn logging a failure the author cannot connect to a
		// registration. Abstract is the one worth spelling: UDreamUIControl itself is abstract, and
		// a family base slipping into the palette places a control with no tree.
		if (!Descriptor.ControlClass.IsValid())
		{
			OutError = LOCTEXT("MissingControlClassObject", "The control has no class to instance.");
			return false;
		}
		if (!Descriptor.ControlClass->IsChildOf(UDreamUserWidget::StaticClass()))
		{
			OutError = FText::Format(LOCTEXT("ControlClassNotUserWidget",
				"ControlClass must derive from UDreamUserWidget, and {0} does not."),
				FText::FromString(Descriptor.ControlClass->GetName()));
			return false;
		}
		if (Descriptor.ControlClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			OutError = FText::Format(LOCTEXT("ControlClassNotConcrete",
				"{0} cannot be instanced."), FText::FromString(Descriptor.ControlClass->GetName()));
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
	// THE CONTROL LIBRARY IS THE CONTROLS CATEGORY.
	//
	// Every row here was a Blueprint preset under /DreamGUI/Controls until now, and the presets are
	// still registered -- in the legacy category, at the bottom of this function. What changed is
	// which one an author reaches by default, and the reasons are not stylistic:
	//
	//   - A preset is an ASSET, so a defect in one is a defect in every copy already placed, and
	//     fixing it reaches none of them. Two shipped broken for months with nothing saying so:
	//     BP_Button carries no UIButton at all, and BP_VerticalScrollbar no UIScrollbar. A control
	//     cannot be half-built -- WireParts puts the behaviour on, and a test sweeps every class.
	//   - A preset cannot branch on a property, which is why there were two sliders, two scrollbars,
	//     two scroll boxes and two text inputs. One class with a Direction covers each pair, and the
	//     two palette rows survive as two ENTRIES that differ by one property write.
	//   - A control reads the project style sheet, so a project restyles every button at once.
	//
	// Names keep their registry keys ("Button", "CheckBox") because those are what a favourite, a
	// layout preference and any project extension already refer to. The legacy rows below take
	// suffixed keys instead, since the pair has to coexist.
	Register(MakeControlClass(TEXT("Button"), TEXT("Button"),
		UDreamButton::StaticClass(), TEXT("ClassIcon.Button")));
	Register(MakeControlClass(TEXT("CheckBox"), TEXT("Check Box"),
		UDreamToggle::StaticClass(), TEXT("ClassIcon.CheckBox")));
	Register(MakeControlClass(TEXT("RadioButton"), TEXT("Radio Button"),
		UDreamRadioButton::StaticClass(), TEXT("ClassIcon.CheckBox")));
	Register(MakeControlClass(TEXT("ComboBox"), TEXT("Combo Box"),
		UDreamDropdown::StaticClass(), TEXT("ClassIcon.ComboBox")));
	Register(MakeControlClass(TEXT("SpinBox"), TEXT("Spin Box"),
		UDreamSpinBox::StaticClass(), TEXT("ClassIcon.SpinBox")));

	Register(MakeControlClass(TEXT("HorizontalSlider"), TEXT("Horizontal Slider"),
		UDreamSlider::StaticClass(), TEXT("ClassIcon.Slider"), ConfigureSliderHorizontal));
	Register(MakeControlClass(TEXT("VerticalSlider"), TEXT("Vertical Slider"),
		UDreamSlider::StaticClass(), TEXT("ClassIcon.Slider"), ConfigureSliderVertical));
	Register(MakeControlClass(TEXT("HorizontalScrollbar"), TEXT("Horizontal Scrollbar"),
		UDreamScrollBar::StaticClass(), TEXT("ClassIcon.ScrollBar"), ConfigureScrollBarHorizontal));
	Register(MakeControlClass(TEXT("VerticalScrollbar"), TEXT("Vertical Scrollbar"),
		UDreamScrollBar::StaticClass(), TEXT("ClassIcon.ScrollBar"), ConfigureScrollBarVertical));
	Register(MakeControlClass(TEXT("HorizontalScrollView"), TEXT("Horizontal Scroll Box"),
		UDreamScrollBox::StaticClass(), TEXT("ClassIcon.Scrollbox"), ConfigureScrollBoxHorizontal));
	Register(MakeControlClass(TEXT("VerticalScrollView"), TEXT("Vertical Scroll Box"),
		UDreamScrollBox::StaticClass(), TEXT("ClassIcon.Scrollbox"), ConfigureScrollBoxVertical));
	Register(MakeControlClass(TEXT("TextInput"), TEXT("Text Input"),
		UDreamTextInput::StaticClass(), TEXT("ClassIcon.EditableTextBox")));
	Register(MakeControlClass(TEXT("TextInputMultiline"), TEXT("Text Input (Multiline)"),
		UDreamTextInput::StaticClass(), TEXT("ClassIcon.MultilineEditableTextBox"), ConfigureTextInputMultiline));

	Register(MakeControlClass(TEXT("TabView"), TEXT("Tab View"),
		UDreamTabView::StaticClass(), TEXT("ClassIcon.WidgetSwitcher")));
	Register(MakeControlClass(TEXT("ExpandableArea"), TEXT("Expandable Area"),
		UDreamExpandableArea::StaticClass(), TEXT("ClassIcon.ExpandableArea")));
	Register(MakeControlClass(TEXT("Dialog"), TEXT("Dialog"),
		UDreamDialog::StaticClass(), TEXT("ClassIcon.NamedSlot")));
	// No ClassIcon.InputKeySelector in the UMG style set -- the icon sweep found that, which is what
	// it is for. FindIconForClass always resolves, falling back to the generic class icon.
	FDreamUIControlDescriptor InputKeySelector = MakeControlClass(TEXT("InputKeySelector"), TEXT("Input Key Selector"),
		UDreamInputKeySelector::StaticClass(), TEXT("ClassIcon.Button"));
	InputKeySelector.Icon = MakeClassIcon(UDreamInputKeySelector::StaticClass());
	Register(InputKeySelector);
	Register(MakeControlClass(TEXT("RingMenu"), TEXT("Ring Menu"),
		UDreamRingMenu::StaticClass(), TEXT("ClassIcon.Border")));

	// The toggle group is the one entry with no control of its own, because it needs none: it is a
	// behaviour that a set of toggles points at, with nothing to draw and no parts to build. The
	// preset existed only because a palette had no way to offer a bare component. This one does.
	FDreamUIControlDescriptor ToggleGroup = MakeComponent(TEXT("ToggleGroup"), TEXT("Toggle Group"), UUIToggleGroup::StaticClass());
	// In Controls rather than the Components category MakeComponent defaults to: this row REPLACES a
	// preset that lived in Controls, and an author who goes looking for a toggle group where they
	// have always found one should not have to know it stopped being a prefab.
	ToggleGroup.Category = TEXT("Controls");
	Register(ToggleGroup);

	Register(MakeFrameworkPanel(TEXT("CanvasPanel"), UDreamLayoutContainerCanvasPanel::StaticClass(), TEXT("ClassIcon.CanvasPanel"), true));
	Register(MakeFrameworkPanel(TEXT("Overlay"), UDreamLayoutContainerOverlay::StaticClass(), TEXT("ClassIcon.Overlay"), true));
	Register(MakeFrameworkPanel(TEXT("HorizontalBox"), UDreamLayoutContainerHorizontalBox::StaticClass(), TEXT("ClassIcon.HorizontalBox"), true));
	Register(MakeFrameworkPanel(TEXT("VerticalBox"), UDreamLayoutContainerVerticalBox::StaticClass(), TEXT("ClassIcon.VerticalBox"), true));
	Register(MakeFrameworkPanel(TEXT("StackBox"), UDreamLayoutContainerStackBox::StaticClass(), TEXT("ClassIcon.StackBox"), true));
	Register(MakeFrameworkPanel(TEXT("WrapBox"), UDreamLayoutContainerWrapBox::StaticClass(), TEXT("ClassIcon.WrapBox"), true));
	Register(MakeFrameworkPanel(TEXT("GridPanel"), UDreamLayoutContainerGridPanel::StaticClass(), TEXT("ClassIcon.GridPanel"), true));
	Register(MakeFrameworkPanel(TEXT("UniformGridPanel"), UDreamLayoutContainerUniformGridPanel::StaticClass(), TEXT("ClassIcon.UniformGridPanel"), true));
	// SafeZone / ScaleBox / SizeBox get their ContentWidget from UDreamLayoutContainer::GetRequiredBehaviourClasses
	// when CreateNewLayoutContainer runs; listing it here too would add a second one.
	Register(MakeFrameworkPanel(TEXT("SafeZone"), UDreamLayoutContainerSafeZone::StaticClass(), TEXT("ClassIcon.SafeZone"), true));
	Register(MakeFrameworkPanel(TEXT("ScaleBox"), UDreamLayoutContainerScaleBox::StaticClass(), TEXT("ClassIcon.ScaleBox"), true));
	Register(MakeFrameworkPanel(TEXT("SizeBox"), UDreamLayoutContainerSizeBox::StaticClass(), TEXT("ClassIcon.Sizebox"), true));
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

	// THE CONTROL LIBRARY IN THE PALETTE, and the three entries that had two implementations.
	//
	// Each of these names a control the library already had and the palette was still offering the
	// LGUI-era behaviour for. They are not two ways of spelling one thing: the control builds and
	// owns its parts and reads the project style sheet, and the behaviour is a component you drop
	// on a widget whose parts you then wire yourself. Registering the control here and moving the
	// behaviour to the legacy category below is what makes the palette agree with `.dui`, where
	// `Native.ProgressBar` has been the answer since the library landed.
	//
	// Only three, deliberately. The other fourteen controls have a BP preset in this category that
	// they also replace, and retiring THOSE is a separate change with its own decisions to make.
	Register(MakeControlClass(TEXT("NativeProgressBar"), TEXT("Progress Bar"),
		UDreamProgressBar::StaticClass(), TEXT("ClassIcon.ProgressBar")));
	Register(MakeControlClass(TEXT("NativeList"), TEXT("List"),
		UDreamListView::StaticClass(), TEXT("ClassIcon.ListView")));
	Register(MakeControlClass(TEXT("NativeTreeView"), TEXT("Tree View"),
		UDreamTreeView::StaticClass(), TEXT("ClassIcon.TreeView")));

	FDreamUIControlDescriptor Progress = MakeBehaviour(TEXT("ProgressBar"), UUIProgressBar::StaticClass(), TEXT("ClassIcon.ProgressBar"), ConfigureProgressBar);
	Progress.VisualClass = UDreamImage::StaticClass();
	Progress.DisplayName = FText::FromString(TEXT("Progress Bar (Behaviour)"));
	Progress.Category = LegacyControlsCategory;
	Register(Progress);
	Register(MakeBehaviour(TEXT("ContentWidget"), UDreamContentWidget::StaticClass(), TEXT("ClassIcon.NativeWidgetHost")));
	// The hole a widget blueprint opens for whoever places it. The one below is the older, unrelated
	// thing with a confusingly similar name: a runtime name->child map inside one hierarchy.
	Register(MakeBehaviour(TEXT("NamedSlot"), UDreamNamedSlot::StaticClass(), TEXT("ClassIcon.NamedSlot")));
	Register(MakeBehaviour(TEXT("NamedSlotHost"), UDreamNamedSlotHost::StaticClass(), TEXT("ClassIcon.NamedSlot")));
	FDreamUIControlDescriptor ListView = MakeBehaviour(TEXT("ListView"), UUIListView::StaticClass(), TEXT("ClassIcon.ListView"), ConfigureListView);
	ListView.DisplayName = FText::FromString(TEXT("List View (Behaviour)"));
	ListView.Category = LegacyControlsCategory;
	Register(ListView);
	// NOT moved, because nothing replaces it: the control library has List and TreeView and no tile
	// view, so this is the only way to get one. It is the same recycling stack as the other two and
	// carries the same caveats; it is here rather than in the legacy category because retiring an
	// entry with no successor is just deleting a feature.
	Register(MakeBehaviour(TEXT("TileView"), UUITileView::StaticClass(), TEXT("ClassIcon.TileView"), ConfigureListView));
	FDreamUIControlDescriptor TreeView = MakeBehaviour(TEXT("TreeView"), UUITreeView::StaticClass(), TEXT("ClassIcon.TreeView"), ConfigureListView);
	TreeView.DisplayName = FText::FromString(TEXT("Tree View (Behaviour)"));
	TreeView.Category = LegacyControlsCategory;
	Register(TreeView);

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

	// THE TWELVE BLUEPRINT PRESETS, retired into the legacy category.
	//
	// Registered last, so they read as the footnote they now are. The keys carry a suffix because
	// the control rows above took the plain ones -- a favourite or a layout preference pointing at
	// "Button" should follow the control, which is what an author who saved it meant.
	//
	// The ASSETS are untouched, deliberately: /DreamGUI/Controls still ships them, anything already
	// placed still loads, and BP_NavigationSelectionInputHandler was never a palette entry at all.
	// Two of these are known broken and stay that way -- BP_Button has no UIButton and
	// BP_VerticalScrollbar no UIScrollbar -- because repairing an asset does not reach the copies a
	// project has already placed, and the row above is the repair.
	Register(MakeLegacyPreset(TEXT("ButtonPreset"), TEXT("Button"), TEXT("Button"), TEXT("ClassIcon.Button")));
	Register(MakeLegacyPreset(TEXT("CheckBoxPreset"), TEXT("Check Box"), TEXT("Toggle"), TEXT("ClassIcon.CheckBox")));
	Register(MakeLegacyPreset(TEXT("ToggleGroupPreset"), TEXT("Toggle Group"), TEXT("ToggleGroup"), TEXT("ClassIcon.CheckBox")));
	Register(MakeLegacyPreset(TEXT("HorizontalSliderPreset"), TEXT("Horizontal Slider"), TEXT("HorizontalSlider"), TEXT("ClassIcon.Slider")));
	Register(MakeLegacyPreset(TEXT("VerticalSliderPreset"), TEXT("Vertical Slider"), TEXT("VerticalSlider"), TEXT("ClassIcon.Slider")));
	Register(MakeLegacyPreset(TEXT("HorizontalScrollbarPreset"), TEXT("Horizontal Scrollbar"), TEXT("HorizontalScrollbar"), TEXT("ClassIcon.ScrollBar")));
	Register(MakeLegacyPreset(TEXT("VerticalScrollbarPreset"), TEXT("Vertical Scrollbar"), TEXT("VerticalScrollbar"), TEXT("ClassIcon.ScrollBar")));
	Register(MakeLegacyPreset(TEXT("ComboBoxPreset"), TEXT("Combo Box"), TEXT("Dropdown"), TEXT("ClassIcon.ComboBox")));
	Register(MakeLegacyPreset(TEXT("TextInputPreset"), TEXT("Text Input"), TEXT("TextInput"), TEXT("ClassIcon.EditableTextBox")));
	Register(MakeLegacyPreset(TEXT("TextInputMultilinePreset"), TEXT("Text Input (Multiline)"), TEXT("TextInput_Multiline"), TEXT("ClassIcon.MultilineEditableTextBox")));
	Register(MakeLegacyPreset(TEXT("HorizontalScrollViewPreset"), TEXT("Horizontal Scroll Box"), TEXT("HorizontalScrollView"), TEXT("ClassIcon.Scrollbox")));
	Register(MakeLegacyPreset(TEXT("VerticalScrollViewPreset"), TEXT("Vertical Scroll Box"), TEXT("VerticalScrollView"), TEXT("ClassIcon.Scrollbox")));
}

#undef LOCTEXT_NAMESPACE
