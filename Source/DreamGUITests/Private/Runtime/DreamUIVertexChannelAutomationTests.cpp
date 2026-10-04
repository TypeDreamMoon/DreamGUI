// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Containers/Set.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/Text/DreamTextPainter.h"
#include "DreamUIRender/DreamUIMeshIndex.h"
#include "DreamUIRender/DreamUIMeshVertex.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Engine/World.h"
#include "RHI.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#include "Utils/DreamUIUtils.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

#include <new>

/*
 * The vertex every DreamGUI mesh is made of, and the channel painted text added to it.
 *
 * UV4 is a painted glyph's place in its gradient's boxes (FDreamUIMeshVertex::UV4). It was added after the tangents so that
 * nothing before it moved: the attribute numbers the two vertex shaders read, the offsets the byte copies and compares of
 * geometry rely on, and the UV channels the modifiers and the UE renderer loop over. What these tests hold to:
 *  - the layout: 64 bytes, every earlier field where it was, UV4 at 56, and (0, 0) out of every constructor -- whatever
 *    the memory held before, because arrays grow into memory they reuse;
 *  - the declaration: UV4 is attribute LEXUI_VERTEX_UV4_ATTRIBUTE (8), a float2 at offset 56 of stream 0;
 *  - the UE renderer's copy: five UV channels, UV4 the fifth, which a material reads as TexCoord(4);
 *  - the code a glyph quad carries in UV2.x, with its paint slot on top, decoded the way DreamUIText_UnpackPaintSlot and the
 *    shading decode it -- mirrored here line for line, since the suite runs without a GPU;
 *  - the counters the round added to DreamUI.Stats, and what a mesh reports to DreamGUI.Memory.
 */
namespace DreamUIVertexChannelTestLocal
{
	/** A vertex constructed by InConstruct in memory that held nothing but set bits: a field a constructor leaves alone reads NaN. */
	template<typename ConstructType>
	FDreamUIMeshVertex ConstructOverGarbage(ConstructType InConstruct)
	{
		alignas(FDreamUIMeshVertex) uint8 Storage[sizeof(FDreamUIMeshVertex)];
		FMemory::Memset(Storage, 0xFF, sizeof(Storage));
		FDreamUIMeshVertex* Vertex = InConstruct(Storage);
		const FDreamUIMeshVertex Result = *Vertex;
		Vertex->~FDreamUIMeshVertex();
		return Result;
	}

	/** DreamUIText_UnpackPaintSlot (DreamUIText.ush), as the shader writes it. */
	void ShaderUnpackPaintSlot(float InPacked, float& OutSlot, float& OutCode)
	{
		OutSlot = FMath::FloorToFloat((InPacked + 0.5f * 128.0f) / 128.0f);
		OutCode = InPacked - OutSlot * 128.0f;
	}

	/** DreamUIText_UnpackGlyphChannel (DreamUIText.ush): a code that is no colour glyph, as layer and dilate. */
	void ShaderUnpackGlyphChannel(float InCode, float& OutLayer, float& OutDilateEm)
	{
		OutLayer = FMath::FloorToFloat((InCode + 0.5f * 16.0f) / 16.0f);
		OutDilateEm = InCode - OutLayer * 16.0f;
	}

	/** What DreamUI_ShadePixelWith (DreamUIShade.ush) draws a glyph quad as, by its code with the slot taken off, in its order. */
	enum class EQuadKind : uint8
	{
		ColorSilhouette,
		ColorEffects,
		ColorFace,
		Coverage,
		FieldFace,
		FieldEffects,
		FieldBoth,
	};

	const TCHAR* KindName(EQuadKind InKind)
	{
		switch (InKind)
		{
		case EQuadKind::ColorSilhouette: return TEXT("colour silhouette");
		case EQuadKind::ColorEffects: return TEXT("colour effects");
		case EQuadKind::ColorFace: return TEXT("colour face");
		case EQuadKind::Coverage: return TEXT("coverage");
		case EQuadKind::FieldFace: return TEXT("field face");
		case EQuadKind::FieldEffects: return TEXT("field effects");
		default: return TEXT("field face and effects");
		}
	}

