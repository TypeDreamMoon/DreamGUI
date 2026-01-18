// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/MovieSceneLexUIMaterialSystem.h"

#include "Core/Components/LexImage.h"
#include "Core/Components/LexText.h"
#include "PrefabSystem/PrefabAnimation/MovieSceneLexUIComponentTypes.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"

#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseDoubleBlenderSystem.h"

#include "Materials/MaterialInstanceDynamic.h"

#include "Core/Components/LexVisualBatchMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneLexUIMaterialSystem)

namespace UE::MovieScene
{

FLexUIMaterialAccessor::FLexUIMaterialAccessor(const FLexUIMaterialKey& InKey)
	: Visual(CastChecked<ULexVisualBatchMesh>(InKey.Object.ResolveObjectPtr(), ECastCheckedType::NullAllowed))
{
	if (Visual)
	{
		MaterialHandle = InKey.MaterialHandle;
	}
}

FLexUIMaterialAccessor::FLexUIMaterialAccessor(UObject* InObject, FLexUIMaterialHandle InLGUIMaterialHandle)
	: Visual(Cast<ULexVisualBatchMesh>(InObject))
	, MaterialHandle(MoveTemp(InLGUIMaterialHandle))
{
	check(!InObject || Visual);
}

FLexUIMaterialAccessor::operator bool() const
{
	return Visual != nullptr;
}

FString FLexUIMaterialAccessor::ToString() const
{
	return FString::Printf(TEXT("CustomUIMaterial %s"), *Visual->GetPathName());
}

UMaterialInterface* FLexUIMaterialAccessor::GetMaterial() const
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

void FLexUIMaterialAccessor::SetMaterial(UMaterialInterface* InMaterial) const
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

UMaterialInstanceDynamic* FLexUIMaterialAccessor::CreateDynamicMaterial(UMaterialInterface* InMaterial)
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

TAutoRegisterPreAnimatedStorageID<FPreAnimatedLexUIMaterialSwitcherStorage> FPreAnimatedLexUIMaterialSwitcherStorage::StorageID;
TAutoRegisterPreAnimatedStorageID<FPreAnimatedLexUIMaterialParameterStorage> FPreAnimatedLexUIMaterialParameterStorage::StorageID;

} // namespace UE::MovieScene

UMovieSceneLexUIMaterialSystem::UMovieSceneLexUIMaterialSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneLexUIComponentTypes*    LGUIComponents  = FMovieSceneLexUIComponentTypes::Get();
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

void UMovieSceneLexUIMaterialSystem::OnLink()
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneLexUIComponentTypes* LGUIComponents  = FMovieSceneLexUIComponentTypes::Get();

	SystemImpl.MaterialSwitcherStorage = Linker->PreAnimatedState.GetOrCreateStorage<FPreAnimatedLexUIMaterialSwitcherStorage>();
	SystemImpl.MaterialParameterStorage = Linker->PreAnimatedState.GetOrCreateStorage<FPreAnimatedLexUIMaterialParameterStorage>();

	SystemImpl.OnLink(Linker, BuiltInComponents->BoundObject, LGUIComponents->LGUIMaterialHandle);
}

void UMovieSceneLexUIMaterialSystem::OnUnlink()
{
	SystemImpl.OnUnlink(Linker);
}

void UMovieSceneLexUIMaterialSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneLexUIComponentTypes* LGUIComponents  = FMovieSceneLexUIComponentTypes::Get();

	SystemImpl.OnRun(Linker, BuiltInComponents->BoundObject, LGUIComponents->LGUIMaterialHandle, InPrerequisites, Subsequents);
}

void UMovieSceneLexUIMaterialSystem::SavePreAnimatedState(const FPreAnimationParameters& InParameters)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*       BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneLexUIComponentTypes* LGUIComponents  = FMovieSceneLexUIComponentTypes::Get();

	SystemImpl.SavePreAnimatedState(Linker, BuiltInComponents->BoundObject, LGUIComponents->LGUIMaterialHandle, InParameters);
}
