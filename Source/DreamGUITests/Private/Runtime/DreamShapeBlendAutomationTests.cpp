// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIShapeBlend.h"
#include "Event/DreamPointerEventData.h"
#include "Extensions/Effects/DreamHoloFoil.h"
#include "Extensions/Effects/DreamPointerLight.h"
#include "Extensions/Effects/DreamTiltCard.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * Shape blends on the CPU: the layout held to the shader's, the smooth minimum and its colours, a group's members gathered in
 * its leader's frame, what draws and what does not, and a click landing on the nearest member; and the pointer light, the
 * tilt card and the holo foil handing what they make to what they work on. The pictures are in RHI/DreamShapeBlendPixelAutomationTests.cpp.
 */
namespace DreamShapeBlendTestLocal
{
	bool Near(float A, float B, float Tolerance = 1e-3f)
	{
		return FMath::Abs(A - B) <= Tolerance;
	}

	UDreamWidget* MakeWidget(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
		Widget->SetDisplayName(InName);
		Widget->SetWidth(static_cast<float>(InSize.X));
		Widget->SetHeight(static_cast<float>(InSize.Y));
		Widget->OnRegister();
		if (InParent != nullptr)
		{
			Widget->TrySetParent(InParent, false);
			Widget->SetAnchoredPosition(InPosition);
		}
		return Widget;
	}

	UDreamRectBlock* MakeBlock(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColor, float InCorner = 8.0f)
	{
		UDreamWidget* Widget = MakeWidget(InWorld, InParent, InName, InSize, InPosition);
		UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
		if (Block != nullptr)
		{
			Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetCornerRadius(FVector4(InCorner, InCorner, InCorner, InCorner));
			Block->SetBodyColor(InColor);
		}
		return Block;
	}

	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;

