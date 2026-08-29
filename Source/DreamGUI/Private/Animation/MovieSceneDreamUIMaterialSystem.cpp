// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Animation/MovieSceneDreamUIMaterialSystem.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Animation/MovieSceneDreamUIComponentTypes.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"

#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseDoubleBlenderSystem.h"

#include "Materials/MaterialInstanceDynamic.h"

#include "Core/Components/DreamVisualBatchMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneDreamUIMaterialSystem)

namespace UE::MovieScene
{

FDreamUIMaterialAccessor::FDreamUIMaterialAccessor(const FDreamUIMaterialKey& InKey)
	: Visual(CastChecked<UDreamVisualBatchMesh>(InKey.Object.ResolveObjectPtr(), ECastCheckedType::NullAllowed))
{
	if (Visual)
	{
		MaterialHandle = InKey.MaterialHandle;
	}
}

FDreamUIMaterialAccessor::FDreamUIMaterialAccessor(UObject* InObject, FDreamUIMaterialHandle InDreamGUIMaterialHandle)
	: Visual(Cast<UDreamVisualBatchMesh>(InObject))
	, MaterialHandle(MoveTemp(InDreamGUIMaterialHandle))
{
	check(!InObject || Visual);
}

FDreamUIMaterialAccessor::operator bool() const
{
	return Visual != nullptr;
}

FString FDreamUIMaterialAccessor::ToString() const
{
	return FString::Printf(TEXT("CustomUIMaterial %s"), *Visual->GetPathName());
}

UMaterialInterface* FDreamUIMaterialAccessor::GetMaterial() const
{
	if (auto Text = Cast<UDreamText>(Visual))
	{
		return Text->GetOverrideMaterial();
	}
	else if (auto Image = Cast<UDreamImage>(Visual))
	{
		return Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
	}
	return nullptr;
}

void FDreamUIMaterialAccessor::SetMaterial(UMaterialInterface* InMaterial) const
{
	if (auto Text = Cast<UDreamText>(Visual))
	{
		Text->SetOverrideMaterial(InMaterial);
	}
	else if (auto Image = Cast<UDreamImage>(Visual))
	{
		auto Brush = Image->GetBrush();
		Brush.SetResourceObject(InMaterial);
		Image->SetBrush(Brush);
	}
}

UMaterialInstanceDynamic* FDreamUIMaterialAccessor::CreateDynamicMaterial(UMaterialInterface* InMaterial)
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

TAutoRegisterPreAnimatedStorageID<FPreAnimatedDreamUIMaterialSwitcherStorage> FPreAnimatedDreamUIMaterialSwitcherStorage::StorageID;
TAutoRegisterPreAnimatedStorageID<FPreAnimatedDreamUIMaterialParameterStorage> FPreAnimatedDreamUIMaterialParameterStorage::StorageID;

} // namespace UE::MovieScene

UMovieSceneDreamUIMaterialSystem::UMovieSceneDreamUIMaterialSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneDreamUIComponentTypes*    DreamGUIComponents  = FMovieSceneDreamUIComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents  = FMovieSceneTracksComponentTypes::Get();

	RelevantComponent = DreamGUIComponents->DreamUIMaterialHandle;
	Phase = ESystemPhase::Instantiation;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentConsumer(GetClass(), BuiltInComponents->ObjectResult);
		DefineComponentConsumer(GetClass(), BuiltInComponents->BoundObject);
		DefineComponentProducer(GetClass(), TracksComponents->BoundMaterial);
		DefineImplicitPrerequisite(UMovieSceneCachePreAnimatedStateSystem::StaticClass(), GetClass());
	}
}

void UMovieSceneDreamUIMaterialSystem::OnLink()
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneDreamUIComponentTypes* DreamUIComponents  = FMovieSceneDreamUIComponentTypes::Get();

	SystemImpl.MaterialSwitcherStorage = Linker->PreAnimatedState.GetOrCreateStorage<FPreAnimatedDreamUIMaterialSwitcherStorage>();
	SystemImpl.MaterialParameterStorage = Linker->PreAnimatedState.GetOrCreateStorage<FPreAnimatedDreamUIMaterialParameterStorage>();

	SystemImpl.OnLink(Linker, BuiltInComponents->BoundObject, DreamUIComponents->DreamUIMaterialHandle);
}

void UMovieSceneDreamUIMaterialSystem::OnUnlink()
{
	SystemImpl.OnUnlink(Linker);
}

void UMovieSceneDreamUIMaterialSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneDreamUIComponentTypes* DreamUIComponents  = FMovieSceneDreamUIComponentTypes::Get();

	SystemImpl.OnRun(Linker, BuiltInComponents->BoundObject, DreamUIComponents->DreamUIMaterialHandle, InPrerequisites, Subsequents);
}

void UMovieSceneDreamUIMaterialSystem::SavePreAnimatedState(const FPreAnimationParameters& InParameters)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneDreamUIComponentTypes* DreamUIComponents  = FMovieSceneDreamUIComponentTypes::Get();

	SystemImpl.SavePreAnimatedState(Linker, BuiltInComponents->BoundObject, DreamUIComponents->DreamUIMaterialHandle, InParameters);
}
