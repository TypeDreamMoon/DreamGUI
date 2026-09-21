// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamUISequenceTrack.h"
#include "Animation/DreamUISequence.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
// NewComponentType / DuplicateChildComponent are templates whose definitions live here; without it
// this TU links only while a unity-blob neighbour happens to include it first.
#include "EntitySystem/MovieSceneEntityFactoryTemplates.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneSpawnablesSystem.h"
#include "Evaluation/MovieSceneEvaluationField.h"
#include "Bindings/MovieSceneCustomBinding.h"
#include "Evaluation/MovieSceneEvaluationState.h"
#include "Evaluation/MovieSceneRootOverridePath.h"

#define LOCTEXT_NAMESPACE "DreamUISequenceTrack"

namespace UE::MovieScene
{
	FDreamUISequenceComponentTypes* FDreamUISequenceComponentTypes::Get()
	{
		static TUniquePtr<FDreamUISequenceComponentTypes> GTypes;
		if (!GTypes.IsValid())
		{
			GTypes.Reset(new FDreamUISequenceComponentTypes);
		}
		return GTypes.Get();
	}

	FDreamUISequenceComponentTypes::FDreamUISequenceComponentTypes()
	{
		FComponentRegistry* ComponentRegistry = UMovieSceneEntitySystemLinker::GetComponents();
		ComponentRegistry->NewComponentType(&DreamUISequence, TEXT("DreamUI Sequence"));
		ComponentRegistry->Factories.DuplicateChildComponent(DreamUISequence);
	}
}

bool UDreamUISequenceSection::PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder)
{
	const int32 EntityIndex = OutFieldBuilder->FindOrAddEntity(this, 0);
	const int32 MetaDataIndex = OutFieldBuilder->AddMetaData(InMetaData);
	OutFieldBuilder->AddPersistentEntity(EffectiveRange, EntityIndex, MetaDataIndex);
	return true;
}

void UDreamUISequenceSection::ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	if (!EntityLinker->GetInstanceRegistry()->IsHandleValid(Params.Sequence.InstanceHandle))
	{
		return;
	}

	// The root-binding override is for the ASSET form only, and it is conditional rather than an
	// early exit -- the default sub-section components at the bottom are what make ANY sub-sequence
	// evaluate at all, and returning before them is why an embedded animation in one of these
	// sections used to sit there doing nothing.
	//
	// A .DreamUISequence asset addresses its widgets as paths from a root binding, so playing one
	// under a different widget means overriding that one binding: the whole re-rooting scheme. An
	// EMBEDDED UDreamWidgetAnimation has no root binding and needs none -- its references resolve
	// against whatever context the engine hands them, which for a nested animation is the widget its
	// own component lives on, exactly as when it is played on its own. Nesting one is composition in
	// time (a long animation assembled out of short ones), not retargeting.
	if (UDreamUISequence* Asset = Cast<UDreamUISequence>(GetSequence()))
	{
		if (Asset->GetRootBindingGuid().IsValid())
		{
			const FSubSequencePath PathToRoot = EntityLinker->GetInstanceRegistry()->GetInstance(Params.Sequence.InstanceHandle).GetSubSequencePath();
			const FMovieSceneSequenceID ResolvedSequenceID = PathToRoot.ResolveChildSequenceID(GetSequenceID());

			// The component stores the asset's root binding as a resolved-from-root operand; the system
			// overrides it to this section's own object binding when the section links.
			FDreamUISequenceComponentData ComponentData;
			ComponentData.InnerOperand = FMovieSceneEvaluationOperand(ResolvedSequenceID, Asset->GetRootBindingGuid());

			const FGuid ObjectBindingID = Params.GetObjectBindingID();

			OutImportedEntity->AddBuilder(
				FEntityBuilder()
					.AddConditional(FBuiltInComponentTypes::Get()->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid())
					.Add(FDreamUISequenceComponentTypes::Get()->DreamUISequence, ComponentData));
		}
	}

	BuildDefaultSubSectionComponents(EntityLinker, Params, OutImportedEntity);
}

bool UDreamUISequenceTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UDreamUISequenceSection::StaticClass();
}

UMovieSceneSection* UDreamUISequenceTrack::CreateNewSection()
{
	return NewObject<UDreamUISequenceSection>(this, NAME_None, RF_Transactional);
}

#if WITH_EDITORONLY_DATA
FText UDreamUISequenceTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "DreamUI Animation");
}
#endif

UDreamUISequenceSystem::UDreamUISequenceSystem(const FObjectInitializer& ObjInit)
	: UMovieSceneEntitySystem(ObjInit)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FDreamUISequenceComponentTypes* DreamComponents = FDreamUISequenceComponentTypes::Get();

	Phase = ESystemPhase::Spawn;
	RelevantComponent = DreamComponents->DreamUISequence;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(GetClass(), UMovieSceneSpawnablesSystem::StaticClass());
	}

	ApplicableFilter.Filter.All({ DreamComponents->DreamUISequence });
	ApplicableFilter.Filter.Any({ BuiltInComponents->Tags.NeedsLink, BuiltInComponents->Tags.NeedsUnlink });
}

void UDreamUISequenceSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	if (!ApplicableFilter.Matches(Linker->EntityManager))
	{
		return;
	}

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FDreamUISequenceComponentTypes* DreamComponents = FDreamUISequenceComponentTypes::Get();
	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	auto SetupTeardownBindingOverrides = [BuiltInComponents, InstanceRegistry](
			FEntityAllocationIteratorItem AllocationItem,
			TRead<FInstanceHandle> InstanceHandles,
			TRead<FGuid> ObjectBindingIDs,
			TRead<FDreamUISequenceComponentData> SequenceDatas)
	{
		const FComponentMask& Mask = AllocationItem.GetAllocationType();
		const bool bHasNeedsLink = Mask.Contains(BuiltInComponents->Tags.NeedsLink);
		const bool bHasNeedsUnlink = Mask.Contains(BuiltInComponents->Tags.NeedsUnlink);

		const int32 Num = AllocationItem.GetAllocation()->Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FSequenceInstance& SequenceInstance = InstanceRegistry->GetInstance(InstanceHandles[Index]);
			const FGuid& ObjectBindingID = ObjectBindingIDs[Index];
			const FDreamUISequenceComponentData& SequenceData = SequenceDatas[Index];

			IStaticBindingOverridesPlaybackCapability* StaticOverrides = SequenceInstance.GetSharedPlaybackState()->FindCapability<IStaticBindingOverridesPlaybackCapability>();
			if (ensure(StaticOverrides))
			{
				if (bHasNeedsLink)
				{
					const FMovieSceneSequenceID SequenceID = SequenceInstance.GetSequenceID();
					const FMovieSceneEvaluationOperand OuterOperand(SequenceID, ObjectBindingID);
					StaticOverrides->AddBindingOverride(SequenceData.InnerOperand, OuterOperand);
				}
				else if (bHasNeedsUnlink)
				{
					StaticOverrides->RemoveBindingOverride(SequenceData.InnerOperand);
				}
			}
		}
	};

	FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Read(BuiltInComponents->GenericObjectBinding)
		.Read(DreamComponents->DreamUISequence)
		.FilterAny({ BuiltInComponents->Tags.NeedsLink, BuiltInComponents->Tags.NeedsUnlink })
		.Iterate_PerAllocation(&Linker->EntityManager, SetupTeardownBindingOverrides);
}

#undef LOCTEXT_NAMESPACE
