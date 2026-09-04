// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUISpriteDataFactory.h"
#include "DreamGUIEditorModule.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUISpriteData.h"
#include "Engine/Texture2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ScopedTransaction.h"
#include "Utils/DreamUIUtils.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DreamUISpriteDataFactory"


UDreamUISpriteDataFactory::UDreamUISpriteDataFactory()
{
	SupportedClass = UDreamUISpriteData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}
UObject* UDreamUISpriteDataFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	bool isDefaltTexture = false;
	if (SpriteTexture == nullptr)
	{
		SpriteTexture = FDreamUIUtils::GetDefaultWhiteTexture();
		isDefaltTexture = true;
	}
	// check size
	if (SpriteTexture.IsValid() && !isDefaltTexture)
	{
		int32 atlasPadding = GetDefault<UDreamUISettings>()->DefaultAtlasSetting.SpaceBetweenSprites;
		if (SpriteTexture->GetSurfaceWidth() + atlasPadding * 2 > WARNING_ATLAS_SIZE || SpriteTexture->GetSurfaceHeight() + atlasPadding * 2 > WARNING_ATLAS_SIZE)
		{
			auto LogMsg = LOCTEXT("TextureSizeError", "Target texture width or height is too large! Consider use UITexture to render this texture.");
			UE_LOG(DreamGUIEditor, Error, TEXT("%s"), *(LogMsg.ToString()));
			FNotificationInfo Info(LogMsg);
			Info.Image = FAppStyle::GetBrush(TEXT("LevelEditor.RecompileGameCode"));
			Info.FadeInDuration = 0.1f;
			Info.FadeOutDuration = 0.5f;
			Info.ExpireDuration = 8.0f;
			Info.bUseThrobber = false;
			Info.bUseSuccessFailIcons = true;
			Info.bUseLargeFont = true;
			Info.bFireAndForget = false;
			Info.bAllowThrottleWhenFrameRateIsLow = false;
			auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
			NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
			NotificationItem->ExpireAndFadeout();

			auto CompileFailSound = LoadObject<USoundBase>(NULL, TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
			GEditor->PlayEditorSound(CompileFailSound);

			return nullptr;
		}
		// Apply setting for sprite creation
		//SpriteTexture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
		//
		// Creating the sprite rewrites the SOURCE texture's compression, LOD group and sRGB, which is
		// a second asset changing behind the author's back. UTexture's contract is that a property
		// write sits inside PreEditChange/PostEditChange -- that is what blocks the async build and
		// rebuilds the resource -- and the transaction is what lets the change be undone with the
		// asset it was made for.
		// The question of whether anything needs writing is asked on the outside: a texture that is
		// already configured must not be dirtied, nor have its resource rebuilt, just to make a sprite.
		{
			UTexture2D* Texture = SpriteTexture.Get();
			if (UDreamUISpriteData::NeedsSpriteTextureSetting(Texture))
			{
				const FScopedTransaction Transaction(LOCTEXT("ApplySpriteTextureSettings", "Apply Sprite Texture Settings"));
				Texture->Modify();
				Texture->PreEditChange(nullptr);
				UDreamUISpriteData::CheckAndApplySpriteTextureSetting(Texture);
				Texture->PostEditChange();
			}
		}
	}

	UDreamUISpriteData* NewAsset = NewObject<UDreamUISpriteData>(InParent, Class, Name, Flags | RF_Transactional);
	if (SpriteTexture.IsValid())
	{
		NewAsset->SpriteTexture = SpriteTexture.Get();
		NewAsset->SpriteInfo.Width = SpriteTexture->GetSurfaceWidth();
		NewAsset->SpriteInfo.Height = SpriteTexture->GetSurfaceHeight();
	}
	return NewAsset;
}

#undef LOCTEXT_NAMESPACE
