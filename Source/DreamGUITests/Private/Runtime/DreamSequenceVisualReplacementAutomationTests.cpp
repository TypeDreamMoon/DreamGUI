// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUserWidget.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamSequenceVisualReplacementTest,
	"DreamGUI.Extensions.SequencePlayers.FollowTheCurrentVisualAfterReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamSequenceVisualReplacementTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Sprite sheet seek and playback follow the replacement texture"));
	OutTestCommands.Add(TEXT("Sheet"));
	OutBeautifiedNames.Add(TEXT("Sprite sequence seek and playback follow the replacement sprite"));
	OutTestCommands.Add(TEXT("Sequence"));
}

bool FDreamSequenceVisualReplacementTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(640, 480));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the runtime rig is available"), Rig.IsUsable()))return false;
	UDreamWidget* Widget = NewObject<UDreamWidget>(Rig.GetWorld());
	if (!TestNotNull(TEXT("the animation widget exists"), Widget))return false;
	Widget->SetDisplayName(TEXT("Animated"));
	Widget->SetWidth(64.0f);
	Widget->SetHeight(64.0f);
	Widget->SetParentBeforeRegister(Rig.Root());
	UTexture2D* Texture = UTexture2D::CreateTransient(8, 8);
	if (!TestNotNull(TEXT("the animation has a texture"), Texture))return false;

	if (Parameters == TEXT("Sheet"))
	{
		TStrongObjectPtr<UDreamTexture> Original(Cast<UDreamTexture>(Widget->CreateNewVisual(UDreamTexture::StaticClass())));
		Original->SetTexture(Texture);
		// Configure before registration, then let the public hierarchy registration begin autoplay.
		UUISpriteSheetTexturePlayer* Player = Widget->AddComponent<UUISpriteSheetTexturePlayer>();
		if (!TestNotNull(TEXT("the sheet player exists"), Player))return false;
		Player->SetWidthCount(4);
		Player->SetHeightCount(1);
		Player->SetFps(4.0f);
		RegisterDreamWidgetHierarchy(Widget);
		TestTrue(TEXT("registration starts the configured sheet animation"), Player->GetIsPlaying());
		Player->SeekFrame(1);
		TestEqual(TEXT("the initial visual displays the requested frame"), Original->GetUVRect().X, 0.25f);

		TStrongObjectPtr<UDreamTexture> Replacement(Cast<UDreamTexture>(Widget->CreateNewVisual(UDreamTexture::StaticClass())));
		Replacement->SetTexture(Texture);
		TestTrue(TEXT("replacement does not invalidate the old visual"), Original.IsValid());
		TestTrue(TEXT("the owner displays the replacement"), Widget->GetVisual() == Replacement.Get());
		Player->SeekFrame(2);
		TestEqual(TEXT("seeking updates the replacement visual"), Replacement->GetUVRect().X, 0.5f);
		TestEqual(TEXT("seeking leaves the detached visual untouched"), Original->GetUVRect().X, 0.25f);

		TStrongObjectPtr<UDreamTexture> PlayingReplacement(Cast<UDreamTexture>(Widget->CreateNewVisual(UDreamTexture::StaticClass())));
		PlayingReplacement->SetTexture(Texture);
		const FVector4f DetachedUV = Replacement->GetUVRect();
		TestTrue(TEXT("the animation remains active during replacement"), Player->GetIsPlaying());
		Rig.Context().PumpOneFrame(0.3f);
		TestEqual(TEXT("the next runtime tick displays frame three on the current visual"), PlayingReplacement->GetUVRect().X, 0.75f);
		TestTrue(TEXT("runtime playback leaves the detached replacement untouched"), Replacement->GetUVRect() == DetachedUV);
		Player->Stop();
	}
	else
	{
		TArray<UDreamUISpriteData_BaseObject*> Frames;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Frames.Add(UDreamUISpriteData::CreateDreamUISpriteData(Widget, Texture, FMargin(0.0f), NAME_None));
			if (!TestNotNull(TEXT("the sequence frame exists"), Frames.Last()))return false;
		}
		TStrongObjectPtr<UDreamSprite> Original(Cast<UDreamSprite>(Widget->CreateNewVisual(UDreamSprite::StaticClass())));
		UUISpriteSequencePlayer* Player = Widget->AddComponent<UUISpriteSequencePlayer>();
		if (!TestNotNull(TEXT("the sprite player exists"), Player))return false;
		Player->SetSpriteSequence(Frames);
		Player->SetSnapSpriteSize(false);
		Player->SetFps(4.0f);
		RegisterDreamWidgetHierarchy(Widget);
		TestTrue(TEXT("registration starts the configured sprite animation"), Player->GetIsPlaying());
		Player->SeekFrame(1);
		TestEqual(TEXT("the initial visual displays the requested frame"), Original->GetSprite(), Frames[1]);

		TStrongObjectPtr<UDreamSprite> Replacement(Cast<UDreamSprite>(Widget->CreateNewVisual(UDreamSprite::StaticClass())));
		TestTrue(TEXT("replacement does not invalidate the old visual"), Original.IsValid());
		TestTrue(TEXT("the owner displays the replacement"), Widget->GetVisual() == Replacement.Get());
		Player->SeekFrame(2);
		TestEqual(TEXT("seeking updates the replacement visual"), Replacement->GetSprite(), Frames[2]);
		TestEqual(TEXT("seeking leaves the detached visual untouched"), Original->GetSprite(), Frames[1]);

		TStrongObjectPtr<UDreamSprite> PlayingReplacement(Cast<UDreamSprite>(Widget->CreateNewVisual(UDreamSprite::StaticClass())));
		UDreamUISpriteData_BaseObject* DetachedFrame = Replacement->GetSprite();
		TestTrue(TEXT("the animation remains active during replacement"), Player->GetIsPlaying());
		Rig.Context().PumpOneFrame(0.3f);
		TestEqual(TEXT("the next runtime tick displays frame three on the current visual"), PlayingReplacement->GetSprite(), Frames[3]);
		TestEqual(TEXT("runtime playback leaves the detached replacement untouched"), Replacement->GetSprite(), DetachedFrame);
		Player->Stop();
	}
	return true;
}

#endif
