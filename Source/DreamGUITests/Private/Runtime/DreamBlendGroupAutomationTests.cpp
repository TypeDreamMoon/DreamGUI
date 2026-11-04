// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIClipData.h"
#include "DreamUIRender/DreamUICompositor.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Utils/DreamUIUtils.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * The blend modes and the groups, on the CPU: the formulas held to the W3C definitions and to the shader's numbers, the blend
 * hardware's modes held to the composite they stand for, the batching of the modes that read what is behind, and the
 * markers a canvas puts around what it draws apart. The pictures are in RHI/DreamBlendGroupPixelAutomationTests.cpp.
 */
namespace DreamBlendGroupTestLocal
{
	bool Near(float A, float B, float Tolerance = 1e-4f)
	{
		return FMath::Abs(A - B) <= Tolerance;
	}

	bool Near(const FVector3f& A, const FVector3f& B, float Tolerance = 1e-4f)
	{
		return Near(A.X, B.X, Tolerance) && Near(A.Y, B.Y, Tolerance) && Near(A.Z, B.Z, Tolerance);
	}

	bool Near(const FVector4f& A, const FVector4f& B, float Tolerance = 1e-4f)
	{
		return Near(A.X, B.X, Tolerance) && Near(A.Y, B.Y, Tolerance) && Near(A.Z, B.Z, Tolerance) && Near(A.W, B.W, Tolerance);
	}

	FString Describe(const FVector4f& V)
	{
		return FString::Printf(TEXT("(%.4f, %.4f, %.4f, %.4f)"), V.X, V.Y, V.Z, V.W);
	}

	FString ModeName(EDreamUIBlendMode InMode)
	{
		return StaticEnum<EDreamUIBlendMode>()->GetNameStringByValue(static_cast<int64>(InMode));
	}

	EDreamUIBlendMode ModeAt(int32 InIndex)
	{
		return static_cast<EDreamUIBlendMode>(InIndex);
	}

	/** A registered widget of InSize under InParent, its centre at InPosition from the parent's. */
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

