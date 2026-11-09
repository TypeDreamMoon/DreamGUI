// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamNiagaraEngineAccess.h"

#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"

#define LOCTEXT_NAMESPACE "DreamNiagara"

/*
 * Reference: SourySK/NiagaraUIRenderer and liufei2008/LGUI_ParticleSystem (both MIT) read particle data the same way --
 * through the renderer's attribute bindings and the data set's accessors. This is written from the engine's headers and
 * the sprite vertex factory, not from either.
 */
namespace DreamNiagaraEngineLocal
{
	EDreamNiagaraSpriteAlignment AlignmentOf(const UNiagaraSpriteRendererProperties& InSprite)
	{
		switch (InSprite.Alignment)
		{
		case ENiagaraSpriteAlignment::VelocityAligned:
			return EDreamNiagaraSpriteAlignment::Velocity;
		case ENiagaraSpriteAlignment::CustomAlignment:
			return EDreamNiagaraSpriteAlignment::Custom;
		case ENiagaraSpriteAlignment::Automatic:
			// Niagara's own choice: custom where the alignment attribute is bound.
			return InSprite.SpriteAlignmentBinding.IsValid() && InSprite.SpriteAlignmentBinding.DoesBindingExistOnSource()
				? EDreamNiagaraSpriteAlignment::Custom : EDreamNiagaraSpriteAlignment::Unaligned;
		default:
			return EDreamNiagaraSpriteAlignment::Unaligned;
		}
	}

	EDreamNiagaraSortMode SortOf(ENiagaraSortMode InMode)
	{
		switch (InMode)
		{
		case ENiagaraSortMode::None:
			return EDreamNiagaraSortMode::None;
		case ENiagaraSortMode::CustomDecending:
			return EDreamNiagaraSortMode::Descending;
		default:
			// By the view, which has no depth on a flat UI, the custom key is what is left to sort by: lower drawn first.
			return EDreamNiagaraSortMode::Ascending;
		}
	}

	FDreamNiagaraRibbonUVSettings UVOf(const FNiagaraRibbonUVSettings& InUV)
	{
		FDreamNiagaraRibbonUVSettings UV;
		switch (InUV.DistributionMode)
		{
		case ENiagaraRibbonUVDistributionMode::ScaledUsingRibbonSegmentLength:
			UV.Distribution = EDreamNiagaraRibbonUVDistribution::ScaledByLength;
			break;
		case ENiagaraRibbonUVDistributionMode::TiledOverRibbonLength:
		case ENiagaraRibbonUVDistributionMode::TiledFromStartOverRibbonLength:
			UV.Distribution = EDreamNiagaraRibbonUVDistribution::TiledByLength;
			break;
		default:
			UV.Distribution = EDreamNiagaraRibbonUVDistribution::ScaledUniformly;
			break;
		}
		UV.TilingLength = InUV.TilingLength;
		UV.Offset = FVector2f(InUV.Offset);
		UV.Scale = FVector2f(InUV.Scale);
		UV.bUseUOverride = InUV.bEnablePerParticleUOverride;
		return UV;
	}

	/** The data set's name for a binding, or none where the binding reads nothing the particles carry. */
	FName ParticleNameOf(const FNiagaraVariableAttributeBinding& InBinding)
	{
		if (!InBinding.IsValid() || !InBinding.IsParticleBinding())
		{
			return NAME_None;
		}
		return InBinding.GetDataSetBindableVariable().GetName();
	}

	/** The emitter instance InRenderer reads, simulated on the CPU, with this frame's particles; null otherwise. */
	const FNiagaraEmitterInstance* EmitterOf(UNiagaraComponent* InComponent, const FDreamNiagaraRendererInfo& InRenderer)
	{
		if (InComponent == nullptr)
		{
			return nullptr;
		}
		FNiagaraSystemInstanceControllerPtr Controller = InComponent->GetSystemInstanceController();
		FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
		if (Instance == nullptr)
		{
			return nullptr;
		}
		TConstArrayView<FNiagaraEmitterInstanceRef> Emitters = Instance->GetEmitters();
		if (!Emitters.IsValidIndex(InRenderer.EmitterIndex))
		{
			return nullptr;
		}
		const FNiagaraEmitterInstance& Emitter = Emitters[InRenderer.EmitterIndex].Get();
		if (Emitter.GetSimTarget() != ENiagaraSimTarget::CPUSim || Emitter.IsDisabled())
		{
			return nullptr;
		}
		return &Emitter;
	}

