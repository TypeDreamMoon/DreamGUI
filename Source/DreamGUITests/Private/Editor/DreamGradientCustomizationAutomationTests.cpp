// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamGradientAsset.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/Text/DreamTextPaint.h"
#include "DetailCustomization/PropertyType/DreamGradientCustomization.h"
#include "Editor.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The gradient row of the details panel (FDreamGradientCustomization).
 *
 * Its stops are edited by the engine's gradient editor through FDreamGradientStopCurves, which turns stops into four
 * curves and back on every edit. A conversion that rounded a little on each trip would move every stop of every gradient
 * a user so much as clicked on -- invisible for one edit, a different gradient after fifty -- so the round trip is held
 * to exact equality, in each colour space. Its writes go through the property system, which is what makes them undoable
 * and lets them reach every object of a selection; the CSS paste is held to both.
 */
namespace DreamGradientCustomizationTestLocal
{
	/** The gradient row's stop curves over a gradient held here, with every write they make kept. */
	struct FStopCurvesFixture
	{
		FDreamGradient Source;
		TArray<TArray<FDreamGradientStop>> Writes;
		TArray<bool> InteractiveWrites;
		TSharedPtr<FDreamGradientStopCurves> Curves;

		explicit FStopCurvesFixture(const FDreamGradient& InSource)
			: Source(InSource)
		{
			FDreamGradientStopCurves::FBinding Binding;
			Binding.Read = [this](FDreamGradient& OutGradient)
			{
				OutGradient = Source;
				return true;
			};
			Binding.Write = [this](const TArray<FDreamGradientStop>& InStops, bool bInInteractive)
			{
				Source.Stops = InStops;
				Writes.Add(InStops);
				InteractiveWrites.Add(bInInteractive);
			};
			Curves = MakeShared<FDreamGradientStopCurves>(MoveTemp(Binding));
			Curves->Refresh();
		}

		// The bindings hold this fixture's address.
		FStopCurvesFixture(const FStopCurvesFixture&) = delete;
		FStopCurvesFixture& operator=(const FStopCurvesFixture&) = delete;

		FRealCurve& Curve(int32 InChannel) const
		{
			return *Curves->GetCurves()[InChannel].CurveToEdit;
		}

		/** What SColorGradientEditor does after each of its edits. */
		void Changed(bool bInInteractive = false) const
		{
			Curves->SetOnCurveChangedIsInteractive(bInInteractive);
			Curves->OnCurveChanged(Curves->GetCurves());
			Curves->SetOnCurveChangedIsInteractive(false);
		}

		/** The key of InChannel at InTime, the way the editor's marks are found. */
		FKeyHandle FindKey(int32 InChannel, float InTime) const
		{
			const FRealCurve& ChannelCurve = Curve(InChannel);
			for (auto It = ChannelCurve.GetKeyHandleIterator(); It; ++It)
			{
				if (ChannelCurve.GetKeyTime(*It) == InTime)
				{
					return *It;
				}
			}
			return FKeyHandle::Invalid();
		}

		/** A colour mark at InTime, as the editor adds one with a plain click: each channel's straight line there. */
		void AddColorMark(float InTime) const
		{
			for (int32 Channel = 0; Channel < 3; ++Channel)
			{
				FRealCurve& ChannelCurve = Curve(Channel);
				ChannelCurve.AddKey(InTime, ChannelCurve.Eval(InTime, 1.0f));
			}
		}
	};

