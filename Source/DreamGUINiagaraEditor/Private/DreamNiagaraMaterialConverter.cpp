// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamNiagaraMaterialConverter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DreamNiagaraSystem.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionParticleSubUV.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

/*
 * Reference: the material mapping tables of SourySK/NiagaraUIRenderer and liufei2008/LGUI_ParticleSystem (both MIT) are what
 * this conversion replaces; neither converts a material. Written from the engine's material expressions.
 */
namespace DreamNiagaraMaterialConverterLocal
{
	const TCHAR* const WidgetPropertyFunction = TEXT("/DreamGUI/Materials/MF_DreamWidgetProperty.MF_DreamWidgetProperty");
	const TCHAR* const ClipFunction = TEXT("/DreamGUI/Materials/MF_DreamUI_Clip.MF_DreamUI_Clip");
	const TCHAR* const ConvertVertexColorFunction = TEXT("/DreamGUI/Materials/MF_DreamUI_ConvertVertexColor.MF_DreamUI_ConvertVertexColor");

	/** The material inputs a particle material may feed. */
	const EMaterialProperty Properties[] = { MP_EmissiveColor, MP_Opacity, MP_OpacityMask, MP_BaseColor, MP_Normal, MP_WorldPositionOffset };

	template<typename T>
	T* Make(UMaterial* InMaterial, UMaterialExpression* InNear, int32 InOffsetX, int32 InOffsetY = 0)
	{
		const int32 X = InNear != nullptr ? InNear->MaterialExpressionEditorX + InOffsetX : InOffsetX;
		const int32 Y = InNear != nullptr ? InNear->MaterialExpressionEditorY + InOffsetY : InOffsetY;
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(InMaterial, T::StaticClass(), X, Y));
	}

	UMaterialExpressionMaterialFunctionCall* MakeCall(UMaterial* InMaterial, const TCHAR* InFunction, UMaterialExpression* InNear, int32 InOffsetX, int32 InOffsetY = 0)
	{
		UMaterialFunctionInterface* Function = LoadObject<UMaterialFunctionInterface>(nullptr, InFunction);
		UMaterialExpressionMaterialFunctionCall* Call = Make<UMaterialExpressionMaterialFunctionCall>(InMaterial, InNear, InOffsetX, InOffsetY);
		if (Call != nullptr && Function != nullptr)
		{
			Call->SetMaterialFunction(Function);
		}
		return Call;
	}

	UMaterialExpressionComponentMask* Mask(UMaterial* InMaterial, UMaterialExpression* InFrom, int32 InOutput, bool bR, bool bG, bool bB, bool bA)
	{
		UMaterialExpressionComponentMask* Masked = Make<UMaterialExpressionComponentMask>(InMaterial, InFrom, 200);
		Masked->R = bR;
		Masked->G = bG;
		Masked->B = bB;
		Masked->A = bA;
		Masked->Input.Connect(InOutput, InFrom);
		return Masked;
	}

	UMaterialExpressionTextureCoordinate* TexCoord(UMaterial* InMaterial, UMaterialExpression* InNear, int32 InIndex, int32 InOffsetY)
	{
		UMaterialExpressionTextureCoordinate* Coordinate = Make<UMaterialExpressionTextureCoordinate>(InMaterial, InNear, -400, InOffsetY);
		Coordinate->CoordinateIndex = InIndex;
		return Coordinate;
	}

	/** Every input of the material and its expressions that reads InOld, pointed at what InMap gives for the output it read. */
	void Rewire(UMaterial* InMaterial, UMaterialExpression* InOld, TFunctionRef<TPair<UMaterialExpression*, int32>(int32)> InMap)
	{
		auto Repoint = [&](FExpressionInput* InInput)
		{
			if (InInput != nullptr && InInput->Expression == InOld)
			{
				const TPair<UMaterialExpression*, int32> To = InMap(InInput->OutputIndex);
				InInput->Connect(To.Value, To.Key);
			}
		};
		for (UMaterialExpression* Expression : InMaterial->GetExpressions())
		{
			if (Expression == nullptr || Expression == InOld)
			{
				continue;
			}
			for (FExpressionInputIterator It(Expression); It; ++It)
			{
				Repoint(It.Input);
			}
		}
		for (EMaterialProperty Property : Properties)
		{
			Repoint(InMaterial->GetExpressionInputForProperty(Property));
		}
	}

	/** Particle Color: the vertex colour, taken to linear light, times its scale (UV3.x). */
	void ReplaceParticleColor(UMaterial* InMaterial, UMaterialExpression* InOld)
	{
		UMaterialExpressionVertexColor* Vertex = Make<UMaterialExpressionVertexColor>(InMaterial, InOld, -600);
		UMaterialExpressionMaterialFunctionCall* Linear = MakeCall(InMaterial, ConvertVertexColorFunction, InOld, -300);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Vertex, TEXT(""), Linear, TEXT("Color"));
		UMaterialExpressionTextureCoordinate* Packed = TexCoord(InMaterial, InOld, 3, 160);
		UMaterialExpressionCustom* Scale = Make<UMaterialExpressionCustom>(InMaterial, InOld, -300, 160);
		Scale->Description = TEXT("DreamUI particle colour scale");
		// The step read half the gap the blend leaves below the next (DreamUIParticle_Unpack): UV3.x arrives interpolated.
		Scale->Code = TEXT("return max(floor(Packed + 0.001953125), 16.0) / 16.0;");
		Scale->OutputType = CMOT_Float1;
		Scale->Inputs.Reset();
		FCustomInput& Input = Scale->Inputs.AddDefaulted_GetRef();
		Input.InputName = TEXT("Packed");
		Input.Input.Connect(0, Mask(InMaterial, Packed, 0, true, false, false, false));
		UMaterialExpressionMultiply* RGB = Make<UMaterialExpressionMultiply>(InMaterial, InOld, -100);
		RGB->A.Connect(0, Linear);
		RGB->B.Connect(0, Scale);
		UMaterialExpressionAppendVector* RGBA = Make<UMaterialExpressionAppendVector>(InMaterial, InOld, 100);
		RGBA->A.Connect(0, RGB);
		RGBA->B.Connect(4, Vertex);
		// RGB, R, G, B, A, RGBA.
		Rewire(InMaterial, InOld, [&](int32 InOutput) -> TPair<UMaterialExpression*, int32>
		{
			switch (InOutput)
			{
			case 1: return { Mask(InMaterial, RGB, 0, true, false, false, false), 0 };
			case 2: return { Mask(InMaterial, RGB, 0, false, true, false, false), 0 };
			case 3: return { Mask(InMaterial, RGB, 0, false, false, true, false), 0 };
			case 4: return { Vertex, 4 };
			case 5: return { RGBA, 0 };
			default: return { RGB, 0 };
			}
		});
	}

	/** Dynamic Parameter: x from UV1.y, y from UV3.y, z and w from UV4. */
	void ReplaceDynamicParameter(UMaterial* InMaterial, UMaterialExpression* InOld)
	{
		UMaterialExpression* X = Mask(InMaterial, TexCoord(InMaterial, InOld, 1, 0), 0, false, true, false, false);
		UMaterialExpression* Y = Mask(InMaterial, TexCoord(InMaterial, InOld, 3, 120), 0, false, true, false, false);
		UMaterialExpressionTextureCoordinate* ZW = TexCoord(InMaterial, InOld, 4, 240);
		UMaterialExpression* Z = Mask(InMaterial, ZW, 0, true, false, false, false);
		UMaterialExpression* W = Mask(InMaterial, ZW, 0, false, true, false, false);
		UMaterialExpressionAppendVector* XY = Make<UMaterialExpressionAppendVector>(InMaterial, InOld, 100);
		XY->A.Connect(0, X);
		XY->B.Connect(0, Y);
		UMaterialExpressionAppendVector* XYZ = Make<UMaterialExpressionAppendVector>(InMaterial, InOld, 200);
		XYZ->A.Connect(0, XY);
		XYZ->B.Connect(0, Z);
		UMaterialExpressionAppendVector* XYZW = Make<UMaterialExpressionAppendVector>(InMaterial, InOld, 300);
		XYZW->A.Connect(0, XYZ);
		XYZW->B.Connect(0, W);
		// Param 1 to 4, RGB, RGBA.
		Rewire(InMaterial, InOld, [&](int32 InOutput) -> TPair<UMaterialExpression*, int32>
		{
			switch (InOutput)
			{
			case 0: return { X, 0 };
			case 1: return { Y, 0 };
			case 2: return { Z, 0 };
			case 3: return { W, 0 };
			case 4: return { XYZ, 0 };
			default: return { XYZW, 0 };
			}
		});
	}

	/** Particle SubUV: the two frames (UV0, UV2) sampled and blended by the fraction of UV3.x. */
	void ReplaceSubUV(UMaterial* InMaterial, UMaterialExpressionParticleSubUV* InOld)
	{
		UMaterialExpression* TextureSource = InOld->TextureObject.Expression;
		int32 TextureOutput = InOld->TextureObject.OutputIndex;
		if (TextureSource == nullptr)
		{
			UMaterialExpressionTextureObject* Object = Make<UMaterialExpressionTextureObject>(InMaterial, InOld, -600, -120);
			Object->Texture = InOld->Texture;
			Object->SamplerType = InOld->SamplerType;
			TextureSource = Object;
			TextureOutput = 0;
		}
		UMaterialExpressionCustom* Blended = Make<UMaterialExpressionCustom>(InMaterial, InOld, -200);
		Blended->Description = TEXT("DreamUI particle frames");
		Blended->Code = TEXT("float Blend = saturate((Packed - floor(Packed + 0.001953125)) / 0.99609375);\nreturn lerp(Tex.Sample(TexSampler, Frame), Tex.Sample(TexSampler, Next), Blend);");
		Blended->OutputType = CMOT_Float4;
		Blended->Inputs.Reset();
		auto AddInput = [Blended](const TCHAR* InName, UMaterialExpression* InFrom, int32 InOutput)
		{
			FCustomInput& Input = Blended->Inputs.AddDefaulted_GetRef();
			Input.InputName = InName;
			Input.Input.Connect(InOutput, InFrom);
		};
		AddInput(TEXT("Tex"), TextureSource, TextureOutput);
		AddInput(TEXT("Frame"), TexCoord(InMaterial, InOld, 0, 120), 0);
		AddInput(TEXT("Next"), TexCoord(InMaterial, InOld, 2, 240), 0);
		AddInput(TEXT("Packed"), Mask(InMaterial, TexCoord(InMaterial, InOld, 3, 360), 0, true, false, false, false), 0);
		// RGB, R, G, B, A, RGBA.
		Rewire(InMaterial, InOld, [&](int32 InOutput) -> TPair<UMaterialExpression*, int32>
		{
			switch (InOutput)
			{
			case 0: return { Mask(InMaterial, Blended, 0, true, true, true, false), 0 };
			case 1: return { Mask(InMaterial, Blended, 0, true, false, false, false), 0 };
			case 2: return { Mask(InMaterial, Blended, 0, false, true, false, false), 0 };
			case 3: return { Mask(InMaterial, Blended, 0, false, false, true, false), 0 };
			case 4: return { Mask(InMaterial, Blended, 0, false, false, false, true), 0 };
			default: return { Blended, 0 };
			}
		});
	}

	/** Why a node cannot be made the UI's, or empty when it can be left as it is. */
	FString WhyUnconvertible(const UMaterialExpression* InExpression)
	{
		const FString Class = InExpression->GetClass()->GetName();
		if (Class.StartsWith(TEXT("MaterialExpressionParticle")) || Class.StartsWith(TEXT("MaterialExpressionSphericalParticle")))
		{
			return TEXT("reads a particle attribute a UI vertex does not carry; it draws with its default");
		}
		static const TCHAR* const SceneReads[] = { TEXT("MaterialExpressionDepthFade"), TEXT("MaterialExpressionSceneDepth"), TEXT("MaterialExpressionSceneTexture"),
			TEXT("MaterialExpressionPixelDepth"), TEXT("MaterialExpressionSceneColor"), TEXT("MaterialExpressionDistanceToNearestSurface") };
		for (const TCHAR* Read : SceneReads)
		{
			if (Class == Read)
			{
				return TEXT("reads the scene, which a UI draw has none of");
			}
		}
		if (const UMaterialExpressionTextureCoordinate* Coordinate = Cast<UMaterialExpressionTextureCoordinate>(InExpression))
		{
			if (Coordinate->CoordinateIndex > 1)
			{
				return FString::Printf(TEXT("reads texture coordinate %d, which DreamGUI's particle vertex keeps for itself"), Coordinate->CoordinateIndex);
			}
		}
		return FString();
	}

	FString Describe(const UMaterialExpression* InExpression)
	{
		return InExpression->GetClass()->GetName().RightChop(18);
	}

	UMaterial* ConvertMaterial(UMaterial* InSource, UPackage* InPackage, const FString& InName, FDreamNiagaraMaterialConversion& OutConversion)
	{
		UMaterial* Material = DuplicateObject<UMaterial>(InSource, InPackage, FName(*InName));
		Material->SetFlags(RF_Public | RF_Standalone);
		Material->ClearFlags(RF_Transient);
		Material->PreEditChange(nullptr);
		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		if (Material->BlendMode == BLEND_Opaque || Material->BlendMode == BLEND_Masked)
		{
			Material->BlendMode = BLEND_Translucent;
		}

		// Collected first: replacing adds expressions to the list being walked.
		TArray<UMaterialExpression*> Expressions;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (Expression != nullptr)
			{
				Expressions.Add(Expression);
			}
		}
		for (UMaterialExpression* Expression : Expressions)
		{
			if (UMaterialExpressionParticleSubUV* SubUV = Cast<UMaterialExpressionParticleSubUV>(Expression))
			{
				ReplaceSubUV(Material, SubUV);
				OutConversion.Replaced.Add(TEXT("ParticleSubUV -> the two frames DreamGUI writes (UV0, UV2), blended by UV3.x"));
			}
			else if (Expression->IsA<UMaterialExpressionParticleColor>())
			{
				ReplaceParticleColor(Material, Expression);
				OutConversion.Replaced.Add(TEXT("ParticleColor -> vertex colour in linear light, times its scale (UV3.x)"));
			}
			else if (Expression->IsA<UMaterialExpressionDynamicParameter>())
			{
				ReplaceDynamicParameter(Material, Expression);
				OutConversion.Replaced.Add(TEXT("DynamicParameter -> UV1.y, UV3.y, UV4"));
			}
			else if (UMaterialExpressionTextureCoordinate* Coordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression))
			{
				if (Coordinate->CoordinateIndex == 1)
				{
					// A particle's second coordinate (a sprite's next frame, a ribbon's second UV set) is DreamGUI's UV2: UV1 is the widget's.
					Coordinate->CoordinateIndex = 2;
					OutConversion.Replaced.Add(TEXT("TextureCoordinate 1 -> 2"));
					continue;
				}
			}
			else
			{
				const FString Why = WhyUnconvertible(Expression);
				if (!Why.IsEmpty())
				{
					OutConversion.Unconverted.Add(FString::Printf(TEXT("%s: %s"), *Describe(Expression), *Why));
				}
				continue;
			}
			if (!Expression->IsA<UMaterialExpressionTextureCoordinate>())
			{
				UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
			}
		}

		// DreamGUI's clip on what is drawn: the widget record's clip row into the clip function, times the old opacity.
		const bool bMaskedOutput = false;
		FExpressionInput* OpacityInput = Material->GetExpressionInputForProperty(bMaskedOutput ? MP_OpacityMask : MP_Opacity);
		UMaterialExpressionMaterialFunctionCall* Record = MakeCall(Material, WidgetPropertyFunction, nullptr, -800, 600);
		UMaterialExpressionMaterialFunctionCall* Clip = MakeCall(Material, ClipFunction, nullptr, -400, 600);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Record, TEXT("ClipDataCoordinate"), Clip, TEXT("CoordinateInDataTex"));
		if (OpacityInput != nullptr && OpacityInput->Expression != nullptr)
		{
			UMaterialExpression* Old = OpacityInput->Expression;
			const int32 OldOutput = OpacityInput->OutputIndex;
			for (FFunctionExpressionInput& Input : Clip->FunctionInputs)
			{
				if (Input.ExpressionInput != nullptr && Input.ExpressionInput->InputName == TEXT("Opacity"))
				{
					Input.Input.Connect(OldOutput, Old);
				}
			}
		}
		if (OpacityInput != nullptr)
		{
			OpacityInput->Connect(0, Clip);
		}
		// An additive material often leaves its opacity alone and draws its emissive: the clip goes into that too.
		if (Material->BlendMode == BLEND_Additive)
		{
			FExpressionInput* Emissive = Material->GetExpressionInputForProperty(MP_EmissiveColor);
			if (Emissive != nullptr && Emissive->Expression != nullptr)
			{
				UMaterialExpressionMultiply* Clipped = Make<UMaterialExpressionMultiply>(Material, nullptr, -150, 400);
				Clipped->A.Connect(Emissive->OutputIndex, Emissive->Expression);
				Clipped->B.Connect(0, Clip);
				Emissive->Connect(0, Clipped);
			}
		}
		Material->PostEditChange();
		Material->MarkPackageDirty();
		return Material;
	}

	/** The package named InPackageName, emptied of an earlier conversion's asset of InName. */
	UPackage* PreparePackage(const FString& InPackageName, const FString& InName)
	{
		UPackage* Package = CreatePackage(*InPackageName);
		Package->FullyLoad();
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *InName))
		{
			// Renamed out of the way and left for the collector: whatever still points at it is pointed at the new one by reload.
			Existing->Rename(*MakeUniqueObjectName(GetTransientPackage(), Existing->GetClass(), *(InName + TEXT("_Old"))).ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
		}
		return Package;
	}
}

