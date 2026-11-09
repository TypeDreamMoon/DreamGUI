// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/DreamLayerStyleTypes.h"
#include "Core/DreamUILayerStyleMath.h"
#include "DreamUIRender/DreamUILayerStyleRecord.h"

/*
 * An effect's light read and written where it is kept (DreamLayerStyleLight): an effect that uses the global light takes the
 * style's and turns it, one that does not keeps its own; a shadow's offset in widget space is the one the renderer packs,
 * turned over; and an offset gives back the angle and the distance it came from.
 */
namespace DreamLayerStyleLightTestLocal
{
	bool Near(float A, float B, float Tolerance = 1e-3f)
	{
		return FMath::Abs(A - B) <= Tolerance;
	}

	/** The smaller way round from one angle to another, in degrees. */
	float AngleBetween(float A, float B)
	{
		return FMath::Abs(DreamLayerStyleLight::NormalizeAngle(A - B));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleLightAngleTest,
	"DreamGUI.LayerStyle.Light.AnEffectOnTheGlobalLightTurnsItAndOneOffItTurnsItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStyleLightAngleTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleLightTestLocal;
	FDreamLayerEffects Style;
	Style.GlobalLightAngle = 120.0f;
	FDreamStyleDropShadow& Global = Style.DropShadows.AddDefaulted_GetRef();
	Global.bUseGlobalLight = true;
	Global.Angle = 45.0f;
	FDreamStyleInnerShadow& Own = Style.InnerShadows.AddDefaulted_GetRef();
	Own.bUseGlobalLight = false;
	Own.Angle = -30.0f;

	TestEqual(TEXT("An effect on the global light reads the global angle"), DreamLayerStyleLight::GetAngle(Style, Global.bUseGlobalLight, Global.Angle), 120.0f);
	TestEqual(TEXT("...one off it, its own"), DreamLayerStyleLight::GetAngle(Style, Own.bUseGlobalLight, Own.Angle), -30.0f);

	DreamLayerStyleLight::SetAngle(Style, Global.bUseGlobalLight, Global.Angle, 200.0f);
	TestEqual(TEXT("Turning the first turns the global light, wrapped into (-180, 180]"), Style.GlobalLightAngle, -160.0f);
	TestEqual(TEXT("...and leaves its own angle as it was"), Global.Angle, 45.0f);
	DreamLayerStyleLight::SetAngle(Style, Own.bUseGlobalLight, Own.Angle, 10.0f);
	TestEqual(TEXT("Turning the second turns it alone"), Own.Angle, 10.0f);
	TestEqual(TEXT("...the global light staying where it was"), Style.GlobalLightAngle, -160.0f);

	float OwnAltitude = 45.0f;
	DreamLayerStyleLight::SetAltitude(Style, /*bInUseGlobalLight*/ true, OwnAltitude, 120.0f);
	TestEqual(TEXT("The global altitude is held to [0, 90]"), Style.GlobalLightAltitude, 90.0f);
	TestEqual(TEXT("...the effect's own untouched"), OwnAltitude, 45.0f);
	TestEqual(TEXT("And read back from where it is kept"), DreamLayerStyleLight::GetAltitude(Style, true, OwnAltitude), 90.0f);

	TestEqual(TEXT("180 stays 180"), DreamLayerStyleLight::NormalizeAngle(180.0f), 180.0f);
	TestEqual(TEXT("-180 is 180"), DreamLayerStyleLight::NormalizeAngle(-180.0f), 180.0f);
	TestEqual(TEXT("540 is 180"), DreamLayerStyleLight::NormalizeAngle(540.0f), 180.0f);
	TestEqual(TEXT("-190 is 170"), DreamLayerStyleLight::NormalizeAngle(-190.0f), 170.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleLightOffsetTest,
	"DreamGUI.LayerStyle.Light.AShadowsOffsetIsTheOneTheRendererPacksAndGivesBackItsAngleAndDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStyleLightOffsetTest::RunTest(const FString&)
{
	using namespace DreamLayerStyleLightTestLocal;
	using namespace DreamUILayerStyleLayout;

	const FVector2f Below = DreamLayerStyleLight::GetShadowOffset(90.0f, 4.0f);
	TestTrue(*FString::Printf(TEXT("A light from above casts the shadow 4 down (+Y up): (%.3f, %.3f)"), Below.X, Below.Y), Near(Below.X, 0.0f) && Near(Below.Y, -4.0f));
	const FVector2f Left = DreamLayerStyleLight::GetShadowOffset(0.0f, 3.0f);
	TestTrue(TEXT("...a light from the right casts it to the left"), Near(Left.X, -3.0f) && Near(Left.Y, 0.0f));

	// What the renderer packs: +Y down, so the same offset turned over.
	for (const float Angle : { -150.0f, -45.0f, 0.0f, 30.0f, 120.0f, 179.0f })
	{
		FDreamLayerEffects Style;
		FDreamStyleDropShadow& Shadow = Style.DropShadows.AddDefaulted_GetRef();
		Shadow.bUseGlobalLight = false;
		Shadow.Angle = Angle;
		Shadow.Distance = 7.0f;
		Style.Scale = 1.5f;
		FDreamUILayerStyleRecord Record;
		if (!TestTrue(TEXT("It packs"), FDreamUILayerStyleMath::Pack(Style, Record)))
		{
			return false;
		}
		const FVector4f Entry = Record.Texels[HeaderTexels + 2];
		const FVector2f Offset = DreamLayerStyleLight::GetShadowOffset(Angle, 7.0f * 1.5f);
		TestTrue(*FString::Printf(TEXT("At %.0f degrees the packed offset (%.4f, %.4f) is (%.4f, %.4f) turned over"), Angle, Entry.Z, Entry.W, Offset.X, Offset.Y),
			Entry.Z == Offset.X && Entry.W == -Offset.Y);
	}

	// An offset gives back its angle and its distance.
	FRandomStream Random(1109);
	int32 Wrong = 0;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const float Angle = Random.FRandRange(-179.9f, 180.0f);
		const float Distance = Random.FRandRange(0.5f, 80.0f);
		float BackAngle = 0.0f;
		float BackDistance = 0.0f;
		DreamLayerStyleLight::GetAngleAndDistance(DreamLayerStyleLight::GetShadowOffset(Angle, Distance), BackAngle, BackDistance);
		if (AngleBetween(Angle, BackAngle) > 1e-2f || !Near(Distance, BackDistance, 1e-3f))
		{
			++Wrong;
		}
	}
	TestEqual(TEXT("Two hundred offsets give back the angle and the distance they came from"), Wrong, 0);
	float Kept = 33.0f;
	float None = -1.0f;
	DreamLayerStyleLight::GetAngleAndDistance(FVector2f::ZeroVector, Kept, None);
	TestTrue(TEXT("A zero offset keeps the angle it is given, at no distance"), Kept == 33.0f && None == 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
