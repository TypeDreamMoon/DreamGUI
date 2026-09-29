// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Driver/Designer/DreamDesignerDriver.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamWidgetBlueprint.h"

#include "Framework/Application/SlateApplication.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

namespace DreamTests
{
	FDesignerTestAsset CreateDesignerTestAsset(const TCHAR* InName, bool bGiveRootAPanel)
	{
		FDesignerTestAsset Asset;
		const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s_%s"),
			InName, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Asset.Package = CreatePackage(*PackageName);
		if (Asset.Package == nullptr)
		{
			return Asset;
		}
		Asset.Package->AddToRoot();
		Asset.Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UDreamUserWidget::StaticClass(), Asset.Package, FName(InName), BPTYPE_Normal,
			UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		if (Asset.Blueprint == nullptr)
		{
			return Asset;
		}
		UDreamWidgetTree* Tree = Asset.Blueprint->GetOrCreateWidgetTree();
		Tree->RootWidget->SetDisplayName(TEXT("Root"));
		if (bGiveRootAPanel)
		{
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
		}
		FKismetEditorUtilities::CompileBlueprint(Asset.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		return Asset;
	}

	void ReleaseDesignerTestAsset(FDesignerTestAsset& InOutAsset)
	{
		if (InOutAsset.Package != nullptr)
		{
			InOutAsset.Package->RemoveFromRoot();
		}
		InOutAsset.Package = nullptr;
		InOutAsset.Blueprint = nullptr;
	}

	UDreamWidget* DesignerTemplateRoot(const UDreamWidgetBlueprint* InBlueprint)
	{
		return ::IsValid(InBlueprint) && ::IsValid(InBlueprint->WidgetTree)
			? InBlueprint->WidgetTree->RootWidget.Get()
			: nullptr;
	}

	UDreamWidget* FirstLiveChildOf(const UDreamWidget* InParent)
	{
		if (!::IsValid(InParent))
		{
			return nullptr;
		}
		for (UDreamWidget* Child : InParent->GetChildren())
		{
			if (::IsValid(Child))
			{
				return Child;
			}
		}
		return nullptr;
	}

	int32 CountDescendantsOfClass(const UDreamWidget* InRoot, const UClass* InClass)
	{
		if (!::IsValid(InRoot) || InClass == nullptr)
		{
			return 0;
		}
		int32 Count = 0;
		for (const UDreamWidget* Child : InRoot->GetChildren())
		{
			if (!::IsValid(Child))
			{
				continue;
			}
			if (Child->IsA(InClass))
			{
				++Count;
			}
			Count += CountDescendantsOfClass(Child, InClass);
		}
		return Count;
	}

	bool HasNullDescendantUnder(const UDreamWidget* InRoot)
	{
		if (!::IsValid(InRoot))
		{
			return false;
		}
		for (const UDreamWidget* Child : InRoot->GetChildren())
		{
			if (!::IsValid(Child) || HasNullDescendantUnder(Child))
			{
				return true;
			}
		}
		return false;
	}

	TArray<UDreamWidget*> LiveChildrenOf(const UDreamWidget* InParent)
	{
		TArray<UDreamWidget*> Children;
		if (::IsValid(InParent))
		{
			for (UDreamWidget* Child : InParent->GetChildren())
			{
				if (::IsValid(Child))
				{
					Children.Add(Child);
				}
			}
		}
		return Children;
	}

	UDreamWidget* DropOntoRootAndFindTemplate(FDreamDesignerDriver& InDriver, UClass* InWidgetClass, FIntPoint InPixel)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		UDreamWidget* TemplateRoot = Toolkit != nullptr ? DesignerTemplateRoot(Toolkit->GetWidgetBlueprint()) : nullptr;
		if (TemplateRoot == nullptr)
		{
			return nullptr;
		}
		const TArray<UDreamWidget*> Before = LiveChildrenOf(TemplateRoot);
		if (!InDriver.DropFromPalette(InWidgetClass, InPixel))
		{
			return nullptr;
		}
		for (UDreamWidget* Child : LiveChildrenOf(TemplateRoot))
		{
			if (!Before.Contains(Child))
			{
				return Child;
			}
		}
		return nullptr;
	}

	FScopedDesignerSession::FScopedDesignerSession(const TCHAR* InName, bool bGiveRootAPanel, FIntPoint InViewportSize)
	{
		Asset = CreateDesignerTestAsset(InName, bGiveRootAPanel);
		if (!Asset.IsValid())
		{
			Failure = TEXT("the Widget Blueprint could not be created");
			return;
		}
		TSharedPtr<FDreamDesignerDriver> Opened = FDreamDesignerDriver::Open(Asset.Blueprint);
		if (!Opened.IsValid())
		{
			Failure = TEXT("the designer did not open, or opened without a viewport (see LogDreamDesignerDriver)");
			return;
		}
		if (!Opened->EnsureHeadlessSize(InViewportSize))
		{
			Failure = FString::Printf(TEXT("the designer viewport could not be given a size; it measures %dx%d"),
				Opened->ViewportPixelSize().X, Opened->ViewportPixelSize().Y);
			// The same close the destructor makes, in the same order, since the destructor will not see
			// a driver to close.
			Opened->Close();
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().Tick();
			}
			return;
		}
		// One frame, so the preview world has ticked once with the size it now has before anything is
		// asked of its geometry.
		Opened->PumpFrame();
		Driver = Opened;
	}

	FScopedDesignerSession::~FScopedDesignerSession()
	{
		if (Driver.IsValid())
		{
			Driver->Close();
			Driver.Reset();
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().Tick();
			}
		}
		ReleaseDesignerTestAsset(Asset);
	}
}