FString FDreamNiagaraMaterialConverter::DefaultPackageName(const UMaterialInterface* InParticleMaterial)
{
	if (InParticleMaterial == nullptr)
	{
		return FString();
	}
	const FString Source = InParticleMaterial->GetPackage()->GetName();
	FString Folder = FPackageName::GetLongPackagePath(Source);
	// An engine or plugin material converted for a project goes into the project's content, never beside the original.
	if (!Folder.StartsWith(TEXT("/Game")))
	{
		Folder = TEXT("/Game/DreamUI/ConvertedParticleMaterials");
	}
	return Folder / (FPackageName::GetShortName(Source) + TEXT("_DreamUI"));
}

FDreamNiagaraMaterialConversion FDreamNiagaraMaterialConverter::Convert(UMaterialInterface* InParticleMaterial, const FString& InPackageName)
{
	using namespace DreamNiagaraMaterialConverterLocal;
	FDreamNiagaraMaterialConversion Conversion;
	Conversion.Source = InParticleMaterial;
	if (InParticleMaterial == nullptr || InPackageName.IsEmpty())
	{
		Conversion.Error = TEXT("No material, or nowhere to put the conversion.");
		return Conversion;
	}
	const FString Name = FPackageName::GetShortName(InPackageName);
	if (UMaterial* Source = Cast<UMaterial>(InParticleMaterial))
	{
		UPackage* Package = PreparePackage(InPackageName, Name);
		Conversion.Result = ConvertMaterial(Source, Package, Name, Conversion);
		FAssetRegistryModule::AssetCreated(Conversion.Result);
		return Conversion;
	}
	if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(InParticleMaterial))
	{
		// The parent made the UI's first, beside itself; the instance copied and given it.
		UMaterial* Base = Instance->GetMaterial();
		FDreamNiagaraMaterialConversion Parent = Convert(Base, DefaultPackageName(Base));
		Conversion.Replaced = Parent.Replaced;
		Conversion.Unconverted = Parent.Unconverted;
		if (Parent.Result == nullptr)
		{
			Conversion.Error = Parent.Error;
			return Conversion;
		}
		UPackage* Package = PreparePackage(InPackageName, Name);
		UMaterialInstanceConstant* Copy = DuplicateObject<UMaterialInstanceConstant>(Instance, Package, FName(*Name));
		Copy->SetFlags(RF_Public | RF_Standalone);
		Copy->SetParentEditorOnly(Parent.Result);
		Copy->PostEditChange();
		Copy->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Copy);
		Conversion.Result = Copy;
		return Conversion;
	}
	Conversion.Error = FString::Printf(TEXT("%s is neither a material nor a material instance constant."), *InParticleMaterial->GetPathName());
	return Conversion;
}

