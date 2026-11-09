// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "NiagaraSystem.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipMask.h"
#include "DreamNiagaraMaterialConverter.h"
#include "DreamNiagaraSystem.h"
#include "DreamNiagaraTrigger.h"
#include "DreamNiagaraEngineAccess.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "DreamWidgetBlueprint.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * Niagara particles drawn: the test system (DFX/Tests/NS_DreamUITest_Particles.dfs) stepped a fixed number of times and
 * paused, so that each picture is one moment of it -- sprites of a sheet's four frames, blended half way, a ribbon cut into a
 * curve, sprites thrown and falling -- held to its golden image; under DreamGUI's clip masks; in layer groups and blend modes;
 * and through a converted particle material, which draws what the built-in shader does.
 */
namespace DreamNiagaraPixelTestLocal
{
	using namespace DreamGalleryStage;

	const TCHAR* const TestSystemPath = TEXT("/DreamGUI/Tests/Niagara/NS_DreamUITest_Particles.NS_DreamUITest_Particles");
	constexpr float Step = 1.0f / 30.0f;

	/** A 2 by 2 sheet: a soft disc in each frame, red, green, blue and gold. */
	UTexture2D* MakeSheet(FGalleryStage& InStage)
	{
		return InStage.MakeTexture(64, [](int32 X, int32 Y)
		{
			static const FColor Frames[4] = { FColor(255, 90, 80), FColor(90, 230, 120), FColor(90, 140, 255), FColor(255, 210, 80) };
			const int32 Frame = (X >= 32 ? 1 : 0) + (Y >= 32 ? 2 : 0);
			const float Distance = FVector2f((X % 32) + 0.5f - 16.0f, (Y % 32) + 0.5f - 16.0f).Size() / 16.0f;
			FColor Colour = Frames[Frame];
			Colour.A = static_cast<uint8>(FMath::Clamp(1.2f - Distance, 0.0f, 1.0f) * 255.0f);
			return Colour;
		});
	}