	template<typename TType>
	struct TReader
	{
		TReader(const FNiagaraDataSet& InDataSet, FName InName)
		{
			if (!InName.IsNone())
			{
				FNiagaraDataSetAccessor<TType> Accessor(InDataSet, InName);
				if (Accessor.IsValid())
				{
					Reader.Emplace(Accessor.GetReader(InDataSet));
				}
			}
		}
		TType Get(int32 InIndex, const TType& InDefault) const
		{
			return Reader.IsSet() ? Reader->GetSafe(InIndex, InDefault) : InDefault;
		}
		bool IsSet() const { return Reader.IsSet(); }
		using FReader = decltype(DeclVal<const FNiagaraDataSetAccessor<TType>&>().GetReader(DeclVal<const FNiagaraDataSet&>()));
		TOptional<FReader> Reader;
	};
}

void DreamNiagaraEngine::DescribeSystem(const UNiagaraSystem* InSystem, TArray<FDreamNiagaraRendererInfo>& OutRenderers)
{
	using namespace DreamNiagaraEngineLocal;
	OutRenderers.Reset();
	if (InSystem == nullptr)
	{
		return;
	}
	const TArray<FNiagaraEmitterHandle>& Handles = InSystem->GetEmitterHandles();
	for (int32 EmitterIndex = 0; EmitterIndex < Handles.Num(); ++EmitterIndex)
	{
		const FNiagaraEmitterHandle& Handle = Handles[EmitterIndex];
		if (!Handle.GetIsEnabled())
		{
			continue;
		}
		const bool bStandard = Handle.GetEmitterMode() == ENiagaraEmitterMode::Standard;
		const FVersionedNiagaraEmitterData* Emitter = bStandard ? Handle.GetEmitterData() : nullptr;
		if (Emitter == nullptr)
		{
			FDreamNiagaraRendererInfo& Info = OutRenderers.AddDefaulted_GetRef();
			Info.EmitterIndex = EmitterIndex;
			Info.EmitterName = Handle.GetName();
			Info.RendererType = TEXT("Emitter");
			Info.Notes.Add(bStandard
				? LOCTEXT("NoEmitterData", "The emitter has no data to read.")
				: LOCTEXT("Stateless", "A lightweight (stateless) emitter: its particles are not simulated where DreamGUI can read them. Make it a standard emitter on the CPU."));
			continue;
		}
		const bool bCPU = Emitter->SimTarget == ENiagaraSimTarget::CPUSim;
		const TArray<UNiagaraRendererProperties*>& Renderers = Emitter->GetRenderers();
		for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
		{
			const UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];
			if (Renderer == nullptr || !Renderer->GetIsEnabled())
			{
				continue;
			}
			FDreamNiagaraRendererInfo& Info = OutRenderers.AddDefaulted_GetRef();
			Info.EmitterIndex = EmitterIndex;
			Info.EmitterName = Handle.GetName();
			Info.RendererIndex = RendererIndex;
			Info.RendererType = Renderer->GetClass()->GetName();
			Info.bLocalSpace = Emitter->bLocalSpace;
			Info.SortOrderHint = Renderer->SortOrderHint;
			Info.Properties = Renderer;
			if (!bCPU)
			{
				Info.Notes.Add(LOCTEXT("GPU", "The emitter is simulated on the GPU, whose particles DreamGUI does not read yet. Set its Sim Target to CPU."));
				continue;
			}
			if (const UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{
				if (Sprite->SourceMode != ENiagaraRendererSourceDataMode::Particles)
				{
					Info.Notes.Add(LOCTEXT("EmitterSource", "A sprite renderer drawing one sprite for the emitter: only particle sprites are drawn."));
					continue;
				}
				Info.Kind = EDreamNiagaraRendererKind::Sprite;
				Info.Material = Sprite->Material;
				Info.Sprite.SubImageSize = FIntPoint(FMath::Max(1, FMath::RoundToInt(Sprite->SubImageSize.X)), FMath::Max(1, FMath::RoundToInt(Sprite->SubImageSize.Y)));
				Info.Sprite.bSubImageBlend = Sprite->bSubImageBlend;
				Info.Sprite.Alignment = AlignmentOf(*Sprite);
				Info.Sprite.SortMode = SortOf(Sprite->SortMode);
				if (Sprite->SortMode == ENiagaraSortMode::ViewDepth || Sprite->SortMode == ENiagaraSortMode::ViewDistance)
				{
					Info.Notes.Add(LOCTEXT("ViewSort", "Sorted by the view, which a flat UI has no depth for: sorted by the custom sorting attribute instead, lower drawn first."));
				}
				if (Sprite->FacingMode == ENiagaraSpriteFacingMode::CustomFacingVector)
				{
					Info.Notes.Add(LOCTEXT("Facing", "A custom facing is not read: every sprite faces the reader."));
				}
			}
			else if (const UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer))
			{
				Info.Kind = EDreamNiagaraRendererKind::Ribbon;
				Info.Material = Ribbon->Material;
				Info.Ribbon.UV0 = UVOf(Ribbon->UV0Settings);
				Info.Ribbon.UV1 = UVOf(Ribbon->UV1Settings);
				Info.Ribbon.CurveTension = Ribbon->CurveTension;
				Info.Ribbon.MaxNumRibbons = Ribbon->MaxNumRibbons;
				Info.Ribbon.bDrawLastFirst = Ribbon->DrawDirection == ENiagaraRibbonDrawDirection::BackToFront;
				switch (Ribbon->TessellationMode)
				{
				case ENiagaraRibbonTessellationMode::Disabled:
					Info.Ribbon.MaxTessellation = 1;
					break;
				case ENiagaraRibbonTessellationMode::Custom:
					Info.Ribbon.MaxTessellation = FMath::Clamp(Ribbon->TessellationFactor, 1, 16);
					Info.Ribbon.bConstantTessellation = Ribbon->bUseConstantFactor;
					Info.Ribbon.TessellationAngle = FMath::Max(Ribbon->TessellationAngle, 1.0f);
					break;
				default:
					// Niagara's automatic tessellation follows the bend on screen: here, a piece for every 15 degrees, at most 16.
					Info.Ribbon.MaxTessellation = 16;
					Info.Ribbon.TessellationAngle = 15.0f;
					break;
				}
				if (Ribbon->Shape != ENiagaraRibbonShapeMode::Plane)
				{
					Info.Notes.Add(LOCTEXT("RibbonShape", "A ribbon of another shape than a plane is drawn as a plane facing the reader."));
				}
				if (Ribbon->UV0Settings.LeadingEdgeMode == ENiagaraRibbonUVEdgeMode::SmoothTransition || Ribbon->UV0Settings.TrailingEdgeMode == ENiagaraRibbonUVEdgeMode::SmoothTransition)
				{
					Info.Notes.Add(LOCTEXT("RibbonEdges", "The UV edges' smooth transition is drawn as locked edges."));
				}
				if (Ribbon->UV0Settings.DistributionMode == ENiagaraRibbonUVDistributionMode::TiledFromStartOverRibbonLength)
				{
					Info.Notes.Add(LOCTEXT("RibbonUVDistance", "Tiled by the particles' UV distance is drawn tiled over the ribbon's length."));
				}
			}
			else
			{
				Info.Notes.Add(FText::Format(LOCTEXT("Renderer", "{0} is not drawn: DreamGUI draws sprite and ribbon renderers."), FText::FromString(Info.RendererType)));
				continue;
			}
			UTexture* Texture = nullptr;
			InferShading(Info.Material.Get(), Texture, Info.InferredBlendMode);
			Info.InferredTexture = Texture;
		}
	}
}

