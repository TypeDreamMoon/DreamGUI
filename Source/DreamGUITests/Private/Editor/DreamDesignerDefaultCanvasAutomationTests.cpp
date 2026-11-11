// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIAnchorData.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"

#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/*
 * The canvas a designer opens on when nobody has chosen one.
 *
 * A card of 560 by 236 used to open as a speck in the middle of 1920 by 1080, and the author had to type a size into the
 * toolbar before they could see what they had made. The canvas now follows a root that has a size of its own, but only
 * while nothing has picked one: a chosen size is the author's, whatever the file says afterwards. A root that fills its
 * host has no size to follow, and keeps the default it is laid out against.
 */

namespace DreamDesignerDefaultCanvasTestLocal
{
	/** A plain Widget Blueprint with a root, in a package of its own under /Temp. */
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName, UClass* InParent = UDreamUserWidget::StaticClass())
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s_%s"), InName,
				*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			if (Package == nullptr)
			{
				return;
			}
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParent, Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		/** The root as the file would have built it: InSize, centred (not stretched) unless InStretched. */
		void SetRoot(FVector2D InSize, bool bInStretched = false) const
		{
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			FDreamUIAnchorData Anchors;
			Anchors.SizeDelta = InSize;
			if (bInStretched)
			{
				Anchors.AnchorMin = FVector2D(0.0, 0.0);
				Anchors.AnchorMax = FVector2D(1.0, 1.0);
			}
			Tree->RootWidget->SetAnchorData(Anchors);
		}
	};

	/** A .dui under Saved/, where no DUI root is and so no source watcher looks; removed with the object. */
	struct FScopedDuiFile
	{
		FString Path;

		FScopedDuiFile(const TCHAR* InName, const TArray<FString>& InLines)
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"),
				FString::Printf(TEXT("%s_%s.dui"), InName, *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			FPaths::NormalizeFilename(Path);
			if (!FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *Path))
			{
				Path.Reset();
			}
		}

		~FScopedDuiFile()
		{
			if (!Path.IsEmpty())
			{
				IFileManager::Get().Delete(*Path, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerCanvasFollowsASizedRootTest,
	"DreamGUI.Designer.AnUnchosenCanvasTakesTheSizeOfARootThatHasOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerCanvasFollowsASizedRootTest::RunTest(const FString&)
{
	using namespace DreamDesignerDefaultCanvasTestLocal;
	const FIntPoint Default = FDreamWidgetDesignerData().CanvasSize;

	{
		FScopedBlueprint Scoped(TEXT("CanvasSizedRoot"));
		if (!TestNotNull(TEXT("The Blueprint was made"), Scoped.Blueprint))
		{
			return false;
		}
		Scoped.SetRoot(FVector2D(560.0, 236.0));
		TestTrue(TEXT("A root with a size of its own changes the canvas"), Scoped.Blueprint->AdoptRootSizeAsDefaultCanvas());
		TestEqual(TEXT("...to that size"), Scoped.Blueprint->GetDesignerCanvasSize(), FIntPoint(560, 236));
		TestEqual(TEXT("...without recording it as a picked resolution"),
			Scoped.Blueprint->DesignerData.DesignViewportSize, FIntPoint::ZeroValue);
		TestFalse(TEXT("Asked again it has nothing left to do"), Scoped.Blueprint->AdoptRootSizeAsDefaultCanvas());
	}
	{
		FScopedBlueprint Scoped(TEXT("CanvasStretchedRoot"));
		if (!TestNotNull(TEXT("The Blueprint was made"), Scoped.Blueprint))
		{
			return false;
		}
		Scoped.SetRoot(FVector2D(0.0, 0.0), /*bInStretched*/true);
		TestFalse(TEXT("A root that fills its host leaves the canvas alone"), Scoped.Blueprint->AdoptRootSizeAsDefaultCanvas());
		TestEqual(TEXT("A root that fills keeps the default canvas"), Scoped.Blueprint->GetDesignerCanvasSize(), Default);
	}
	{
		FScopedBlueprint Scoped(TEXT("CanvasUnsizedRoot"));
		if (!TestNotNull(TEXT("The Blueprint was made"), Scoped.Blueprint))
		{
			return false;
		}
		// What a root nobody sized comes out as.
		Scoped.SetRoot(FDreamUIAnchorData().SizeDelta);
		TestFalse(TEXT("A root that was never given a size is not mistaken for one"), Scoped.Blueprint->AdoptRootSizeAsDefaultCanvas());
		TestEqual(TEXT("and the default stays"), Scoped.Blueprint->GetDesignerCanvasSize(), Default);
	}
	{
		FScopedBlueprint Scoped(TEXT("CanvasChosenSize"));
		if (!TestNotNull(TEXT("The Blueprint was made"), Scoped.Blueprint))
		{
			return false;
		}
		Scoped.SetRoot(FVector2D(560.0, 236.0));
		Scoped.Blueprint->DesignerData.DesignViewportSize = FIntPoint(1280, 720);
		TestFalse(TEXT("A picked resolution is not replaced"), Scoped.Blueprint->AdoptRootSizeAsDefaultCanvas());
		TestEqual(TEXT("and the canvas stays"), Scoped.Blueprint->GetDesignerCanvasSize(), Default);

		Scoped.Blueprint->DesignerData.DesignViewportSize = FIntPoint::ZeroValue;
		Scoped.Blueprint->DesignerData.CanvasSize = FIntPoint(800, 600);
		TestFalse(TEXT("Nor is a canvas somebody changed without picking a resolution"), Scoped.Blueprint->AdoptRootSizeAsDefaultCanvas());
		TestEqual(TEXT("...which stays too"), Scoped.Blueprint->GetDesignerCanvasSize(), FIntPoint(800, 600));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerCanvasFollowsTheFileTest,
	"DreamGUI.Designer.ACompileOfATextFileGivesItsCardACanvasOfItsOwnSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerCanvasFollowsTheFileTest::RunTest(const FString&)
{
	using namespace DreamDesignerDefaultCanvasTestLocal;

	const FScopedDuiFile File(TEXT("DefaultCanvasCard"), {
		TEXT("Widget Root {"),
		TEXT("    AnchorData.SizeDelta = (560, 236)"),
		TEXT("}"),
	});
	if (!TestFalse(TEXT("The .dui was written"), File.Path.IsEmpty()))
	{
		return false;
	}
	FScopedBlueprint Scoped(TEXT("DefaultCanvasCard"), UDreamTextUserWidget::StaticClass());
	if (!TestNotNull(TEXT("The Blueprint was made"), Scoped.Blueprint))
	{
		return false;
	}
	if (!TestTrue(TEXT("The Blueprint took the file as its source"),
		DreamUITextAuthoring::SetAuthoredSourcePath(Scoped.Blueprint, File.Path)))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Scoped.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	TestEqual(TEXT("The canvas is the card's own size once the file is built"),
		Scoped.Blueprint->GetDesignerCanvasSize(), FIntPoint(560, 236));
	const UDreamWidgetGeneratedClass* Generated = Cast<UDreamWidgetGeneratedClass>(Scoped.Blueprint->GeneratedClass);
	if (TestNotNull(TEXT("The class was generated"), Generated))
	{
		TestEqual(TEXT("and the class carries it as its design size"), Generated->GetDesignSize(), FIntPoint(560, 236));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerCanvasSizeScriptingTest,
	"DreamGUI.Designer.AScriptCanSetAndReadTheDesignCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerCanvasSizeScriptingTest::RunTest(const FString&)
{
	using namespace DreamDesignerDefaultCanvasTestLocal;

	FScopedBlueprint Scoped(TEXT("CanvasScripting"));
	if (!TestNotNull(TEXT("The Blueprint was made"), Scoped.Blueprint))
	{
		return false;
	}
	// A root of its own size, which the next compile would otherwise make the canvas.
	Scoped.SetRoot(FVector2D(560.0, 236.0));

	TestFalse(TEXT("A canvas with an empty side is refused"), Scoped.Blueprint->SetDesignerCanvasSize(FIntPoint(0, 600)));
	TestEqual(TEXT("and nothing changed"), Scoped.Blueprint->GetDesignerCanvasSize(), FDreamWidgetDesignerData().CanvasSize);

	TestTrue(TEXT("A canvas is taken"), Scoped.Blueprint->SetDesignerCanvasSize(FIntPoint(800, 600)));
	TestEqual(TEXT("and read back"), Scoped.Blueprint->GetDesignerCanvasSize(), FIntPoint(800, 600));
	TestEqual(TEXT("It is recorded as the picked resolution"),
		Scoped.Blueprint->DesignerData.DesignViewportSize, FIntPoint(800, 600));
	TestFalse(TEXT("so the root's own size no longer replaces it"), Scoped.Blueprint->AdoptRootSizeAsDefaultCanvas());
	TestEqual(TEXT("and it is still there"), Scoped.Blueprint->GetDesignerCanvasSize(), FIntPoint(800, 600));
	return true;
}

#endif
