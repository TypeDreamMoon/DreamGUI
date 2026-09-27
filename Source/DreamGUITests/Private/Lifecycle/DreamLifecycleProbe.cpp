// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Lifecycle/DreamLifecycleProbe.h"

#if WITH_EDITOR

#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

namespace DreamTests::Lifecycle
{
	TArray<FString> FindZeroSizeDynamicTextures()
	{
		TArray<FString> Found;
		for (TObjectIterator<UTexture2DDynamic> It; It; ++It)
		{
			const UTexture2DDynamic* Texture = *It;
			if (!IsValid(Texture) || Texture->IsTemplate())
			{
				continue;
			}
			if (Texture->SizeX <= 0 || Texture->SizeY <= 0 || Texture->NumMips <= 0)
			{
				Found.Add(FString::Printf(TEXT("%s (%dx%d, %d mips)"), *Texture->GetPathName(), Texture->SizeX, Texture->SizeY, Texture->NumMips));
			}
		}
		return Found;
	}

	TArray<FString> FindPersistentCanvasMeshes(const UWorld* InWorld)
	{
		TArray<FString> Found;
		for (TObjectIterator<UDreamUIMeshComponent> It; It; ++It)
		{
			const UDreamUIMeshComponent* Mesh = *It;
			if (!IsValid(Mesh) || Mesh->IsTemplate() || Mesh->GetTypedOuter<UWorld>() != InWorld)
			{
				continue;
			}
			if (!Mesh->HasAnyFlags(RF_Transient))
			{
				Found.Add(Mesh->GetPathName());
			}
		}
		return Found;
	}

	TArray<FString> FindObjectsInPackage(const UPackage* InPackage, const UClass* InClass)
	{
		TArray<FString> Found;
		if (InPackage == nullptr || InClass == nullptr)
		{
			return Found;
		}
		ForEachObjectWithPackage(InPackage, [&Found, InClass](UObject* Object)
		{
			if (IsValid(Object) && Object->IsA(InClass))
			{
				Found.Add(Object->GetPathName());
			}
			return true;
		}, EGetObjectsFlags::IncludeNestedObjects);
		return Found;
	}

	void DrawFrames(UWorld* InWorld, int32 FrameCount)
	{
		UDreamUIManagerWorldSubsystem* Manager = InWorld != nullptr ? InWorld->GetSubsystem<UDreamUIManagerWorldSubsystem>() : nullptr;
		if (Manager == nullptr)
		{
			return;
		}
		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			Manager->Tick(1.0f / 60.0f);
			Manager->SubmitCanvasDrawCall();
		}
	}

	TArray<AActor*> CopyPasteActor(UWorld* InWorld, AActor* InActor, int32 Times, FString* OutCopiedText)
	{
		TArray<AActor*> Pasted;
		if (GEditor == nullptr || InWorld == nullptr || InActor == nullptr)
		{
			return Pasted;
		}
		GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
		GEditor->SelectActor(InActor, /*bInSelected*/ true, /*bNotify*/ false, /*bSelectEvenIfHidden*/ true);
		FString Copied;
		GEditor->edactCopySelected(InWorld, &Copied);
		for (int32 Paste = 0; Paste < Times; ++Paste)
		{
			GEditor->SelectNone(false, true);
			GEditor->edactPasteSelected(InWorld, /*bDuplicate*/ false, /*bOffsetLocations*/ false, /*bWarnIfHidden*/ false, &Copied);
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				if (AActor* Actor = Cast<AActor>(*It))
				{
					Pasted.Add(Actor);
				}
			}
		}
		GEditor->SelectNone(false, true);
		if (OutCopiedText != nullptr)
		{
			*OutCopiedText = MoveTemp(Copied);
		}
		return Pasted;
	}

	UWorld* DuplicateWorldForPlayInEditor(UWorld* InWorld)
	{
		if (InWorld == nullptr)
		{
			return nullptr;
		}
		// An instance id no real session in this editor uses, so the package name cannot collide with one.
		constexpr int32 InstanceId = 73;
		UPackage* SourcePackage = InWorld->GetOutermost();
		const FString PlayName = UWorld::ConvertToPIEPackageName(SourcePackage->GetName(), InstanceId);
		UPackage* PlayPackage = CreatePackage(*PlayName);
		// What UEditorEngine::CreatePIEWorldByDuplication gives the package before it duplicates into it.
		PlayPackage->SetPackageFlags(PKG_PlayInEditor | PKG_NewlyCreated);
		PlayPackage->SetPIEInstanceID(InstanceId);
		PlayPackage->MarkAsFullyLoaded();
		return UWorld::GetDuplicatedWorldForPIE(InWorld, PlayPackage, InstanceId);
	}

	void DestroyDuplicatedWorld(UWorld* InDuplicate)
	{
		if (InDuplicate == nullptr)
		{
			return;
		}
		// Never initialised: no scene, physics or subsystems to shut down. What is left is letting every
		// duplicated object go, the package with them.
		UPackage* Package = InDuplicate->GetOutermost();
		ForEachObjectWithPackage(Package, [](UObject* Object)
		{
			Object->ClearFlags(RF_Standalone | RF_Public);
			Object->MarkAsGarbage();
			return true;
		}, EGetObjectsFlags::IncludeNestedObjects);
		Package->ClearFlags(RF_Standalone | RF_Public);
		Package->MarkAsGarbage();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	}
}

#endif
