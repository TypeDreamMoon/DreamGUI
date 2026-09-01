// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIWorldContext.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/Texture2D.h"
#include "Extensions/2DLineRenderer/Dream2DLineChildrenAsPoints.h"
#include "Extensions/2DLineRenderer/Dream2DLineRaw.h"
#include "Extensions/DreamPolygon.h"
#include "Extensions/DreamPolygonLine.h"
#include "Extensions/DreamRing.h"
#include "Extensions/DreamStaticMesh.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

/*
 * The "natural size" protocol: what a visual answers when a layout asks how much room its CONTENT
 * needs, and -- the part that keeps going wrong -- what it answers when it does not know.
 *
 * UDreamVisual::GetPreferredWidth/Height has three possible answers and only two of them are a
 * number. Positive is a measurement. Negative is an abstention, and every caller in the plugin
 * already handles it: UDreamPanelLayoutBase::GetIntrinsicSize skips negatives and falls back to the
 * authored rect. ZERO IS NEITHER -- it is the element asserting that it wants no room at all, and
 * it wins a Max() against every abstention around it, so one element answering 0 by accident
 * collapses everything measured with it. That has already happened once here, to a ring menu whose
 * labels answered 0 because they had not laid out yet.
 *
 * The sweep below is the structural half of the fix. The targeted tests after it are the
 * interesting decisions: which visuals genuinely have a natural size, and which ones only look like
 * they do.
 *
 * Everything here builds its widgets under the transient package rather than in a world, which is
 * not a shortcut -- it is the second half of the contract. A measure has to answer from a Blueprint
 * authoring tree, where there is no world at all, so a test with a world would not be testing the
 * case that crashes.
 */

namespace DreamPreferredSizeTestLocal
{
	/** A widget with one visual on it, alive for the duration of a scope and torn down after. */
	struct FVisualScope
	{
		UDreamWidgetTree* Tree = nullptr;
		UDreamWidget* Widget = nullptr;
		UDreamVisual* Visual = nullptr;

