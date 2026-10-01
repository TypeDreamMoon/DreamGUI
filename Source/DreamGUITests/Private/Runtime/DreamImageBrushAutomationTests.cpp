// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamWidget.h"
#include "DreamScopedWorld.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Utils/DreamUIUtils.h"

/*
 * An image's brush set to a material. SetBrush_Material, whose name says it takes one, was declared with a texture
 * parameter -- upstream fixed the same slip later (d6952e790) -- so a Blueprint could not hand it a material at all.
 * SetBrushFromMaterial (UMG's name) takes the material; the old function is deprecated and still does what it did.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamImageSetBrushFromMaterialTest,
	"DreamGUI.Image.SetBrushFromMaterialDrawsTheBrushWithTheMaterialAndTheOldSetterStillTakesATexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamImageSetBrushFromMaterialTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	Widget->SetWidth(64.0f);
	Widget->SetHeight(64.0f);
	Widget->OnRegister();
	UDreamImage* Image = Widget->CreateNewVisual<UDreamImage>();
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamGUI/Materials/DreamUI_ImageAndFont.DreamUI_ImageAndFont"));
	if (!TestNotNull(TEXT("An image"), Image) || !TestNotNull(TEXT("The plugin's image material loads"), Material))
	{
		Widget->DestroyWidget();
		return false;
	}

	Image->SetBrushFromMaterial(Material);
	TestTrue(TEXT("The brush draws with the material"), Image->GetBrush().GetResourceObject() == Material);
	Image->SetBrushFromMaterial(nullptr);
	TestTrue(TEXT("...and with nothing once it is handed none"), Image->GetBrush().GetResourceObject() == nullptr);

	UTexture2D* Texture = FDreamUIUtils::GetDefaultWhiteTexture();
	Image->SetBrush_Material(Texture);
	TestTrue(TEXT("The deprecated setter still puts the texture it is given on the brush"), Image->GetBrush().GetResourceObject() == Texture);

	Widget->DestroyWidget();
	return true;
}

#endif
