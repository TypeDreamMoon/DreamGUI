// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/MovieSceneDreamUIComponentTypes.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneEntityFactoryTemplates.h"
#include "EntitySystem/MovieScenePropertyComponentHandler.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"

namespace UE
{
namespace MovieScene
{

static bool GMovieSceneDreamUIComponentTypesDestroyed = false;
static TUniquePtr<FMovieSceneDreamUIComponentTypes> GMovieSceneDreamUIComponentTypes;

FMovieSceneDreamUIComponentTypes::FMovieSceneDreamUIComponentTypes()
{
	FComponentRegistry* ComponentRegistry = UMovieSceneEntitySystemLinker::GetComponents();

	ComponentRegistry->NewComponentType(&DreamUIMaterialPath, TEXT("DreamUI Material Path"), EComponentTypeFlags::CopyToChildren | EComponentTypeFlags::CopyToOutput);
	ComponentRegistry->NewComponentType(&DreamUIMaterialHandle, TEXT("DreamUI Material Handle"), EComponentTypeFlags::CopyToOutput);
	/** Initializer that initializes the value of an FDreamGUIMaterialHandle derived from an FDreamGUIMaterialPath */
	struct FDreamGUIMaterialHandleInitializer : TChildEntityInitializer<FDreamUIMaterialPath, FDreamUIMaterialHandle>
	{
		explicit FDreamGUIMaterialHandleInitializer(TComponentTypeID<FDreamUIMaterialPath> Path, TComponentTypeID<FDreamUIMaterialHandle> Handle)
			: TChildEntityInitializer<FDreamUIMaterialPath, FDreamUIMaterialHandle>(Path, Handle)
		{}

		virtual void Run(const FEntityRange& ChildRange, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets)
		{
			TComponentReader<FDreamUIMaterialPath>   PathComponents = ParentAllocation->ReadComponents(this->GetParentComponent());
			TComponentWriter<FDreamUIMaterialHandle> HandleComponents = ChildRange.Allocation->WriteComponents(this->GetChildComponent(), FEntityAllocationWriteContext::NewAllocation());
			TOptionalComponentReader<UObject*>      BoundObjectComponents = ChildRange.Allocation->TryReadComponents(FBuiltInComponentTypes::Get()->BoundObject);
			if (!ensure(BoundObjectComponents))
			{
				return;
			}

			for (int32 Index = 0; Index < ChildRange.Num; ++Index)
			{
				const int32 ParentIndex = ParentAllocationOffsets[Index];
				const int32 ChildIndex = ChildRange.ComponentStartOffset + Index;

				if (auto Image = Cast<UDreamImage>(BoundObjectComponents[ChildIndex]))
				{
					if (auto ResourceMat = Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject()))
					{
						if (auto BrushStructProperty = FindFProperty<FStructProperty>(Image->GetClass(), UDreamImage::GetPropertyName_Brush()))
						{
							auto BrushStructValuePtr = BrushStructProperty->ContainerPtrToValuePtr<void>(Image);
							if (auto ResourceObjectProperty = FindFProperty<FProperty>(Image->GetClass(), FDreamUIImageBrush::GetPropertyName_ResourceObject()))
							{
								HandleComponents[ChildIndex] = FDreamUIMaterialHandle(ResourceObjectProperty->ContainerPtrToValuePtr<void>(BrushStructValuePtr));
							}
						}
					}
				}
				else if (auto Text = Cast<UDreamText>(BoundObjectComponents[ChildIndex]))
				{
					if (auto OverrideMatProperty = FindFProperty<FProperty>(Text->GetClass(), UDreamText::GetPropertyName_OverrideMaterial()))
					{
						HandleComponents[ChildIndex] = FDreamUIMaterialHandle(OverrideMatProperty->ContainerPtrToValuePtr<void>(Text));
					}
				}
			}
		}
	};

	ComponentRegistry->Factories.DefineChildComponent(FDreamGUIMaterialHandleInitializer(DreamUIMaterialPath, DreamUIMaterialHandle));
}

FMovieSceneDreamUIComponentTypes::~FMovieSceneDreamUIComponentTypes()
{
}

void FMovieSceneDreamUIComponentTypes::Destroy()
{
	GMovieSceneDreamUIComponentTypes.Reset();
	GMovieSceneDreamUIComponentTypesDestroyed = true;
}

FMovieSceneDreamUIComponentTypes* FMovieSceneDreamUIComponentTypes::Get()
{
	if (!GMovieSceneDreamUIComponentTypes.IsValid())
	{
		check(!GMovieSceneDreamUIComponentTypesDestroyed);
		GMovieSceneDreamUIComponentTypes.Reset(new FMovieSceneDreamUIComponentTypes);
	}
	return GMovieSceneDreamUIComponentTypes.Get();
}


} // namespace MovieScene
} // namespace UE