void DreamNiagaraEngine::InferShading(const UMaterialInterface* InMaterial, UTexture*& OutTexture, EDreamUIBlendMode& OutBlendMode)
{
	OutTexture = nullptr;
	OutBlendMode = EDreamUIBlendMode::Alpha;
	if (InMaterial == nullptr)
	{
		return;
	}
	switch (InMaterial->GetBlendMode())
	{
	case BLEND_Additive:
		OutBlendMode = EDreamUIBlendMode::Additive;
		break;
	case BLEND_Modulate:
		OutBlendMode = EDreamUIBlendMode::Multiply;
		break;
	default:
		break;
	}
	UTexture* Named = nullptr;
	if (InMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Texture")), Named) && Named != nullptr)
	{
		OutTexture = Named;
		return;
	}
	// Then the first texture parameter that has a texture.
	TArray<FMaterialParameterInfo> Parameters;
	TArray<FGuid> Ids;
	InMaterial->GetAllTextureParameterInfo(Parameters, Ids);
	for (const FMaterialParameterInfo& Parameter : Parameters)
	{
		UTexture* Value = nullptr;
		if (InMaterial->GetTextureParameterValue(Parameter, Value) && Value != nullptr)
		{
			OutTexture = Value;
			return;
		}
	}
#if WITH_EDITORONLY_DATA
	// Then what the graph samples. A graph that samples none draws procedurally, as the engine's default particle materials
	// do: no texture, rather than whatever the compiled material happens to reference.
	// Only read: the base material's inputs are asked for through a function that does not promise not to write.
	if (UMaterial* Base = const_cast<UMaterial*>(InMaterial->GetMaterial()))
	{
		if (Base->GetExpressions().Num() > 0)
		{
			// Walked back from the emissive and the opacity, so that a sample nothing draws with -- in a branch a switch
			// turns off, left over on the graph -- is not taken.
			TArray<UMaterialExpression*> Pending;
			TSet<UMaterialExpression*> Seen;
			for (EMaterialProperty Property : { MP_EmissiveColor, MP_BaseColor, MP_Opacity, MP_OpacityMask })
			{
				if (const FExpressionInput* Input = Base->GetExpressionInputForProperty(Property))
				{
					if (Input->Expression != nullptr)
					{
						Pending.Add(Input->Expression);
					}
				}
			}
			while (Pending.Num() > 0)
			{
				UMaterialExpression* Expression = Pending[0];
				Pending.RemoveAt(0, EAllowShrinking::No);
				if (Expression == nullptr || Seen.Contains(Expression))
				{
					continue;
				}
				Seen.Add(Expression);
				if (const UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(Expression))
				{
					if (Sample->Texture != nullptr)
					{
						OutTexture = Sample->Texture;
						return;
					}
				}
				for (FExpressionInputIterator It(Expression); It; ++It)
				{
					if (It.Input->Expression != nullptr)
					{
						Pending.Add(It.Input->Expression);
					}
				}
			}
			return;
		}
	}
#endif
	// Cooked, the graph is gone: the textures the compiled material uses.
	TArray<UTexture*> Used;
	InMaterial->GetUsedTextures(Used);
	for (UTexture* Texture : Used)
	{
		if (Texture != nullptr)
		{
			OutTexture = Texture;
			return;
		}
	}
}

bool DreamNiagaraEngine::IsReadyToRun(const UNiagaraSystem* InSystem)
{
	return InSystem != nullptr && InSystem->IsValid() && InSystem->IsReadyToRun();
}

void DreamNiagaraEngine::FinishTick(UNiagaraComponent* InComponent)
{
	if (InComponent == nullptr)
	{
		return;
	}
	FNiagaraSystemInstanceControllerPtr Controller = InComponent->GetSystemInstanceController();
	if (Controller.IsValid())
	{
		Controller->WaitForConcurrentTickAndFinalize();
	}
}

int32 DreamNiagaraEngine::ReadSprites(UNiagaraComponent* InComponent, const FDreamNiagaraRendererInfo& InRenderer, TArray<FDreamNiagaraSpriteParticle>& OutParticles)
{
	using namespace DreamNiagaraEngineLocal;
	const UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(InRenderer.Properties.Get());
	const FNiagaraEmitterInstance* Emitter = Sprite != nullptr ? EmitterOf(InComponent, InRenderer) : nullptr;
	if (Emitter == nullptr)
	{
		return 0;
	}
	const FNiagaraDataSet& DataSet = Emitter->GetParticleData();
	const FNiagaraDataBuffer* Data = DataSet.GetCurrentData();
	const int32 Num = Data != nullptr ? static_cast<int32>(Data->GetNumInstances()) : 0;
	if (Num <= 0)
	{
		return 0;
	}
	const TReader<FNiagaraPosition> Position(DataSet, ParticleNameOf(Sprite->PositionBinding));
	const TReader<FLinearColor> Color(DataSet, ParticleNameOf(Sprite->ColorBinding));
	const TReader<FVector2f> Size(DataSet, ParticleNameOf(Sprite->SpriteSizeBinding));
	const TReader<float> Rotation(DataSet, ParticleNameOf(Sprite->SpriteRotationBinding));
	const TReader<float> SubImage(DataSet, ParticleNameOf(Sprite->SubImageIndexBinding));
	const TReader<FVector3f> Velocity(DataSet, ParticleNameOf(Sprite->VelocityBinding));
	const TReader<FVector3f> Alignment(DataSet, ParticleNameOf(Sprite->SpriteAlignmentBinding));
	const TReader<FVector2f> Pivot(DataSet, ParticleNameOf(Sprite->PivotOffsetBinding));
	const TReader<FVector2f> UVScale(DataSet, ParticleNameOf(Sprite->UVScaleBinding));
	const TReader<FVector4f> Dynamic(DataSet, ParticleNameOf(Sprite->DynamicMaterialBinding));
	const TReader<float> Sort(DataSet, ParticleNameOf(Sprite->CustomSortingBinding));
	const FVector2f DefaultPivot(Sprite->PivotInUVSpace);
	const FDreamNiagaraSpriteParticle Defaults;

	const int32 First = OutParticles.Num();
	OutParticles.SetNum(First + Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		FDreamNiagaraSpriteParticle& Particle = OutParticles[First + Index];
		Particle.Position = Position.Get(Index, FNiagaraPosition(Defaults.Position));
		Particle.Color = Color.Get(Index, Defaults.Color);
		Particle.Size = Size.Get(Index, Defaults.Size);
		Particle.Rotation = Rotation.Get(Index, Defaults.Rotation);
		Particle.SubImageIndex = SubImage.Get(Index, Defaults.SubImageIndex);
		Particle.Velocity = Velocity.Get(Index, Defaults.Velocity);
		Particle.Alignment = Alignment.Get(Index, Defaults.Alignment);
		Particle.PivotOffset = Pivot.Get(Index, DefaultPivot);
		Particle.UVScale = UVScale.Get(Index, Defaults.UVScale);
		Particle.DynamicParameter = Dynamic.Get(Index, Defaults.DynamicParameter);
		Particle.SortKey = Sort.Get(Index, 0.0f);
	}
	return Num;
}

int32 DreamNiagaraEngine::ReadRibbons(UNiagaraComponent* InComponent, const FDreamNiagaraRendererInfo& InRenderer, int64 InKeySalt, TArray<FDreamNiagaraRibbonParticle>& OutParticles)
{
	using namespace DreamNiagaraEngineLocal;
	const UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(InRenderer.Properties.Get());
	const FNiagaraEmitterInstance* Emitter = Ribbon != nullptr ? EmitterOf(InComponent, InRenderer) : nullptr;
	if (Emitter == nullptr)
	{
		return 0;
	}
	const FNiagaraDataSet& DataSet = Emitter->GetParticleData();
	const FNiagaraDataBuffer* Data = DataSet.GetCurrentData();
	const int32 Num = Data != nullptr ? static_cast<int32>(Data->GetNumInstances()) : 0;
	if (Num <= 0)
	{
		return 0;
	}
	const TReader<FNiagaraPosition> Position(DataSet, ParticleNameOf(Ribbon->PositionBinding));
	const TReader<FLinearColor> Color(DataSet, ParticleNameOf(Ribbon->ColorBinding));
	const TReader<float> Width(DataSet, ParticleNameOf(Ribbon->RibbonWidthBinding));
	const TReader<float> LinkOrder(DataSet, ParticleNameOf(Ribbon->RibbonLinkOrderBinding));
	const TReader<float> Age(DataSet, ParticleNameOf(Ribbon->NormalizedAgeBinding));
	const TReader<FVector4f> Dynamic(DataSet, ParticleNameOf(Ribbon->DynamicMaterialBinding));
	const TReader<float> U0(DataSet, ParticleNameOf(Ribbon->U0OverrideBinding));
	const TReader<float> U1(DataSet, ParticleNameOf(Ribbon->U1OverrideBinding));
	// The ribbon's identity is an id or a plain integer, as the emitter declares it; with neither, one ribbon.
	const FName IdName = ParticleNameOf(Ribbon->RibbonIdBinding);
	const TReader<int32> IntId(DataSet, IdName);
	TOptional<FNiagaraDataSetReaderStruct<FNiagaraID>> StructId;
	if (!IdName.IsNone() && !IntId.IsSet())
	{
		FNiagaraDataSetAccessor<FNiagaraID> Accessor(DataSet, IdName);
		if (Accessor.IsValid())
		{
			StructId.Emplace(Accessor.GetReader(DataSet));
		}
	}
	const FDreamNiagaraRibbonParticle Defaults;

	const int32 First = OutParticles.Num();
	OutParticles.SetNum(First + Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		FDreamNiagaraRibbonParticle& Particle = OutParticles[First + Index];
		Particle.Position = Position.Get(Index, FNiagaraPosition(Defaults.Position));
		Particle.Color = Color.Get(Index, Defaults.Color);
		Particle.Width = Width.Get(Index, Defaults.Width);
		Particle.NormalizedAge = Age.Get(Index, 0.0f);
		// Without a link order Niagara's ribbons run by age, the youngest point first.
		Particle.LinkOrder = LinkOrder.IsSet() ? LinkOrder.Get(Index, 0.0f) : Particle.NormalizedAge;
		Particle.DynamicParameter = Dynamic.Get(Index, Defaults.DynamicParameter);
		Particle.U0Override = U0.Get(Index, 0.0f);
		Particle.U1Override = U1.Get(Index, 0.0f);
		int64 Key = 0;
		if (IntId.IsSet())
		{
			Key = IntId.Get(Index, 0);
		}
		else if (StructId.IsSet())
		{
			const FNiagaraID Id = StructId->GetSafe(Index, FNiagaraID());
			Key = (static_cast<int64>(Id.AcquireTag) << 32) | static_cast<uint32>(Id.Index);
		}
		Particle.RibbonKey = Key ^ (InKeySalt << 48);
	}
	return Num;
}

EDreamNiagaraParameterType DreamNiagaraEngine::FindUserParameter(const UNiagaraSystem* InSystem, FName InName)
{
	if (InSystem == nullptr)
	{
		return EDreamNiagaraParameterType::None;
	}
	const FString Wanted = FString(TEXT("User.")) + InName.ToString();
	for (const FNiagaraVariableWithOffset& Variable : InSystem->GetExposedParameters().ReadParameterVariables())
	{
		if (!Variable.GetName().ToString().Equals(Wanted, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FNiagaraTypeDefinition& Type = Variable.GetType();
		if (Type == FNiagaraTypeDefinition::GetFloatDef()) return EDreamNiagaraParameterType::Float;
		if (Type == FNiagaraTypeDefinition::GetIntDef()) return EDreamNiagaraParameterType::Int;
		if (Type == FNiagaraTypeDefinition::GetBoolDef()) return EDreamNiagaraParameterType::Bool;
		if (Type == FNiagaraTypeDefinition::GetVec2Def()) return EDreamNiagaraParameterType::Vector2;
		if (Type == FNiagaraTypeDefinition::GetVec3Def()) return EDreamNiagaraParameterType::Vector3;
		if (Type == FNiagaraTypeDefinition::GetPositionDef()) return EDreamNiagaraParameterType::Position;
		if (Type == FNiagaraTypeDefinition::GetVec4Def()) return EDreamNiagaraParameterType::Vector4;
		if (Type == FNiagaraTypeDefinition::GetColorDef()) return EDreamNiagaraParameterType::Color;
		return EDreamNiagaraParameterType::None;
	}
	return EDreamNiagaraParameterType::None;
}

int32 DreamNiagaraEngine::CountParticles(UNiagaraComponent* InComponent)
{
	if (InComponent == nullptr)
	{
		return 0;
	}
	FNiagaraSystemInstanceControllerPtr Controller = InComponent->GetSystemInstanceController();
	FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
	if (Instance == nullptr)
	{
		return 0;
	}
	int32 Count = 0;
	for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
	{
		Count += Emitter->GetNumParticles();
	}
	return Count;
}

#undef LOCTEXT_NAMESPACE
