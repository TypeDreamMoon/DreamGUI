// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"

#include "Systems/MovieSceneMaterialSystem.h"
#include "MovieSceneLexUIComponentTypes.h"

#include "MovieSceneLexUIMaterialSystem.generated.h"

class ULexVisualBatchMesh;
class UMovieScenePiecewiseDoubleBlenderSystem;

namespace UE::MovieScene
{

struct FLexUIMaterialKey
{
	FObjectKey Object;
	FLexUIMaterialHandle MaterialHandle;

	friend uint32 GetTypeHash(const FLexUIMaterialKey& In)
	{
		uint32 Accumulator = GetTypeHash(In.Object);
		Accumulator ^= GetTypeHash(In.MaterialHandle);
		return Accumulator;
	}
	friend bool operator==(const FLexUIMaterialKey& A, const FLexUIMaterialKey& B)
	{
		return A.Object == B.Object && A.MaterialHandle == B.MaterialHandle;
	}
};

struct FLexUIMaterialAccessor
{
	using KeyType = FLexUIMaterialKey;

	ULexVisualBatchMesh* Visual;
	FLexUIMaterialHandle MaterialHandle;

	FLexUIMaterialAccessor(const FLexUIMaterialKey& InKey);
	FLexUIMaterialAccessor(UObject* InObject, FLexUIMaterialHandle InLGUIMaterialHandle);

	explicit operator bool() const;

	UMaterialInterface* GetMaterial() const;
	void SetMaterial(UMaterialInterface* InMaterial) const;
	UMaterialInstanceDynamic* CreateDynamicMaterial(UMaterialInterface* InMaterial);
	FString ToString() const;
};

using FPreAnimatedLexUIMaterialTraits          = TPreAnimatedMaterialTraits<FLexUIMaterialAccessor, UObject*, FLexUIMaterialHandle>;
using FPreAnimatedLexUIMaterialParameterTraits = TPreAnimatedMaterialParameterTraits<FLexUIMaterialAccessor, UObject*, FLexUIMaterialHandle>;

struct FPreAnimatedLexUIMaterialSwitcherStorage
	: public TPreAnimatedStateStorage<TPreAnimatedMaterialTraits<FLexUIMaterialAccessor, UObject*, FLexUIMaterialHandle>>
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedLexUIMaterialSwitcherStorage> StorageID;
};

struct FPreAnimatedLexUIMaterialParameterStorage
	: public TPreAnimatedStateStorage<TPreAnimatedMaterialParameterTraits<FLexUIMaterialAccessor, UObject*, FLexUIMaterialHandle>>
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedLexUIMaterialParameterStorage> StorageID;
};

} // namespace UE::MovieScene


UCLASS(MinimalAPI)
class UMovieSceneLexUIMaterialSystem
	: public UMovieSceneEntitySystem
	, public IMovieScenePreAnimatedStateSystemInterface
{
public:

	GENERATED_BODY()

	UMovieSceneLexUIMaterialSystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnLink() override;
	virtual void OnUnlink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	virtual void SavePreAnimatedState(const FPreAnimationParameters& InParameters) override;

private:

	UE::MovieScene::TMovieSceneMaterialSystem<UE::MovieScene::FLexUIMaterialAccessor, UObject*, FLexUIMaterialHandle> SystemImpl;
};