		FStage()
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(World.World, nullptr, TEXT("Root"), FVector2D(800.0, 600.0));
			Canvas = Root->AddComponent<UDreamCanvas>();
			if (Canvas != nullptr)
			{
				Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
				Canvas->SetCullElementsOutsideCanvas(false);
			}
		}
		bool IsUsable() const { return Manager != nullptr && Root != nullptr && Canvas != nullptr && Root->GetRenderCanvas() == Canvas; }
		void Frames(int32 InCount = 3) const
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}
	};

	/** A ray straight through InWorldPoint along the widget's normal, as a pointer over it would cast. */
	bool Hits(const UDreamRectBlock* InBlock, const FVector& InWorldPoint)
	{
		const UDreamWidget* Widget = InBlock->GetWidget();
		const FVector Normal = Widget->GetWorldTransform().TransformVectorNoScale(FVector(1.0, 0.0, 0.0));
		FDreamUIHitResult Hit;
		return InBlock->LineTraceUI(Hit, InWorldPoint + Normal * 100.0, InWorldPoint - Normal * 100.0);
	}

	FVector OnCanvas(const UDreamWidget* InRoot, FVector2D InPoint)
	{
		return InRoot->GetWorldTransform().TransformPosition(FVector(0.0, InPoint.X, InPoint.Y));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamShapeBlendLayoutTest,
	"DreamGUI.ShapeBlend.TheLayoutIsTheShadersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShapeBlendLayoutTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("DreamGUI is a plugin this editor knows"), Plugin.IsValid()))
	{
		return false;
	}
	FString Source;
	if (!TestTrue(TEXT("DreamUIShapeBlend.ush reads"), FFileHelper::LoadFileToString(Source, *FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private/DreamUIShapeBlend.ush")))))
	{
		return false;
	}
	TMap<FString, int32> Defines;
	FRegexMatcher Matcher(FRegexPattern(TEXT("#define DREAMUI_SHAPE_BLEND_([A-Z0-9_]+) (\\d+)")), Source);
	while (Matcher.FindNext())
	{
		Defines.Add(Matcher.GetCaptureGroup(1), FCString::Atoi(*Matcher.GetCaptureGroup(2)));
	}
	using namespace DreamUIShapeBlendLayout;
	const TMap<FString, int32> Expected = {
		{ TEXT("HEADER_TEXELS"), HeaderTexels }, { TEXT("MEMBER_TEXELS"), MemberTexels }, { TEXT("MAX_MEMBERS"), MaxMembers },
		{ TEXT("UNION"), (int32)EDreamShapeBlendOperation::Union }, { TEXT("SUBTRACT"), (int32)EDreamShapeBlendOperation::Subtract },
		{ TEXT("INTERSECT"), (int32)EDreamShapeBlendOperation::Intersect },
	};
	for (const TPair<FString, int32>& Pair : Expected)
	{
		const int32* Shader = Defines.Find(Pair.Key);
		if (TestNotNull(*FString::Printf(TEXT("DreamUIShapeBlend.ush defines DREAMUI_SHAPE_BLEND_%s"), *Pair.Key), Shader))
		{
			TestEqual(*FString::Printf(TEXT("DREAMUI_SHAPE_BLEND_%s is the C++ value"), *Pair.Key), *Shader, Pair.Value);
		}
	}
	for (const TPair<FString, int32>& Pair : Defines)
	{
		TestTrue(*FString::Printf(TEXT("DREAMUI_SHAPE_BLEND_%s has a C++ twin this test knows"), *Pair.Key), Expected.Contains(Pair.Key));
	}
	TestTrue(TEXT("A whole shape fits in a rect block's row"), MaxTexels * 16 <= 12 * 16 + DreamUILayerStyleLayout::MaxTexels * 16);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamShapeBlendMathTest,
	"DreamGUI.ShapeBlend.MembersMeltBySmoothMinimumAndTheirColoursFollow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShapeBlendMathTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendTestLocal;
	// Two squares of half size 20, 50 apart centre to centre: a gap of 10 between them.
	TArray<FDreamUIShapeBlendMember> Members;
	FDreamUIShapeBlendMember& A = Members.AddDefaulted_GetRef();
	A.Centre = FVector2f(-25.0f, 0.0f);
	A.HalfSize = FVector2f(20.0f, 20.0f);
	A.Color = FColor(255, 0, 0);
	FDreamUIShapeBlendMember& B = Members.AddDefaulted_GetRef();
	B.Centre = FVector2f(25.0f, 0.0f);
	B.HalfSize = FVector2f(20.0f, 20.0f);
	B.Color = FColor(0, 0, 255);
	FDreamUIShapeBlendShape Shape;
	FDreamUIShapeBlendMath::Pack(Members, 0.0f, Shape);
	TestEqual(TEXT("Two members"), Shape.NumMembers(), 2);
	TestTrue(TEXT("With no smoothness, the gap is open: 5 out in the middle"), Near(FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f::ZeroVector), 5.0f, 1e-2f));
	FDreamUIShapeBlendMath::Pack(Members, 30.0f, Shape);
	const float Melted = FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f::ZeroVector);
	TestTrue(*FString::Printf(TEXT("Smoothed by 30, the gap fills: inside in the middle (%.3f)"), Melted), Melted < 0.0f);
	TestTrue(TEXT("...and far from both, they are as they were"), Near(FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f(-25.0f, 0.0f)), -20.0f, 0.5f));
	FVector4f Color;
	int32 Nearest = -1;
	FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f::ZeroVector, &Color, &Nearest);
	TestTrue(*FString::Printf(TEXT("In the middle the colour is half each (%.3f, %.3f)"), Color.X, Color.Z), Near(Color.X, Color.Z, 1e-3f) && Color.X > 0.1f);
	FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f(-30.0f, 0.0f), &Color, &Nearest);
	TestTrue(TEXT("Over the first, its own red"), Color.X > 0.9f && Color.Z < 0.05f);
	TestEqual(TEXT("...and it is the nearest member"), Nearest, 0);

	// A cut: a third member subtracted from the middle of the first.
	FDreamUIShapeBlendMember& Cut = Members.AddDefaulted_GetRef();
	Cut.Centre = FVector2f(-25.0f, 0.0f);
	Cut.HalfSize = FVector2f(6.0f, 6.0f);
	Cut.Operation = EDreamShapeBlendOperation::Subtract;
	FDreamUIShapeBlendMath::Pack(Members, 2.0f, Shape);
	TestTrue(TEXT("Subtracted, the first's middle is a hole"), FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f(-25.0f, 0.0f)) > 0.0f);
	FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f(-25.0f, 0.0f), nullptr, &Nearest);
	TestTrue(TEXT("...and a cut is never the member a click lands on"), Nearest != 2);
	// An intersection keeps only where it meets the shape.
	Members[2].Operation = EDreamShapeBlendOperation::Intersect;
	FDreamUIShapeBlendMath::Pack(Members, 0.0f, Shape);
	TestTrue(TEXT("Intersected, only the third's square is left"), FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f(-25.0f, 0.0f)) < 0.0f
		&& FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f(25.0f, 0.0f)) > 0.0f);

	// A member turned: its axes carry it into the frame.
	TArray<FDreamUIShapeBlendMember> Turned;
	FDreamUIShapeBlendMember& T = Turned.AddDefaulted_GetRef();
	T.HalfSize = FVector2f(30.0f, 5.0f);
	T.AxisX = FVector2f(0.0f, 1.0f);
	T.AxisY = FVector2f(-1.0f, 0.0f);
	FDreamUIShapeBlendMath::Pack(Turned, 0.0f, Shape);
	TestTrue(TEXT("A bar turned a quarter stands upright"), FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f(0.0f, 25.0f)) < 0.0f
		&& FDreamUIShapeBlendMath::Distance(Shape.Texels.GetData(), FVector2f(25.0f, 0.0f)) > 0.0f);
	const FBox2f Bounds = FDreamUIShapeBlendMath::Bounds(Turned, 4.0f);
	TestTrue(TEXT("...and its bounds are its turned extent and the smoothness"), Near(Bounds.Max.X, 9.0f) && Near(Bounds.Max.Y, 34.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamShapeBlendGroupTest,
	"DreamGUI.ShapeBlend.TheFirstMemberDrawsTheShapeAndAClickLandsOnTheNearestMember",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShapeBlendGroupTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* Group = MakeWidget(World, Stage.Root, TEXT("Group"), FVector2D(300.0, 120.0));
	UDreamRectBlock* Left = MakeBlock(World, Group, TEXT("Left"), FVector2D(60.0, 60.0), FVector2D(-40.0, 0.0), FColor(240, 80, 80));
	UDreamRectBlock* Right = MakeBlock(World, Group, TEXT("Right"), FVector2D(60.0, 60.0), FVector2D(40.0, 0.0), FColor(80, 80, 240));
	Stage.Frames();
	auto QuadWidth = [](const UDreamRectBlock* InBlock)
	{
		const FDreamUIGeometry* Geometry = InBlock->GetGeometry();
		if (Geometry == nullptr || Geometry->OriginVertices.Num() < 4)
		{
			return 0.0f;
		}
		float Min = TNumericLimits<float>::Max(), Max = -TNumericLimits<float>::Max();
		for (const FDreamUIOriginVertexData& Vertex : Geometry->OriginVertices)
		{
			Min = FMath::Min(Min, Vertex.Position.Y);
			Max = FMath::Max(Max, Vertex.Position.Y);
		}
		return Max - Min;
	};
	const float OwnWidth = QuadWidth(Left);
	TestFalse(TEXT("Without a group, the right block is not melted"), Right->IsMeltedIntoShapeBlend());

	UDreamShapeBlendGroup* Blend = Group->AddComponent<UDreamShapeBlendGroup>();
	// A gap of 20 between the two: closed in the middle only by a smoothness over 40 (a smooth minimum sinks k / 4 there).
	Blend->SetSmoothness(48.0f);
	Blend->Refresh();
	Stage.Frames();
	TestTrue(TEXT("The group is active"), Blend->IsActive());
	TestTrue(TEXT("The first member leads"), Blend->GetLeader() == Left && Left->IsShapeBlendLeader());
	TestTrue(TEXT("The second is melted and does not lead"), Right->IsMeltedIntoShapeBlend() && !Right->IsShapeBlendLeader());
	TestTrue(*FString::Printf(TEXT("The leader's quad covers both and the smoothness (%.1f, its own %.1f)"), QuadWidth(Left), OwnWidth), QuadWidth(Left) >= 140.0f + 2.0f * 48.0f - 1.0f);
	TestTrue(TEXT("The other member draws nothing of its own"), Near(QuadWidth(Right), 0.0f));

	// Clicks: on each block's middle, its own; in the melted bridge between them, the nearer; outside, neither.
	TestTrue(TEXT("A click on the left block's middle lands on it"), Hits(Left, OnCanvas(Stage.Root, FVector2D(-40.0, 0.0))) && !Hits(Right, OnCanvas(Stage.Root, FVector2D(-40.0, 0.0))));
	TestTrue(TEXT("...the right block's on it"), Hits(Right, OnCanvas(Stage.Root, FVector2D(40.0, 0.0))) && !Hits(Left, OnCanvas(Stage.Root, FVector2D(40.0, 0.0))));
	TestTrue(TEXT("In the bridge left of the middle, the left block"), Hits(Left, OnCanvas(Stage.Root, FVector2D(-4.0, 0.0))));
	TestTrue(TEXT("...though it is outside the left block's own rect"), -4.0 > -40.0 + 30.0);
	TestFalse(TEXT("Well away from both, nothing"), Hits(Left, OnCanvas(Stage.Root, FVector2D(0.0, 50.0))) || Hits(Right, OnCanvas(Stage.Root, FVector2D(0.0, 50.0))));

	// Moving a member moves the shape.
	FDreamUIShapeBlendShape Before;
	Blend->MakeShapeInFrame(Left->GetWidget(), Before);
	Right->GetWidget()->SetAnchoredPosition(FVector2D(80.0, 0.0));
	Blend->Refresh();
	FDreamUIShapeBlendShape After;
	Blend->MakeShapeInFrame(Left->GetWidget(), After);
	TestFalse(TEXT("Moved, the shape is another"), Before == After);
	if (After.NumMembers() == 2)
	{
		TestTrue(TEXT("...the right member 120 to the right of the leader's centre"), Near(After.Texels[1 + 4 + 1].X, 120.0f, 0.01f));
	}

	// A layer style on the group is drawn by the leader, round the melted shape.
	UDreamLayerStyle* Style = Group->AddComponent<UDreamLayerStyle>();
	FDreamLayerEffects Effects;
	FDreamStyleStroke& Stroke = Effects.Strokes.AddDefaulted_GetRef();
	Stroke.Size = 3.0f;
	Style->SetStyle(Effects);
	Stage.Frames();
	TestEqual(TEXT("A style on a shape blend group is drawn in its leader's pixels"), Style->GetPath(), EDreamLayerStylePath::Element);

	// Off: each on its own again.
	Blend->SetBlendEnabled(false);
	Stage.Frames();
	TestFalse(TEXT("Off, nothing is melted"), Left->IsMeltedIntoShapeBlend() || Right->IsMeltedIntoShapeBlend());
	TestTrue(TEXT("...and the right block draws itself"), QuadWidth(Right) > 50.0f);
	TestEqual(TEXT("...and the group's style is drawn apart, round the subtree"), Style->GetPath(), EDreamLayerStylePath::Group);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerLightTest,
	"DreamGUI.PointerLight.ItLightsTheBevelsAndRevealsTheBordersUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPointerLightTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* Panel = MakeWidget(World, Stage.Root, TEXT("Panel"), FVector2D(400.0, 200.0));
	UDreamRectBlock* Bevelled = MakeBlock(World, Panel, TEXT("Bevelled"), FVector2D(80.0, 80.0), FVector2D(-100.0, 0.0), FColor(120, 120, 140));
	FDreamLayerEffects Effects;
	Effects.BevelEmboss.bEnabled = true;
	UDreamLayerStyle* Style = Bevelled->GetWidget()->AddComponent<UDreamLayerStyle>();
	Style->SetStyle(Effects);
	UDreamRectBlock* Bordered = MakeBlock(World, Panel, TEXT("Bordered"), FVector2D(80.0, 80.0), FVector2D(100.0, 0.0), FColor(40, 40, 50));
	Bordered->SetEnableBorder(true);
	UDreamPointerLight* Light = Panel->AddComponent<UDreamPointerLight>();
	Light->SetFollowPointer(false);
	Light->SetLightPosition(FVector2D(-100.0, 30.0));
	Light->Update(0.0f);
	TestTrue(TEXT("Standing where it is put, it is at full strength"), Near(Light->GetCurrentIntensity(), 1.0f));
	TestTrue(TEXT("The bevel is lit by it"), Style->IsPointLit());
	TestTrue(TEXT("The border is revealed by it"), Bordered->HasRevealHighlight());
	// The bevel's light, in the bevelled block's frame: 30 above its centre is -30 there, +Y being down.
	const FVector4f* R = Style->GetRecord().Texels.GetData();
	const int32 Kinds = FMath::RoundToInt(R[0].X);
	bool bFound = false;
	for (int32 Index = 0; Index < Kinds; ++Index)
	{
		const FVector4f* E = R + DreamUILayerStyleLayout::HeaderTexels + Index * DreamUILayerStyleLayout::EntryTexels;
		if (FMath::RoundToInt(E[0].X) == DreamUILayerStyleLayout::KindBevel)
		{
			bFound = true;
			TestTrue(TEXT("The bevel's record says it is lit by a point light"), (FMath::RoundToInt(E[0].W) & DreamUILayerStyleLayout::EntryFlagPointLight) != 0);
			TestTrue(*FString::Printf(TEXT("...standing over its centre, 30 up (%.2f, %.2f)"), E[6].X, E[6].Y), Near(E[6].X, 0.0f, 0.01f) && Near(E[6].Y, -30.0f, 0.01f));
		}
	}
	TestTrue(TEXT("The record has the bevel"), bFound);
	// Removed, everything goes back to its own light.
	Panel->RemoveComponent(Light);
	TestFalse(TEXT("Gone, the bevel is lit by its own light again"), Style->IsPointLit());
	TestFalse(TEXT("...and the border is its own"), Bordered->HasRevealHighlight());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTiltCardTest,
	"DreamGUI.TiltCard.ItSpringsTowardsItsTargetAndBackFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTiltCardTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Card = MakeWidget(Stage.World.World, Stage.Root, TEXT("Card"), FVector2D(160.0, 220.0));
	UDreamTiltCard* Tilt = Card->AddComponent<UDreamTiltCard>();
	Tilt->SetFollowPointer(false);
	Tilt->SetTargetTilt(FVector2D(1.0, 0.0));
	for (int32 Step = 0; Step < 120; ++Step)
	{
		Tilt->Step(1.0f / 60.0f);
	}
	TestTrue(*FString::Printf(TEXT("After two seconds it has settled at its target (%.3f, %.3f)"), Tilt->GetTilt().X, Tilt->GetTilt().Y), Near(Tilt->GetTilt().X, 1.0f, 0.02f) && Near(Tilt->GetTilt().Y, 0.0f, 0.02f));
	// Its right edge dips away: a negative yaw (a positive one brings that edge towards the viewer).
	TestTrue(*FString::Printf(TEXT("...turned about its up axis by its most, right edge away (%.2f)"), Card->GetRenderRotation().Yaw), Near(Card->GetRenderRotation().Yaw, -12.0f, 0.3f));
	TestTrue(TEXT("...and grown a little"), Card->GetRenderScale().Y > 1.02);
	Tilt->SetTargetTilt(FVector2D::ZeroVector);
	for (int32 Step = 0; Step < 120; ++Step)
	{
		Tilt->Step(1.0f / 60.0f);
	}
	TestTrue(TEXT("Back to no target, flat again"), Near(Card->GetRenderRotation().Yaw, 0.0f, 0.3f) && Near(Card->GetRenderScale().Y, 1.0f, 0.01f));
	Card->RemoveComponent(Tilt);
	TestTrue(TEXT("Gone, its render transform is cleared"), Card->GetRenderRotation().IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHoloFoilTest,
	"DreamGUI.HoloFoil.ItDrawsItsElementWithItsMaterialAndPutsTheOldOneBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHoloFoilTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamRectBlock* Card = MakeBlock(Stage.World.World, Stage.Root, TEXT("Card"), FVector2D(160.0, 220.0), FVector2D::ZeroVector, FColor(30, 40, 70));
	UDreamHoloFoil* Foil = Card->GetWidget()->AddComponent<UDreamHoloFoil>();
	if (!TestNotNull(TEXT("The foil made its material (DreamUI_HoloFoilRect)"), Foil->GetFoilMaterial()))
	{
		return false;
	}
	TestTrue(TEXT("The rect block draws with it"), Card->GetMaterialOverride() == Foil->GetFoilMaterial());
	TestFalse(TEXT("...and so not with the built-in shader"), Card->IsDrawnByBuiltInShader());
	Foil->SetViewSource(EDreamHoloFoilView::Manual);
	Foil->SetManualView(FVector2D(0.5, -0.25));
	Foil->Update(0.0f);
	FLinearColor View;
	View = Foil->GetFoilMaterial()->K2_GetVectorParameterValue(FName(TEXT("DreamUI_FoilView")));
	TestTrue(TEXT("Its view is the material's"), Near(View.R, 0.5f) && Near(View.G, -0.25f));
	TArray<FColor> Texels;
	UDreamHoloFoil::BakeRainbow(FDreamStyleGradient::FromColors(FColor::Red, FColor::Blue), Texels, 16);
	TestTrue(TEXT("Its rainbow is its gradient, end to end"), Texels.Num() == 16 && Texels[0] == FColor(255, 0, 0, 255) && Texels[15] == FColor(0, 0, 255, 255));
	Card->GetWidget()->RemoveComponent(Foil);
	TestNull(TEXT("Gone, the rect block draws with its own material again"), Card->GetMaterialOverride());

	// On a texture with a material of the author's: drawn with the foil's while it is on, the author's left as it was -- what
	// a save or a copy of the texture then holds -- and drawn with again once the foil goes.
	UDreamWidget* Picture = MakeWidget(Stage.World.World, Stage.Root, TEXT("Picture"), FVector2D(120.0, 160.0));
	UDreamTexture* Texture = Picture->CreateNewVisual<UDreamTexture>();
	UMaterialInterface* AuthorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamGUI/Materials/DreamUI_HoloFoil.DreamUI_HoloFoil"));
	if (!TestNotNull(TEXT("A texture and a material of the author's"), Texture) || AuthorMaterial == nullptr)
	{
		return false;
	}
	Texture->SetOverrideMaterial(AuthorMaterial);
	UDreamHoloFoil* PictureFoil = Picture->AddComponent<UDreamHoloFoil>();
	TestTrue(TEXT("The texture draws with the foil's material"), PictureFoil->GetFoilMaterial() != nullptr && Texture->GetMaterialOverride() == PictureFoil->GetFoilMaterial());
	TestTrue(TEXT("...and keeps the author's own"), Texture->GetOverrideMaterial() == AuthorMaterial);
	Picture->RemoveComponent(PictureFoil);
	TestTrue(TEXT("Gone, the texture draws with the author's material again"), Texture->GetMaterialOverride() == nullptr && Texture->GetOverrideMaterial() == AuthorMaterial);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