		explicit FVisualScope(UClass* VisualClass, FName Name = TEXT("Measured"))
		{
			Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
			Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), Name);
			if (Widget != nullptr)
			{
				Tree->RootWidget = Widget;
				Widget->SetWidth(200.0f);
				Widget->SetHeight(150.0f);
				Visual = Widget->CreateNewVisual(VisualClass);
			}
		}

		~FVisualScope()
		{
			// Every case tears its widget down: a widget left for the collector reports its
			// complaints whenever the collector happens to run, which lands them on whichever test
			// is unlucky enough to be running then.
			if (Widget != nullptr)
			{
				Widget->DestroyWidget();
			}
		}

		template<class T>
		T* As()const { return Cast<T>(Visual); }
	};

	/** Whether a size is a legal answer: a measurement, or an abstention. Never exactly zero. */
	inline bool IsLegalAnswer(float Value)
	{
		return FMath::IsFinite(Value) && (Value < 0.0f || Value > 0.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVisualPreferredSizeNeverZeroTest,
	"DreamGUI.Layout.NoVisualClaimsZeroSizeWhenItHasNothingToMeasure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamVisualPreferredSizeNeverZeroTest::RunTest(const FString& Parameters)
{
	using namespace DreamPreferredSizeTestLocal;

	// Every visual, in the emptiest state it can be in: no world, no canvas, no sprite chosen, no
	// texture, no mesh, nothing laid out. That is the state where an implementation reaching for
	// "whatever my measurement cache happens to hold" produces a zero, and it is exactly the state
	// the ring menu's labels were in.
	//
	// This is the guardrail rather than a list of expectations: it does not care WHICH number a
	// visual gives, only that it never gives the one number that is neither a size nor a refusal.
	// A visual added later gets held to the same rule for free.
	int32 Checked = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UDreamVisual::StaticClass()) || Class == UDreamVisual::StaticClass())
		{
			continue;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		// Native only. A Blueprint subclass can override the measure in script and whether somebody
		// else's asset obeys the rule is not this codebase's claim to make.
		if (Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
		{
			continue;
		}

		FVisualScope Scope(Class, FName(*Class->GetName()));
		if (!TestNotNull(*FString::Printf(TEXT("%s builds on a widget"), *Class->GetName()), Scope.Visual))
		{
			continue;
		}
		++Checked;

		// The premise of the whole exercise, asserted rather than assumed: this really is the
		// worldless case, so anything below that reached through GetWorld() would have crashed here.
		TestNull(*FString::Printf(TEXT("%s is measured with no world, as an authoring tree would be"),
			*Class->GetName()), DreamUI::GetWorldSafe(Scope.Visual));

		const float Width = Scope.Visual->GetPreferredWidth();
		const float Height = Scope.Visual->GetPreferredHeight();
		TestTrue(*FString::Printf(
			TEXT("%s answers a width that is a size or a refusal, not 0 (got %f). Return a negative ")
			TEXT("number when there is nothing to measure -- 0 means 'give me no room' and wins."),
			*Class->GetName(), Width), IsLegalAnswer(Width));
		TestTrue(*FString::Printf(
			TEXT("%s answers a height that is a size or a refusal, not 0 (got %f)"),
			*Class->GetName(), Height), IsLegalAnswer(Height));

		// Free and repeatable. A measure that answers differently the second time is a measure that
		// changed something, and a layout that runs to convergence calls this more than once.
		TestEqual(*FString::Printf(TEXT("%s measures the same width twice"), *Class->GetName()),
			Scope.Visual->GetPreferredWidth(), Width);
		TestEqual(*FString::Printf(TEXT("%s measures the same height twice"), *Class->GetName()),
			Scope.Visual->GetPreferredHeight(), Height);
	}

	// A sweep that swept nothing passes for the wrong reason. Nineteen concrete visuals ship today.
	TestTrue(*FString::Printf(TEXT("the sweep found the visual library (saw %d)"), Checked), Checked >= 15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProceduralShapeBrushSizeTest,
	"DreamGUI.Layout.ProceduralShapesDoNotInheritTheirBrushSizeAsTheirOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamProceduralShapeBrushSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPreferredSizeTestLocal;

	// The control, and the reason the bug below was invisible: on an IMAGE, the brush size IS the
	// content size, and answering with it is right.
	{
		FVisualScope Scope(UDreamImage::StaticClass(), TEXT("Image"));
		UDreamImage* Image = Scope.As<UDreamImage>();
		if (!TestNotNull(TEXT("the image builds"), Image))return false;
		TestEqual(TEXT("an image measures as its brush, which is its content"),
			Image->GetPreferredWidth(), Image->GetBrush().ImageSize.X);
		TestTrue(TEXT("and the default brush is a real number, so the answer below is a choice"),
			Image->GetBrush().ImageSize.X > 0.0f);
	}

	// The shapes. Each derives from UDreamImage purely to borrow the brush as a source of pixels to
	// paint with, and each builds its vertices out of the RECT -- Widget->GetWidth() * 0.5f is the
	// radius, in the ring and in both polygons. Inherited, they would every one of them claim to
	// want the brush's 32x32, and a ring drawn 400px across in an Auto slot would be measured at 32
	// and squeezed to a dot with nothing logged anywhere.
	//
	// The 2D line base cancels it for all four of its subclasses at once; UDreamPolygon cancels it
	// for itself. UDream2DLineRaw is deliberately absent -- it re-overrides and answers for real,
	// which is the next test.
	const TArray<UClass*> ShapesWithNoNaturalSize =
	{
		UDreamPolygon::StaticClass(),
		UDreamRing::StaticClass(),
		UDreamPolygonLine::StaticClass(),
		// The one that looks measurable and is not: its points are its children's relative
		// locations, and a child's relative location is a function of the rect it is anchored
		// inside. Measuring the parent from them feeds the layout that decides them.
		UDream2DLineChildrenAsPoints::StaticClass(),
	};

	for (UClass* Class : ShapesWithNoNaturalSize)
	{
		FVisualScope Scope(Class, FName(*Class->GetName()));
		UDreamImage* AsImage = Scope.As<UDreamImage>();
		if (!TestNotNull(*FString::Printf(TEXT("%s builds"), *Class->GetName()), AsImage))
		{
			continue;
		}
		// Stated, so the test still means something if the default brush ever changes: there IS an
		// inherited answer available, and it is being refused rather than merely absent.
		TestTrue(*FString::Printf(TEXT("%s carries a brush with a size on it"), *Class->GetName()),
			AsImage->GetBrush().ImageSize.X > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s abstains rather than reporting its brush across"),
			*Class->GetName()), AsImage->GetPreferredWidth() < 0.0f);
		TestTrue(*FString::Printf(TEXT("%s abstains rather than reporting its brush down"),
			*Class->GetName()), AsImage->GetPreferredHeight() < 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRawLineMeasuresItsPointsTest,
	"DreamGUI.Layout.ARawLineMeasuresItsAuthoredPointsAndIgnoresTheRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRawLineMeasuresItsPointsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPreferredSizeTestLocal;

	FVisualScope Scope(UDream2DLineRaw::StaticClass(), TEXT("RawLine"));
	UDream2DLineRaw* Line = Scope.As<UDream2DLineRaw>();
	if (!TestNotNull(TEXT("the raw line builds"), Line))return false;

	const float Stroke = Line->GetLineWidth();
	TestTrue(TEXT("the line has a width to straddle its path with"), Stroke > 0.0f);

	// The default is a 200-unit horizontal run. Across, that is the run plus the stroke; down, it is
	// the stroke alone -- and the fact that the DOWN answer is not zero is the point. A flat line
	// still occupies its own thickness, and reporting 0 there would be the collapse this protocol
	// exists to prevent.
	TestEqual(TEXT("the default run measures its span plus the stroke"),
		Line->GetPreferredWidth(), 200.0f + Stroke, 0.01f);
	TestEqual(TEXT("and its thickness across the run"),
		Line->GetPreferredHeight(), Stroke, 0.01f);

	// This is what separates the raw line from its siblings: its points are authored, so its
	// measurement cannot move when the rect does. A ring's would, which is why a ring has to
	// abstain -- an answer that follows the rect is not a measurement, it is an echo.
	Scope.Widget->SetWidth(800.0f);
	Scope.Widget->SetHeight(600.0f);
	TestEqual(TEXT("resizing the widget does not move the answer"),
		Line->GetPreferredWidth(), 200.0f + Stroke, 0.01f);

	Line->SetPoints({ FVector2D(0.0, 0.0), FVector2D(30.0, 40.0) });
	TestEqual(TEXT("a diagonal measures its bounding box across"),
		Line->GetPreferredWidth(), 30.0f + Stroke, 0.01f);
	TestEqual(TEXT("and down"),
		Line->GetPreferredHeight(), 40.0f + Stroke, 0.01f);

	// One point is not a line. There is no shape to bound, and no shape is an abstention rather than
	// a claim of nothing -- the caller should fall back to the authored rect, not shrink to it.
	Line->SetPoints({ FVector2D(5.0, 5.0) });
	TestTrue(TEXT("a single point abstains across"), Line->GetPreferredWidth() < 0.0f);
	TestTrue(TEXT("a single point abstains down"), Line->GetPreferredHeight() < 0.0f);

	Line->SetPoints(TArray<FVector2D>());
	TestTrue(TEXT("and so does an empty line"), Line->GetPreferredWidth() < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextureMeasuresItselfTest,
	"DreamGUI.Layout.ATextureMeasuresItselfOnlyOnceItHasOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextureMeasuresItselfTest::RunTest(const FString& Parameters)
{
	using namespace DreamPreferredSizeTestLocal;

	FVisualScope Scope(UDreamTexture::StaticClass(), TEXT("Texture"));
	UDreamTexture* Texture = Scope.As<UDreamTexture>();
	if (!TestNotNull(TEXT("the texture visual builds"), Texture))return false;

	// Nothing assigned. The natural size of a texture is the texture, and there isn't one.
	TestNull(TEXT("no texture to begin with"), Texture->GetTexture());
	TestTrue(TEXT("so it abstains across"), Texture->GetPreferredWidth() < 0.0f);
	TestTrue(TEXT("and down"), Texture->GetPreferredHeight() < 0.0f);

	// Deliberately not square, so a width/height swap cannot pass.
	UTexture2D* Source = UTexture2D::CreateTransient(64, 32);
	if (!TestNotNull(TEXT("a transient texture to measure"), Source))return false;
	Texture->SetTexture(Source);

	TestEqual(TEXT("a texture measures as its own dimensions across"),
		Texture->GetPreferredWidth(), 64.0f, 0.01f);
	TestEqual(TEXT("and down"),
		Texture->GetPreferredHeight(), 32.0f, 0.01f);

	// The same pair SetSizeFromTexture would write into the widget. These two have to agree or
	// "measure me" and "size me to my content" mean different things on the same object.
	Texture->SetSizeFromTexture();
	TestEqual(TEXT("the measurement agrees with SetSizeFromTexture across"),
		Scope.Widget->GetWidth(), Texture->GetPreferredWidth(), 0.01f);
	TestEqual(TEXT("and down"),
		Scope.Widget->GetHeight(), Texture->GetPreferredHeight(), 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStaticMeshBoundsMeasureTest,
	"DreamGUI.Layout.AStaticMeshWithNoCachedBoundsAbstains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStaticMeshBoundsMeasureTest::RunTest(const FString& Parameters)
{
	using namespace DreamPreferredSizeTestLocal;

	FVisualScope Scope(UDreamStaticMesh::StaticClass(), TEXT("Mesh"));
	UDreamStaticMesh* Mesh = Scope.As<UDreamStaticMesh>();
	if (!TestNotNull(TEXT("the static mesh visual builds"), Mesh))return false;

	TestNull(TEXT("no cache to begin with"), Mesh->GetMeshCache());
	TestTrue(TEXT("so it abstains across"), Mesh->GetPreferredWidth() < 0.0f);
	TestTrue(TEXT("and down"), Mesh->GetPreferredHeight() < 0.0f);

	// The case worth pinning: a cache exists but has no bounds in it. That is every cache asset
	// saved before MeshBounds was added to the class, and FBox deserialises with IsValid clear and
	// Min/Max UNINITIALISED. Reading the extents of that box would be reading garbage; reporting the
	// zero box would be claiming the mesh wants no room. Neither is an answer.
	//
	// The measured side is not asserted here on purpose: filling MeshBounds means converting a real
	// UStaticMesh's render data, which needs an imported asset this suite does not ship. What can be
	// held here is the half that fails silently, which is the half that abstains.
	UDreamUIStaticMeshCacheData* EmptyCache = NewObject<UDreamUIStaticMeshCacheData>(GetTransientPackage());
	TestTrue(TEXT("a fresh cache has no bounds"), EmptyCache->GetMeshBounds().IsValid == 0);
	Mesh->SetMesh(EmptyCache);
	TestTrue(TEXT("an unbounded cache abstains across"), Mesh->GetPreferredWidth() < 0.0f);
	TestTrue(TEXT("and down"), Mesh->GetPreferredHeight() < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUnlaidTextAbstainsTest,
	"DreamGUI.Layout.AnUnlaidOutTextAbstainsRatherThanMeasuringZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUnlaidTextAbstainsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPreferredSizeTestLocal;

	// The original sin, kept red so it stays fixed.
	//
	// UpdateCacheTextGeometry gives up when there is no font or no render canvas -- the state of
	// every text in a headless test and in a Blueprint authoring tree -- and leaves the display list
	// as constructed, whose PreferredSize is (0,0). Returning that plus the margins made a text
	// claim it wanted no room, and 0 beats -1 in the Max() that a stack layout measures its children
	// with. A ring menu measured its whole ring at nothing this way, and the workaround at the time
	// was to hang an authored-surface layout on the control rather than to fix the number.
	FVisualScope Scope(UDreamText::StaticClass(), TEXT("Label"));
	UDreamText* Text = Scope.As<UDreamText>();
	if (!TestNotNull(TEXT("the text builds"), Text))return false;

	Text->SetText(FText::FromString(TEXT("Some words that would measure wide if they could")));

	// The distinction that makes -1 correct here rather than merely convenient: no layout has ever
	// run, so there is no measurement to report -- as opposed to a laid-out empty string, which may
	// legitimately measure zero because it really does want no room.
	TestEqual(TEXT("no layout has run without a canvas to lay out against"),
		Text->GetCacheTextGeometryData().GetLayoutRunCount(), 0);
	TestTrue(TEXT("so the text abstains across"), Text->GetPreferredWidth() < 0.0f);
	TestTrue(TEXT("and down"), Text->GetPreferredHeight() < 0.0f);

	// Asking did not make it lay out. A measure that laid out here would be doing the very thing
	// the contract forbids -- work with a side effect, inside a pass that runs per element.
	Text->GetPreferredWidth();
	TestEqual(TEXT("and asking cost no layout"),
		Text->GetCacheTextGeometryData().GetLayoutRunCount(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