	EQuadKind ShaderClassify(float InCode)
	{
		// The thresholds DreamUIText.ush defines (DREAMUI_TEXT_CODE_*), in the order the shading tests them.
		if (InCode < -24.5f)
		{
			return EQuadKind::ColorSilhouette;
		}
		if (InCode < -0.5f)
		{
			return InCode < -8.5f ? EQuadKind::ColorEffects : EQuadKind::ColorFace;
		}
		float Layer = 0.0f;
		float DilateEm = 0.0f;
		ShaderUnpackGlyphChannel(InCode, Layer, DilateEm);
		if (Layer > 2.5f)
		{
			return EQuadKind::Coverage;
		}
		return Layer == 0.0f ? EQuadKind::FieldFace : (Layer == 1.0f ? EQuadKind::FieldEffects : EQuadKind::FieldBoth);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIVertexLayoutTest,
	"DreamGUI.Render.TheVertexIsSixtyFourBytesWithUV4AfterTheTangentsAndZeroOutOfEveryConstructor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIVertexLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIVertexChannelTestLocal;

	TestEqual(TEXT("A vertex is 64 bytes"), static_cast<int32>(sizeof(FDreamUIMeshVertex)), 64);
	// Where every field was before UV4 came: the declaration, the byte copies and the compares of geometry use these.
	TestEqual(TEXT("Position at 0"), static_cast<int32>(STRUCT_OFFSET(FDreamUIMeshVertex, Position)), 0);
	TestEqual(TEXT("Colour at 12"), static_cast<int32>(STRUCT_OFFSET(FDreamUIMeshVertex, Color)), 12);
	TestEqual(TEXT("The four texture coordinates from 16"), static_cast<int32>(STRUCT_OFFSET(FDreamUIMeshVertex, TextureCoordinate)), 16);
	TestEqual(TEXT("TangentX at 48"), static_cast<int32>(STRUCT_OFFSET(FDreamUIMeshVertex, TangentX)), 48);
	TestEqual(TEXT("TangentZ at 52"), static_cast<int32>(STRUCT_OFFSET(FDreamUIMeshVertex, TangentZ)), 52);
	TestEqual(TEXT("UV4 last, at 56"), static_cast<int32>(STRUCT_OFFSET(FDreamUIMeshVertex, UV4)), 56);
	TestEqual(TEXT("Still four texture coordinates in the array"), LEXUI_VERTEX_TEXCOORDINATE_COUNT, 4);
	TestEqual(TEXT("Five UV channels in all"), LEXUI_VERTEX_UV_CHANNEL_COUNT, 5);

	// Every constructor zeroes UV4, the empty one included, over memory that held set bits.
	const FDreamUIMeshVertex Empty = ConstructOverGarbage([](uint8* InStorage) { return new (InStorage) FDreamUIMeshVertex(); });
	const FDreamUIMeshVertex AtPosition = ConstructOverGarbage([](uint8* InStorage) { return new (InStorage) FDreamUIMeshVertex(FVector3f(1.0f, 2.0f, 3.0f)); });
	const FDreamUIMeshVertex Coloured = ConstructOverGarbage([](uint8* InStorage) { return new (InStorage) FDreamUIMeshVertex(FVector3f(1.0f, 2.0f, 3.0f), FColor::Red); });
	const FDreamUIMeshVertex Full = ConstructOverGarbage([](uint8* InStorage)
	{
		return new (InStorage) FDreamUIMeshVertex(FVector3f::ZeroVector, FVector3f(1.0f, 0.0f, 0.0f), FVector3f(0.0f, 0.0f, 1.0f), FColor::White,
			FVector2f(0.1f, 0.2f), FVector2f(0.3f, 0.4f), FVector2f(0.5f, 0.6f), FVector2f(0.7f, 0.8f));
	});
	for (const FDreamUIMeshVertex* Vertex : { &Empty, &AtPosition, &Coloured, &Full })
	{
		TestTrue(TEXT("UV4 starts at (0, 0) out of every constructor"), Vertex->UV4.X == 0.0f && Vertex->UV4.Y == 0.0f);
	}
	TestEqual(TEXT("The full constructor still writes UV3"), Full.TextureCoordinate[3], FVector2f(0.7f, 0.8f));

	// An array grown into memory it reuses constructs what it adds: every new vertex reads (0, 0) whatever the bytes held.
	TArray<FDreamUIMeshVertex> Vertices;
	Vertices.SetNumUninitialized(6);
	FMemory::Memset(Vertices.GetData(), 0xFF, Vertices.Num() * sizeof(FDreamUIMeshVertex));
	Vertices.Reset();
	Vertices.AddDefaulted(4);
	Vertices.SetNum(6);
	bool bAllZero = true;
	for (const FDreamUIMeshVertex& Vertex : Vertices)
	{
		bAllZero &= Vertex.UV4.X == 0.0f && Vertex.UV4.Y == 0.0f;
	}
	TestTrue(TEXT("AddDefaulted and SetNum give every vertex a UV4 of (0, 0) in reused memory"), bAllZero);

	// And a whole-vertex copy takes it along, as the geometry copies and the draw-call merge copy vertices.
	FDreamUIMeshVertex Painted(FVector3f(4.0f, 5.0f, 6.0f));
	Painted.UV4 = FVector2f(0.25f, 0.75f);
	FDreamUIMeshVertex Copied;
	FMemory::Memcpy(&Copied, &Painted, sizeof(FDreamUIMeshVertex));
	TestEqual(TEXT("A byte copy of a vertex carries its UV4"), Copied.UV4, FVector2f(0.25f, 0.75f));
	const FDreamUIMeshVertex Assigned = Painted;
	TestEqual(TEXT("...and so does an assignment"), Assigned.UV4, FVector2f(0.25f, 0.75f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIVertexDeclarationTest,
	"DreamGUI.Render.TheVertexDeclarationReadsUV4AsAttributeEightAndLeavesEveryOtherAttributeWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIVertexDeclarationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("UV4 is attribute 8"), LEXUI_VERTEX_UV4_ATTRIBUTE, 8);

	FVertexDeclarationElementList Elements;
	FDreamUIMeshVertexDeclaration::MakeElements(Elements);
	if (!TestEqual(TEXT("Nine elements: position, colour, four UVs, two tangents, UV4"), Elements.Num(), 9))
	{
		return false;
	}
	struct FExpected
	{
		int32 Offset;
		EVertexElementType Type;
	};
	const FExpected Expected[9] =
	{
		{ 0, VET_Float3 },
		{ 12, VET_Color },
		{ 16, VET_Float2 },
		{ 24, VET_Float2 },
		{ 32, VET_Float2 },
		{ 40, VET_Float2 },
		{ 48, VET_PackedNormal },
		{ 52, VET_PackedNormal },
		{ 56, VET_Float2 },
	};
	for (int32 Index = 0; Index < Elements.Num(); ++Index)
	{
		const FVertexElement& Element = Elements[Index];
		const FString Which = FString::Printf(TEXT("element %d"), Index);
		TestEqual(*(Which + TEXT(": its attribute index is its place")), static_cast<int32>(Element.AttributeIndex), Index);
		TestEqual(*(Which + TEXT(": its offset")), static_cast<int32>(Element.Offset), Expected[Index].Offset);
		TestEqual(*(Which + TEXT(": its type")), static_cast<int32>(Element.Type.GetValue()), static_cast<int32>(Expected[Index].Type));
		TestEqual(*(Which + TEXT(": stream 0")), static_cast<int32>(Element.StreamIndex), 0);
		TestEqual(*(Which + TEXT(": the vertex's stride")), static_cast<int32>(Element.Stride), static_cast<int32>(sizeof(FDreamUIMeshVertex)));
		TestEqual(*(Which + TEXT(": per vertex")), static_cast<int32>(Element.bUseInstanceIndex), 0);
	}
	const FVertexElement& UV4Element = Elements.Last();
	TestEqual(TEXT("The last element is UV4's attribute"), static_cast<int32>(UV4Element.AttributeIndex), LEXUI_VERTEX_UV4_ATTRIBUTE);
	TestEqual(TEXT("...at UV4's offset"), static_cast<int32>(UV4Element.Offset), static_cast<int32>(STRUCT_OFFSET(FDreamUIMeshVertex, UV4)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIUERendererChannelTest,
	"DreamGUI.Render.TheUERenderersCopyOfAVertexCarriesUV4AsItsFifthChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIUERendererChannelTest::RunTest(const FString& Parameters)
{
	FStaticMeshVertexBuffers Buffers;
	UDreamUIMeshComponent::InitUERendererVertexBuffers(Buffers, 2);
	TestEqual(TEXT("Every UV channel a vertex carries"), static_cast<int32>(Buffers.StaticMeshVertexBuffer.GetNumTexCoords()), LEXUI_VERTEX_UV_CHANNEL_COUNT);
	TestTrue(TEXT("At full precision: a code in UV2.x and a place in UV4 are not halves"), Buffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs());
	TestEqual(TEXT("Two positions"), static_cast<int32>(Buffers.PositionVertexBuffer.GetNumVertices()), 2);
	TestEqual(TEXT("Two colours"), static_cast<int32>(Buffers.ColorVertexBuffer.GetNumVertices()), 2);

	FDreamUIMeshVertex Vertices[2] = { FDreamUIMeshVertex(FVector3f(1.0f, 2.0f, 3.0f), FColor(10, 20, 30, 40)), FDreamUIMeshVertex(FVector3f(4.0f, 5.0f, 6.0f), FColor(50, 60, 70, 80)) };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		for (int32 Channel = 0; Channel < LEXUI_VERTEX_TEXCOORDINATE_COUNT; ++Channel)
		{
			// Unique per vertex and channel, so a channel written from another shows.
			Vertices[Index].TextureCoordinate[Channel] = FVector2f(Index * 10.0f + Channel, Channel * 0.5f + 0.25f);
		}
		// A painted quad's code: 128 times slot 3, over a field face's dilate.
		Vertices[Index].TextureCoordinate[2].X = DreamTextQuadCode::AddSlot(0.25f, 3);
	}
	Vertices[0].UV4 = FVector2f(0.375f, 0.625f);
	Vertices[1].UV4 = FVector2f(1.0f, 0.0f);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UDreamUIMeshComponent::WriteUERendererVertex(Buffers, Index, Vertices[Index], true);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		for (int32 Channel = 0; Channel < LEXUI_VERTEX_TEXCOORDINATE_COUNT; ++Channel)
		{
			TestEqual(*FString::Printf(TEXT("Vertex %d channel %d is its texture coordinate"), Index, Channel),
				Buffers.StaticMeshVertexBuffer.GetVertexUV(Index, Channel), Vertices[Index].TextureCoordinate[Channel]);
		}
		TestEqual(*FString::Printf(TEXT("Vertex %d channel 4 is its UV4"), Index),
			Buffers.StaticMeshVertexBuffer.GetVertexUV(Index, LEXUI_VERTEX_TEXCOORDINATE_COUNT), Vertices[Index].UV4);
		TestEqual(*FString::Printf(TEXT("Vertex %d keeps its position"), Index), Buffers.PositionVertexBuffer.VertexPosition(Index), Vertices[Index].Position);
		TestEqual(*FString::Printf(TEXT("Vertex %d keeps its colour"), Index), Buffers.ColorVertexBuffer.VertexColor(Index), Vertices[Index].Color);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIQuadCodeDecodeTest,
	"DreamGUI.Render.EveryGlyphQuadCodeDecodesToItsKindUnderEveryPaintSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIQuadCodeDecodeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIVertexChannelTestLocal;

	struct FCase
	{
		float Code;
		EQuadKind Kind;
	};
	// Every code a painter or a modifier writes: field faces, effects and both, with dilates up to the field's reach (the
	// effects copy's widest dilate), coverage phases 0 to 3, the colour glyph's face, effects copy and silhouette, and a bitmap
	// font's 0.
	const FCase Cases[] =
	{
		{ 0.0f, EQuadKind::FieldFace },
		{ 0.25f, EQuadKind::FieldFace },
		{ 7.5f, EQuadKind::FieldFace },
		{ DreamTextQuadCode::FieldLayerStride, EQuadKind::FieldEffects },
		{ DreamTextQuadCode::FieldLayerStride + 3.75f, EQuadKind::FieldEffects },
		{ 2.0f * DreamTextQuadCode::FieldLayerStride + 1.5f, EQuadKind::FieldBoth },
		{ DreamTextQuadCode::CoverageBase, EQuadKind::Coverage },
		{ DreamTextQuadCode::CoverageBase + 3.0f, EQuadKind::Coverage },
		{ DreamTextQuadCode::ColorFace, EQuadKind::ColorFace },
		{ DreamTextQuadCode::ColorEffects, EQuadKind::ColorEffects },
		{ DreamTextQuadCode::ColorSilhouette, EQuadKind::ColorSilhouette },
	};
	// What interpolation can leave of a quad's constant UV2.x, and well beyond.
	const float Noises[] = { 0.0f, 0.03125f, -0.03125f };
	for (const FCase& Case : Cases)
	{
		TestTrue(*FString::Printf(TEXT("Code %g lies in [-64, 64), where the slot's decode is exact"), Case.Code),
			Case.Code >= -0.5f * DreamTextQuadCode::SlotStride && Case.Code < 0.5f * DreamTextQuadCode::SlotStride);
		for (int32 Slot = 0; Slot < DreamTextQuadCode::SlotCount; ++Slot)
		{
			const float Packed = DreamTextQuadCode::AddSlot(Case.Code, Slot);
			// The CPU's helpers and the shader's formula agree on what was written.
			TestEqual(*FString::Printf(TEXT("GetSlot of code %g in slot %d"), Case.Code, Slot), DreamTextQuadCode::GetSlot(Packed), Slot);
			TestEqual(*FString::Printf(TEXT("StripSlot of code %g in slot %d"), Case.Code, Slot), DreamTextQuadCode::StripSlot(Packed), Case.Code);
			for (const float Noise : Noises)
			{
				float DecodedSlot = 0.0f;
				float DecodedCode = 0.0f;
				ShaderUnpackPaintSlot(Packed + Noise, DecodedSlot, DecodedCode);
				const FString Which = FString::Printf(TEXT("code %g in slot %d, %+g off"), Case.Code, Slot, Noise);
				TestEqual(*(Which + TEXT(": the shader finds the slot")), DecodedSlot, static_cast<float>(Slot));
				TestTrue(*(Which + TEXT(": ...and the code")), FMath::IsNearlyEqual(DecodedCode, Case.Code, 0.04f));
				const EQuadKind Kind = ShaderClassify(DecodedCode);
				TestTrue(*FString::Printf(TEXT("%s: drawn as a %s (was %s)"), *Which, KindName(Case.Kind), KindName(Kind)), Kind == Case.Kind);
			}
		}
		// Slot 0 -- every quad of a text with no paint -- hands every decode its code exactly as it was written.
		float Slot0 = 0.0f;
		float Code0 = 0.0f;
		ShaderUnpackPaintSlot(Case.Code, Slot0, Code0);
		TestTrue(*FString::Printf(TEXT("Code %g in slot 0 decodes bit for bit"), Case.Code), Slot0 == 0.0f && Code0 == Case.Code);
	}
	TestTrue(TEXT("The silhouette threshold lies between the silhouette and the effects copy"),
		DreamTextQuadCode::ColorSilhouette < DreamTextQuadCode::ColorSilhouetteThreshold
		&& DreamTextQuadCode::ColorSilhouetteThreshold < DreamTextQuadCode::ColorEffects);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRenderStatsNamesTest,
	"DreamGUI.Render.EveryRenderStatsCounterHasANameOfItsOwnAndStartsAgainFromZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRenderStatsNamesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRenderStats;

	TSet<FString> Names;
	for (int32 Index = 0; Index < CounterCount; ++Index)
	{
		const FString Name = GetCounterName(static_cast<ECounter>(Index));
		TestNotEqual(*FString::Printf(TEXT("Counter %d has a name"), Index), Name, FString(TEXT("?")));
		TestFalse(*FString::Printf(TEXT("%s is the only counter of that name"), *Name), Names.Contains(Name));
		Names.Add(Name);
	}
	TestEqual(TEXT("The round's counters are named as the contract has them"), FString(GetCounterName(ECounter::FontAtlasUploadBytes)), FString(TEXT("FontAtlasUploadBytes")));
	TestEqual(TEXT("...the first of them too"), FString(GetCounterName(ECounter::TextPaints)), FString(TEXT("TextPaints")));

	// Game-thread counters, which nothing else adds to while a test runs on the game thread.
	TakeSnapshot(/*bInReset*/ true);
	AddCount(ECounter::TextPaints, 3);
	AddCount(ECounter::CoverageFlushes, 2);
	AddCount(ECounter::FontAtlasUploadBytes, 4096);
	const FSnapshot Counted = TakeSnapshot(/*bInReset*/ true);
	TestEqual(TEXT("A snapshot holds what was counted"), Counted.Counters[static_cast<int32>(ECounter::TextPaints)], static_cast<int64>(3));
	TestEqual(TEXT("...for every new counter"), Counted.Counters[static_cast<int32>(ECounter::CoverageFlushes)], static_cast<int64>(2));
	TestEqual(TEXT("...bytes included"), Counted.Counters[static_cast<int32>(ECounter::FontAtlasUploadBytes)], static_cast<int64>(4096));
	TestTrue(TEXT("The description names them"), Describe(Counted).Contains(TEXT("FontAtlasUploadBytes")));
	const FSnapshot Afterwards = TakeSnapshot(/*bInReset*/ false);
	TestEqual(TEXT("The reset started them again from zero"), Afterwards.Counters[static_cast<int32>(ECounter::TextPaints)], static_cast<int64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIMeshMemoryInfoTest,
	"DreamGUI.Render.AMeshReportsTheSectionsItHoldsAndTheBytesOfTheirVerticesAndIndices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIMeshMemoryInfoTest::RunTest(const FString& Parameters)
{
	{
		// A mesh with no section has nothing to report.
		const UDreamUIMeshComponent* Bare = NewObject<UDreamUIMeshComponent>(GetTransientPackage());
		int32 Sections = -1;
		int32 Pooled = -1;
		int64 VertexBytes = -1;
		int64 IndexBytes = -1;
		Bare->GetMemoryInfo(Sections, Pooled, VertexBytes, IndexBytes);
		TestTrue(TEXT("A mesh with no section reports nothing"), Sections == 0 && Pooled == 0 && VertexBytes == 0 && IndexBytes == 0);
	}

	// A world-space panel drawing one image: one mesh section, its quad's vertices and indices.
	DreamTests::Lifecycle::FScopedWorld World(EWorldType::Game);
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
	if (!TestNotNull(TEXT("A game world with a UI manager"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
	Root->SetWidth(200.0f);
	Root->SetHeight(200.0f);
	Root->OnRegister();
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("A canvas on the panel"), Canvas))
	{
		return false;
	}
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	UDreamWidget* Block = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
	Block->SetWidth(40.0f);
	Block->SetHeight(30.0f);
	Block->OnRegister();
	Block->TrySetParent(Root, false);
	UDreamTexture* Visual = Block->CreateNewVisual<UDreamTexture>();
	if (!TestNotNull(TEXT("A texture visual on the block"), Visual))
	{
		return false;
	}
	Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		++GFrameCounter;
		Manager->Tick(1.0f / 30.0f);
		Manager->SubmitCanvasDrawCall();
	}
	const UDreamUIMeshComponent* Mesh = Canvas->GetUIMesh();
	if (!TestNotNull(TEXT("The canvas has its mesh"), Mesh))
	{
		return false;
	}
	int32 Sections = 0;
	int32 Pooled = 0;
	int64 VertexBytes = 0;
	int64 IndexBytes = 0;
	Mesh->GetMemoryInfo(Sections, Pooled, VertexBytes, IndexBytes);
	TestTrue(TEXT("The image's section is in use"), Sections >= 1);
	TestTrue(TEXT("Its quad's vertices are counted, 64 bytes each"), VertexBytes >= 4 * static_cast<int64>(sizeof(FDreamUIMeshVertex)));
	TestTrue(TEXT("...and its two triangles' indices"), IndexBytes >= 6 * static_cast<int64>(sizeof(FDreamUIMeshIndex)));
	TestTrue(TEXT("Pooled sections are counted apart, none negative"), Pooled >= 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
