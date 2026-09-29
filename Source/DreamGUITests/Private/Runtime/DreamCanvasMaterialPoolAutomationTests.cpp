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

/*
 * EVERY MATERIAL INSTANCE A CANVAS KEEPS SAMPLES ITS DATA TEXTURES AS THEY GROW, NOT ONLY THE ONES IN USE.
 *
 * A canvas makes a material instance for each draw call through a custom material and keeps them from one
 * frame to the next: a frame with fewer such draw calls leaves the rest waiting in a pool. When the widget
 * property data outgrew its texture it used to move to a new one, and only the instances that frame's draw
 * calls took were told: an instance waiting in the pool kept the old texture, and the frame that took it back
 * drew through a texture the canvas no longer wrote to. A data texture now grows in place, and an instance
 * binds its reference, so none of them has anything to be told.
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
	FDreamCanvasPooledMaterialDataTextureTest,
	"DreamGUI.Canvas.AMaterialInstanceWaitingInItsPoolSamplesTheDataTextureStillWhenItGrows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasPooledMaterialDataTextureTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasMaterialPoolTestLocal;
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

	// Two images through the same custom material, on different textures so that they cannot share a draw
	// call: two draw calls, an instance each.
	UTexture2D* OtherTexture = UTexture2D::CreateTransient(4, 4);
	UDreamWidget* First = Rig.MakeWidget(TEXT("First"), nullptr, FVector2D(50.0, 50.0), FVector2D(-100.0, 0.0));
	UDreamWidget* Second = Rig.MakeWidget(TEXT("Second"), nullptr, FVector2D(50.0, 50.0), FVector2D(100.0, 0.0));
	UDreamTexture* FirstImage = First != nullptr ? First->CreateNewVisual<UDreamTexture>() : nullptr;
	UDreamTexture* SecondImage = Second != nullptr ? Second->CreateNewVisual<UDreamTexture>() : nullptr;
	if (!TestNotNull(TEXT("A texture to tell the images apart"), OtherTexture)
		|| !TestNotNull(TEXT("The first image"), FirstImage) || !TestNotNull(TEXT("The second image"), SecondImage))
	{
		return false;
	}
	FirstImage->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
	FirstImage->SetOverrideMaterial(Material);
	SecondImage->SetTexture(OtherTexture);
	SecondImage->SetOverrideMaterial(Material);
	DrawFrames(Rig, Manager, 2);

	const TArray<UMaterialInstanceDynamic*> Instances = InstancesMadeFrom(Canvas, Material);
	if (!TestEqual(TEXT("Two draw calls through the custom material, an instance each"), Instances.Num(), 2))
	{
		return false;
	}

	// One image hidden: the next frame's one draw call takes one instance, and the other waits in the pool.
	Second->SetWidgetActive(false);
	DrawFrames(Rig, Manager, 2);

	// The widget property data outgrows its texture. The rows claimed here stand for widgets that need one.
	UDreamUIDataAsTexture* PropertyData = Canvas->GetWidgetPropertyDataAsTexture();
	if (!TestNotNull(TEXT("The canvas keeps widget property data"), PropertyData))
	{
		return false;
	}
	const UTexture* Texture = PropertyData->GetDataTexture();
	const int HeightBefore = PropertyData->GetTextureHeight();
	TArray<int> ClaimedRows;
	while (PropertyData->GetTextureHeight() == HeightBefore && ClaimedRows.Num() < 65536)
	{
		ClaimedRows.Add(PropertyData->RegisterBuffer());
	}
	const int HeightAfter = PropertyData->GetTextureHeight();
	for (const int Row : ClaimedRows)
	{
		PropertyData->UnregisterBuffer(Row);
	}
	if (!TestTrue(FString::Printf(TEXT("The property data grew (%d rows, then %d)"), HeightBefore, HeightAfter), HeightAfter > HeightBefore))
	{
		return false;
	}
	TestTrue(TEXT("...in place: it is the same texture"), PropertyData->GetDataTexture() == Texture);
	DrawFrames(Rig, Manager, 2);

	// Nothing has to be given to anyone: the instances bind the texture's reference, which the growth pointed at
	// the taller one. The pooled instance, which no draw call took this frame, has the texture as much as the other.
	for (const UMaterialInstanceDynamic* Instance : Instances)
	{
		TestTrue(FString::Printf(TEXT("%s samples the property data texture, grown"), *Instance->GetName()),
			PropertyDataTextureOf(Instance) == Texture);
	}
	return true;
}

#endif
