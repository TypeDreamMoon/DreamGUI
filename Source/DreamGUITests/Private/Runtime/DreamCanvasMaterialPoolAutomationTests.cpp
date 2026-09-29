// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIManager.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectHash.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriverRig.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A CANVAS ANSWERS FOR ITS MATERIALS WITHOUT A MATERIAL INSTANCE OF ITS OWN.
 *
 * It made one per draw call through a custom material, pooled per material, and set its parameters on it; it
 * answers those parameters through a render-thread proxy of the material now, and a material instance given to
 * an image is answered for the same way, never written to.
 */
namespace DreamCanvasMaterialPoolTestLocal
{
	/** Every material instance InCanvas made from InSource. */
	TArray<UMaterialInstanceDynamic*> InstancesMadeFrom(const UDreamCanvas* InCanvas, const UMaterialInterface* InSource)
	{
		TArray<UMaterialInstanceDynamic*> Instances;
		ForEachObjectWithOuter(InCanvas, [&Instances, InSource](UObject* InObject)
		{
			UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(InObject);
			if (IsValid(Instance) && Instance->Parent == InSource)
			{
				Instances.Add(Instance);
			}
		}, EGetObjectsFlags::None);
		return Instances;
	}

	/**
	 * InFrames whole frames. The rig's frame ends with the manager's tick; the draw-call submission that
	 * ends an engine frame -- where a canvas takes its draw calls and gives them their materials -- is run
	 * here after it.
	 */
	void DrawFrames(FDreamDriverRig& InRig, UDreamUIManagerWorldSubsystem* InManager, int32 InFrames)
	{
		for (int32 Frame = 0; Frame < InFrames; ++Frame)
		{
			InRig.PumpFrames(1);
			InManager->SubmitCanvasDrawCall();
		}
	}

	/** The widget property data texture InInstance samples; null when it has been given none. */
	const UTexture* PropertyDataTextureOf(const UMaterialInstanceDynamic* InInstance)
	{
		for (const FTextureParameterValue& Value : InInstance->TextureParameterValues)
		{
			if (Value.ParameterInfo.Name == UDreamCanvas::DreamUI_WidgetPropertyDataTexture_MaterialParameterName)
			{
				return Value.ParameterValue;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasMaterialProxiesTest,
	"DreamGUI.Canvas.ACanvasAnswersForItsMaterialsWithoutMakingOrWritingToAMaterialInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasMaterialProxiesTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasMaterialPoolTestLocal;

	/*
	 * A canvas answers the parameters it gives a material of its own through a render-thread proxy of the material, not
	 * a material instance per draw call: nothing to pool as an object, to keep from the collector, or to be copied with
	 * the mesh into a saved level. A material instance given to an image is answered for the same way, never written to.
	 */
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamCanvas* Canvas = Rig.RootCanvas();
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has its UI manager"), Manager))
	{
		return false;
	}
	UMaterialInterface* Material = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultUIMaterial, TEXT("DefaultUIMaterial"));
	if (!TestNotNull(TEXT("The default UI material, used here as a custom one"), Material)
		|| !TestTrue(TEXT("It takes the canvas's parameters"), UDreamCanvas::IsMaterialContainsDreamUIParameter(Material)))
	{
		return false;
	}
	UMaterialInstanceDynamic* Given = UMaterialInstanceDynamic::Create(Material, GetTransientPackage());
	UTexture2D* OtherTexture = UTexture2D::CreateTransient(4, 4);
	UDreamWidget* First = Rig.MakeWidget(TEXT("First"), nullptr, FVector2D(50.0, 50.0), FVector2D(-100.0, 0.0));
	UDreamWidget* Second = Rig.MakeWidget(TEXT("Second"), nullptr, FVector2D(50.0, 50.0), FVector2D(100.0, 0.0));
	UDreamTexture* FirstImage = First != nullptr ? First->CreateNewVisual<UDreamTexture>() : nullptr;
	UDreamTexture* SecondImage = Second != nullptr ? Second->CreateNewVisual<UDreamTexture>() : nullptr;
	if (!TestNotNull(TEXT("A material instance to give"), Given) || !TestNotNull(TEXT("A texture to tell the images apart"), OtherTexture)
		|| !TestNotNull(TEXT("The first image"), FirstImage) || !TestNotNull(TEXT("The second image"), SecondImage))
	{
		return false;
	}
	FirstImage->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
	FirstImage->SetOverrideMaterial(Material);
	SecondImage->SetTexture(OtherTexture);
	SecondImage->SetOverrideMaterial(Given);
	DrawFrames(Rig, Manager, 2);

	TestEqual(TEXT("No material instance of the material was made"), InstancesMadeFrom(Canvas, Material).Num(), 0);
	TestNull(TEXT("The instance given was not given the property data texture"), PropertyDataTextureOf(Given));
	TestEqual(TEXT("...nor any texture at all"), Given->TextureParameterValues.Num(), 0);
	return true;
}

#endif