	/** A white block: what batches with every other one, wherever it is. */
	UDreamWidget* MakeBlock(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector
		, EDreamUIBlendMode InMode = EDreamUIBlendMode::Alpha)
	{
		UDreamWidget* Widget = MakeWidget(InWorld, InParent, InName, InSize, InPosition);
		if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			Visual->SetBlendMode(InMode);
		}
		return Widget;
	}

	/** A game world with a UI manager and a screen-space canvas root, which draws groups apart (InMode by default). */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;

		explicit FStage(EDreamRenderMode InMode = EDreamRenderMode::ScreenSpaceOverlay)
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(World.World, nullptr, TEXT("Root"), FVector2D(800.0, 600.0));
			Canvas = Root->AddComponent<UDreamCanvas>();
			if (Canvas != nullptr)
			{
				Canvas->SetRenderMode(InMode);
				// No viewport here, so a screen-space canvas has no rect to speak of: nothing is culled by it.
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
		const TArray<FDreamUIDrawCall>& DrawCalls() const { return Canvas->GetCurrentDrawCalls(); }
		/** The draw calls as letters: B a batch, ( a group opened, ) a group closed, anything else ?. */
		FString Shape() const
		{
			FString Out;
			for (const FDreamUIDrawCall& DrawCall : DrawCalls())
			{
				switch (DrawCall.Type)
				{
				case EDreamUIDrawCallType::BatchMesh: Out += TEXT("B"); break;
				case EDreamUIDrawCallType::GroupBegin: Out += TEXT("("); break;
				case EDreamUIDrawCallType::GroupEnd: Out += TEXT(")"); break;
				default: Out += TEXT("?"); break;
				}
			}
			return Out;
		}
	};

	/** A quad of InMode, as the geometry stage leaves it, wrapped as the canvas's prepare wraps one. */
	FDreamUIRenderData MakeQuad(const FVector2D& InMin, const FVector2D& InMax, EDreamUIBlendMode InMode)
	{
		FDreamUIGeometry Geo;
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMin.X, (float)InMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMax.X, (float)InMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMin.X, (float)InMax.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMax.X, (float)InMax.Y)));
		Geo.Triangles = { 0, 3, 2, 0, 1, 3 };
		Geo.BoundsMin2DInCanvasSpace = InMin;
		Geo.BoundsMax2DInCanvasSpace = InMax;
		Geo.bSupportDrawcallBatching = true;
		Geo.BlendMode = InMode;
		FDreamUIRenderData RenderData(EDreamUIDrawCallType::BatchMesh);
		const TSharedRef<FDreamUIGeometry> Prepared = MakeShared<FDreamUIGeometry>();
		Prepared->CopyDataForPrepare(Geo);
		RenderData.BatchMeshGeometry = Prepared;
		return RenderData;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendModeNumbersTest,
	"DreamGUI.Blend.TheModeNumbersAreTheShadersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBlendModeNumbersTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}
	FString Source;
	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private/DreamUIBlend.ush"));
	if (!TestTrue(TEXT("DreamUIBlend.ush is read"), FFileHelper::LoadFileToString(Source, *Path)))
	{
		return false;
	}
	TMap<FString, int64> Defines;
	FRegexMatcher Matcher(FRegexPattern(TEXT("#define\\s+DREAMUI_BLEND_([A-Z_]+)\\s+(0x[0-9A-Fa-f]+u?|[0-9]+)")), Source);
	while (Matcher.FindNext())
	{
		FString Value = Matcher.GetCaptureGroup(2);
		Value.RemoveFromEnd(TEXT("u"));
		Defines.Add(Matcher.GetCaptureGroup(1), Value.StartsWith(TEXT("0x")) ? FParse::HexNumber64(*Value.Mid(2)) : FCString::Atoi64(*Value));
	}
	const int64* Seed = Defines.Find(TEXT("DISSOLVE_SEED"));
	TestTrue(TEXT("The dissolve seed is the CPU's"), Seed != nullptr && static_cast<uint32>(*Seed) == FDreamUIBlendMath::DissolveSeed);
	Defines.Remove(TEXT("DISSOLVE_SEED"));
	TestEqual(TEXT("The shader numbers every mode, and nothing else"), Defines.Num(), DreamUIBlend::NumModes);
	for (int32 Index = 0; Index < DreamUIBlend::NumModes; ++Index)
	{
		// ColorBurn -> COLOR_BURN
		const FString Name = ModeName(ModeAt(Index));
		FString Upper;
		for (int32 Char = 0; Char < Name.Len(); ++Char)
		{
			if (Char > 0 && FChar::IsUpper(Name[Char]))
			{
				Upper += TEXT("_");
			}
			Upper += FChar::ToUpper(Name[Char]);
		}
		const int64* Value = Defines.Find(Upper);
		TestTrue(FString::Printf(TEXT("%s is DREAMUI_BLEND_%s = %d"), *Name, *Upper, Index), Value != nullptr && *Value == Index);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendW3CTest,
	"DreamGUI.Blend.EachModeIsTheW3CDefinitionAtKnownPoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBlendW3CTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	using M = EDreamUIBlendMode;
	// One channel: what is behind B, the element S, and what W3C Compositing and Blending Level 1 (and, for the modes it does
	// not define, the image editors' own definitions) make of them, worked out by hand.
	struct FCase { M Mode; float B; float S; float Expected; };
	const FCase Cases[] =
	{
		{ M::Alpha, 0.3f, 0.6f, 0.6f },
		{ M::Multiply, 0.5f, 0.4f, 0.2f },
		{ M::Screen, 0.5f, 0.4f, 0.7f },
		{ M::Overlay, 0.3f, 0.6f, 0.36f },
		{ M::Overlay, 0.7f, 0.6f, 0.76f },
		{ M::Darken, 0.3f, 0.6f, 0.3f },
		{ M::Lighten, 0.3f, 0.6f, 0.6f },
		{ M::ColorDodge, 0.4f, 0.5f, 0.8f },
		{ M::ColorDodge, 0.6f, 0.5f, 1.0f },
		{ M::ColorDodge, 0.0f, 1.0f, 0.0f },
		{ M::ColorDodge, 0.2f, 1.0f, 1.0f },
		{ M::ColorBurn, 0.6f, 0.5f, 0.2f },
		{ M::ColorBurn, 0.4f, 0.5f, 0.0f },
		{ M::ColorBurn, 1.0f, 0.0f, 1.0f },
		{ M::ColorBurn, 0.8f, 0.0f, 0.0f },
		{ M::HardLight, 0.6f, 0.3f, 0.36f },
		{ M::HardLight, 0.6f, 0.7f, 0.76f },
		{ M::SoftLight, 0.25f, 0.25f, 0.15625f },
		{ M::SoftLight, 0.64f, 0.75f, 0.72f },
		{ M::SoftLight, 0.2f, 0.75f, 0.324f },
		{ M::Difference, 0.3f, 0.8f, 0.5f },
		{ M::Exclusion, 0.3f, 0.8f, 0.62f },
		{ M::LinearBurn, 0.7f, 0.6f, 0.3f },
		{ M::LinearBurn, 0.3f, 0.4f, 0.0f },
		{ M::VividLight, 0.6f, 0.25f, 0.2f },
		{ M::VividLight, 0.4f, 0.75f, 0.8f },
		{ M::LinearLight, 0.5f, 0.6f, 0.7f },
		{ M::LinearLight, 0.1f, 0.2f, 0.0f },
		{ M::PinLight, 0.7f, 0.25f, 0.5f },
		{ M::PinLight, 0.2f, 0.8f, 0.6f },
		{ M::HardMix, 0.5f, 0.6f, 1.0f },
		{ M::HardMix, 0.3f, 0.6f, 0.0f },
		{ M::Subtract, 0.7f, 0.2f, 0.5f },
		{ M::Subtract, 0.2f, 0.7f, 0.0f },
		{ M::Divide, 0.4f, 0.8f, 0.5f },
		{ M::Divide, 0.6f, 0.5f, 1.0f },
		{ M::Divide, 0.3f, 0.0f, 1.0f },
		{ M::Additive, 0.5f, 0.6f, 1.0f },
		{ M::Additive, 0.2f, 0.3f, 0.5f },
	};
	for (const FCase& Case : Cases)
	{
		const float Got = FDreamUIBlendMath::Function(Case.Mode, FVector3f(Case.B), FVector3f(Case.S)).X;
		TestTrue(FString::Printf(TEXT("%s(%.2f, %.2f) is %.5f (got %.5f)"), *ModeName(Case.Mode), Case.B, Case.S, Case.Expected, Got), Near(Got, Case.Expected));
	}
	// The modes of whole colours.
	const FVector3f Red(1.0f, 0.0f, 0.0f);
	const FVector3f Grey(0.5f, 0.5f, 0.5f);
	const FVector3f Blueish(0.2f, 0.4f, 0.6f);
	// SetLum(red, 0.5) = (1.2, 0.2, 0.2) clipped about its luminosity 0.5: 0.5 + (0.7, -0.3, -0.3) * 0.5 / 0.7.
	const FVector3f RedAtHalf(1.0f, 0.5f - 0.3f * 0.5f / 0.7f, 0.5f - 0.3f * 0.5f / 0.7f);
	TestTrue(TEXT("Luminosity of grey over red is red at grey's luminosity"), Near(FDreamUIBlendMath::Function(M::Luminosity, Red, Grey), RedAtHalf));
	TestTrue(TEXT("Color of red over grey is the same"), Near(FDreamUIBlendMath::Function(M::Color, Grey, Red), RedAtHalf));
	// Hue: SetSat(red, Sat(blueish) = 0.4) = (0.4, 0, 0), then SetLum to Lum(blueish) = 0.362: + 0.242 each.
	TestTrue(TEXT("Hue of red over a blue"), Near(FDreamUIBlendMath::Function(M::Hue, Blueish, Red), FVector3f(0.642f, 0.242f, 0.242f)));
	// Saturation: SetSat(blueish, 1) = (0, 0.5, 1), SetLum to 0.362 = (-0.043, 0.457, 0.957), clipped below about 0.362.
	const float K = 0.362f / (0.362f + 0.043f);
	TestTrue(TEXT("Saturation of red over a blue"), Near(FDreamUIBlendMath::Function(M::Saturation, Blueish, Red),
		FVector3f(0.362f - 0.405f * K, 0.362f + 0.095f * K, 0.362f + 0.595f * K), 1e-3f));
	TestTrue(TEXT("Darker colour keeps what is behind when it is darker"), Near(FDreamUIBlendMath::Function(M::DarkerColor, Blueish, Grey), Blueish));
	TestTrue(TEXT("Lighter colour takes the element when it is lighter"), Near(FDreamUIBlendMath::Function(M::LighterColor, Blueish, Grey), Grey));
	// The sRGB curve both ways.
	for (float X = 0.0f; X <= 1.0f; X += 0.0625f)
	{
		TestTrue(FString::Printf(TEXT("sRGB at %.4f comes back"), X), Near(FDreamUIBlendMath::ToLinearChannel(FDreamUIBlendMath::ToSrgbChannel(X)), X, 1e-5f));
	}
	TestTrue(TEXT("Linear 0.5 is 0.7354 encoded"), Near(FDreamUIBlendMath::ToSrgbChannel(0.5f), 0.735357f, 1e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendHardwareTest,
	"DreamGUI.Blend.TheHardwareModesAreTheCompositeAtAnyAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBlendHardwareTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	using M = EDreamUIBlendMode;
	// Over an opaque backdrop, which is what the hardware modes are for: the shader's output through the mode's blend state
	// is the W3C composite, at every alpha. Additive is the one exception the hardware has always had, and is held to the
	// composite only where nothing is clamped.
	const M Modes[] = { M::Alpha, M::Multiply, M::Screen, M::Exclusion };
	const float Alphas[] = { 0.0f, 0.25f, 0.5f, 0.8f, 1.0f };
	const FVector3f Behind[] = { FVector3f(0.1f, 0.5f, 0.9f), FVector3f(0.8f, 0.3f, 0.0f), FVector3f(1.0f, 1.0f, 1.0f) };
	const FVector3f Elements[] = { FVector3f(0.9f, 0.2f, 0.4f), FVector3f(0.0f, 0.6f, 1.0f), FVector3f(0.5f, 0.5f, 0.5f) };
	for (M Mode : Modes)
	{
		for (const FVector3f& Cb : Behind)
		{
			for (const FVector3f& Cs : Elements)
			{
				for (float Sa : Alphas)
				{
					const FVector4f Out = FDreamUIBlendMath::FixedOutput(Mode, Cs * Sa, Sa);
					const FVector4f Got = FDreamUIBlendMath::ApplyFixedBlendState(Mode, Out, FVector4f(Cb.X, Cb.Y, Cb.Z, 1.0f));
					const FVector4f Want = FDreamUIBlendMath::ReferenceComposite(Mode, Cb, 1.0f, Cs, Sa);
					TestTrue(FString::Printf(TEXT("%s at alpha %.2f: %s is %s"), *ModeName(Mode), Sa, *Describe(Got), *Describe(Want)), Near(Got, Want));
				}
			}
		}
	}
	for (float Sa : Alphas)
	{
		const FVector3f Cb(0.2f, 0.3f, 0.1f);
		const FVector3f Cs(0.4f, 0.5f, 0.6f);
		const FVector4f Got = FDreamUIBlendMath::ApplyFixedBlendState(M::Additive, FDreamUIBlendMath::FixedOutput(M::Additive, Cs * Sa, Sa), FVector4f(Cb.X, Cb.Y, Cb.Z, 1.0f));
		const FVector4f Want = FDreamUIBlendMath::ReferenceComposite(M::Additive, Cb, 1.0f, Cs, Sa);
		TestTrue(FString::Printf(TEXT("Additive at alpha %.2f, unclamped"), Sa), Near(Got, Want));
	}
	// Dissolve is Normal at whatever alpha its pattern leaves: 0 or 1.
	for (float Sa : { 0.0f, 1.0f })
	{
		const FVector3f Cb(0.2f, 0.3f, 0.1f);
		const FVector3f Cs(0.4f, 0.5f, 0.6f);
		const FVector4f Got = FDreamUIBlendMath::ApplyFixedBlendState(M::Dissolve, FDreamUIBlendMath::FixedOutput(M::Dissolve, Cs * Sa, Sa), FVector4f(Cb.X, Cb.Y, Cb.Z, 1.0f));
		TestTrue(TEXT("Dissolve at a whole alpha is Normal"), Near(Got, FDreamUIBlendMath::ReferenceComposite(M::Alpha, Cb, 1.0f, Cs, Sa)));
	}
	// Which modes the hardware does, and which read what is behind.
	int32 Hardware = 0;
	for (int32 Index = 0; Index < DreamUIBlend::NumModes; ++Index)
	{
		Hardware += DreamUIBlend::ReadsBackdrop(ModeAt(Index)) ? 0 : 1;
	}
	TestEqual(TEXT("Six modes are the hardware's"), Hardware, 6);
	TestTrue(TEXT("Subtract reads what is behind: the hardware would clamp a partly transparent one"), DreamUIBlend::ReadsBackdrop(M::Subtract));
	TestTrue(TEXT("Linear burn reads what is behind, for the same reason"), DreamUIBlend::ReadsBackdrop(M::LinearBurn));
	// The case that sent them there: the hardware's D - Sa * Cs, clamped, is not the composite.
	{
		const FVector3f Cb(0.2f);
		const FVector3f Cs(0.6f);
		const FVector4f Want = FDreamUIBlendMath::ReferenceComposite(M::Subtract, Cb, 1.0f, Cs, 0.5f);
		TestTrue(TEXT("Half a lighter colour subtracted keeps half of what was behind"), Near(Want.X, 0.1f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendOverBackdropTest,
	"DreamGUI.Blend.ADrawOverItsBackdropIsTheW3CComposite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBlendOverBackdropTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	// The shader's output for a mode that reads what is behind, put over the target by One / InverseSourceAlpha, is the W3C
	// composite -- over an opaque backdrop, a partly transparent one and nothing at all -- for every mode, encoded or linear.
	const float Alphas[] = { 0.0f, 0.3f, 1.0f };
	const float BackdropAlphas[] = { 1.0f, 0.6f, 0.0f };
	const FVector3f Behind[] = { FVector3f(0.1f, 0.5f, 0.9f), FVector3f(0.8f, 0.3f, 0.05f) };
	const FVector3f Elements[] = { FVector3f(0.9f, 0.2f, 0.4f), FVector3f(0.3f, 0.6f, 1.0f) };
	int32 Checked = 0;
	for (int32 Index = 0; Index < DreamUIBlend::NumModes; ++Index)
	{
		const EDreamUIBlendMode Mode = ModeAt(Index);
		if (!DreamUIBlend::ReadsBackdrop(Mode))
		{
			continue;
		}
		for (const bool bLinear : { false, true })
		{
			for (const FVector3f& Cb : Behind)
			{
				for (float Ab : BackdropAlphas)
				{
					for (const FVector3f& Cs : Elements)
					{
						for (float Sa : Alphas)
						{
							const FVector4f Backdrop(Cb.X * Ab, Cb.Y * Ab, Cb.Z * Ab, Ab);
							const FVector4f Out = FDreamUIBlendMath::OverBackdrop(Mode, Cs, Sa, Backdrop, bLinear, false);
							const FVector4f Got(Out.X + Backdrop.X * (1.0f - Sa), Out.Y + Backdrop.Y * (1.0f - Sa), Out.Z + Backdrop.Z * (1.0f - Sa), Out.W + Ab * (1.0f - Sa));
							// The reference in the blend's space: encoded colours for a linear target, decoded after.
							FVector4f Want;
							if (bLinear)
							{
								const FVector3f B = FDreamUIBlendMath::ToLinear(FDreamUIBlendMath::Function(Mode, FDreamUIBlendMath::ToSrgb(Cb), FDreamUIBlendMath::ToSrgb(Cs)));
								const FVector3f Mixed = Cs * (1.0f - Ab) + B * Ab;
								const FVector3f Co = Mixed * Sa + Cb * Ab * (1.0f - Sa);
								Want = FVector4f(Co.X, Co.Y, Co.Z, Sa + Ab * (1.0f - Sa));
							}
							else
							{
								Want = FDreamUIBlendMath::ReferenceComposite(Mode, Cb, Ab, Cs, Sa);
							}
							++Checked;
							if (!Near(Got, Want, 1e-4f))
							{
								AddError(FString::Printf(TEXT("%s (%s) at alpha %.1f over alpha %.1f: %s, not %s"), *ModeName(Mode), bLinear ? TEXT("linear") : TEXT("encoded")
									, Sa, Ab, *Describe(Got), *Describe(Want)));
							}
						}
					}
				}
			}
		}
	}
	TestTrue(TEXT("Every mode that reads what is behind was checked"), Checked == 21 * 2 * 2 * 3 * 2 * 3);
	// A viewport's picture is opaque whatever its alpha says.
	const FVector4f Opaque = FDreamUIBlendMath::OverBackdrop(EDreamUIBlendMode::Difference, FVector3f(1.0f), 1.0f, FVector4f(0.25f, 0.5f, 0.75f, 0.0f), false, true);
	TestTrue(TEXT("Difference with white over a viewport inverts it, its alpha 0 or not"), Near(Opaque, FVector4f(0.75f, 0.5f, 0.25f, 1.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendDissolveTest,
	"DreamGUI.Blend.DissolveKeepsAsManyPixelsAsItsAlphaSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBlendDissolveTest::RunTest(const FString& Parameters)
{
	int32 AtThird = 0;
	int32 AtTwoThirds = 0;
	int32 Grown = 0;
	constexpr int32 Side = 96;
	for (int32 Y = -Side / 2; Y < Side / 2; ++Y)
	{
		for (int32 X = -Side / 2; X < Side / 2; ++X)
		{
			const float A = FDreamUIBlendMath::DissolveAlpha(1.0f / 3.0f, X, Y);
			const float B = FDreamUIBlendMath::DissolveAlpha(2.0f / 3.0f, X, Y);
			AtThird += A > 0.5f ? 1 : 0;
			AtTwoThirds += B > 0.5f ? 1 : 0;
			// A pixel drawn at the lower alpha is drawn at the higher: a fade in only adds pixels.
			Grown += (A > 0.5f && B < 0.5f) ? 1 : 0;
		}
	}
	const float Total = Side * Side;
	TestTrue(FString::Printf(TEXT("A third of the pixels at a third (%.3f)"), AtThird / Total), FMath::Abs(AtThird / Total - 1.0f / 3.0f) < 0.02f);
	TestTrue(FString::Printf(TEXT("Two thirds at two thirds (%.3f)"), AtTwoThirds / Total), FMath::Abs(AtTwoThirds / Total - 2.0f / 3.0f) < 0.02f);
	TestEqual(TEXT("No pixel drawn at a third is left out at two thirds"), Grown, 0);
	TestEqual(TEXT("Alpha 1 keeps every pixel"), FDreamUIBlendMath::DissolveAlpha(1.0f, 7, -3), 1.0f);
	TestEqual(TEXT("Alpha 0 keeps none"), FDreamUIBlendMath::DissolveAlpha(0.0f, 7, -3), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendBackdropBatchingTest,
	"DreamGUI.Blend.ElementsOfABackdropModeShareADrawOnlyWhileTheyDoNotOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBlendBackdropBatchingTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	const FVector2D CanvasMin(-500.0, -500.0);
	const FVector2D CanvasMax(500.0, 500.0);
	auto Count = [&](TArray<FDreamUIRenderData>&& InData)
	{
		TArray<FDreamUIDrawCall> DrawCalls;
		UDreamCanvas::BatchDrawCallAsync(CanvasMin, CanvasMax, InData, DrawCalls);
		return DrawCalls.Num();
	};
	{
		TArray<FDreamUIRenderData> Data;
		Data.Add(MakeQuad(FVector2D(0.0, 0.0), FVector2D(50.0, 50.0), EDreamUIBlendMode::Darken));
		Data.Add(MakeQuad(FVector2D(100.0, 0.0), FVector2D(150.0, 50.0), EDreamUIBlendMode::Darken));
		TestEqual(TEXT("Two apart share a draw and a copy"), Count(MoveTemp(Data)), 1);
	}
	{
		TArray<FDreamUIRenderData> Data;
		Data.Add(MakeQuad(FVector2D(0.0, 0.0), FVector2D(50.0, 50.0), EDreamUIBlendMode::Darken));
		Data.Add(MakeQuad(FVector2D(25.0, 25.0), FVector2D(75.0, 75.0), EDreamUIBlendMode::Darken));
		TestEqual(TEXT("Two that overlap are drawn apart, the second over the first"), Count(MoveTemp(Data)), 2);
	}
	{
		TArray<FDreamUIRenderData> Data;
		Data.Add(MakeQuad(FVector2D(0.0, 0.0), FVector2D(50.0, 50.0), EDreamUIBlendMode::Screen));
		Data.Add(MakeQuad(FVector2D(25.0, 25.0), FVector2D(75.0, 75.0), EDreamUIBlendMode::Screen));
		TestEqual(TEXT("A hardware mode batches overlapping elements as Normal does"), Count(MoveTemp(Data)), 1);
	}
	{
		// The third overlaps the first only: it joins the second's draw, after the first's.
		TArray<FDreamUIRenderData> Data;
		Data.Add(MakeQuad(FVector2D(0.0, 0.0), FVector2D(50.0, 50.0), EDreamUIBlendMode::Overlay));
		Data.Add(MakeQuad(FVector2D(25.0, 25.0), FVector2D(75.0, 75.0), EDreamUIBlendMode::Overlay));
		Data.Add(MakeQuad(FVector2D(-40.0, -40.0), FVector2D(10.0, 10.0), EDreamUIBlendMode::Overlay));
		TestEqual(TEXT("A third that overlaps the first and not the second goes in with the second"), Count(MoveTemp(Data)), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupProjectBoundsTest,
	"DreamGUI.Group.ABoxIsProjectedOntoThePixelsItCovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGroupProjectBoundsTest::RunTest(const FString& Parameters)
{
	// An orthographic view of world x, y onto a 200 x 100 view at (10, 20) of its target: x -1..1 across, y 1..-1 down.
	const FIntRect View(10, 20, 210, 120);
	const FMatrix WorldToClip(
		FPlane(1.0 / 100.0, 0.0, 0.0, 0.0),
		FPlane(0.0, 1.0 / 50.0, 0.0, 0.0),
		FPlane(0.0, 0.0, 0.5, 0.0),
		FPlane(0.0, 0.0, 0.5, 1.0));
	const FIntRect Covered = FDreamUICompositor::ProjectBounds(FBox(FVector(-50.0, -25.0, 0.0), FVector(50.0, 25.0, 0.0)), WorldToClip, View);
	TestEqual(TEXT("The middle half of the view, and a pixel round it"), Covered, FIntRect(59, 44, 161, 96));
	TestEqual(TEXT("An invalid box covers the view"), FDreamUICompositor::ProjectBounds(FBox(ForceInit), WorldToClip, View), View);
	const FIntRect Outside = FDreamUICompositor::ProjectBounds(FBox(FVector(500.0, 0.0, 0.0), FVector(600.0, 10.0, 0.0)), WorldToClip, View);
	TestTrue(TEXT("A box out of view covers nothing"), Outside.Width() <= 0 || Outside.Height() <= 0);
	const FIntRect Edge = FDreamUICompositor::ProjectBounds(FBox(FVector(80.0, -100.0, 0.0), FVector(300.0, 100.0, 0.0)), WorldToClip, View);
	TestEqual(TEXT("A box past an edge is cut at the view"), Edge, FIntRect(189, 20, 210, 120));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupMarkersTest,
	"DreamGUI.Group.AnIsolatedGroupIsDrawnBetweenItsMarkersAndNothingBatchesAcrossThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGroupMarkersTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	MakeBlock(World, Stage.Root, TEXT("Before"), FVector2D(40.0, 40.0), FVector2D(-200.0, 0.0));
	UDreamWidget* Container = MakeWidget(World, Stage.Root, TEXT("Container"), FVector2D(200.0, 200.0));
	MakeBlock(World, Container, TEXT("A"), FVector2D(60.0, 60.0), FVector2D(-20.0, 0.0));
	MakeBlock(World, Container, TEXT("B"), FVector2D(60.0, 60.0), FVector2D(20.0, 0.0));
	MakeBlock(World, Stage.Root, TEXT("After"), FVector2D(40.0, 40.0), FVector2D(200.0, 0.0));
	UDreamLayerGroup* Group = Container->AddComponent<UDreamLayerGroup>();
	Stage.Frames();
	TestEqual(TEXT("Passed through, the group is not there: one draw for all four"), Stage.Shape(), FString(TEXT("B")));
	TestFalse(TEXT("A pass-through group with its defaults is not drawn apart"), Group->IsIsolated());

	Group->SetIsolation(EDreamLayerGroupIsolation::Isolated);
	Stage.Frames();
	TestEqual(TEXT("Isolated, its two children are drawn between its markers, apart from the two outside"), Stage.Shape(), FString(TEXT("B(B)B")));
	const TArray<FDreamUIDrawCall>& DrawCalls = Stage.DrawCalls();
	if (DrawCalls.Num() == 5)
	{
		TestEqual(TEXT("The group's draw holds both its children"), DrawCalls[2].BatchMeshGeometryArray.Num(), 2);
		TestTrue(TEXT("The markers carry one composite, the group's"), DrawCalls[1].Composite.IsValid() && DrawCalls[1].Composite == DrawCalls[3].Composite);
		TestTrue(TEXT("The opening marker knows the group it reads its opacity from"), DrawCalls[1].LayerGroup.Get() == Group);
		TestTrue(TEXT("...and opens it"), DrawCalls[1].bCompositeBegin && !DrawCalls[3].bCompositeBegin);
	}

	// The opacity and the mode are read every frame: changing them makes no draw call again.
	DreamUIRenderStats::TakeSnapshot(true);
	Group->SetGroupOpacity(0.5f);
	Group->SetGroupBlendMode(EDreamUIBlendMode::Multiply);
	Stage.Frames();
	const DreamUIRenderStats::FSnapshot Snapshot = DreamUIRenderStats::TakeSnapshot(true);
	TestEqual(TEXT("A group's opacity and mode change without a rebuild"), Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::DrawCallRebuilds)], (int64)0);
	TestEqual(TEXT("...and keep their markers"), Stage.Shape(), FString(TEXT("B(B)B")));

	// Back to its defaults and passed through, the markers go.
	Group->SetGroupOpacity(1.0f);
	Group->SetGroupBlendMode(EDreamUIBlendMode::Alpha);
	Group->SetIsolation(EDreamLayerGroupIsolation::PassThrough);
	Stage.Frames();
	TestEqual(TEXT("Passed through again, one draw"), Stage.Shape(), FString(TEXT("B")));

	// An opacity below one isolates it without being asked.
	Group->SetGroupOpacity(0.25f);
	Stage.Frames();
	TestTrue(TEXT("An opacity isolates a pass-through group"), Group->IsIsolated());
	TestEqual(TEXT("...and draws it apart"), Stage.Shape(), FString(TEXT("B(B)B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupNestingTest,
	"DreamGUI.Group.GroupsNestAndTheInnerOneClosesFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGroupNestingTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* Outer = MakeWidget(World, Stage.Root, TEXT("Outer"), FVector2D(300.0, 300.0));
	MakeBlock(World, Outer, TEXT("OuterBlock"), FVector2D(280.0, 280.0));
	UDreamWidget* Middle = MakeWidget(World, Outer, TEXT("Middle"), FVector2D(200.0, 200.0));
	MakeBlock(World, Middle, TEXT("MiddleBlock"), FVector2D(180.0, 180.0));
	UDreamWidget* Inner = MakeWidget(World, Middle, TEXT("Inner"), FVector2D(100.0, 100.0));
	MakeBlock(World, Inner, TEXT("InnerBlock"), FVector2D(80.0, 80.0));
	MakeBlock(World, Middle, TEXT("MiddleAfter"), FVector2D(20.0, 20.0), FVector2D(70.0, 70.0));
	for (UDreamWidget* Container : { Outer, Middle, Inner })
	{
		Container->AddComponent<UDreamLayerGroup>()->SetIsolation(EDreamLayerGroupIsolation::Isolated);
	}
	Stage.Frames();
	TestEqual(TEXT("Three groups, one inside the next, each holding its own blocks"), Stage.Shape(), FString(TEXT("(B(B(B)B))")));
	const TArray<FDreamUIDrawCall>& DrawCalls = Stage.DrawCalls();
	TArray<uint32> Open;
	bool bNested = true;
	TSet<uint32> Ids;
	for (const FDreamUIDrawCall& DrawCall : DrawCalls)
	{
		if (!DrawCall.IsGroupMarker() || !DrawCall.Composite.IsValid())
		{
			continue;
		}
		if (DrawCall.bCompositeBegin)
		{
			Open.Add(DrawCall.Composite->Id);
			Ids.Add(DrawCall.Composite->Id);
		}
		else
		{
			bNested &= Open.Num() > 0 && Open.Pop() == DrawCall.Composite->Id;
		}
	}
	TestTrue(TEXT("Each closing marker closes the innermost group open"), bNested && Open.Num() == 0);
	TestEqual(TEXT("Each group has its own id"), Ids.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupClipBelowTest,
	"DreamGUI.Group.ClippingToALayerNoLevelFollowsIsDoneByAGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGroupClipBelowTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	// The layer below is a container: the layer its children make, which no clip level follows.
	UDreamWidget* Base = MakeWidget(World, Stage.Root, TEXT("Base"), FVector2D(100.0, 100.0));
	MakeBlock(World, Base, TEXT("BaseLeft"), FVector2D(60.0, 60.0), FVector2D(-40.0, 0.0));
	MakeBlock(World, Base, TEXT("BaseRight"), FVector2D(60.0, 60.0), FVector2D(40.0, 0.0));
	UDreamWidget* Clipped = MakeBlock(World, Stage.Root, TEXT("Clipped"), FVector2D(160.0, 40.0));
	UDreamWidget* Second = MakeBlock(World, Stage.Root, TEXT("Second"), FVector2D(40.0, 160.0));
	MakeBlock(World, Stage.Root, TEXT("Above"), FVector2D(20.0, 20.0), FVector2D(200.0, 200.0));
	Clipped->SetClipToBelow(true);
	Second->SetClipToBelow(true);
	Stage.Frames();
	TestEqual(TEXT("The layer below in one group, what clips to it in the next, the rest after"), Stage.Shape(), FString(TEXT("(B)(B)B")));
	const TArray<FDreamUIDrawCall>& DrawCalls = Stage.DrawCalls();
	if (DrawCalls.Num() == 7)
	{
		TestTrue(TEXT("The first is the layer below"), DrawCalls[0].Composite.IsValid() && DrawCalls[0].Composite->Role == FDreamUICompositeDesc::ERole::ClipBase);
		TestTrue(TEXT("The second is what clips to it, under the same id"), DrawCalls[3].Composite.IsValid() && DrawCalls[3].Composite->Role == FDreamUICompositeDesc::ERole::ClipBody
			&& DrawCalls[3].Composite->Id == DrawCalls[0].Composite->Id);
		TestEqual(TEXT("Both clipped widgets share one draw in it"), DrawCalls[4].BatchMeshGeometryArray.Num(), 2);
	}
	// Hit where the layer below's widget rect is: its picture is the GPU's alone.
	if (const TSharedPtr<FDreamUIClipData> Clip = Clipped->GetClipData().Pin())
	{
		auto PointAt = [Clipped](double X, double Y)
		{
			const FVector2D Centre = Clipped->GetLocalSpaceCenter();
			return Clipped->GetWorldTransform().TransformPosition(FVector(0.0, Centre.X + X, Centre.Y + Y));
		};
		TestTrue(TEXT("Inside the layer below's rect, what clips to it is hit"), Clip->IsPointVisible(PointAt(30.0, 0.0)));
		TestFalse(TEXT("Past it, it is not"), Clip->IsPointVisible(PointAt(70.0, 0.0)));
	}
	else
	{
		AddError(TEXT("The clipped widget has a clip"));
	}
	// A layer below a level follows needs no group.
	Clipped->SetClipToBelow(false);
	Second->SetClipToBelow(false);
	UDreamWidget* Image = MakeBlock(World, Stage.Root, TEXT("Image"), FVector2D(100.0, 100.0), FVector2D(-200.0, -200.0));
	UDreamWidget* OnImage = MakeBlock(World, Stage.Root, TEXT("OnImage"), FVector2D(40.0, 40.0), FVector2D(-200.0, -200.0));
	OnImage->SetClipToBelow(true);
	Stage.Frames();
	TestFalse(TEXT("Clipping to an image is a clip level, not a group"), Stage.Shape().Contains(TEXT("(")));
	(void)Image;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupOfOneTest,
	"DreamGUI.Group.AnElementAMaterialDrawsByANewModeIsAGroupOfOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGroupOfOneTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* Plain = MakeBlock(World, Stage.Root, TEXT("Plain"), FVector2D(40.0, 40.0), FVector2D(-100.0, 0.0), EDreamUIBlendMode::Screen);
	UDreamWidget* WithMaterial = MakeBlock(World, Stage.Root, TEXT("WithMaterial"), FVector2D(40.0, 40.0), FVector2D(100.0, 0.0), EDreamUIBlendMode::Screen);
	if (UDreamTexture* Visual = Cast<UDreamTexture>(WithMaterial->GetVisual()))
	{
		Visual->SetOverrideMaterial(UMaterial::GetDefaultMaterial(MD_UI));
	}
	Stage.Frames();
	const bool bBuiltIn = UDreamUISettings::GetUseBuiltInUIShader();
	if (bBuiltIn)
	{
		TestEqual(TEXT("The built-in shader draws Screen itself; the material's element is put down through a group"), Stage.Shape(), FString(TEXT("B(B)")));
	}
	if (UDreamTexture* Visual = Cast<UDreamTexture>(WithMaterial->GetVisual()))
	{
		Visual->SetBlendMode(EDreamUIBlendMode::Additive);
	}
	Stage.Frames();
	TestFalse(TEXT("A mode a material decides for itself needs no group"), Stage.Shape().Contains(TEXT("(")));
	(void)Plain;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupWorldSpaceTest,
	"DreamGUI.Group.AWorldSpaceCanvasDrawsItsGroupsPassedThroughAndSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGroupWorldSpaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	FStage Stage(EDreamRenderMode::WorldSpace_DreamUI);
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* Container = MakeWidget(World, Stage.Root, TEXT("Container"), FVector2D(200.0, 200.0));
	MakeBlock(World, Container, TEXT("A"), FVector2D(60.0, 60.0));
	Container->AddComponent<UDreamLayerGroup>()->SetGroupOpacity(0.5f);
	AddExpectedMessagePlain(TEXT("are not drawn apart"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Stage.Frames();
	TestFalse(TEXT("The canvas does not draw groups apart"), Stage.Canvas->CanDrawGroupsApart());
	TestFalse(TEXT("No markers"), Stage.Shape().Contains(TEXT("(")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupMaskTest,
	"DreamGUI.Group.AGroupsMaskIsAChainOfItsOwnThatClipsNoElement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGroupMaskTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* Container = MakeWidget(World, Stage.Root, TEXT("Container"), FVector2D(200.0, 200.0));
	UDreamWidget* Child = MakeBlock(World, Container, TEXT("Child"), FVector2D(160.0, 160.0));
	UDreamLayerGroup* Group = Container->AddComponent<UDreamLayerGroup>();
	FDreamClipMask Mask;
	Mask.Shape = EDreamClipMaskShape::Ellipse;
	Group->SetGroupMask(Mask);
	Stage.Frames();
	TestTrue(TEXT("A mask isolates the group"), Group->IsIsolated());
	TestEqual(TEXT("...and draws it apart"), Stage.Shape(), FString(TEXT("(B)")));
	FDreamUICompositeDesc Desc;
	Group->FillComposite(Desc, Stage.Canvas);
	TestTrue(TEXT("The composite reads the mask's row"), Desc.HasMask());
	TestTrue(TEXT("...of the canvas's clip data"), Desc.Textures.ClipDataTexture.Get() == Stage.Canvas->GetClipDataTexture());
	const TSharedPtr<FDreamUIClipData> ChildClip = Child->GetClipData().Pin();
	TestTrue(TEXT("The child is clipped by nothing: the mask is the composite's"), !ChildClip.IsValid() || ChildClip->GetBufferStartPos() != static_cast<int32>(Desc.MaskClipCoordinate));
	Group->SetGroupMask(FDreamClipMask());
	Group->FillComposite(Desc, Stage.Canvas);
	TestFalse(TEXT("No mask, no row"), Desc.HasMask());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