	/**
	 * Stops that a careless conversion would not bring back: positions no decimal print holds, a hard edge (two stops at
	 * one place), alpha between 0 and 255 and at 0, a position past the end, and colours whose sRGB values do not survive
	 * every trip through linear light.
	 */
	FDreamGradient MakeAwkwardGradient(EDreamPaintInterpolation InInterpolation)
	{
		FDreamGradient Gradient;
		Gradient.Interpolation = InInterpolation;
		Gradient.Stops.Add(FDreamGradientStop(0.0f, FColor(255, 0, 0, 255)));
		Gradient.Stops.Add(FDreamGradientStop(1.0f / 3.0f, FColor(1, 128, 254, 200)));
		Gradient.Stops.Add(FDreamGradientStop(0.5f, FColor(10, 200, 30, 255)));
		Gradient.Stops.Add(FDreamGradientStop(0.5f, FColor(250, 251, 5, 128)));
		Gradient.Stops.Add(FDreamGradientStop(0.8f, FColor(17, 34, 51, 0)));
		Gradient.Stops.Add(FDreamGradientStop(1.3f, FColor(255, 255, 255, 255)));
		return Gradient;
	}

	/** The gradient's colours along it, as the stop editor's strip shows them: what FDreamGradient::Evaluate paints there. */
	FLinearColor EvaluateAlong(const FDreamGradient& InGradient, float InPosition)
	{
		FDreamGradient Strip;
		Strip.Type = EDreamPaintType::Linear;
		Strip.Angle = 90.0f;
		Strip.Spread = EDreamPaintSpread::Pad;
		Strip.Interpolation = InGradient.Interpolation;
		Strip.Stops = InGradient.Stops;
		return Strip.Evaluate(FVector2f(InPosition, 0.5f), 1.0f);
	}

	bool IsNear(const FColor& InA, const FColor& InB, int32 InTolerance)
	{
		return FMath::Abs((int32)InA.R - (int32)InB.R) <= InTolerance && FMath::Abs((int32)InA.G - (int32)InB.G) <= InTolerance
			&& FMath::Abs((int32)InA.B - (int32)InB.B) <= InTolerance && FMath::Abs((int32)InA.A - (int32)InB.A) <= InTolerance;
	}

	FColor ToStopColor(const FLinearColor& InColor)
	{
		FColor Color = FLinearColor(InColor.R, InColor.G, InColor.B, 1.0f).ToFColorSRGB();
		Color.A = (uint8)FMath::Clamp(FMath::RoundToInt(InColor.A * 255.0f), 0, 255);
		return Color;
	}

	const FDreamGradientStop* FindStopAt(const TArray<FDreamGradientStop>& InStops, float InPosition)
	{
		return InStops.FindByPredicate([InPosition](const FDreamGradientStop& Stop) { return Stop.Position == InPosition; });
	}

	/** A real property handle over the objects' InPropertyName: the row generator is the headless way to get one. */
	struct FScopedRows
	{
		TSharedPtr<IPropertyRowGenerator> Generator;

		explicit FScopedRows(const TArray<UObject*>& InObjects)
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			Generator = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
			Generator->SetObjects(InObjects);
		}

		/** The first node, depth first, whose property is InPropertyName. */
		TSharedPtr<IDetailTreeNode> FindPropertyNode(FName InPropertyName) const
		{
			for (const TSharedRef<IDetailTreeNode>& Root : Generator->GetRootTreeNodes())
			{
				if (TSharedPtr<IDetailTreeNode> Found = FindPropertyNode(Root, InPropertyName))
				{
					return Found;
				}
			}
			return nullptr;
		}

		static TSharedPtr<IDetailTreeNode> FindPropertyNode(const TSharedRef<IDetailTreeNode>& InNode, FName InPropertyName)
		{
			if (const TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle())
			{
				if (Handle->GetProperty() != nullptr && Handle->GetProperty()->GetFName() == InPropertyName)
				{
					return InNode;
				}
			}
			TArray<TSharedRef<IDetailTreeNode>> Children;
			InNode->GetChildren(Children, /*bInIgnoreVisibility*/ true);
			for (const TSharedRef<IDetailTreeNode>& Child : Children)
			{
				if (TSharedPtr<IDetailTreeNode> Found = FindPropertyNode(Child, InPropertyName))
				{
					return Found;
				}
			}
			return nullptr;
		}

