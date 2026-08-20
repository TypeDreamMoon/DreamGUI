// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"

#include "Systems/MovieSceneMaterialSystem.h"
#include "MovieSceneDreamUIComponentTypes.h"

#include "MovieSceneDreamUIMaterialSystem.generated.h"

class UDreamVisualBatchMesh;
class UMovieScenePiecewiseDoubleBlenderSystem;

namespace UE::MovieScene
{

struct FDreamUIMaterialKey
{
	FObjectKey Object;
	FDreamUIMaterialHandle MaterialHandle;

	friend uint32 GetTypeHash(const FDreamUIMaterialKey& In)
	{
		uint32 Accumulator = GetTypeHash(In.Object);
		Accumulator ^= GetTypeHash(In.MaterialHandle);
		return Accumulator;
	}
	friend bool operator==(const FDreamUIMaterialKey& A, const FDreamUIMaterialKey& B)
	{
		return A.Object == B.Object && A.MaterialHandle == B.MaterialHandle;
	}
};

struct FDreamUIMaterialAccessor
{
	using KeyType = FDreamUIMaterialKey;

	UDreamVisualBatchMesh* Visual;
	FDreamUIMaterialHandle MaterialHandle;

	FDreamUIMaterialAccessor(const FDreamUIMaterialKey& InKey);
	FDreamUIMaterialAccessor(UObject* InObject, FDreamUIMaterialHandle InDreamGUIMaterialHandle);

	explicit operator bool() const;

	UMaterialInterface* GetMaterial() const;
	void SetMaterial(UMaterialInterface* InMaterial) const;
	UMaterialInstanceDynamic* CreateDynamicMaterial(UMaterialInterface* InMaterial);
	FString ToString() const;
};

using FPreAnimatedDreamUIMaterialTraits          = TPreAnimatedMaterialTraits<FDreamUIMaterialAccessor, UObject*, FDreamUIMaterialHandle>;
using FPreAnimatedDreamUIMaterialParameterTraits = TPreAnimatedMaterialParameterTraits<FDreamUIMaterialAccessor, UObject*, FDreamUIMaterialHandle>;

struct FPreAnimatedDreamUIMaterialSwitcherStorage
	: public TPreAnimatedStateStorage<TPreAnimatedMaterialTraits<FDreamUIMaterialAccessor, UObject*, FDreamUIMaterialHandle>>
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedDreamUIMaterialSwitcherStorage> StorageID;
};

struct FPreAnimatedDreamUIMaterialParameterStorage
	: public TPreAnimatedStateStorage<TPreAnimatedMaterialParameterTraits<FDreamUIMaterialAccessor, UObject*, FDreamUIMaterialHandle>>
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedDreamUIMaterialParameterStorage> StorageID;
};

} // namespace UE::MovieScene


UCLASS(MinimalAPI)
class UMovieSceneDreamUIMaterialSystem
	: public UMovieSceneEntitySystem
	, public IMovieScenePreAnimatedStateSystemInterface
{
public:

	GENERATED_BODY()

	UMovieSceneDreamUIMaterialSystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnLink() override;
	virtual void OnUnlink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	virtual void SavePreAnimatedState(const FPreAnimationParameters& InParameters) override;

private:

	UE::MovieScene::TMovieSceneMaterialSystem<UE::MovieScene::FDreamUIMaterialAccessor, UObject*, FDreamUIMaterialHandle> SystemImpl;
};