	/** The test system on a widget of InSize at InPosition, stepped InSteps times and paused, its renderers given the sheet and a gradient. */
	UDreamNiagaraSystem* AddParticles(FGalleryStage& InStage, const TCHAR* InName, FVector2D InSize, FVector2D InPosition, UDreamWidget* InParent,
		float InPixelsPerUnit = 1.0f, int32 InSteps = 12, EDreamUIBlendMode InFallMode = EDreamUIBlendMode::Additive)
	{
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TestSystemPath);
		if (System == nullptr)
		{
			InStage.GetTest().AddError(FString::Printf(TEXT("%s does not load"), TestSystemPath));
			return nullptr;
		}
		System->WaitForCompilationComplete();
		if (!DreamNiagaraEngine::IsReadyToRun(System))
		{
			System->RequestCompile(false);
			System->WaitForCompilationComplete();
		}
		UDreamWidget* Holder = InStage.AddWidget(InName, InSize, InPosition, InParent);
		UDreamNiagaraSystem* Visual = Holder->CreateNewVisual<UDreamNiagaraSystem>();
		Visual->SetPixelsPerUnit(InPixelsPerUnit);
		TArray<FDreamNiagaraRendererOverride> Overrides;
		FDreamNiagaraRendererOverride& Grid = Overrides.AddDefaulted_GetRef();
		Grid.Emitter = TEXT("Grid");
		Grid.Texture = MakeSheet(InStage);
		FDreamNiagaraRendererOverride& Trail = Overrides.AddDefaulted_GetRef();
		Trail.Emitter = TEXT("Trail");
		Trail.Texture = InStage.MakeGradient(32, FColor(255, 255, 255, 255), FColor(255, 255, 255, 60));
		FDreamNiagaraRendererOverride& Fall = Overrides.AddDefaulted_GetRef();
		Fall.Emitter = TEXT("Fall");
		Fall.bOverrideBlendMode = true;
		Fall.BlendMode = InFallMode;
		Visual->SetRendererOverrides(Overrides);
		Visual->SetSystem(System);
		Visual->Advance(InSteps * Step, Step);
		// Held at that moment: the stage's world is the editor's, whose frames would step it on.
		Visual->SetPaused(true);
		InStage.KeepAlive(System);
		return Visual;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraGalleryTest,
	"DreamGUI.RHI.Niagara.ParticlesMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamNiagaraGalleryTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	// The grid of frames below the ribbon; the falling sprites drawn additively, to the right.
	if (AddParticles(*Stage, TEXT("Particles"), FVector2D(220.0, 220.0), FVector2D(-10.0, -24.0), nullptr) == nullptr)
	{
		Stage->TearDown();
		return false;
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Niagara_Particles"), 6000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraMaskedGalleryTest,
	"DreamGUI.RHI.Niagara.ParticlesUnderClipMasksMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamNiagaraMaskedGalleryTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	// Left: a feathered ellipse. Right: a five-pointed star. Both smaller than what their particles cover.
	UDreamWidget* Ellipse = Stage->AddWidget(TEXT("Ellipse"), FVector2D(110.0, 150.0), FVector2D(-62.0, 0.0));
	Ellipse->SetClipping(EDreamWidgetClipping::ClipToBounds);
	FDreamClipMask EllipseMask;
	EllipseMask.Shape = EDreamClipMaskShape::Ellipse;
	EllipseMask.Feather = FMargin(14.0f);
	Ellipse->SetClipMask(EllipseMask);
	UDreamWidget* Star = Stage->AddWidget(TEXT("Star"), FVector2D(116.0, 116.0), FVector2D(64.0, 0.0));
	Star->SetClipping(EDreamWidgetClipping::ClipToBounds);
	FDreamClipMask StarMask;
	StarMask.Shape = EDreamClipMaskShape::Star;
	StarMask.Sides = 5;
	StarMask.InnerRatio = 0.5f;
	Star->SetClipMask(StarMask);
	if (AddParticles(*Stage, TEXT("InEllipse"), FVector2D(150.0, 150.0), FVector2D(0.0, -30.0), Ellipse, 0.8f) == nullptr
		|| AddParticles(*Stage, TEXT("InStar"), FVector2D(150.0, 150.0), FVector2D(0.0, -30.0), Star, 0.8f) == nullptr)
	{
		Stage->TearDown();
		return false;
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Niagara_Masked"), 4000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraGroupGalleryTest,
	"DreamGUI.RHI.Niagara.ParticlesInLayerGroupsAndBlendModesMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamNiagaraGroupGalleryTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	// Behind both: stripes the blend modes show against.
	UTexture2D* Stripes = Stage->MakeTexture(64, [](int32 X, int32 Y)
	{
		return (X / 8) % 2 == 0 ? FColor(200, 70, 60, 255) : FColor(60, 160, 200, 255);
	});
	Stage->AddBlock(TEXT("Stripes"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Stripes);
	// Left: a group at half opacity, its particles over a block of its own, so that they do not show through each other.
	UDreamWidget* Faded = Stage->AddWidget(TEXT("Faded"), FVector2D(84.0, 200.0), FVector2D(-86.0, 0.0));
	UDreamLayerGroup* FadedGroup = Faded->AddComponent<UDreamLayerGroup>();
	FadedGroup->SetIsolation(EDreamLayerGroupIsolation::Isolated);
	FadedGroup->SetGroupOpacity(0.5f);
	Stage->AddBlock(TEXT("Card"), FVector2D(76.0, 90.0), FVector2D(0.0, -20.0), FColor(240, 240, 240, 255), nullptr, Faded);
	// Middle: a group holding nothing but particles, multiplied over the stripes. Its grid and ribbon blend by alpha: an
	// additive draw leaves the group's alpha alone, as it does for any element, so light alone in a group multiplies by nothing.
	UDreamWidget* Multiplied = Stage->AddWidget(TEXT("Multiplied"), FVector2D(84.0, 200.0), FVector2D(0.0, 0.0));
	UDreamLayerGroup* MultipliedGroup = Multiplied->AddComponent<UDreamLayerGroup>();
	MultipliedGroup->SetIsolation(EDreamLayerGroupIsolation::Isolated);
	MultipliedGroup->SetGroupBlendMode(EDreamUIBlendMode::Multiply);
	// Right: no group, the falling sprites in Overlay, a mode that reads what is behind it.
	UDreamWidget* Overlaid = Stage->AddWidget(TEXT("Overlaid"), FVector2D(84.0, 200.0), FVector2D(86.0, 0.0));
	UDreamNiagaraSystem* InMultiplied = nullptr;
	if (AddParticles(*Stage, TEXT("InFaded"), FVector2D(84.0, 160.0), FVector2D(0.0, -30.0), Faded, 0.5f) == nullptr
		|| (InMultiplied = AddParticles(*Stage, TEXT("InMultiplied"), FVector2D(84.0, 160.0), FVector2D(0.0, -30.0), Multiplied, 0.5f)) == nullptr
		|| AddParticles(*Stage, TEXT("InOverlaid"), FVector2D(84.0, 160.0), FVector2D(0.0, -30.0), Overlaid, 0.5f, 30, EDreamUIBlendMode::Overlay) == nullptr)
	{
		Stage->TearDown();
		return false;
	}
	InMultiplied->SetBlendModeOverride(true, EDreamUIBlendMode::Alpha);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Niagara_Groups"), 30000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraConvertedMaterialTest,
	"DreamGUI.RHI.Niagara.AConvertedParticleMaterialDrawsAsTheBuiltInShaderDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamNiagaraConvertedMaterialTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraPixelTestLocal;
	// The same particles three times, side by side: through the built-in shader with the engine's default materials (which
	// sample no texture, so the soft default stands in), through DreamGUI's own UI material, and through those default
	// materials converted. The conversions go into /Temp, and are not saved.
	FStageRef Stage = BeginStage(*this, FIntPoint(384, 256));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	UDreamNiagaraSystem* BuiltIn = AddParticles(*Stage, TEXT("BuiltIn"), FVector2D(128.0, 200.0), FVector2D(-128.0, -20.0), nullptr, 0.8f);
	UDreamNiagaraSystem* UIMaterial = AddParticles(*Stage, TEXT("UIMaterial"), FVector2D(128.0, 200.0), FVector2D(0.0, -20.0), nullptr, 0.8f);
	UDreamNiagaraSystem* Converted = AddParticles(*Stage, TEXT("Converted"), FVector2D(128.0, 200.0), FVector2D(128.0, -20.0), nullptr, 0.8f);
	if (BuiltIn == nullptr || UIMaterial == nullptr || Converted == nullptr)
	{
		Stage->TearDown();
		return false;
	}
	// Each renderer draws what its own material says: no overrides.
	BuiltIn->SetRendererOverrides({});
	UIMaterial->SetRendererOverrides({});
	Converted->SetRendererOverrides({});
	UMaterialInterface* DreamUIMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamGUI/Materials/DreamUI_ImageAndFont.DreamUI_ImageAndFont"));
	TMap<UMaterialInterface*, UMaterialInterface*> DreamUIMap;
	TMap<UMaterialInterface*, UMaterialInterface*> ConvertedMap;
	for (const FDreamNiagaraRendererInfo& Renderer : Converted->GetRendererInfo())
	{
		UMaterialInterface* Source = Renderer.Material.Get();
		if (Source == nullptr || ConvertedMap.Contains(Source))
		{
			continue;
		}
		const FDreamNiagaraMaterialConversion Conversion = FDreamNiagaraMaterialConverter::Convert(Source,
			FString::Printf(TEXT("/Temp/DreamGUITests/Niagara/%s_DreamUI"), *Source->GetName()));
		if (!TestNotNull(*FString::Printf(TEXT("%s converts (%s)"), *Source->GetName(), *Conversion.Error), Conversion.Result))
		{
			Stage->TearDown();
			return false;
		}
		for (const FString& Line : Conversion.Replaced)
		{
			AddInfo(FString::Printf(TEXT("%s: replaced %s"), *Source->GetName(), *Line));
		}
		for (const FString& Line : Conversion.Unconverted)
		{
			AddInfo(FString::Printf(TEXT("%s: not converted %s"), *Source->GetName(), *Line));
		}
		Stage->KeepAlive(Conversion.Result);
		ConvertedMap.Add(Source, Conversion.Result);
		DreamUIMap.Add(Source, DreamUIMaterial);
	}
	UIMaterial->SetMaterialMap(DreamUIMap);
	UIMaterial->SetDefaultShading(EDreamNiagaraShading::Material);
	Converted->SetMaterialMap(ConvertedMap);
	Converted->SetDefaultShading(EDreamNiagaraShading::Material);
	EnqueueFramesUntilDrawn(Stage, 2000);
	// The materials' shaders are asked for the first time they draw, and compiled while the stage waits for the last third.
	auto CountDrawn = [](const TArray<FColor>& InPixels, FIntPoint InSize, int32 InThird, FColor InBack, double& OutLight)
	{
		int32 Drawn = 0;
		OutLight = 0.0;
		const int32 Width = InSize.X / 3;
		for (int32 Y = 0; Y < InSize.Y; ++Y)
		{
			for (int32 X = InThird * Width; X < (InThird + 1) * Width; ++X)
			{
				const FColor Pixel = InPixels[Y * InSize.X + X];
				const int32 Delta = FMath::Abs(Pixel.R - InBack.R) + FMath::Abs(Pixel.G - InBack.G) + FMath::Abs(Pixel.B - InBack.B);
				Drawn += Delta > 12 ? 1 : 0;
				OutLight += Delta;
			}
		}
		return Drawn;
	};
	EnqueueStep([Stage, CountDrawn, Start = FPlatformTime::Seconds()]()
	{
		GShaderCompilingManager->FinishAllCompilation();
		Stage->RequestRedraw();
		TArray<FColor> Pixels;
		FIntPoint Size;
		if (!Stage->ReadBack(Pixels, Size))
		{
			return true;
		}
		double Light = 0.0;
		const bool bAll = CountDrawn(Pixels, Size, 1, Stage->GetBackdrop(), Light) > 500 && CountDrawn(Pixels, Size, 2, Stage->GetBackdrop(), Light) > 500;
		return bAll || FPlatformTime::Seconds() - Start > 30.0;
	});
	// What the conversions came to, for a run where they draw nothing: compiled or not, and with which nodes.
	EnqueueStep([this, ConvertedMap]()
	{
		for (const TPair<UMaterialInterface*, UMaterialInterface*>& Pair : ConvertedMap)
		{
			UMaterial* Material = Cast<UMaterial>(Pair.Value);
			if (Material == nullptr)
			{
				continue;
			}
			const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
			TArray<FString> Nodes;
			for (const UMaterialExpression* Expression : Material->GetExpressions())
			{
				Nodes.Add(Expression != nullptr ? Expression->GetClass()->GetName().RightChop(18) : TEXT("null"));
			}
			AddInfo(FString::Printf(TEXT("%s: blend %d, finished %d, shader map %d, errors [%s], nodes [%s]"), *Material->GetName(), int32(Material->GetBlendMode()),
				Resource != nullptr && Resource->IsCompilationFinished() ? 1 : 0, Resource != nullptr && Resource->GetGameThreadShaderMap() != nullptr ? 1 : 0,
				Resource != nullptr ? *FString::Join(Resource->GetCompileErrors(), TEXT(" | ")) : TEXT("no resource"), *FString::Join(Nodes, TEXT(", "))));
		}
		return true;
	});
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilStable(Stage);
	EnqueueStep([this, Stage, CountDrawn]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size;
		if (!TestTrue(TEXT("The picture reads back"), Stage->ReadBack(Pixels, Size)))
		{
			return true;
		}
		FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Gallery_Niagara_ConvertedMaterial"));
		double BuiltInLight = 0.0, UILight = 0.0, ConvertedLight = 0.0;
		const int32 BuiltInDrawn = CountDrawn(Pixels, Size, 0, Stage->GetBackdrop(), BuiltInLight);
		const int32 UIDrawn = CountDrawn(Pixels, Size, 1, Stage->GetBackdrop(), UILight);
		const int32 ConvertedDrawn = CountDrawn(Pixels, Size, 2, Stage->GetBackdrop(), ConvertedLight);
		AddInfo(FString::Printf(TEXT("Drawn pixels: built-in %d, DreamGUI's material %d, converted %d"), BuiltInDrawn, UIDrawn, ConvertedDrawn));
		TestTrue(*FString::Printf(TEXT("The built-in third draws particles (%d pixels)"), BuiltInDrawn), BuiltInDrawn > 1000);
		// DreamGUI's own material shades a particle with the same function the built-in shader does.
		TestTrue(*FString::Printf(TEXT("DreamGUI's material draws what the built-in shader does (%d against %d pixels)"), UIDrawn, BuiltInDrawn),
			UIDrawn > BuiltInDrawn * 0.8 && UIDrawn < BuiltInDrawn * 1.25);
		// The engine's default sprite material draws its disc procedurally, the built-in shader with a soft texture of the
		// same falloff: about the same places, about the same light.
		TestTrue(*FString::Printf(TEXT("The converted third covers about as much (%d against %d pixels)"), ConvertedDrawn, BuiltInDrawn),
			ConvertedDrawn > BuiltInDrawn * 0.5 && ConvertedDrawn < BuiltInDrawn * 2.0);
		TestTrue(*FString::Printf(TEXT("...with about as much light (%.0f against %.0f)"), ConvertedLight, BuiltInLight),
			ConvertedLight > BuiltInLight * 0.4 && ConvertedLight < BuiltInLight * 2.5);
		return true;
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraSampleTest,
	"DreamGUI.RHI.Niagara.TheShippedEffectsSampleCompilesAndEachOfItsTriggersPlaysItsSystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamNiagaraSampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraPixelTestLocal;
	// The sample made as its header tells a user to: a widget Blueprint of its class, pointed at the file, compiled, shown.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("The plugin is installed"), Plugin.IsValid()))
	{
		return false;
	}
	FString SamplePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetContentDir(), TEXT("Samples"), TEXT("NiagaraEffects.dui")));
	FPaths::NormalizeFilename(SamplePath);
	UPackage* Package = CreatePackage(TEXT("/Game/UI/WBP_NiagaraEffects"));
	Package->AddToRoot();
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UDreamTextUserWidget::StaticClass(), Package, FName(TEXT("WBP_NiagaraEffects")), BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
		? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("A widget Blueprint for it"), Defaults))
	{
		Package->RemoveFromRoot();
		return false;
	}
	Defaults->SourceFile.FilePath = SamplePath;
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		AddInfo(Message->ToText().ToString());
	}
	if (!TestEqual(TEXT("The sample compiles without an error"), Results.NumErrors, 0))
	{
		Package->RemoveFromRoot();
		return false;
	}
	FStageRef Stage = BeginStage(*this, FIntPoint(1120, 340));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		Package->RemoveFromRoot();
		return false;
	}
	Stage->UseBuiltInShader(true);
	UDreamUserWidget* Sample = CreateDreamWidget(Stage->GetWorld(), Blueprint->GeneratedClass.Get(), Stage->GetRoot());
	if (!TestNotNull(TEXT("The sample makes a widget"), Sample))
	{
		Stage->TearDown();
		Package->RemoveFromRoot();
		return false;
	}
	Sample->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	Sample->SetAnchoredPosition(FVector2D::ZeroVector);
	Sample->SetSizeDelta(FVector2D::ZeroVector);
	EnqueueFrames(Stage, 3);
	EnqueueDo([this, Placed = TWeakObjectPtr<UDreamUserWidget>(Sample)]()
	{
		UDreamUserWidget* Widget = Placed.Get();
		if (Widget == nullptr)
		{
			return;
		}
		// Each card's trigger, fired as its pointer event would: the system it plays is the one the card names.
		struct FCard { const TCHAR* Trigger; const TCHAR* System; EDreamNiagaraTriggerAction Action; };
		const FCard Cards[] = {
			{ TEXT("SparksCard"), TEXT("Sparks"), EDreamNiagaraTriggerAction::BurstAtCentre },
			{ TEXT("TrailCard"), TEXT("Trail"), EDreamNiagaraTriggerAction::Activate },
			{ TEXT("Button"), TEXT("Edge"), EDreamNiagaraTriggerAction::Activate },
			{ TEXT("ChargeCard"), TEXT("Burst"), EDreamNiagaraTriggerAction::BurstAtCentre },
			{ TEXT("Chest"), TEXT("Coins"), EDreamNiagaraTriggerAction::BurstToTarget },
		};
		for (const FCard& Card : Cards)
		{
			UDreamWidget* Holder = Widget->FindChildByDisplayName(Card.Trigger, true);
			UDreamNiagaraTrigger* Trigger = Holder != nullptr ? Holder->GetComponent<UDreamNiagaraTrigger>() : nullptr;
			if (!TestNotNull(*FString::Printf(TEXT("%s has a trigger"), Card.Trigger), Trigger))
			{
				continue;
			}
			UDreamNiagaraSystem* System = Trigger->FindSystem();
			TestTrue(*FString::Printf(TEXT("%s's trigger plays %s"), Card.Trigger, Card.System), System != nullptr && System->GetWidget()->GetDisplayName() == Card.System);
			TestTrue(*FString::Printf(TEXT("%s has something to draw"), Card.System), System != nullptr && System->HaveValidData());
			Trigger->Fire(Card.Action);
		}
	});
	EnqueueFrames(Stage, 10);
	EnqueueDo([this, Stage, Placed = TWeakObjectPtr<UDreamUserWidget>(Sample)]()
	{
		UDreamUserWidget* Widget = Placed.Get();
		UDreamWidget* Coins = Widget != nullptr ? Widget->FindChildByDisplayName(TEXT("Coins"), true) : nullptr;
		UDreamNiagaraSystem* CoinSystem = Coins != nullptr ? Cast<UDreamNiagaraSystem>(Coins->GetVisual()) : nullptr;
		TestTrue(TEXT("The coins are in flight"), CoinSystem != nullptr && CoinSystem->GetBurstCount() == 1);
		TArray<FColor> Pixels;
		FIntPoint Size;
		if (TestTrue(TEXT("The picture reads back"), Stage->ReadBack(Pixels, Size)))
		{
			FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Sample_NiagaraEffects"));
		}
	});
	EnqueueTearDown(Stage);
	EnqueueDo([Package]() { Package->RemoveFromRoot(); });
	return true;
}

#endif
