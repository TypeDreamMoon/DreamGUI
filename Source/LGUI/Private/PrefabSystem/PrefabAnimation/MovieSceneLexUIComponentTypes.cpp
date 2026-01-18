// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/MovieSceneLexUIComponentTypes.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneEntityFactoryTemplates.h"
#include "EntitySystem/MovieScenePropertyComponentHandler.h"
#include "Core/Components/LexImage.h"
#include "Core/Components/LexText.h"

namespace UE
{
namespace MovieScene
{

static bool GMovieSceneLexUIComponentTypesDestroyed = false;
static TUniquePtr<FMovieSceneLexUIComponentTypes> GMovieSceneLexUIComponentTypes;

FMovieSceneLexUIComponentTypes::FMovieSceneLexUIComponentTypes()
{
	FComponentRegistry* ComponentRegistry = UMovieSceneEntitySystemLinker::GetComponents();

	ComponentRegistry->NewComponentType(&LGUIMaterialPath, TEXT("LexUI Material Path"), EComponentTypeFlags::CopyToChildren | EComponentTypeFlags::CopyToOutput);
	ComponentRegistry->NewComponentType(&LGUIMaterialHandle, TEXT("LexUI Material Handle"), EComponentTypeFlags::CopyToOutput);
	/** Initializer that initializes the value of an FLGUIMaterialHandle derived from an FLGUIMaterialPath */
	struct FLGUIMaterialHandleInitializer : TChildEntityInitializer<FLexUIMaterialPath, FLexUIMaterialHandle>
	{
		explicit FLGUIMaterialHandleInitializer(TComponentTypeID<FLexUIMaterialPath> Path, TComponentTypeID<FLexUIMaterialHandle> Handle)
			: TChildEntityInitializer<FLexUIMaterialPath, FLexUIMaterialHandle>(Path, Handle)
		{}

		virtual void Run(const FEntityRange& ChildRange, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets)
		{
			TComponentReader<FLexUIMaterialPath>   PathComponents = ParentAllocation->ReadComponents(this->GetParentComponent());
			TComponentWriter<FLexUIMaterialHandle> HandleComponents = ChildRange.Allocation->WriteComponents(this->GetChildComponent(), FEntityAllocationWriteContext::NewAllocation());
			TOptionalComponentReader<UObject*>      BoundObjectComponents = ChildRange.Allocation->TryReadComponents(FBuiltInComponentTypes::Get()->BoundObject);
			if (!ensure(BoundObjectComponents))
			{
				return;
			}

			for (int32 Index = 0; Index < ChildRange.Num; ++Index)
			{
				const int32 ParentIndex = ParentAllocationOffsets[Index];
				const int32 ChildIndex = ChildRange.ComponentStartOffset + Index;

				if (auto Image = Cast<ULexImage>(BoundObjectComponents[ChildIndex]))
				{
					if (auto ResourceMat = Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject()))
					{
						if (auto BrushStructProperty = FindFProperty<FStructProperty>(Image->GetClass(), ULexImage::GetPropertyName_Brush()))
						{
							auto BrushStructValuePtr = BrushStructProperty->ContainerPtrToValuePtr<void>(Image);
							if (auto ResourceObjectProperty = FindFProperty<FProperty>(Image->GetClass(), FLexUIImageBrush::GetPropertyName_ResourceObject()))
							{
								HandleComponents[ChildIndex] = FLexUIMaterialHandle(ResourceObjectProperty->ContainerPtrToValuePtr<void>(BrushStructValuePtr));
							}
						}
					}
				}
				else if (auto Text = Cast<ULexText>(BoundObjectComponents[ChildIndex]))
				{
					if (auto OverrideMatProperty = FindFProperty<FProperty>(Text->GetClass(), ULexText::GetPropertyName_OverrideMaterial()))
					{
						HandleComponents[ChildIndex] = FLexUIMaterialHandle(OverrideMatProperty->ContainerPtrToValuePtr<void>(Text));
					}
				}
			}
		}
	};

	ComponentRegistry->Factories.DefineChildComponent(FLGUIMaterialHandleInitializer(LGUIMaterialPath, LGUIMaterialHandle));
}

FMovieSceneLexUIComponentTypes::~FMovieSceneLexUIComponentTypes()
{
}

void FMovieSceneLexUIComponentTypes::Destroy()
{
	GMovieSceneLexUIComponentTypes.Reset();
	GMovieSceneLexUIComponentTypesDestroyed = true;
}

FMovieSceneLexUIComponentTypes* FMovieSceneLexUIComponentTypes::Get()
{
	if (!GMovieSceneLexUIComponentTypes.IsValid())
	{
		check(!GMovieSceneLexUIComponentTypesDestroyed);
		GMovieSceneLexUIComponentTypes.Reset(new FMovieSceneLexUIComponentTypes);
	}
	return GMovieSceneLexUIComponentTypes.Get();
}


} // namespace MovieScene
} // namespace UE