bool FDreamNiagaraMaterialConverter::Save(UObject* InAsset)
{
	if (InAsset == nullptr)
	{
		return false;
	}
	UPackage* Package = InAsset->GetPackage();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.Error = GWarn;
	return UPackage::Save(Package, InAsset, *Filename, Args).IsSuccessful();
}

TArray<FDreamNiagaraMaterialConversion> FDreamNiagaraMaterialConverter::ConvertForVisual(UDreamNiagaraSystem* InVisual, bool bInSave)
{
	TArray<FDreamNiagaraMaterialConversion> Conversions;
	if (InVisual == nullptr)
	{
		return Conversions;
	}
	TSet<UMaterialInterface*> Done;
	for (const FDreamNiagaraRendererInfo& Renderer : InVisual->GetRendererInfo())
	{
		UMaterialInterface* Source = Renderer.IsDrawn() ? Renderer.Material.Get() : nullptr;
		if (Source == nullptr || Done.Contains(Source))
		{
			continue;
		}
		Done.Add(Source);
		FDreamNiagaraMaterialConversion Conversion = Convert(Source, DefaultPackageName(Source));
		if (Conversion.Result != nullptr)
		{
			if (bInSave)
			{
				if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Conversion.Result))
				{
					Save(Instance->Parent);
				}
				Save(Conversion.Result);
			}
			InVisual->Modify();
			InVisual->SetMappedMaterial(Source, Conversion.Result);
		}
		Conversions.Add(MoveTemp(Conversion));
	}
	if (Conversions.ContainsByPredicate([](const FDreamNiagaraMaterialConversion& InConversion) { return InConversion.Result != nullptr; }))
	{
		InVisual->SetDefaultShading(EDreamNiagaraShading::Material);
	}
	return Conversions;
}
