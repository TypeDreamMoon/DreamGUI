// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrefabAnimation/MovieSceneLGUIMaterialSystem.h"

#include "Core/Components/LexImage.h"
#include "Core/Components/LexText.h"
#include "PrefabAnimation/MovieSceneLGUIComponentTypes.h"

#include "EntitySystem/MovieSceneEntityMutations.h"

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"

#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseDoubleBlenderSystem.h"

#include "Materials/MaterialInstanceDynamic.h"

#include "Core/Components/LexVisualBatchMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneLGUIMaterialSystem)

namespace UE::MovieScene
{

FLGUIMaterialAccessor::FLGUIMaterialAccessor(const FLGUIMaterialKey& InKey)
	: Visual(CastChecked<ULexVisualBatchMesh>(InKey.Object.ResolveObjectPtr(), ECastCheckedType::NullAllowed))
{
	if (Visual)
	{
		LGUIMaterialHandle = InKey.LGUIMaterialHandle;
	}
}

FLGUIMaterialAccessor::FLGUIMaterialAccessor(UObject* InObject, FLGUIMaterialHandle InLGUIMaterialHandle)
	: Visual(Cast<ULexVisualBatchMesh>(InObject))
	, LGUIMaterialHandle(MoveTemp(InLGUIMaterialHandle))
{
	check(!InObject || Visual);
}

FLGUIMaterialAccessor::operator bool() const
{
	return Visual != nullptr;
}

FString FLGUIMaterialAccessor::ToString() const
{
	return FString::Printf(TEXT("CustomUIMaterial %s"), *Visual->GetPathName());
}

UMaterialInterface* FLGUIMaterialAccessor::GetMaterial() const
{
	if (auto Text = Cast<ULexText>(Visual))
	{
		return Text->GetOverrideMaterial();
	}
	else if (auto Image = Cast<ULexImage>(Visual))
	{
		return Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
	}
	return nullptr;
}

void FLGUIMaterialAccessor::SetMaterial(UMaterialInterface* InMaterial) const
{
	if (auto Text = Cast<ULexText>(Visual))
	{
		Text->SetOverrideMaterial(InMaterial);
	}
	else if (auto Image = Cast<ULexImage>(Visual))
	{
		auto Brush = Image->GetBrush();
		Brush.SetResourceObject(InMaterial);
		Image->SetBrush(Brush);
	}
}

UMaterialInstanceDynamic* FLGUIMaterialAccessor::CreateDynamicMaterial(UMaterialInterface* InMaterial)
{
	// Need to create a new MID, either because the parent has changed, or because one doesn't already exist
	TStringBuilder<128> DynamicName;
	InMaterial->GetFName().ToString(DynamicName);
	DynamicName.Append(TEXT("_Animated"));
	FName UniqueDynamicName = MakeUniqueObjectName(Visual, UMaterialInstanceDynamic::StaticClass() , DynamicName.ToString());

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(InMaterial, Visual, UniqueDynamicName);
	SetMaterial(MID);
	return MID;
}

TAutoRegisterPreAnimatedStorageID<FPreAnimatedLGUIMaterialSwitcherStorage> FPreAnimatedLGUIMaterialSwitcherStorage::StorageID;
TAutoRegisterPreAnimatedStorageID<FPreAnimatedLGUIMaterialParameterStorage> FPreAnimatedLGUIMaterialParameterStorage::StorageID;

} // namespace UE::MovieScene

UMovieSceneLGUIMaterialSystem::UMovieSceneLGUIMaterialSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneLGUIComponentTypes*    LGUIComponents  = FMovieSceneLGUIComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents  = FMovieSceneTracksComponentTypes::Get();

	RelevantComponent = LGUIComponents->LGUIMaterialHandle;
	Phase = ESystemPhase::Instantiation;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentConsumer(GetClass(), BuiltInComponents->ObjectResult);
		DefineComponentConsumer(GetClass(), BuiltInComponents->BoundObject);
		DefineComponentProducer(GetClass(), TracksComponents->BoundMaterial);
		DefineImplicitPrerequisite(UMovieSceneCachePreAnimatedStateSystem::StaticClass(), GetClass());
	}
}

void UMovieSceneLGUIMaterialSystem::OnLink()
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneLGUIComponentTypes* LGUIComponents  = FMovieSceneLGUIComponentTypes::Get();

	SystemImpl.MaterialSwitcherStorage = Linker->PreAnimatedState.GetOrCreateStorage<FPreAnimatedLGUIMaterialSwitcherStorage>();
	SystemImpl.MaterialParameterStorage = Linker->PreAnimatedState.GetOrCreateStorage<FPreAnimatedLGUIMaterialParameterStorage>();

	SystemImpl.OnLink(Linker, BuiltInComponents->BoundObject, LGUIComponents->LGUIMaterialHandle);
}

void UMovieSceneLGUIMaterialSystem::OnUnlink()
{
	SystemImpl.OnUnlink(Linker);
}

void UMovieSceneLGUIMaterialSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneLGUIComponentTypes* LGUIComponents  = FMovieSceneLGUIComponentTypes::Get();

	SystemImpl.OnRun(Linker, BuiltInComponents->BoundObject, LGUIComponents->LGUIMaterialHandle, InPrerequisites, Subsequents);
}

void UMovieSceneLGUIMaterialSystem::SavePreAnimatedState(const FPreAnimationParameters& InParameters)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneLGUIComponentTypes* LGUIComponents  = FMovieSceneLGUIComponentTypes::Get();

	SystemImpl.SavePreAnimatedState(Linker, BuiltInComponents->BoundObject, LGUIComponents->LGUIMaterialHandle, InParameters);
}
