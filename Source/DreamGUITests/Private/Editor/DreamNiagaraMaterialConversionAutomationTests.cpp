// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "DreamNiagaraMaterialConverter.h"
#include "Engine/Texture2D.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionParticleRandom.h"
#include "Materials/MaterialExpressionParticleSubUV.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialFunctionInterface.h"
#include "UObject/Package.h"

/*
 * A particle material made a UI one, node by node: what the conversion replaces, what it leaves and says so, and how it puts
 * DreamGUI's clip on the opacity. The graph only -- whether the result draws is the Rhi test
 * DreamGUI.RHI.Niagara.AConvertedParticleMaterialDrawsAsTheBuiltInShaderDoes.
 */
namespace DreamNiagaraMaterialConversionTestLocal
{
	template<typename T>
	T* Add(UMaterial* InMaterial)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(InMaterial, T::StaticClass()));
	}

	/** Whether InFrom's graph reaches an expression of class InClass, walking back through inputs. */
	bool Reaches(UMaterialExpression* InFrom, const UClass* InClass)
	{
		TArray<UMaterialExpression*> Pending = { InFrom };
		TSet<UMaterialExpression*> Seen;
		while (Pending.Num() > 0)
		{
			UMaterialExpression* Expression = Pending.Pop();
			if (Expression == nullptr || Seen.Contains(Expression))
			{
				continue;
			}
			Seen.Add(Expression);
			if (Expression->IsA(InClass))
			{
				return true;
			}
			for (FExpressionInputIterator It(Expression); It; ++It)
			{
				if (It.Input->Expression != nullptr)
				{
					Pending.Add(It.Input->Expression);
				}
			}
		}
		return false;
	}

	const FFunctionExpressionInput* FunctionInput(const UMaterialExpressionMaterialFunctionCall* InCall, const TCHAR* InName)
	{
		for (const FFunctionExpressionInput& Input : InCall->FunctionInputs)
		{
			if (Input.ExpressionInput != nullptr && Input.ExpressionInput->InputName == InName)
			{
				return &Input;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraMaterialConversionTest,
	"DreamGUI.Editor.Niagara.AParticleMaterialIsConvertedIntoAUIMaterialThatReadsDreamGUIsVertexAndClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraMaterialConversionTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraMaterialConversionTestLocal;
	// A particle material of the usual kind: a sheet's frames tinted by the particle's colour, faded by its alpha and the
	// dynamic parameter's first value, a second texture coordinate read, and a node no UI vertex can answer.
	UPackage* SourcePackage = CreatePackage(TEXT("/Temp/DreamGUITests/Niagara/M_TestParticle"));
	UMaterial* Source = NewObject<UMaterial>(SourcePackage, TEXT("M_TestParticle"), RF_Public | RF_Standalone | RF_Transient);
	Source->BlendMode = BLEND_Translucent;
	Source->SetShadingModel(MSM_Unlit);
	UMaterialExpressionParticleSubUV* SubUV = Add<UMaterialExpressionParticleSubUV>(Source);
	SubUV->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	UMaterialExpressionParticleColor* Color = Add<UMaterialExpressionParticleColor>(Source);
	UMaterialExpressionDynamicParameter* Dynamic = Add<UMaterialExpressionDynamicParameter>(Source);
	UMaterialExpressionTextureCoordinate* Second = Add<UMaterialExpressionTextureCoordinate>(Source);
	Second->CoordinateIndex = 1;
	UMaterialExpressionParticleRandom* Random = Add<UMaterialExpressionParticleRandom>(Source);
	UMaterialExpressionMultiply* Tint = Add<UMaterialExpressionMultiply>(Source);
	Tint->A.Connect(0, SubUV);
	Tint->B.Connect(0, Color);
	UMaterialExpressionMultiply* Fade = Add<UMaterialExpressionMultiply>(Source);
	Fade->A.Connect(4, Color);
	Fade->B.Connect(0, Dynamic);
	UMaterialExpressionMultiply* Mottled = Add<UMaterialExpressionMultiply>(Source);
	Mottled->A.Connect(0, Fade);
	Mottled->B.Connect(0, Random);
	Source->GetExpressionInputForProperty(MP_EmissiveColor)->Connect(0, Tint);
	Source->GetExpressionInputForProperty(MP_Opacity)->Connect(0, Mottled);
	// The second coordinate read by something drawn, so that the conversion sees it.
	SubUV->Coordinates.Connect(0, Second);

	const FDreamNiagaraMaterialConversion Conversion = FDreamNiagaraMaterialConverter::Convert(Source, TEXT("/Temp/DreamGUITests/Niagara/M_TestParticle_DreamUI"));
	UMaterial* Result = Cast<UMaterial>(Conversion.Result);
	if (!TestNotNull(*FString::Printf(TEXT("The conversion makes a material (%s)"), *Conversion.Error), Result))
	{
		return false;
	}
	TestEqual(TEXT("A surface material"), Result->MaterialDomain.GetValue(), MD_Surface);
	TestTrue(TEXT("Unlit"), Result->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
	TestTrue(TEXT("Two-sided"), Result->IsTwoSided());
	TestEqual(TEXT("The source's blend kept"), Result->GetBlendMode(), BLEND_Translucent);

	int32 ParticleNodes = 0, VertexColours = 0, Customs = 0, SecondCoordinates = 0, ThirdCoordinates = 0;
	for (UMaterialExpression* Expression : Result->GetExpressions())
	{
		ParticleNodes += Expression->IsA<UMaterialExpressionParticleColor>() || Expression->IsA<UMaterialExpressionParticleSubUV>() || Expression->IsA<UMaterialExpressionDynamicParameter>() ? 1 : 0;
		VertexColours += Expression->IsA<UMaterialExpressionVertexColor>() ? 1 : 0;
		Customs += Expression->IsA<UMaterialExpressionCustom>() ? 1 : 0;
		if (const UMaterialExpressionTextureCoordinate* Coordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression))
		{
			SecondCoordinates += Coordinate->CoordinateIndex == 1 ? 1 : 0;
			ThirdCoordinates += Coordinate->CoordinateIndex == 2 ? 1 : 0;
		}
	}
	TestEqual(TEXT("No Particle Color, SubUV or Dynamic Parameter is left"), ParticleNodes, 0);
	TestTrue(TEXT("The colour is read from the vertex"), VertexColours >= 1);
	TestTrue(TEXT("The colour's scale and the frames' blend are custom nodes"), Customs >= 2);
	TestTrue(TEXT("Texture coordinate 1 is read as 2 (and the dynamic parameter reads 1's y)"), ThirdCoordinates >= 2 && SecondCoordinates >= 1);
	TestTrue(TEXT("The particle's random value is listed as not converted"), Conversion.Unconverted.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("ParticleRandom")); }));
	TestTrue(TEXT("The replacements are listed"), Conversion.Replaced.Num() >= 4);

	// The opacity: the clip function, given the old opacity and the widget record's clip row.
	const FExpressionInput* Opacity = Result->GetExpressionInputForProperty(MP_Opacity);
	const UMaterialExpressionMaterialFunctionCall* Clip = Opacity != nullptr ? Cast<UMaterialExpressionMaterialFunctionCall>(Opacity->Expression) : nullptr;
	if (TestNotNull(TEXT("The opacity is a function's"), Clip))
	{
		TestTrue(TEXT("...DreamGUI's clip"), Clip->MaterialFunction != nullptr && Clip->MaterialFunction->GetName() == TEXT("MF_DreamUI_Clip"));
		TestTrue(*FString::Printf(TEXT("...with its inputs (%d)"), Clip->FunctionInputs.Num()), Clip->FunctionInputs.Num() == 2);
		const FFunctionExpressionInput* OldOpacity = FunctionInput(Clip, TEXT("Opacity"));
		TestTrue(TEXT("...given the old opacity"), OldOpacity != nullptr && OldOpacity->Input.Expression != nullptr && Reaches(OldOpacity->Input.Expression, UMaterialExpressionVertexColor::StaticClass()));
		const FFunctionExpressionInput* Row = FunctionInput(Clip, TEXT("CoordinateInDataTex"));
		const UMaterialExpressionMaterialFunctionCall* Record = Row != nullptr ? Cast<UMaterialExpressionMaterialFunctionCall>(Row->Input.Expression) : nullptr;
		TestTrue(TEXT("...and the widget record's clip row"), Record != nullptr && Record->MaterialFunction != nullptr && Record->MaterialFunction->GetName() == TEXT("MF_DreamWidgetProperty"));
	}
	// The emissive: the frames sampled and the vertex colour.
	const FExpressionInput* Emissive = Result->GetExpressionInputForProperty(MP_EmissiveColor);
	TestTrue(TEXT("The emissive reads the vertex colour"), Emissive != nullptr && Reaches(Emissive->Expression, UMaterialExpressionVertexColor::StaticClass()));
	TestTrue(TEXT("...and the frames through a custom node"), Emissive != nullptr && Reaches(Emissive->Expression, UMaterialExpressionCustom::StaticClass()));
	return true;
}

#endif