		/** Every row under InNode: its tag or its property's name. */
		static void CollectNames(const TSharedRef<IDetailTreeNode>& InNode, TArray<FName>& OutNames)
		{
			TArray<TSharedRef<IDetailTreeNode>> Children;
			InNode->GetChildren(Children, /*bInIgnoreVisibility*/ true);
			for (const TSharedRef<IDetailTreeNode>& Child : Children)
			{
				const TSharedPtr<IPropertyHandle> Handle = Child->CreatePropertyHandle();
				OutNames.Add(Handle.IsValid() && Handle->GetProperty() != nullptr ? Handle->GetProperty()->GetFName() : Child->GetNodeName());
				CollectNames(Child, OutNames);
			}
		}
	};

	UPackage* MakeTestPackage(const TCHAR* InName)
	{
		UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
		Package->SetDirtyFlag(false);
		return Package;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGradientDetailsBuildTest,
	"DreamGUI.Editor.GradientDetails.TheCustomizationBuildsForAGradientProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGradientDetailsBuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamGradientCustomizationTestLocal;
	UPackage* Package = MakeTestPackage(TEXT("GradientDetailsBuild"));
	TStrongObjectPtr<UDreamGradientAsset> Asset(NewObject<UDreamGradientAsset>(Package, NAME_None, RF_Transactional));
	Asset->SetGradient(MakeAwkwardGradient(EDreamPaintInterpolation::SRGB));

	FScopedRows Rows({ Asset.Get() });
	const TSharedPtr<IDetailTreeNode> GradientNode = Rows.FindPropertyNode(TEXT("Gradient"));
	if (!TestTrue(TEXT("the asset's gradient has a row"), GradientNode.IsValid()))
	{
		return false;
	}
	TArray<FName> Names;
	FScopedRows::CollectNames(GradientNode.ToSharedRef(), Names);
	// Rows only the customization makes: without them the struct got the stock layout.
	TestTrue(TEXT("the CSS row is there"), Names.Contains(DreamGradientDetails::CssRowTag));
	TestTrue(TEXT("the stop editor is there"), Names.Contains(DreamGradientDetails::StopsRowTag));
	for (const FName Field : {
		GET_MEMBER_NAME_CHECKED(FDreamGradient, Type), GET_MEMBER_NAME_CHECKED(FDreamGradient, Stops),
		GET_MEMBER_NAME_CHECKED(FDreamGradient, Interpolation), GET_MEMBER_NAME_CHECKED(FDreamGradient, Angle),
		GET_MEMBER_NAME_CHECKED(FDreamGradient, Center), GET_MEMBER_NAME_CHECKED(FDreamGradient, Radius),
		GET_MEMBER_NAME_CHECKED(FDreamGradient, Spread), GET_MEMBER_NAME_CHECKED(FDreamGradient, Scale),
		GET_MEMBER_NAME_CHECKED(FDreamGradient, Offset) })
	{
		TestTrue(*FString::Printf(TEXT("the gradient's '%s' has a row"), *Field.ToString()), Names.Contains(Field));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGradientStopsRoundTripTest,
	"DreamGUI.Editor.GradientDetails.StopsRoundTripThroughTheCurveKeysWithoutDriftInEverySpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGradientStopsRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamGradientCustomizationTestLocal;
	for (const EDreamPaintInterpolation Space : { EDreamPaintInterpolation::SRGB, EDreamPaintInterpolation::Linear, EDreamPaintInterpolation::Oklab })
	{
		const FString SpaceName = StaticEnum<EDreamPaintInterpolation>()->GetNameStringByValue((int64)Space);
		const FDreamGradient Original = MakeAwkwardGradient(Space);
		FStopCurvesFixture Fixture(Original);

		// Read: one colour key and one alpha key a stop, the hard edge's two apart so the editor can tell their marks apart.
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			TestEqual(*FString::Printf(TEXT("%s: channel %d has a key a stop"), *SpaceName, Channel), Fixture.Curve(Channel).GetNumKeys(), Original.Stops.Num());
		}
		TestTrue(*FString::Printf(TEXT("%s: the hard edge's keys sit apart"), *SpaceName),
			Fixture.FindKey(0, 0.5f) != FKeyHandle::Invalid() && Fixture.FindKey(0, 0.5f + FDreamGradientStopCurves::MinKeySpacing) != FKeyHandle::Invalid());
		// The editor draws each mark in the colour at its time: the mark's own, the hard edge's earlier one included.
		TestTrue(*FString::Printf(TEXT("%s: a mark shows its stop's colour"), *SpaceName),
			Fixture.Curves->GetLinearColorValue(0.5f).Equals(FLinearColor(FColor(10, 200, 30, 255)), 1.0e-6f));
		// And its strip the gradient as it paints, in its colour space, not the curves' straight lines in linear light.
		TestTrue(*FString::Printf(TEXT("%s: the strip is the gradient as it paints"), *SpaceName),
			Fixture.Curves->GetLinearColorValue(0.1f).Equals(EvaluateAlong(Original, 0.1f), 1.0e-4f));

		// Written back untouched, as often as the editor likes: exactly the stops it read.
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			Fixture.Changed();
		}
		if (!TestEqual(*FString::Printf(TEXT("%s: the editor wrote each time"), *SpaceName), Fixture.Writes.Num(), 3))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s: no stop drifted"), *SpaceName), Fixture.Writes.Last() == Original.Stops);

		// A plain click adds a colour mark: the stop takes the colour the gradient already has there, in its space.
		Fixture.AddColorMark(0.1f);
		Fixture.Changed();
		const TArray<FDreamGradientStop>& Added = Fixture.Writes.Last();
		TestEqual(*FString::Printf(TEXT("%s: a stop was added"), *SpaceName), Added.Num(), Original.Stops.Num() + 1);
		const FDreamGradientStop* NewStop = FindStopAt(Added, 0.1f);
		if (TestNotNull(*FString::Printf(TEXT("%s: where it was clicked"), *SpaceName), NewStop))
		{
			TestTrue(*FString::Printf(TEXT("%s: in the colour the gradient had there"), *SpaceName),
				IsNear(NewStop->Color, ToStopColor(EvaluateAlong(Original, 0.1f)), 1));
		}
		TestTrue(*FString::Printf(TEXT("%s: with an alpha mark of its own"), *SpaceName), Fixture.FindKey(3, 0.1f) != FKeyHandle::Invalid());
		for (const FDreamGradientStop& Stop : Original.Stops)
		{
			TestTrue(*FString::Printf(TEXT("%s: the stop at %f is untouched"), *SpaceName, Stop.Position), Added.Contains(Stop));
		}

		// Dragging its colour mark takes its alpha mark along: one stop, moving.
		const FKeyHandle Red = Fixture.FindKey(0, 0.1f);
		const FKeyHandle Green = Fixture.FindKey(1, 0.1f);
		const FKeyHandle Blue = Fixture.FindKey(2, 0.1f);
		Fixture.Curve(0).SetKeyTime(Red, 0.15f);
		Fixture.Curve(1).SetKeyTime(Green, 0.15f);
		Fixture.Curve(2).SetKeyTime(Blue, 0.15f);
		Fixture.Changed(/*bInInteractive*/ true);
		TestTrue(*FString::Printf(TEXT("%s: a drag writes as an interactive change"), *SpaceName), Fixture.InteractiveWrites.Last());
		TestTrue(*FString::Printf(TEXT("%s: the alpha mark moved with it"), *SpaceName), Fixture.FindKey(3, 0.15f) != FKeyHandle::Invalid());
		TestNotNull(*FString::Printf(TEXT("%s: the stop is where it was dragged"), *SpaceName), FindStopAt(Fixture.Writes.Last(), 0.15f));
		TestEqual(*FString::Printf(TEXT("%s: and it is still one stop"), *SpaceName), Fixture.Writes.Last().Num(), Original.Stops.Num() + 1);

		// Deleting either mark deletes the stop, and leaves the others as they were.
		const FKeyHandle MovedAlpha = Fixture.FindKey(3, 0.15f);
		if (!TestTrue(*FString::Printf(TEXT("%s: the moved stop's alpha mark is there to delete"), *SpaceName), Fixture.Curve(3).IsKeyHandleValid(MovedAlpha)))
		{
			continue;
		}
		Fixture.Curve(3).DeleteKey(MovedAlpha);
		Fixture.Changed();
		TestTrue(*FString::Printf(TEXT("%s: deleting its alpha mark took the stop away, and only it"), *SpaceName), Fixture.Writes.Last() == Original.Stops);
		TestEqual(*FString::Printf(TEXT("%s: its colour keys went too"), *SpaceName), Fixture.Curve(0).GetNumKeys(), Original.Stops.Num());

		// An alpha mark added on its own makes a stop in the colour the gradient has there.
		FRealCurve& AlphaCurve = Fixture.Curve(3);
		const float AlphaThere = AlphaCurve.Eval(0.65f, 1.0f);
		AlphaCurve.AddKey(0.65f, AlphaThere);
		Fixture.Changed();
		const FDreamGradientStop* AlphaStop = FindStopAt(Fixture.Writes.Last(), 0.65f);
		if (TestNotNull(*FString::Printf(TEXT("%s: an alpha mark made a stop"), *SpaceName), AlphaStop))
		{
			FLinearColor Expected = EvaluateAlong(Original, 0.65f);
			Expected.A = AlphaThere;
			TestTrue(*FString::Printf(TEXT("%s: in the gradient's colour there"), *SpaceName), IsNear(AlphaStop->Color, ToStopColor(Expected), 1));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGradientCssPasteTest,
	"DreamGUI.Editor.GradientDetails.PastingCssSetsEveryGradientOfTheSelectionAndIsUndoable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGradientCssPasteTest::RunTest(const FString& Parameters)
{
	using namespace DreamGradientCustomizationTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}
	UPackage* Package = MakeTestPackage(TEXT("GradientCssPaste"));
	// Transactional, so the write records undo the way it does on a real asset.
	TStrongObjectPtr<UDreamGradientAsset> First(NewObject<UDreamGradientAsset>(Package, NAME_None, RF_Transactional));
	TStrongObjectPtr<UDreamGradientAsset> Second(NewObject<UDreamGradientAsset>(Package, NAME_None, RF_Transactional));
	const FDreamGradient FirstBefore = MakeAwkwardGradient(EDreamPaintInterpolation::SRGB);
	FDreamGradient SecondBefore;
	SecondBefore.Type = EDreamPaintType::Radial;
	SecondBefore.Stops.Add(FDreamGradientStop(0.0f, FColor::Green));
	SecondBefore.Stops.Add(FDreamGradientStop(1.0f, FColor::White));
	First->SetGradient(FirstBefore);
	Second->SetGradient(SecondBefore);

	FScopedRows Rows({ First.Get(), Second.Get() });
	const TSharedPtr<IDetailTreeNode> GradientNode = Rows.FindPropertyNode(TEXT("Gradient"));
	const TSharedPtr<IPropertyHandle> Handle = GradientNode.IsValid() ? GradientNode->CreatePropertyHandle() : nullptr;
	if (!TestTrue(TEXT("a handle over both gradients"), Handle.IsValid()))
	{
		return false;
	}

	const FString Css = TEXT("linear-gradient(90deg, #FFF3B0, #E8B64A 55%, #9C6A12)");
	FDreamGradient Expected;
	if (!TestTrue(TEXT("the CSS reads"), FDreamGradient::ParseCss(Css, Expected)))
	{
		return false;
	}
	Package->SetDirtyFlag(false);
	FText Error;
	TestTrue(TEXT("pasting it succeeds"), DreamGradientDetails::ApplyCss(Handle.ToSharedRef(), Css, Error));
	TestTrue(TEXT("the first gradient is the pasted one"), First->GetGradient() == Expected);
	TestTrue(TEXT("and so is the second: the paste reaches the whole selection"), Second->GetGradient() == Expected);
	TestTrue(TEXT("the assets ask to be saved"), Package->IsDirty());

	GEditor->UndoTransaction();
	TestTrue(TEXT("one undo gives the first its gradient back, exactly"), First->GetGradient() == FirstBefore);
	TestTrue(TEXT("and the second its own"), Second->GetGradient() == SecondBefore);

	// Not a gradient: nothing is written, and the error says why.
	Error = FText::GetEmpty();
	TestFalse(TEXT("text that is not a gradient does not paste"), DreamGradientDetails::ApplyCss(Handle.ToSharedRef(), TEXT("not-a-gradient(#fff)"), Error));
	TestFalse(TEXT("and says why"), Error.IsEmpty());
	TestTrue(TEXT("leaving the gradient alone"), First->GetGradient() == FirstBefore);

	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGradientFieldsFollowTypeTest,
	"DreamGUI.Editor.GradientDetails.TheFieldsShownAreTheOnesTheTypeUses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGradientFieldsFollowTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamGradientDetails;
	const FName Angle = GET_MEMBER_NAME_CHECKED(FDreamGradient, Angle);
	const FName Center = GET_MEMBER_NAME_CHECKED(FDreamGradient, Center);
	const FName Shape = GET_MEMBER_NAME_CHECKED(FDreamGradient, Shape);
	const FName Size = GET_MEMBER_NAME_CHECKED(FDreamGradient, Size);
	const FName Radius = GET_MEMBER_NAME_CHECKED(FDreamGradient, Radius);
	const FName Spread = GET_MEMBER_NAME_CHECKED(FDreamGradient, Spread);
	const FName Interpolation = GET_MEMBER_NAME_CHECKED(FDreamGradient, Interpolation);
	const FName Stops = GET_MEMBER_NAME_CHECKED(FDreamGradient, Stops);
	const EDreamPaintRadialSize Corner = EDreamPaintRadialSize::FarthestCorner;
	const EDreamPaintRadialSize Explicit = EDreamPaintRadialSize::Explicit;

	TestTrue(TEXT("a linear gradient has a direction"), IsFieldUsed(Angle, EDreamPaintType::Linear, Corner));
	TestFalse(TEXT("but no centre: it runs through the box's middle"), IsFieldUsed(Center, EDreamPaintType::Linear, Corner));
	TestFalse(TEXT("nor a size"), IsFieldUsed(Size, EDreamPaintType::Linear, Corner));
	TestTrue(TEXT("a radial gradient has a centre"), IsFieldUsed(Center, EDreamPaintType::Radial, Corner));
	TestTrue(TEXT("and a shape"), IsFieldUsed(Shape, EDreamPaintType::Radial, Corner));
	TestFalse(TEXT("but no angle"), IsFieldUsed(Angle, EDreamPaintType::Radial, Corner));
	TestFalse(TEXT("its radius only with an explicit size"), IsFieldUsed(Radius, EDreamPaintType::Radial, Corner));
	TestTrue(TEXT("which shows it"), IsFieldUsed(Radius, EDreamPaintType::Radial, Explicit));
	TestTrue(TEXT("a conic gradient starts at an angle"), IsFieldUsed(Angle, EDreamPaintType::Conic, Corner));
	TestFalse(TEXT("and has no size"), IsFieldUsed(Size, EDreamPaintType::Conic, Corner));
	TestTrue(TEXT("a diamond turns"), IsFieldUsed(Angle, EDreamPaintType::Diamond, Corner));
	TestFalse(TEXT("and takes the ellipse's radii whatever the shape"), IsFieldUsed(Shape, EDreamPaintType::Diamond, Corner));
	TestFalse(TEXT("four corners have no line to spread along"), IsFieldUsed(Spread, EDreamPaintType::Corners, Corner));
	TestTrue(TEXT("but still mix in a colour space"), IsFieldUsed(Interpolation, EDreamPaintType::Corners, Corner));
	TestFalse(TEXT("nothing to paint, no colour space"), IsFieldUsed(Interpolation, EDreamPaintType::None, Corner));
	TestTrue(TEXT("the stops stay in sight whatever the type"), IsFieldUsed(Stops, EDreamPaintType::None, Corner));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCustomStylePaintRowsTest,
	"DreamGUI.Editor.GradientDetails.ACustomStylesPaintShowsItsPresetAndGradientWithoutTheUnreadSwitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCustomStylePaintRowsTest::RunTest(const FString& Parameters)
{
	using namespace DreamGradientCustomizationTestLocal;
	UPackage* Package = MakeTestPackage(TEXT("CustomStylePaintRows"));
	TStrongObjectPtr<UDreamUIRichTextCustomStyleData> Styles(NewObject<UDreamUIRichTextCustomStyleData>(Package, NAME_None, RF_Transactional));
	FDreamUIRichTextCustomStyleItemData Gold;
	Gold.paintType = EDreamUIRichTextCustomStyleData_PaintType::Set;
	Gold.paint.Gradient = MakeAwkwardGradient(EDreamPaintInterpolation::SRGB);
	TMap<FName, FDreamUIRichTextCustomStyleItemData> DataMap;
	DataMap.Add(TEXT("gold"), Gold);
	Styles->SetDataMap(DataMap);

	FScopedRows Rows({ Styles.Get() });
	TArray<FName> Names;
	for (const TSharedRef<IDetailTreeNode>& Root : Rows.Generator->GetRootTreeNodes())
	{
		FScopedRows::CollectNames(Root, Names);
	}
	TestTrue(TEXT("the entry's paint type has a row"), Names.Contains(GET_MEMBER_NAME_CHECKED(FDreamUIRichTextCustomStyleItemData, paintType)));
	TestTrue(TEXT("its paint's preset has a row"), Names.Contains(GET_MEMBER_NAME_CHECKED(FDreamTextPaint, Preset)));
	TestTrue(TEXT("its paint's gradient has a row"), Names.Contains(GET_MEMBER_NAME_CHECKED(FDreamTextPaint, Gradient)));
	TestTrue(TEXT("drawn by the gradient row"), Names.Contains(DreamGradientDetails::StopsRowTag));
	TestFalse(TEXT("the paint's bEnabled, which a custom style does not read, has none"), Names.Contains(GET_MEMBER_NAME_CHECKED(FDreamTextPaint, bEnabled)));
	TestFalse(TEXT("nor does the paint as a whole: its two fields stand in for it"), Names.Contains(GET_MEMBER_NAME_CHECKED(FDreamUIRichTextCustomStyleItemData, paint)));
	TestTrue(TEXT("the other fields keep theirs"), Names.Contains(GET_MEMBER_NAME_CHECKED(FDreamUIRichTextCustomStyleItemData, colorType)));
	// And live. Their EditCondition is that bEnabled, which the panel no longer offers: an entry whose paint started with it
	// off would show the two greyed out with no way to change that (an EditCondition makes a property edit-const, its
	// value widgets included, whatever the row says). So an entry's paint starts enabled.
	for (const FName Field : { GET_MEMBER_NAME_CHECKED(FDreamTextPaint, Preset), GET_MEMBER_NAME_CHECKED(FDreamTextPaint, Gradient) })
	{
		const TSharedPtr<IDetailTreeNode> FieldNode = Rows.FindPropertyNode(Field);
		const TSharedPtr<IPropertyHandle> FieldHandle = FieldNode.IsValid() ? FieldNode->CreatePropertyHandle() : nullptr;
		TestTrue(*FString::Printf(TEXT("its paint's '%s' can be edited without the switch the panel hides"), *Field.ToString()),
			FieldHandle.IsValid() && FieldHandle->IsEditable());
	}
	return true;
}

#endif
