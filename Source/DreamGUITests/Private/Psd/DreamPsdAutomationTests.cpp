// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"

#include "Import/DreamPsdDuiMerge.h"
#include "Import/DreamPsdDuiModel.h"
#include "Import/DreamPsdImporter.h"
#include "Import/DreamPsdMapper.h"
#include "Psd/DreamPsdColor.h"
#include "Psd/DreamPsdDescriptor.h"
#include "Psd/DreamPsdEngineData.h"
#include "Psd/DreamPsdRaster.h"
#include "Psd/DreamPsdReader.h"
#include "Psd/DreamPsdWriter.h"
#include "Psd/DreamPsdTestDocuments.h"

/*
 * The layered-document importer on the CPU: the reader held to documents the writer makes (every compression, both
 * depths the editor saves layers at, both sizes), its descriptor and engine-data parsers, the mapping of each kind of
 * layer onto DreamGUI nodes, the .dui that mapping prints -- which must build -- and a reimport that keeps what an
 * author added. The pictures are in RHI/DreamPsdPixelAutomationTests.cpp.
 */
namespace DreamPsdTestLocal
{
	using namespace DreamPsdTestDocuments;

	bool ReadBack(const FDreamPsdWriteDocument& InDoc, FDreamPsdDocument& OutDoc, FDreamPsdMessages& OutMessages)
	{
		TArray<uint8> Bytes;
		DreamPsdWriter::Write(InDoc, Bytes);
		return DreamPsdReader::Read(Bytes, OutDoc, OutMessages);
	}

	const FDreamPsdLayer* FindLayer(const FDreamPsdDocument& InDoc, const FString& InName)
	{
		return InDoc.Layers.FindByPredicate([&InName](const FDreamPsdLayer& L) { return L.Name == InName; });
	}

	const FDreamPsdDuiNode* FindNode(const FDreamPsdDuiNode& InRoot, const FString& InId)
	{
		if (InRoot.Id == InId)
		{
			return &InRoot;
		}
		for (const FDreamPsdDuiNode& Child : InRoot.Children)
		{
			if (const FDreamPsdDuiNode* Found = FindNode(Child, InId))
			{
				return Found;
			}
		}
		return nullptr;
	}

	FString ValueOf(const FDreamPsdDuiNode* InNode, const FString& InName)
	{
		const FDreamPsdDuiProperty* P = InNode != nullptr ? InNode->Find(InName) : nullptr;
		return P != nullptr ? P->Value : FString(TEXT("<none>"));
	}

	FDreamPsdMapOptions Options(const FString& InName)
	{
		FDreamPsdMapOptions O;
		O.DocumentName = InName;
		O.TextureFolder = FString::Printf(TEXT("/Temp/DreamGUITests/Psd/%s"), *InName);
		return O;
	}

	const FDreamUINode* FindAstNode(const FDreamUINode& InNode, const FString& InId, FString* OutParent = nullptr, const FString& InParent = FString())
	{
		if (InNode.Id.Equals(InId, ESearchCase::IgnoreCase))
		{
			if (OutParent != nullptr)
			{
				*OutParent = InParent;
			}
			return &InNode;
		}
		for (const FDreamUINode& Child : InNode.Children)
		{
			if (const FDreamUINode* Found = FindAstNode(Child, InId, OutParent, InNode.Id))
			{
				return Found;
			}
		}
		return nullptr;
	}

	UDreamWidget* FindWidget(UDreamWidget* InRoot, const FString& InName)
	{
		if (InRoot == nullptr)
		{
			return nullptr;
		}
		if (InRoot->GetDisplayName() == InName || InRoot->GetName() == InName)
		{
			return InRoot;
		}
		for (UDreamWidget* Child : InRoot->GetChildren())
		{
			if (UDreamWidget* Found = FindWidget(Child, InName))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdDecodersTest,
	"DreamGUI.Psd.PackBitsAndPredictionUndoThemselves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdDecodersTest::RunTest(const FString& Parameters)
{
	// PackBits: runs, literals, the 128 limits, a row of one byte.
	FRandomStream Random(7);
	for (int32 Case = 0; Case < 40; ++Case)
	{
		TArray<uint8> Row;
		const int32 Length = Case == 0 ? 1 : Random.RandRange(1, 700);
		while (Row.Num() < Length)
		{
			const bool bRun = Random.FRand() < 0.5f;
			const int32 Count = FMath::Min(Length - Row.Num(), Random.RandRange(1, 300));
			const uint8 Value = (uint8)Random.RandRange(0, 255);
			for (int32 i = 0; i < Count; ++i)
			{
				Row.Add(bRun ? Value : (uint8)Random.RandRange(0, 255));
			}
		}
		TArray<uint8> Packed, Unpacked;
		DreamPsdPixels::PackBits(Row.GetData(), Row.Num(), Packed);
		const bool bOk = DreamPsdPixels::UnpackBits(Packed.GetData(), Packed.Num(), Unpacked, Row.Num());
		if (!TestTrue(FString::Printf(TEXT("PackBits case %d (%d bytes) comes back as it went in"), Case, Length), bOk && Unpacked == Row))
		{
			return false;
		}
	}
	// A row that decodes short is reported.
	{
		const uint8 Short[] = { 2, 1, 2, 3 };
		TArray<uint8> Out;
		TestFalse(TEXT("PackBits data that decodes to fewer bytes than the row is reported"), DreamPsdPixels::UnpackBits(Short, 4, Out, 6));
		TestEqual(TEXT("...and the row is padded to its width"), Out.Num(), 6);
	}
	// Prediction at 8, 16 and 32 bits: applied then undone is the row itself, and undone is what the format says.
	for (const int32 Depth : { 8, 16, 32 })
	{
		const int32 Width = 37;
		TArray<uint8> Row;
		for (int32 i = 0; i < Width * Depth / 8; ++i)
		{
			Row.Add((uint8)Random.RandRange(0, 255));
		}
		TArray<uint8> Coded = Row;
		DreamPsdPixels::ApplyPrediction(Coded.GetData(), Width, Depth);
		DreamPsdPixels::UndoPrediction(Coded.GetData(), Width, Depth);
		TestTrue(FString::Printf(TEXT("prediction at %d bits undoes itself"), Depth), Coded == Row);
	}
	{
		// 16-bit: each big-endian sample is the sum of the deltas so far.
		uint8 Row[] = { 0x01, 0x00, 0x00, 0x05, 0xFF, 0xFF };
		DreamPsdPixels::UndoPrediction(Row, 3, 16);
		TestTrue(TEXT("16-bit prediction adds the samples in big-endian order"), Row[0] == 0x01 && Row[1] == 0x00 && Row[2] == 0x01 && Row[3] == 0x05 && Row[4] == 0x01 && Row[5] == 0x04);
	}
	// 16 and 32 bits to 8.
	{
		const TArray<uint8> Sixteen = { 0x00, 0x00, 0x80, 0x00, 0xFF, 0xFF };
		TArray<uint8> Eight;
		DreamPsdPixels::ToEightBit(Sixteen, 16, 3, 1, Eight);
		TestTrue(TEXT("16-bit samples scale to 8 (0, half, full)"), Eight.Num() == 3 && Eight[0] == 0 && Eight[1] == 128 && Eight[2] == 255);
		TArray<uint8> Float;
		for (const float V : { 0.0f, 0.2140f, 1.0f })
		{
			uint32 Bits;
			FMemory::Memcpy(&Bits, &V, 4);
			Float.Append({ uint8(Bits >> 24), uint8(Bits >> 16), uint8(Bits >> 8), uint8(Bits) });
		}
		DreamPsdPixels::ToEightBit(Float, 32, 3, 1, Eight);
		TestTrue(FString::Printf(TEXT("32-bit samples are linear and come out encoded (0.214 -> about 128, is %d)"), Eight.Num() == 3 ? Eight[1] : -1),
			Eight.Num() == 3 && Eight[0] == 0 && FMath::Abs(Eight[1] - 128) <= 1 && Eight[2] == 255);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdHeaderTest,
	"DreamGUI.Psd.TheHeaderReadsBothSizesAndRefusesWhatIsNotADocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdHeaderTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	for (const bool bLarge : { false, true })
	{
		FDreamPsdWriteDocument Doc;
		Doc.Width = 33;
		Doc.Height = 17;
		Doc.bLarge = bLarge;
		Doc.Layers.Add(PixelLayer(TEXT("One"), 1, FIntRect(2, 3, 12, 9), [](int32 X, int32 Y) { return FColor(X * 20, Y * 30, 7, 255); }));
		FDreamPsdDocument Read;
		FDreamPsdMessages Messages;
		const bool bRead = ReadBack(Doc, Read, Messages);
		TestTrue(FString::Printf(TEXT("a %s reads (%s)"), bLarge ? TEXT("PSB") : TEXT("PSD"), *Messages.ToString()), bRead);
		TestEqual(TEXT("its version"), Read.Version, bLarge ? 2 : 1);
		TestTrue(TEXT("its size, depth and mode"), Read.Width == 33 && Read.Height == 17 && Read.Depth == 8 && Read.ColorMode == EDreamPsdColorMode::RGB);
		TestTrue(TEXT("its resources: the global angle, the version info"), Read.GlobalAngle == 120.0f && Read.ResourceIds.Contains(1057));
		TestEqual(TEXT("its one layer"), Read.Layers.Num(), 1);
	}
	auto Refused = [this](const TArray<uint8>& InBytes, const TCHAR* InWhat)
	{
		FDreamPsdDocument Read;
		FDreamPsdMessages Messages;
		const bool bRead = DreamPsdReader::Read(InBytes, Read, Messages);
		TestTrue(FString::Printf(TEXT("%s is refused with a reason (%s)"), InWhat, *Messages.ToString()), !bRead && Messages.HasErrors());
	};
	Refused(TArray<uint8>(), TEXT("an empty file"));
	Refused({ 'P', 'K', 3, 4, 0, 0 }, TEXT("a file that is not a document"));
	{
		FDreamPsdWriteDocument Doc;
		Doc.Width = 4;
		Doc.Height = 4;
		TArray<uint8> Bytes;
		DreamPsdWriter::Write(Doc, Bytes);
		TArray<uint8> Version3 = Bytes;
		Version3[5] = 3;
		Refused(Version3, TEXT("version 3"));
		TArray<uint8> Cut(Bytes.GetData(), 20);
		Refused(Cut, TEXT("a header cut short"));
		TArray<uint8> Depth = Bytes;
		Depth[23] = 12;
		Refused(Depth, TEXT("12 bits a channel"));
	}
	{
		// A grayscale document: gray in every channel of the pixels.
		FDreamPsdWriteDocument Doc;
		Doc.Width = 8;
		Doc.Height = 8;
		Doc.ColorMode = EDreamPsdColorMode::Grayscale;
		Doc.Layers.Add(PixelLayer(TEXT("Gray"), 1, FIntRect(0, 0, 8, 8), [](int32 X, int32) { return FColor(X * 30, 0, 0, 255); }));
		FDreamPsdDocument Read;
		FDreamPsdMessages Messages;
		ReadBack(Doc, Read, Messages);
		const FColor P = Read.Layers.Num() > 0 ? Read.Layers[0].Pixels.At(3, 0) : FColor::Black;
		TestTrue(FString::Printf(TEXT("a grayscale layer reads as gray (%s)"), *P.ToString()), P.R == 90 && P.G == 90 && P.B == 90);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdLayerRecordsTest,
	"DreamGUI.Psd.LayerRecordsReadTheirBoundsBlendOpacityFlagsAndNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdLayerRecordsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	FDreamPsdWriteDocument Doc;
	Doc.Width = 64;
	Doc.Height = 48;
	FDreamPsdWriteLayer A = PixelLayer(TEXT("标题 Title"), 41, FIntRect(-5, 3, 20, 30), [](int32, int32) { return FColor::Red; });
	A.BlendKey = TEXT("scrn");
	A.Opacity = 77;
	A.FillOpacity = 200;
	FDreamPsdWriteLayer B = PixelLayer(TEXT("Clipped"), 42, FIntRect(10, 10, 30, 40), [](int32, int32) { return FColor::Green; });
	B.bClipping = true;
	B.bVisible = false;
	Doc.Layers = { A, B };
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	TestTrue(FString::Printf(TEXT("the document reads (%s)"), *Messages.ToString()), ReadBack(Doc, Read, Messages));
	if (!TestEqual(TEXT("two layers"), Read.Layers.Num(), 2))
	{
		return false;
	}
	const FDreamPsdLayer& L = Read.Layers[0];
	TestEqual(TEXT("a name out of 'luni', in any script"), L.Name, FString(TEXT("标题 Title")));
	TestEqual(TEXT("its id"), L.LayerId, 41);
	TestTrue(TEXT("its bounds, off the canvas included"), L.Rect == FIntRect(-5, 3, 20, 30));
	TestEqual(TEXT("its blend key"), L.BlendKey, FString(TEXT("scrn")));
	TestTrue(TEXT("its opacity and fill opacity"), L.Opacity == 77 && L.FillOpacity == 200);
	TestTrue(TEXT("visible and not clipped"), L.bVisible && !L.bClipping);
	TestTrue(TEXT("its channels: alpha and three colours"), L.Channels.Num() == 4 && L.Channels[0].Id == -1);
	TestTrue(TEXT("its blocks, in order"), L.BlockKeys.Num() >= 3 && L.BlockKeys[0] == TEXT("luni") && L.BlockKeys.Contains(TEXT("lyid")) && L.BlockKeys.Contains(TEXT("iOpa")));
	TestEqual(TEXT("its kind"), L.Kind, EDreamPsdLayerKind::Pixel);
	const FDreamPsdLayer& M = Read.Layers[1];
	TestTrue(TEXT("the second is clipped and hidden"), M.bClipping && !M.bVisible);
	TestTrue(TEXT("transparency comes from a negative layer count"), Read.bMergedHasTransparency);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdCompressionTest,
	"DreamGUI.Psd.PixelsReadTheSameAtEveryCompressionDepthAndSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdCompressionTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	// The gallery, every way it can be written: each must read to the same pixels, masks and records.
	FDreamPsdDocument Reference;
	{
		FDreamPsdMessages Messages;
		ReadBack(MakeGallery(8, false, 0), Reference, Messages);
		TestFalse(FString::Printf(TEXT("the raw 8-bit gallery reads (%s)"), *Messages.ToString()), Messages.HasErrors());
	}
	struct FVariant { int32 Depth; bool bLarge; uint16 Compression; const TCHAR* Name; };
	const FVariant Variants[] = {
		{ 8, false, 1, TEXT("8-bit, run-length") }, { 8, false, 2, TEXT("8-bit, zip") }, { 8, false, 3, TEXT("8-bit, zip with prediction") },
		{ 16, false, 0, TEXT("16-bit (layers in Lr16), raw") }, { 16, false, 1, TEXT("16-bit, run-length") }, { 16, false, 3, TEXT("16-bit, zip with prediction") },
		{ 8, true, 1, TEXT("PSB, run-length") }, { 16, true, 3, TEXT("PSB 16-bit, zip with prediction") },
	};
	for (const FVariant& V : Variants)
	{
		FDreamPsdDocument Read;
		FDreamPsdMessages Messages;
		const bool bRead = ReadBack(MakeGallery(V.Depth, V.bLarge, V.Compression), Read, Messages);
		if (!TestTrue(FString::Printf(TEXT("%s reads (%s)"), V.Name, *Messages.ToString()), bRead && Read.Layers.Num() == Reference.Layers.Num()))
		{
			continue;
		}
		int32 Different = 0;
		for (int32 i = 0; i < Read.Layers.Num(); ++i)
		{
			const FDreamPsdLayer& A = Read.Layers[i];
			const FDreamPsdLayer& B = Reference.Layers[i];
			Different += A.Pixels.Pixels == B.Pixels.Pixels && A.Mask.Pixels.Pixels == B.Mask.Pixels.Pixels && A.Name == B.Name && A.Rect == B.Rect ? 0 : 1;
		}
		TestEqual(FString::Printf(TEXT("%s: every layer's pixels and mask are the raw 8-bit one's"), V.Name), Different, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdGroupTreeTest,
	"DreamGUI.Psd.GroupsNestFromTheirDividers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdGroupTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	FDreamPsdWriteDocument Doc;
	Doc.Width = 16;
	Doc.Height = 16;
	auto Pixel = [](const TCHAR* InName, int32 InId) { return PixelLayer(InName, InId, FIntRect(0, 0, 4, 4), [](int32, int32) { return FColor::White; }); };
	// Bottom first: A, then group Outer { B, group Inner { C }, D }, then E.
	Doc.Layers = { Pixel(TEXT("A"), 1), GroupDivider(2), Pixel(TEXT("B"), 3), GroupDivider(4), Pixel(TEXT("C"), 5), GroupRecord(TEXT("Inner"), 6, TEXT("mul ")),
		Pixel(TEXT("D"), 7), GroupRecord(TEXT("Outer"), 8), Pixel(TEXT("E"), 9) };
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	ReadBack(Doc, Read, Messages);
	const FDreamPsdLayerNode Tree = Read.BuildTree();
	auto NameOf = [&Read](const FDreamPsdLayerNode& N) { return Read.Layers.IsValidIndex(N.LayerIndex) ? Read.Layers[N.LayerIndex].Name : FString(TEXT("?")); };
	if (!TestEqual(TEXT("three at the top: A, Outer, E"), Tree.Children.Num(), 3))
	{
		return false;
	}
	TestTrue(TEXT("in drawing order"), NameOf(Tree.Children[0]) == TEXT("A") && NameOf(Tree.Children[1]) == TEXT("Outer") && NameOf(Tree.Children[2]) == TEXT("E"));
	const FDreamPsdLayerNode& Outer = Tree.Children[1];
	TestTrue(TEXT("Outer is a group of B, Inner and D"), Outer.bGroup && Outer.Children.Num() == 3 && NameOf(Outer.Children[0]) == TEXT("B")
		&& NameOf(Outer.Children[1]) == TEXT("Inner") && NameOf(Outer.Children[2]) == TEXT("D"));
	TestTrue(TEXT("Inner holds C"), Outer.Children.Num() == 3 && Outer.Children[1].Children.Num() == 1 && NameOf(Outer.Children[1].Children[0]) == TEXT("C"));
	const FDreamPsdLayer* Inner = FindLayer(Read, TEXT("Inner"));
	TestTrue(TEXT("a group's blend mode comes from its section block"), Inner != nullptr && Inner->BlendKey == TEXT("mul ") && Inner->Kind == EDreamPsdLayerKind::GroupStart);
	// A divider never closed keeps its layers.
	FDreamPsdWriteDocument Broken = Doc;
	Broken.Layers.RemoveAt(7);
	FDreamPsdDocument ReadBroken;
	ReadBack(Broken, ReadBroken, Messages);
	FDreamPsdMessages TreeMessages;
	const FDreamPsdLayerNode BrokenTree = ReadBroken.BuildTree(&TreeMessages);
	int32 Leaves = 0;
	TFunction<void(const FDreamPsdLayerNode&)> Count = [&Leaves, &Count](const FDreamPsdLayerNode& N) { for (const FDreamPsdLayerNode& C : N.Children) { Leaves += C.Children.Num() == 0 && !C.bGroup ? 1 : 0; Count(C); } };
	Count(BrokenTree);
	TestTrue(FString::Printf(TEXT("a group never closed keeps its five layers and says so (%d, %s)"), Leaves, *TreeMessages.ToString()), Leaves == 5 && TreeMessages.Items.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdMasksReadTest,
	"DreamGUI.Psd.MasksReadTheirRectDefaultDensityAndFeather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdMasksReadTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	FDreamPsdWriteDocument Doc;
	Doc.Width = 100;
	Doc.Height = 80;
	FDreamPsdWriteLayer L = PixelLayer(TEXT("Masked"), 1, FIntRect(10, 10, 60, 50), [](int32, int32) { return FColor::Blue; });
	L.bMask = true;
	L.MaskRect = FIntRect(20, 15, 40, 35);
	L.MaskPixels = MakeGray(20, 20, [](int32 X, int32) { return (uint8)(X * 12); });
	L.MaskDefault = 255;
	L.MaskDensity = 0.6f;
	L.MaskFeather = 3.5f;
	L.bVectorMask = true;
	L.VectorPath = RoundedRectPath(FBox2D(FVector2D(12, 12), FVector2D(58, 48)), 6.0f);
	L.LiveShape.Kind = EDreamPsdLiveShapeKind::RoundedRectangle;
	L.LiveShape.Box = FBox2D(FVector2D(12, 12), FVector2D(58, 48));
	L.LiveShape.Radii = FVector4(6, 6, 6, 6);
	Doc.Layers.Add(L);
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	ReadBack(Doc, Read, Messages);
	if (!TestEqual(TEXT("one layer"), Read.Layers.Num(), 1))
	{
		return false;
	}
	const FDreamPsdLayer& R = Read.Layers[0];
	TestTrue(TEXT("the pixel mask: its rectangle and default colour"), R.Mask.bPresent && R.Mask.Rect == FIntRect(20, 15, 40, 35) && R.Mask.DefaultColor == 255);
	TestTrue(FString::Printf(TEXT("its density and feather (%.3f, %.3f)"), R.Mask.Density, R.Mask.Feather), FMath::IsNearlyEqual(R.Mask.Density, 153 / 255.0f, 1e-3f) && FMath::IsNearlyEqual(R.Mask.Feather, 3.5f));
	TestTrue(TEXT("its pixels"), R.Mask.Pixels.Width == 20 && R.Mask.Pixels.At(5, 3) == 60);
	TestTrue(TEXT("the vector mask: one closed subpath of eight knots"), R.VectorMask.bPresent && R.VectorMask.Path.Subpaths.Num() == 1
		&& R.VectorMask.Path.Subpaths[0].Knots.Num() == 8 && R.VectorMask.Path.Subpaths[0].bClosed);
	const FVector2D First = R.VectorMask.Path.Subpaths.Num() > 0 ? R.VectorMask.Path.Subpaths[0].Knots[0].Anchor : FVector2D::ZeroVector;
	TestTrue(FString::Printf(TEXT("its knots in document pixels (%s)"), *First.ToString()), First.Equals(FVector2D(18, 12), 0.01));
	TestTrue(TEXT("the live shape: a rounded rectangle, its box and radii"), R.LiveShape.Kind == EDreamPsdLiveShapeKind::RoundedRectangle
		&& R.LiveShape.Box.Min.Equals(FVector2D(12, 12)) && FMath::IsNearlyEqual(R.LiveShape.Radii.X, 6.0) && !R.LiveShape.bTransformed);
	// Baked: inside the rectangle its pixels, outside the default, density pulling everything towards white.
	const FDreamPsdGray Baked = DreamPsdRaster::BakeLayerMask(R.Mask, FIntRect(0, 0, 100, 80), 0.0f);
	TestTrue(FString::Printf(TEXT("a baked mask: outside is the default, inside the density applied (%d, %d)"), Baked.At(5, 5), Baked.At(20, 20)),
		Baked.At(5, 5) == 255 && FMath::Abs(Baked.At(20, 20) - FMath::RoundToInt(255 - 0.6f * 255)) <= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdDescriptorTest,
	"DreamGUI.Psd.DescriptorsRoundTripEveryValueKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdDescriptorTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdDescriptor;
	FDreamPsdDescriptor Inner = MakeColor(FLinearColor(1.0f, 0.5f, 0.25f));
	FDreamPsdDescriptor D;
	D.Name = TEXT("名前");
	D.ClassId = TEXT("someLongClassId");
	D.Items.Emplace(TEXT("doub"), MakeNumber(2.5));
	D.Items.Emplace(TEXT("Angl"), MakeUnitFloat(TEXT("#Ang"), -45.0));
	D.Items.Emplace(TEXT("long"), MakeInteger(-7));
	D.Items.Emplace(TEXT("enab"), MakeBool(true));
	D.Items.Emplace(TEXT("Nm  "), MakeString(TEXT("渐变 (one)")));
	D.Items.Emplace(TEXT("Md  "), MakeEnum(TEXT("BlnM"), TEXT("linearDodge")));
	D.Items.Emplace(TEXT("Clr "), MakeDescriptor(Inner));
	D.Items.Emplace(TEXT("Clrs"), MakeList({ MakeDescriptor(Inner), MakeInteger(3) }));
	D.Items.Emplace(TEXT("EngineData"), MakeRaw({ 1, 2, 3, 0, 255 }));
	TArray<uint8> Bytes;
	Write(Bytes, D);
	FDreamPsdStream S(Bytes.GetData(), Bytes.Num());
	FDreamPsdDescriptor R;
	FString Error;
	TestTrue(FString::Printf(TEXT("the descriptor reads (%s)"), *Error), Read(S, R, Error));
	TestTrue(TEXT("...to its last byte"), S.AtEnd() && !S.bOverrun);
	TestEqual(TEXT("read back, it dumps as it was written"), R.Dump(), D.Dump());
	TestTrue(TEXT("a number, a unit float with its unit, an integer, a boolean"), R.GetNumber(TEXT("doub")) == 2.5 && R.GetUnitFloat(TEXT("Angl")) == -45.0
		&& R.Find(TEXT("Angl"))->Unit == TEXT("#Ang") && R.GetNumber(TEXT("long")) == -7 && R.GetBool(TEXT("enab")));
	TestTrue(TEXT("a string in any script, an enum"), R.GetString(TEXT("Nm  ")) == TEXT("渐变 (one)") && R.GetEnum(TEXT("Md  ")) == TEXT("linearDodge"));
	FLinearColor Colour;
	TestTrue(TEXT("a colour descriptor, and its keys without their trailing spaces"), R.GetColor(TEXT("Clr"), Colour) && Colour.Equals(FLinearColor(1.0f, 0.5f, 0.25f), 1e-4f));
	TestTrue(TEXT("a list"), R.GetList(TEXT("Clrs")) != nullptr && R.GetList(TEXT("Clrs"))->Num() == 2);
	TestTrue(TEXT("raw data"), R.Find(TEXT("EngineData")) != nullptr && R.Find(TEXT("EngineData"))->Raw.Num() == 5);
	// HSB, gray and CMYK colour descriptors.
	{
		FDreamPsdDescriptor Hsb;
		Hsb.ClassId = TEXT("HSBC");
		Hsb.Items.Emplace(TEXT("H   "), MakeUnitFloat(TEXT("#Ang"), 120.0));
		Hsb.Items.Emplace(TEXT("Strt"), MakeNumber(100.0));
		Hsb.Items.Emplace(TEXT("Brgh"), MakeNumber(100.0));
		FLinearColor C;
		TestTrue(TEXT("an HSB colour is green at 120 degrees"), Hsb.ReadAsColor(C) && C.Equals(FLinearColor(0, 1, 0, 1), 1e-4f));
	}
	// A value type the reader does not know stops the read with a reason.
	{
		TArray<uint8> Bad;
		FDreamPsdDescriptor One;
		One.ClassId = TEXT("null");
		One.Items.Emplace(TEXT("x   "), MakeInteger(1));
		Write(Bad, One);
		Bad[Bad.Num() - 8] = 'Z';
		FDreamPsdStream BadStream(Bad.GetData(), Bad.Num());
		FDreamPsdDescriptor Out;
		FString Why;
		TestFalse(TEXT("an unknown value type is refused"), Read(BadStream, Out, Why));
		TestTrue(FString::Printf(TEXT("...naming it (%s)"), *Why), Why.Contains(TEXT("does not know")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdEngineDataTest,
	"DreamGUI.Psd.EngineDataParsesTheTextEnginesSyntax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdEngineDataTest::RunTest(const FString& Parameters)
{
	// As the engine writes it: tabs, numbers without a leading zero, names, booleans, and UTF-16 strings with their
	// parentheses escaped -- here "a(b)" and "中".
	TArray<uint8> Bytes;
	auto Text = [&Bytes](const char* In) { while (*In) { Bytes.Add((uint8)*In++); } };
	Text("\n\n<<\n\t/EngineDict\n\t<<\n\t\t/Editor << /Text (");
	Bytes.Append({ 0xFE, 0xFF, 0x00, 'a', 0x00, '\\', '(', 0x00, 'b', 0x00, '\\', ')', 0x4E, 0x2D, 0x00, '\r' });
	Text(") >>\n\t\t/Numbers [ .5 -1.25 12 1e2 ]\n\t\t/Flag true /Off false\n\t\t/Kind /Roman\n\t>>\n>>\n");
	FDreamPsdEngineValue Root;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("the engine data parses (%s)"), *Error), DreamPsdEngineData::Parse(Bytes, Root, Error)))
	{
		return false;
	}
	const FDreamPsdEngineValue* Editor = Root.FindPath(TEXT("EngineDict/Editor/Text"));
	TestTrue(FString::Printf(TEXT("a UTF-16 string with escaped parentheses (%s)"), Editor != nullptr ? *Editor->String : TEXT("none")),
		Editor != nullptr && Editor->String == FString(TEXT("a(b)中\r")));
	const FDreamPsdEngineValue* Numbers = Root.FindPath(TEXT("EngineDict/Numbers"));
	TestTrue(TEXT("numbers, a leading dot and an exponent included"), Numbers != nullptr && Numbers->Items.Num() == 4 && Numbers->Items[0].Number == 0.5
		&& Numbers->Items[1].Number == -1.25 && Numbers->Items[3].Number == 100.0);
	const FDreamPsdEngineValue* Engine = Root.Find(TEXT("EngineDict"));
	TestTrue(TEXT("booleans and names"), Engine != nullptr && Engine->GetBool(TEXT("Flag")) && !Engine->GetBool(TEXT("Off"), true) && Engine->GetString(TEXT("Kind")) == TEXT("Roman"));
	TestNotNull(TEXT("a path through an array"), Root.FindPath(TEXT("EngineDict/Numbers/2")));
	// Written and read again, the tree is the same.
	TArray<uint8> Again;
	DreamPsdEngineData::Write(Root, Again);
	FDreamPsdEngineValue Reread;
	TestTrue(TEXT("written, it parses again"), DreamPsdEngineData::Parse(Again, Reread, Error));
	const FDreamPsdEngineValue* Text2 = Reread.FindPath(TEXT("EngineDict/Editor/Text"));
	TestTrue(TEXT("...to the same text"), Text2 != nullptr && Text2->String == FString(TEXT("a(b)中\r")));
	// What does not parse says where.
	TArray<uint8> Broken = { '<', '<', ' ', '/', 'A', ' ', '[', ' ', '1' };
	FDreamPsdEngineValue Nothing;
	TestFalse(TEXT("an array never closed is refused"), DreamPsdEngineData::Parse(Broken, Nothing, Error));
	TestTrue(FString::Printf(TEXT("...at a byte (%s)"), *Error), Error.Contains(TEXT("byte")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdTypeLayerTest,
	"DreamGUI.Psd.TypeLayersReadTheirTextRunsBoxAndWarp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdTypeLayerTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	TSharedPtr<FDreamPsdText> T = MakeShared<FDreamPsdText>();
	T->Text = TEXT("Start (1)\n第二行");
	T->Transform[0] = 2.0; T->Transform[3] = 2.0; T->Transform[4] = 30.0; T->Transform[5] = 40.0;
	T->bBoxText = true;
	T->BoxBounds = FBox2D(FVector2D(0, 0), FVector2D(80, 30));
	FDreamPsdTextStyleRun Run;
	Run.FontName = TEXT("Roboto-Bold");
	Run.FontSize = 14.0f;
	Run.Color = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
	Run.Tracking = 50.0f;
	Run.bFauxItalic = true;
	Run.Length = 6;
	FDreamPsdTextStyleRun Second = Run;
	Second.Start = 6;
	Second.FontSize = 10.0f;
	T->StyleRuns = { Run, Second };
	FDreamPsdTextParagraphRun Paragraph;
	Paragraph.Justification = 2;
	T->ParagraphRuns = { Paragraph };
	T->Warp.Style = TEXT("warpArc");
	T->Warp.Value = 35.0f;
	FDreamPsdWriteDocument Doc;
	Doc.Width = 200;
	Doc.Height = 120;
	FDreamPsdWriteLayer L = PixelLayer(TEXT("Heading"), 9, FIntRect(30, 40, 190, 100), [](int32, int32) { return FColor(255, 0, 0, 128); });
	L.Text = T;
	Doc.Layers.Add(L);
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	ReadBack(Doc, Read, Messages);
	const FDreamPsdLayer* R = FindLayer(Read, TEXT("Heading"));
	if (!TestTrue(FString::Printf(TEXT("a type layer reads as text (%s)"), *Messages.ToString()), R != nullptr && R->Kind == EDreamPsdLayerKind::Text && R->Text.IsValid()))
	{
		return false;
	}
	const FDreamPsdText& X = *R->Text;
	TestEqual(TEXT("its text, the engine's own last break gone, its paragraph break a '\\r'"), X.Text, FString(TEXT("Start (1)\r第二行")));
	TestTrue(TEXT("its transform"), X.Transform[0] == 2.0 && X.Transform[5] == 40.0 && FMath::IsNearlyEqual(X.GetScale(), 2.0) && X.IsAxisAligned());
	TestTrue(TEXT("box text, its box"), X.bBoxText && X.BoxBounds.Max.Equals(FVector2D(80, 30)));
	if (TestEqual(TEXT("two style runs"), X.StyleRuns.Num(), 2))
	{
		TestTrue(TEXT("the face out of the font set, the size, the colour, tracking, faux italic"), X.StyleRuns[0].FontName == TEXT("Roboto-Bold")
			&& X.StyleRuns[0].FontSize == 14.0f && X.StyleRuns[0].Color.Equals(FLinearColor(1, 0, 0, 1)) && X.StyleRuns[0].Tracking == 50.0f && X.StyleRuns[0].bFauxItalic);
		TestTrue(TEXT("the second run starts where the first ends and keeps to the text"), X.StyleRuns[1].Start == 6 && X.StyleRuns[1].Start + X.StyleRuns[1].Length <= X.Text.Len()
			&& X.StyleRuns[1].FontSize == 10.0f);
	}
	TestTrue(TEXT("centred"), X.ParagraphRuns.Num() == 1 && X.ParagraphRuns[0].Justification == 2);
	TestTrue(TEXT("its warp"), X.Warp.Style == TEXT("warpArc") && X.Warp.Value == 35.0f && !X.Warp.IsNone());
	TestTrue(TEXT("its descriptors and engine data are kept"), X.TextDescriptor.IsValid() && X.WarpDescriptor.IsValid() && X.EngineData.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdEffectsAdjustmentsTest,
	"DreamGUI.Psd.EffectsAndAdjustmentsReadIntoTheirModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdEffectsAdjustmentsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	using namespace DreamPsdDescriptor;
	auto Effect = [](const TCHAR* InClass)
	{
		FDreamPsdDescriptor E;
		E.ClassId = InClass;
		E.Items.Emplace(TEXT("enab"), MakeBool(true));
		E.Items.Emplace(TEXT("present"), MakeBool(true));
		E.Items.Emplace(TEXT("Md  "), MakeEnum(TEXT("BlnM"), TEXT("Mltp")));
		E.Items.Emplace(TEXT("Clr "), MakeDescriptor(MakeColor(FLinearColor(0.2f, 0.4f, 0.6f))));
		E.Items.Emplace(TEXT("Opct"), MakeUnitFloat(TEXT("#Prc"), 75.0));
		return E;
	};
	FDreamPsdDescriptor Shadow = Effect(TEXT("DrSh"));
	Shadow.Items.Emplace(TEXT("uglg"), MakeBool(true));
	Shadow.Items.Emplace(TEXT("lagl"), MakeUnitFloat(TEXT("#Ang"), 10.0));
	Shadow.Items.Emplace(TEXT("Dstn"), MakeUnitFloat(TEXT("#Pxl"), 6.0));
	Shadow.Items.Emplace(TEXT("Ckmt"), MakeUnitFloat(TEXT("#Prc"), 20.0));
	Shadow.Items.Emplace(TEXT("blur"), MakeUnitFloat(TEXT("#Pxl"), 9.0));
	FDreamPsdDescriptor Stroke = Effect(TEXT("FrFX"));
	Stroke.Items.Emplace(TEXT("Styl"), MakeEnum(TEXT("FStl"), TEXT("InsF")));
	Stroke.Items.Emplace(TEXT("PntT"), MakeEnum(TEXT("FrFl"), TEXT("SClr")));
	Stroke.Items.Emplace(TEXT("Sz  "), MakeUnitFloat(TEXT("#Pxl"), 3.0));
	FDreamPsdDescriptor Off = Effect(TEXT("OrGl"));
	Off.Items[0].Value = MakeBool(false);
	FDreamPsdDescriptor Root;
	Root.ClassId = TEXT("null");
	Root.Items.Emplace(TEXT("Scl "), MakeUnitFloat(TEXT("#Prc"), 100.0));
	Root.Items.Emplace(TEXT("masterFXSwitch"), MakeBool(true));
	Root.Items.Emplace(TEXT("DrSh"), MakeDescriptor(Shadow));
	Root.Items.Emplace(TEXT("FrFX"), MakeDescriptor(Stroke));
	Root.Items.Emplace(TEXT("OrGl"), MakeDescriptor(Off));

	FDreamPsdWriteDocument Doc;
	Doc.Width = 64;
	Doc.Height = 64;
	Doc.GlobalAngle = 135.0f;
	FDreamPsdWriteLayer Styled = PixelLayer(TEXT("Styled"), 1, FIntRect(4, 4, 40, 40), [](int32, int32) { return FColor::White; });
	Styled.ExtraBlocks.Add(DreamPsdWriter::MakeEffectsBlock(Root));
	FDreamPsdWriteLayer Levels;
	Levels.Name = TEXT("Levels");
	Levels.LayerId = 2;
	Levels.ExtraBlocks.Add(DreamPsdWriter::MakeLevelsBlock(10, 240, 1.2f, 5, 250));
	FDreamPsdWriteLayer Curves;
	Curves.Name = TEXT("Curves");
	Curves.LayerId = 3;
	FDreamPsdAdjustment::FCurve Curve;
	Curve.Channel = 0;
	Curve.Points = { FVector2f(0, 0), FVector2f(128, 160), FVector2f(255, 255) };
	Curves.ExtraBlocks.Add(DreamPsdWriter::MakeCurvesBlock({ Curve }));
	FDreamPsdWriteLayer Hue;
	Hue.Name = TEXT("Hue");
	Hue.LayerId = 4;
	Hue.ExtraBlocks.Add(DreamPsdWriter::MakeHueSaturationBlock(30, -20, 10));
	FDreamPsdWriteLayer Vibrance;
	Vibrance.Name = TEXT("Vibrance");
	Vibrance.LayerId = 5;
	FDreamPsdDescriptor VibranceDescriptor;
	VibranceDescriptor.ClassId = TEXT("null");
	VibranceDescriptor.Items.Emplace(TEXT("vibrance"), MakeInteger(40));
	VibranceDescriptor.Items.Emplace(TEXT("Strt"), MakeInteger(-10));
	Vibrance.ExtraBlocks.Add(DreamPsdWriter::MakeDescriptorBlock(TEXT("vibA"), VibranceDescriptor));
	Doc.Layers = { Styled, Levels, Curves, Hue, Vibrance };
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	ReadBack(Doc, Read, Messages);
	TestFalse(FString::Printf(TEXT("the document reads without a warning (%s)"), *Messages.ToString()), Messages.Count(FDreamPsdMessage::ESeverity::Warning) > 0);

	const FDreamPsdLayer* S = FindLayer(Read, TEXT("Styled"));
	if (TestTrue(TEXT("the layer has its effects"), S != nullptr && S->Effects.bPresent && S->Effects.HasEnabled()))
	{
		TestEqual(TEXT("three effects, in the editor's drawing order (shadow, glow, stroke)"), S->Effects.Effects.Num(), 3);
		const FDreamPsdEffect* D = S->Effects.Effects.FindByPredicate([](const FDreamPsdEffect& E) { return E.Kind == EDreamPsdEffectKind::DropShadow; });
		TestTrue(TEXT("the shadow uses the global light, so its angle is the document's"), D != nullptr && D->bUseGlobalLight && D->Angle == 135.0f);
		TestTrue(TEXT("its distance, spread, size, opacity, colour and blend mode"), D != nullptr && D->Distance == 6.0f && FMath::IsNearlyEqual(D->Spread, 0.2f)
			&& D->Size == 9.0f && FMath::IsNearlyEqual(D->Opacity, 0.75f) && D->Color.Equals(FLinearColor(0.2f, 0.4f, 0.6f), 1e-3f) && D->BlendMode == TEXT("Mltp"));
		const FDreamPsdEffect* K = S->Effects.Effects.FindByPredicate([](const FDreamPsdEffect& E) { return E.Kind == EDreamPsdEffectKind::Stroke; });
		TestTrue(TEXT("the stroke: inside, a colour, 3 pixels"), K != nullptr && K->StrokePosition == TEXT("InsF") && K->FillType == TEXT("SClr") && K->Size == 3.0f);
		const FDreamPsdEffect* G = S->Effects.Effects.FindByPredicate([](const FDreamPsdEffect& E) { return E.Kind == EDreamPsdEffectKind::OuterGlow; });
		TestTrue(TEXT("the glow is there, turned off"), G != nullptr && !G->bEnabled);
		TestEqual(TEXT("descriptor blend ids name blend modes"), FDreamPsdMapper::DescriptorBlendModeName(TEXT("Mltp")), FString(TEXT("Multiply")));
	}
	const FDreamPsdLayer* Lv = FindLayer(Read, TEXT("Levels"));
	TestTrue(TEXT("levels: an adjustment layer"), Lv != nullptr && Lv->Kind == EDreamPsdLayerKind::Adjustment && Lv->Adjustment.IsValid() && Lv->Adjustment->Kind == EDreamPsdAdjustmentKind::Levels);
	if (Lv != nullptr && Lv->Adjustment.IsValid() && Lv->Adjustment->Levels.Num() > 0)
	{
		const FDreamPsdAdjustment::FLevels& C = Lv->Adjustment->Levels[0];
		TestTrue(TEXT("its composite's input, output and gamma"), C.InputBlack == 10 && C.InputWhite == 240 && C.OutputBlack == 5 && C.OutputWhite == 250 && FMath::IsNearlyEqual(C.Gamma, 1.2f));
	}
	const FDreamPsdLayer* Cv = FindLayer(Read, TEXT("Curves"));
	TestTrue(TEXT("curves: the points, input before output"), Cv != nullptr && Cv->Adjustment.IsValid() && Cv->Adjustment->Curves.Num() == 1
		&& Cv->Adjustment->Curves[0].Points.Num() == 3 && Cv->Adjustment->Curves[0].Points[1] == FVector2f(128, 160));
	const FDreamPsdLayer* H = FindLayer(Read, TEXT("Hue"));
	TestTrue(TEXT("hue/saturation: the master's three"), H != nullptr && H->Adjustment.IsValid() && H->Adjustment->MasterHsl == FIntVector(30, -20, 10) && !H->Adjustment->bColorize);
	const FDreamPsdLayer* V = FindLayer(Read, TEXT("Vibrance"));
	TestTrue(TEXT("vibrance, from its descriptor"), V != nullptr && V->Adjustment.IsValid() && V->Adjustment->Vibrance == 40.0f && V->Adjustment->Saturation == -10.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdCompositeReadTest,
	"DreamGUI.Psd.TheMergedImageIsReadWithItsTransparency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdCompositeReadTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	FDreamPsdWriteDocument Doc;
	Doc.Width = 20;
	Doc.Height = 10;
	Doc.Layers.Add(PixelLayer(TEXT("A"), 1, FIntRect(0, 0, 4, 4), [](int32, int32) { return FColor::White; }));
	Doc.Composite = MakeImage(20, 10, [](int32 X, int32 Y) { return FColor(200, 100, X * 10, (uint8)(X * 13)); });
	for (const uint16 Compression : { (uint16)0, (uint16)1 })
	{
		Doc.CompositeCompression = Compression;
		FDreamPsdDocument Read;
		FDreamPsdMessages Messages;
		ReadBack(Doc, Read, Messages);
		if (!TestFalse(TEXT("the merged image is there"), Read.Composite.IsEmpty()))
		{
			return false;
		}
		int32 Worst = 0;
		for (int32 i = 0; i < 200; ++i)
		{
			const FColor A = Read.Composite.Pixels[i];
			const FColor B = Doc.Composite.Pixels[i];
			if (B.A == 0)
			{
				Worst = FMath::Max(Worst, (int32)A.A);
				continue;
			}
			// Matting against white and back loses precision where the alpha is low.
			const int32 Tolerance = FMath::CeilToInt(255.0f / B.A) + 1;
			Worst = FMath::Max(Worst, FMath::Max3(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B)) > Tolerance ? 999 : 0);
			Worst = FMath::Max(Worst, A.A != B.A ? 999 : 0);
		}
		TestEqual(FString::Printf(TEXT("compression %u: the merged image comes back unmatted, its alpha exact"), Compression), Worst, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdRasterTest,
	"DreamGUI.Psd.PathsFillWithTheirOperationsAndGradientsDrawAtTheirAngles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdRasterTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	const FIntRect Region(0, 0, 40, 40);
	FDreamPsdPath Square;
	{
		FDreamPsdSubpath S;
		for (const FVector2D& P : { FVector2D(10, 10), FVector2D(30, 10), FVector2D(30, 30), FVector2D(10, 30) })
		{
			S.Knots.Add({ P, P, P, true });
		}
		Square.Subpaths.Add(S);
	}
	const FDreamPsdGray Filled = DreamPsdRaster::FillPath(Square, Region);
	TestTrue(TEXT("a square fills inside, nothing outside"), Filled.At(20, 20) == 255 && Filled.At(5, 5) == 0 && Filled.At(35, 20) == 0);
	TestTrue(TEXT("its edges are whole pixels: exactly covered"), Filled.At(10, 15) == 255 && Filled.At(9, 15) == 0 && Filled.At(29, 15) == 255 && Filled.At(30, 15) == 0);
	// Half a pixel in: half covered.
	FDreamPsdPath Half = Square;
	for (FDreamPsdPathKnot& K : Half.Subpaths[0].Knots)
	{
		K.Anchor.X += 0.5;
		K.In = K.Out = K.Anchor;
	}
	const FDreamPsdGray HalfFilled = DreamPsdRaster::FillPath(Half, Region);
	TestTrue(FString::Printf(TEXT("an edge half way through a pixel covers half of it (%d)"), HalfFilled.At(10, 15)), FMath::Abs(HalfFilled.At(10, 15) - 128) <= 1);
	// Subtract a smaller square: a frame.
	FDreamPsdPath Frame = Square;
	FDreamPsdSubpath Hole;
	for (const FVector2D& P : { FVector2D(15, 15), FVector2D(25, 15), FVector2D(25, 25), FVector2D(15, 25) })
	{
		Hole.Knots.Add({ P, P, P, true });
	}
	Hole.Operation = EDreamPsdPathOperation::Subtract;
	Frame.Subpaths.Add(Hole);
	const FDreamPsdGray Framed = DreamPsdRaster::FillPath(Frame, Region);
	TestTrue(TEXT("a subtracted subpath cuts a hole"), Framed.At(20, 20) == 0 && Framed.At(12, 20) == 255);
	Frame.Subpaths[1].Operation = EDreamPsdPathOperation::Intersect;
	TestTrue(TEXT("an intersected one keeps only the overlap"), DreamPsdRaster::FillPath(Frame, Region).At(12, 20) == 0 && DreamPsdRaster::FillPath(Frame, Region).At(20, 20) == 255);
	// A circle's area.
	const FDreamPsdGray Circle = DreamPsdRaster::FillPath(EllipsePath(FBox2D(FVector2D(0, 0), FVector2D(40, 40))), Region);
	double Area = 0.0;
	for (uint8 V : Circle.Pixels) { Area += V / 255.0; }
	TestTrue(FString::Printf(TEXT("a circle of radius 20 covers pi r^2 (%.1f against %.1f)"), Area, PI * 400.0), FMath::Abs(Area - PI * 400.0) < 4.0);
	// Gradients: linear at 0 runs left to right, at 90 bottom to top; reversed flips it.
	FDreamPsdGradientFill G;
	G.Gradient.ColorStops = { { 0.0f, 0.5f, FLinearColor::Black }, { 1.0f, 0.5f, FLinearColor::White } };
	G.Angle = 0.0f;
	const FDreamPsdImage Right = DreamPsdRaster::DrawGradient(G, FBox2D(FVector2D(0, 0), FVector2D(100, 10)), FIntPoint(100, 10));
	TestTrue(FString::Printf(TEXT("a linear gradient at 0 degrees runs left to right (%d .. %d)"), Right.At(0, 5).R, Right.At(99, 5).R), Right.At(0, 5).R < 5 && Right.At(99, 5).R > 250 && FMath::Abs(Right.At(50, 5).R - 128) < 4);
	G.Angle = 90.0f;
	const FDreamPsdImage Up = DreamPsdRaster::DrawGradient(G, FBox2D(FVector2D(0, 0), FVector2D(10, 100)), FIntPoint(10, 100));
	TestTrue(TEXT("at 90 degrees, bottom to top"), Up.At(5, 99).R < 5 && Up.At(5, 0).R > 250);
	G.bReverse = true;
	TestTrue(TEXT("reversed, top to bottom"), DreamPsdRaster::DrawGradient(G, FBox2D(FVector2D(0, 0), FVector2D(10, 100)), FIntPoint(10, 100)).At(5, 0).R < 5);
	// A midpoint moved towards the first stop.
	FDreamPsdGradient Mid;
	Mid.ColorStops = { { 0.0f, 0.25f, FLinearColor::Black }, { 1.0f, 0.5f, FLinearColor::White } };
	TestTrue(TEXT("a gradient is half way at its midpoint"), FMath::IsNearlyEqual(Mid.Evaluate(0.25f).R, 0.5f, 1e-3f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdNameTagsTest,
	"DreamGUI.Psd.LayerNamesSaySkipControlAndNineSlice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdNameTagsTest::RunTest(const FString& Parameters)
{
	const FDreamPsdMapper::FNameTags A = FDreamPsdMapper::ParseName(TEXT("Play @Button #9(4, 6, 8, 10)"));
	TestTrue(TEXT("a control and nine-slice margins, the name left over"), A.Name == TEXT("Play") && A.Control == TEXT("Button") && A.bNineSlice
		&& A.NineSlice == FMargin(4, 6, 8, 10) && !A.bSkip);
	const FDreamPsdMapper::FNameTags B = FDreamPsdMapper::ParseName(TEXT("notes !SKIP"));
	TestTrue(TEXT("!skip, in any case"), B.bSkip && B.Name == TEXT("notes"));
	const FDreamPsdMapper::FNameTags C = FDreamPsdMapper::ParseName(TEXT("Frame #9(12)"));
	TestTrue(TEXT("one margin for all four"), C.bNineSlice && C.NineSlice == FMargin(12));
	const FDreamPsdMapper::FNameTags D = FDreamPsdMapper::ParseName(TEXT("#9(3,5)"));
	TestTrue(TEXT("two margins: horizontal and vertical; a name of nothing but a tag keeps the whole name"), D.bNineSlice && D.NineSlice == FMargin(3, 5) && !D.Name.IsEmpty());
	// Ids: letters of any script kept, everything else one '_', the layer id after _psd.
	TestEqual(TEXT("an id from a name"), DreamPsdDui::MakeGeneratedId(TEXT("Main menu / 开始"), 12), FString(TEXT("Main_menu_开始_psd12")));
	TestEqual(TEXT("an id that would start with a digit"), DreamPsdDui::MakeGeneratedId(TEXT("3 dots"), 4), FString(TEXT("L3_dots_psd4")));
	int32 Layer = -1;
	TestTrue(TEXT("the suffix reads back"), DreamPsdDui::ParseGeneratedId(TEXT("Main_menu_开始_psd12"), Layer) && Layer == 12);
	TestFalse(TEXT("an author's id has none"), DreamPsdDui::ParseGeneratedId(TEXT("Main_psdX"), Layer));
	TestEqual(TEXT("asset names are ASCII"), DreamPsdDui::AssetSafe(TEXT("开始 Start!"), TEXT("Layer")), FString(TEXT("Start")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdMapKindsTest,
	"DreamGUI.Psd.LayersMapToNodesOfTheirKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdMapKindsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	ReadBack(MakeGallery(), Read, Messages);
	FDreamPsdMapper Mapper(Read, Options(TEXT("Gallery")));
	FDreamPsdMapResult R;
	if (!TestTrue(TEXT("the gallery maps"), Mapper.Map(R)))
	{
		return false;
	}
	TestTrue(TEXT("the root is the canvas's size"), ValueOf(&R.Root, TEXT("AnchorData.SizeDelta")) == TEXT("(256, 192)"));
	const FDreamPsdDuiNode* Disc = FindNode(R.Root, TEXT("Disc_psd2"));
	TestTrue(TEXT("a pixel layer is an Image"), Disc != nullptr && Disc->Type == TEXT("Image"));
	TestEqual(TEXT("placed at its trimmed pixels, top left anchored, y up"), ValueOf(Disc, TEXT("AnchorData.AnchoredPosition")), FString(TEXT("(32, -32)")));
	TestEqual(TEXT("...sized to them"), ValueOf(Disc, TEXT("AnchorData.SizeDelta")), FString(TEXT("(80, 80)")));
	const FDreamPsdTextureRequest* DiscTexture = R.FindTexture(TEXT("/Temp/DreamGUITests/Psd/Gallery/T_Gallery_Disc_2.T_Gallery_Disc_2"));
	TestTrue(TEXT("its texture, cut to what is not transparent"), DiscTexture != nullptr && DiscTexture->Image.Width == 80 && DiscTexture->Image.Height == 80);
	TestEqual(TEXT("the image draws it"), ValueOf(Disc, TEXT("Brush.ResourceObject")), FString(TEXT("\"/Temp/DreamGUITests/Psd/Gallery/T_Gallery_Disc_2.T_Gallery_Disc_2\"")));
	TestEqual(TEXT("clipping is clip to below"), ValueOf(FindNode(R.Root, TEXT("Stripes_psd3")), TEXT("bClipToBelow")), FString(TEXT("true")));
	TestEqual(TEXT("a blend key is a blend mode"), ValueOf(FindNode(R.Root, TEXT("Shade_psd4")), TEXT("BlendMode")), FString(TEXT("Multiply")));
	const FDreamPsdDuiNode* Glow = FindNode(R.Root, TEXT("Glow_psd5"));
	TestTrue(TEXT("opacity is render opacity"), ValueOf(Glow, TEXT("RenderOpacity")) == TEXT("0.8") && ValueOf(Glow, TEXT("BlendMode")) == TEXT("Overlay"));
	const FDreamPsdDuiNode* Panel = FindNode(R.Root, TEXT("Panel_psd10"));
	const FDreamPsdDuiComponent* Group = Panel != nullptr ? Panel->FindComponent(TEXT("DreamLayerGroup")) : nullptr;
	TestTrue(TEXT("a group that fades is a container with a layer group"), Panel != nullptr && Panel->Type == TEXT("Widget") && Group != nullptr && Panel->Children.Num() == 2);
	TestTrue(TEXT("...isolated, at its opacity, with no render opacity of its own"), Group != nullptr && Group->Properties.ContainsByPredicate([](const FDreamPsdDuiProperty& P) { return P.Name == TEXT("GroupOpacity") && P.Value == TEXT("0.6"); })
		&& ValueOf(Panel, TEXT("RenderOpacity")) == TEXT("<none>"));
	TestTrue(TEXT("...filling its parent"), ValueOf(Panel, TEXT("AnchorData.AnchorMax")) == TEXT("(1, 1)"));
	const FDreamPsdDuiNode* Frame = FindNode(R.Root, TEXT("Frame_psd13"));
	TestTrue(TEXT("a pass-through group is a plain container"), Frame != nullptr && Frame->FindComponent(TEXT("DreamLayerGroup")) == nullptr);
	const FDreamPsdDuiNode* Pill = FindNode(R.Root, TEXT("Pill_psd14"));
	TestTrue(TEXT("a live rounded rectangle is a RectBlock, its radii in pixels"), Pill != nullptr && Pill->Type == TEXT("RectBlock")
		&& ValueOf(Pill, TEXT("CornerRadius.X")) == TEXT("12") && ValueOf(Pill, TEXT("CornerRadius.W")) == TEXT("12") && ValueOf(Pill, TEXT("CornerRadiusUnitMode")) == TEXT("Value"));
	TestEqual(TEXT("...its fill its body"), ValueOf(Pill, TEXT("BodyColor")), FString(TEXT("#965ADCFF")));
	TestEqual(TEXT("...placed at its box"), ValueOf(Pill, TEXT("AnchorData.AnchoredPosition")), FString(TEXT("(196, -146)")));
	const FDreamPsdDuiNode* Tint = FindNode(R.Root, TEXT("Tint_psd15"));
	TestTrue(TEXT("a fill layer is a RectBlock over the canvas, its fill opacity in the body's alpha"), Tint != nullptr && Tint->Type == TEXT("RectBlock")
		&& ValueOf(Tint, TEXT("BodyColor")) == TEXT("#FFFFFF80") && ValueOf(Tint, TEXT("AnchorData.SizeDelta")) == TEXT("(256, 192)"));
	TestEqual(TEXT("a hidden layer is hidden"), ValueOf(FindNode(R.Root, TEXT("Ghost_psd16")), TEXT("Visibility")), FString(TEXT("Hidden")));
	TestNull(TEXT("a !skip layer is left out"), FindNode(R.Root, TEXT("Notes_psd17")));
	TestTrue(FString::Printf(TEXT("every layer but the group dividers and the skipped one is mapped (%d, %d)"), R.LayersMapped, R.LayersSkipped), R.LayersMapped == 15 && R.LayersSkipped == 1);

	// Text: a face through the font map, the size scaled, the colour and alignment.
	TSharedPtr<FDreamPsdText> T = MakeShared<FDreamPsdText>();
	T->Text = TEXT("Hello \"world\"");
	T->Transform[0] = 2.0; T->Transform[3] = 2.0; T->Transform[4] = 10.0; T->Transform[5] = 20.0;
	FDreamPsdTextStyleRun Run;
	Run.FontName = TEXT("Roboto-Bold");
	Run.FontSize = 14.0f;
	Run.Color = FLinearColor(1, 0, 0, 1);
	Run.Length = T->Text.Len();
	T->StyleRuns = { Run };
	T->ParagraphRuns = { FDreamPsdTextParagraphRun{ 0, T->Text.Len(), 2 } };
	FDreamPsdWriteDocument TextDoc;
	TextDoc.Width = 200;
	TextDoc.Height = 100;
	FDreamPsdWriteLayer TextLayer = PixelLayer(TEXT("Hello"), 3, FIntRect(10, 8, 150, 40), [](int32, int32) { return FColor(255, 0, 0, 255); });
	TextLayer.Text = T;
	TextLayer.FillOpacity = 128;
	TextDoc.Layers.Add(TextLayer);
	FDreamPsdDocument TextRead;
	ReadBack(TextDoc, TextRead, Messages);
	FDreamPsdMapOptions TextOptions = Options(TEXT("Text"));
	TextOptions.FontMap.Add(TEXT("Roboto"), TEXT("/Game/Fonts/F_Roboto.F_Roboto"));
	FDreamPsdMapper TextMapper(TextRead, TextOptions);
	FDreamPsdMapResult TR;
	TextMapper.Map(TR);
	const FDreamPsdDuiNode* Text = FindNode(TR.Root, TEXT("Hello_psd3"));
	TestTrue(TEXT("a type layer is a Text"), Text != nullptr && Text->Type == TEXT("Text"));
	TestEqual(TEXT("its string, quoted and escaped"), ValueOf(Text, TEXT("Text")), FString(TEXT("\"Hello \\\"world\\\"\"")));
	TestEqual(TEXT("its face by family"), ValueOf(Text, TEXT("Font")), FString(TEXT("\"/Game/Fonts/F_Roboto.F_Roboto\"")));
	TestEqual(TEXT("its size in pixels"), ValueOf(Text, TEXT("FontSize")), FString(TEXT("28")));
	TestEqual(TEXT("its colour, at its fill opacity"), ValueOf(Text, TEXT("Color")), FString(TEXT("#FF000080")));
	TestEqual(TEXT("centred"), ValueOf(Text, TEXT("HAlign")), FString(TEXT("Center")));
	TestTrue(TEXT("a face the map does not name is listed, and the text takes the default"), TR.UnmappedFonts.Num() == 0);
	TextOptions.FontMap.Reset();
	TextOptions.DefaultFontPath = TEXT("/Game/Fonts/F_Default.F_Default");
	FDreamPsdMapper Unmapped(TextRead, TextOptions);
	FDreamPsdMapResult UR;
	Unmapped.Map(UR);
	TestTrue(TEXT("...when it is not mapped"), UR.UnmappedFonts.Contains(TEXT("Roboto-Bold")) && ValueOf(FindNode(UR.Root, TEXT("Hello_psd3")), TEXT("Font")) == TEXT("\"/Game/Fonts/F_Default.F_Default\""));
	TextOptions.bRasterizeText = true;
	FDreamPsdMapper Rasterized(TextRead, TextOptions);
	FDreamPsdMapResult RR;
	Rasterized.Map(RR);
	TestEqual(TEXT("rasterized, it is an image of its pixels"), FindNode(RR.Root, TEXT("Hello_psd3")) != nullptr ? FindNode(RR.Root, TEXT("Hello_psd3"))->Type : FString(), FString(TEXT("Image")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdMapMasksTest,
	"DreamGUI.Psd.MasksMapToClipMasks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdMapMasksTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	ReadBack(MakeGallery(), Read, Messages);
	FDreamPsdMapper Mapper(Read, Options(TEXT("Gallery")));
	FDreamPsdMapResult R;
	Mapper.Map(R);
	// A pixel mask with a default of black: a texture mask over the mask's own rectangle.
	const FDreamPsdDuiNode* Masked = FindNode(R.Root, TEXT("Masked_psd6"));
	TestTrue(TEXT("a pixel mask is a texture clip mask"), ValueOf(Masked, TEXT("Clipping")) == TEXT("ClipToBounds") && ValueOf(Masked, TEXT("ClipMask.Source")) == TEXT("Texture")
		&& ValueOf(Masked, TEXT("ClipMask.Channel")) == TEXT("Red"));
	const FDreamPsdTextureRequest* MaskTexture = R.FindTexture(TEXT("/Temp/DreamGUITests/Psd/Gallery/TM_Gallery_Masked_6.TM_Gallery_Masked_6"));
	TestTrue(TEXT("...a one-channel texture of its rectangle"), MaskTexture != nullptr && MaskTexture->bGray && MaskTexture->Gray.Width == 90 && MaskTexture->Gray.Height == 52);
	TestEqual(TEXT("...which is the layer's, so no margin"), ValueOf(Masked, TEXT("ClippingMargin")), FString(TEXT("<none>")));
	// The fill's mask covers only the top band: the clip rectangle is the band, by a margin.
	const FDreamPsdDuiNode* Tint = FindNode(R.Root, TEXT("Tint_psd15"));
	TestEqual(TEXT("a mask smaller than its layer narrows the clip by a margin"), ValueOf(Tint, TEXT("ClippingMargin")), FString(TEXT("(0, 0, 0, -168)")));
	// A live ellipse on a pass-through group: a shape mask on the container, no texture.
	const FDreamPsdDuiNode* Frame = FindNode(R.Root, TEXT("Frame_psd13"));
	TestTrue(TEXT("a vector mask that is a live ellipse is an elliptic clip mask"), ValueOf(Frame, TEXT("ClipMask.Shape")) == TEXT("Ellipse") && ValueOf(Frame, TEXT("ClipMask.Source")) == TEXT("<none>"));
	TestEqual(TEXT("...over the ellipse's box"), ValueOf(Frame, TEXT("ClippingMargin")), FString(TEXT("(-96, -8, -104, -128)")));

	// A rounded-rectangle vector mask on a pixel layer: the clip's corner radius. A path that is not a shape: a texture.
	FDreamPsdWriteDocument Doc;
	Doc.Width = 100;
	Doc.Height = 100;
	FDreamPsdWriteLayer Rounded = PixelLayer(TEXT("Rounded"), 1, FIntRect(0, 0, 100, 100), [](int32, int32) { return FColor::White; });
	Rounded.bVectorMask = true;
	Rounded.VectorPath = RoundedRectPath(FBox2D(FVector2D(10, 20), FVector2D(90, 80)), 8.0f);
	Rounded.LiveShape.Kind = EDreamPsdLiveShapeKind::RoundedRectangle;
	Rounded.LiveShape.Box = FBox2D(FVector2D(10, 20), FVector2D(90, 80));
	Rounded.LiveShape.Radii = FVector4(8, 4, 2, 1);
	FDreamPsdWriteLayer Star = PixelLayer(TEXT("Star"), 2, FIntRect(0, 0, 100, 100), [](int32, int32) { return FColor::White; });
	Star.bVectorMask = true;
	Star.VectorPath = EllipsePath(FBox2D(FVector2D(30, 30), FVector2D(70, 60)));
	Doc.Layers = { Rounded, Star };
	FDreamPsdDocument Read2;
	ReadBack(Doc, Read2, Messages);
	FDreamPsdMapper Mapper2(Read2, Options(TEXT("Masks")));
	FDreamPsdMapResult R2;
	Mapper2.Map(R2);
	const FDreamPsdDuiNode* RoundedNode = FindNode(R2.Root, TEXT("Rounded_psd1"));
	const FString Corners = FString::Printf(TEXT("%s %s %s %s"), *ValueOf(RoundedNode, TEXT("ClippingCornerRadius.X")), *ValueOf(RoundedNode, TEXT("ClippingCornerRadius.Y")),
		*ValueOf(RoundedNode, TEXT("ClippingCornerRadius.Z")), *ValueOf(RoundedNode, TEXT("ClippingCornerRadius.W")));
	TestEqual(TEXT("its radii, right bottom, right top, left top, left bottom"), Corners, FString(TEXT("2 4 8 1")));
	TestEqual(TEXT("...over its box"), ValueOf(RoundedNode, TEXT("ClippingMargin")), FString(TEXT("(-10, -20, -10, -20)")));
	const FDreamPsdDuiNode* StarNode = FindNode(R2.Root, TEXT("Star_psd2"));
	TestTrue(FString::Printf(TEXT("a path with no live shape is a texture mask over its bounds (%s, %s)"), *ValueOf(StarNode, TEXT("ClipMask.Source")),
		*ValueOf(StarNode, TEXT("ClippingMargin"))), ValueOf(StarNode, TEXT("ClipMask.Source")) == TEXT("Texture")
		&& ValueOf(StarNode, TEXT("ClippingMargin")) == TEXT("(-30, -30, -30, -40)"));
	// Baked: no clip mask, the mask in the texture's alpha.
	FDreamPsdMapOptions Baked = Options(TEXT("Baked"));
	Baked.bBakeLayerMasks = true;
	FDreamPsdMapper Mapper3(Read, Baked);
	FDreamPsdMapResult R3;
	Mapper3.Map(R3);
	const FDreamPsdTextureRequest* BakedTexture = R3.FindTexture(TEXT("/Temp/DreamGUITests/Psd/Baked/T_Baked_Masked_6.T_Baked_Masked_6"));
	TestTrue(TEXT("baking puts the mask in the texture instead"), ValueOf(FindNode(R3.Root, TEXT("Masked_psd6")), TEXT("ClipMask.Source")) == TEXT("<none>")
		&& BakedTexture != nullptr && BakedTexture->Image.At(89, 10).A < 10 && BakedTexture->Image.At(10, 10).A == 255);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdBlendKeysTest,
	"DreamGUI.Psd.EveryBlendKeyNamesABlendMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdBlendKeysTest::RunTest(const FString& Parameters)
{
	const TCHAR* Keys[] = {
		TEXT("norm"), TEXT("diss"), TEXT("dark"), TEXT("mul "), TEXT("idiv"), TEXT("lbrn"), TEXT("dkCl"), TEXT("lite"), TEXT("scrn"), TEXT("div "),
		TEXT("lddg"), TEXT("lgCl"), TEXT("over"), TEXT("sLit"), TEXT("hLit"), TEXT("vLit"), TEXT("lLit"), TEXT("pLit"), TEXT("hMix"), TEXT("diff"),
		TEXT("smud"), TEXT("fsub"), TEXT("fdiv"), TEXT("hue "), TEXT("sat "), TEXT("colr"), TEXT("lum "),
	};
	const UEnum* Enum = StaticEnum<EDreamUIBlendMode>();
	TSet<FString> Seen;
	for (const TCHAR* Key : Keys)
	{
		const FString Name = FDreamPsdMapper::BlendModeName(Key);
		TestTrue(FString::Printf(TEXT("'%s' is %s, a blend mode there is"), Key, *Name), Enum->GetValueByNameString(Name) != INDEX_NONE);
		Seen.Add(Name);
	}
	TestEqual(TEXT("the 27 keys are 27 modes"), Seen.Num(), 27);
	TestTrue(TEXT("pass-through is no mode of an element's"), FDreamPsdMapper::BlendModeName(TEXT("pass")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdDuiBuildsTest,
	"DreamGUI.Psd.TheGeneratedDuiParsesAndBuilds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdDuiBuildsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	ReadBack(MakeGallery(), Read, Messages);
	FDreamPsdMapper Mapper(Read, Options(TEXT("Build")));
	FDreamPsdMapResult R;
	Mapper.Map(R);
	DreamPsdImporter::MakeTextures(R.Textures, false, Messages);
	const FString Text = DreamPsdDui::Print(R.Root, FString(), TEXT("Gallery.psd"));
	FDreamUIDiagnosticBag Diagnostics;
	FDreamUIAst Ast;
	const bool bParsed = FDreamUISourceFile::Parse(Text, TEXT("Gallery.dui"), Ast, Diagnostics);
	TArray<FDreamWidgetPropertyBinding> Bindings;
	TStrongObjectPtr<UDreamWidgetTree> Tree(bParsed ? FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings) : nullptr);
	if (!TestTrue(FString::Printf(TEXT("the .dui an import prints builds with no error and no warning: %s\n%s"), *Diagnostics.ToString(), *Text),
		Tree.IsValid() && Diagnostics.Diagnostics.Num() == 0))
	{
		return false;
	}
	UDreamWidget* Root = Tree->RootWidget.Get();
	UDreamWidget* Disc = FindWidget(Root, TEXT("Disc_psd2"));
	UDreamImage* DiscImage = Disc != nullptr ? Cast<UDreamImage>(Disc->GetVisual()) : nullptr;
	TestTrue(TEXT("the disc is an image of its texture"), DiscImage != nullptr && Cast<UTexture2D>(DiscImage->GetBrush().GetResourceObject()) != nullptr);
	UDreamWidget* Stripes = FindWidget(Root, TEXT("Stripes_psd3"));
	TestTrue(TEXT("the stripes clip to the layer below"), Stripes != nullptr && Stripes->GetClipToBelow());
	UDreamWidget* Masked = FindWidget(Root, TEXT("Masked_psd6"));
	TestTrue(TEXT("the masked layer's clip mask reads its texture"), Masked != nullptr && Masked->GetClipMask().Source == EDreamClipMaskSource::Texture && Masked->GetClipMask().Texture != nullptr);
	UDreamWidget* Panel = FindWidget(Root, TEXT("Panel_psd10"));
	const TArray<UDreamUIBehaviour*> Groups = Panel != nullptr ? Panel->GetComponents(UDreamLayerGroup::StaticClass()) : TArray<UDreamUIBehaviour*>();
	const UDreamLayerGroup* Group = Groups.Num() > 0 ? Cast<UDreamLayerGroup>(Groups[0]) : nullptr;
	TestTrue(TEXT("the panel carries a layer group at 60 %"), Group != nullptr && FMath::IsNearlyEqual(Group->GetGroupOpacity(), 0.6f));
	UDreamWidget* Frame = FindWidget(Root, TEXT("Frame_psd13"));
	TestTrue(TEXT("the frame's clip mask is an ellipse"), Frame != nullptr && Frame->GetClipMask().Shape == EDreamClipMaskShape::Ellipse);
	UDreamWidget* Pill = FindWidget(Root, TEXT("Pill_psd14"));
	UDreamRectBlock* Block = Pill != nullptr ? Cast<UDreamRectBlock>(Pill->GetVisual()) : nullptr;
	TestTrue(TEXT("the pill is a block with its radii"), Block != nullptr && Block->GetCornerRadius() == FVector4f(12, 12, 12, 12) && Block->GetBodyColor() == FColor(150, 90, 220, 255));

	// Text and controls build too.
	FDreamPsdDuiNode TextRoot = R.Root;
	TextRoot.Children.Reset();
	FDreamPsdDuiNode& T = TextRoot.Children.AddDefaulted_GetRef();
	T.Type = TEXT("Text");
	T.Id = TEXT("Title_psd1");
	T.LayerId = 1;
	T.Set(TEXT("Text"), DreamPsdDui::String(TEXT("Line one\nline two")));
	T.Set(TEXT("FontSize"), TEXT("24"));
	T.Set(TEXT("Color"), TEXT("#FF0000FF"));
	T.Set(TEXT("HAlign"), TEXT("Center"));
	T.Set(TEXT("VAlign"), TEXT("Middle"));
	T.Set(TEXT("OverflowType"), TEXT("HorizontalOverflow"));
	T.Set(TEXT("FontSpace"), TEXT("(1.2, 0)"));
	T.Set(TEXT("FontStyle"), TEXT("Italic"));
	T.Set(TEXT("bUnderline"), TEXT("true"));
	T.AddComponent(TEXT("UIButton"));
	FDreamPsdDuiNode& B = TextRoot.Children.AddDefaulted_GetRef();
	B.Type = TEXT("Image");
	B.Id = TEXT("Toggle_psd2");
	B.LayerId = 2;
	B.Set(TEXT("Brush.DrawAs"), TEXT("Box"));
	B.Set(TEXT("Brush.Margin"), TEXT("(0.1, 0.2, 0.1, 0.2)"));
	B.Set(TEXT("Brush.TintColor"), TEXT("#FFFFFF80"));
	B.AddComponent(TEXT("UIToggle"));
	FDreamPsdDuiComponent& G = B.AddComponent(TEXT("DreamLayerGroup"));
	G.Properties.Add({ TEXT("Isolation"), TEXT("Isolated") });
	G.Properties.Add({ TEXT("GroupBlendMode"), TEXT("Screen") });
	G.Properties.Add({ TEXT("GroupMask.Shape"), TEXT("Ellipse") });
	const FString Text2 = DreamPsdDui::Print(TextRoot, FString(), TEXT("Text.psd"));
	FDreamUIDiagnosticBag Diagnostics2;
	FDreamUIAst Ast2;
	TArray<FDreamWidgetPropertyBinding> Bindings2;
	const bool bParsed2 = FDreamUISourceFile::Parse(Text2, TEXT("Text.dui"), Ast2, Diagnostics2);
	TStrongObjectPtr<UDreamWidgetTree> Tree2(bParsed2 ? FDreamUITextBuilder::Build(Ast2, GetTransientPackage(), Diagnostics2, Bindings2) : nullptr);
	TestTrue(FString::Printf(TEXT("a text, a control and a group's mask build: %s"), *Diagnostics2.ToString()), Tree2.IsValid() && Diagnostics2.Diagnostics.Num() == 0);
	UDreamWidget* Title = Tree2.IsValid() ? FindWidget(Tree2->RootWidget.Get(), TEXT("Title_psd1")) : nullptr;
	UDreamText* TitleText = Title != nullptr ? Cast<UDreamText>(Title->GetVisual()) : nullptr;
	TestTrue(TEXT("the text says its two lines at its size"), TitleText != nullptr && TitleText->GetText().ToString() == TEXT("Line one\nline two") && TitleText->GetFontSize() == 24.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdReimportTest,
	"DreamGUI.Psd.AReimportKeepsWhatTheAuthorAdded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdReimportTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	auto Square = [](const TCHAR* InName, int32 InId, int32 InX)
	{
		return PixelLayer(InName, InId, FIntRect(InX, 10, InX + 20, 30), [](int32, int32) { return FColor::White; });
	};
	// First: Background, a group Card { Title, Icon }, a Badge.
	FDreamPsdWriteDocument V1;
	V1.Width = 200;
	V1.Height = 100;
	V1.Layers = { Square(TEXT("Background"), 1, 0), GroupDivider(2), Square(TEXT("Title"), 4, 30), Square(TEXT("Icon"), 5, 60), GroupRecord(TEXT("Card"), 6), Square(TEXT("Badge"), 7, 90) };
	FDreamPsdDocument Read1;
	FDreamPsdMessages Messages;
	ReadBack(V1, Read1, Messages);
	FDreamPsdMapper Mapper1(Read1, Options(TEXT("Card")));
	FDreamPsdMapResult R1;
	Mapper1.Map(R1);
	FString Text = DreamPsdDui::Print(R1.Root, TEXT("/Game/UI/WBP_Card"), TEXT("Card.psd"));
	const FDreamPsdImportManifest Manifest = FDreamPsdImportManifest::FromTree(R1.Root);

	// The author: a node of their own in the card, a line of their own on the title and an edit of one of the
	// importer's, a node of their own inside the badge, a comment.
	Text.ReplaceInline(TEXT("    Widget Card_psd6 {\n"), TEXT("    Widget Card_psd6 {\n        Text MyLabel { Text = \"mine\" }\n"));
	Text.ReplaceInline(TEXT("        Image Title_psd4 {\n"), TEXT("        Image Title_psd4 {\n            // the heading\n            ToolTipText = \"Hello\"\n"));
	Text.ReplaceInline(TEXT("AnchoredPosition = (30, -10)"), TEXT("AnchoredPosition = (31, -10)"));
	Text.ReplaceInline(TEXT("    Image Badge_psd7 {\n"), TEXT("    Image Badge_psd7 {\n        Widget Extra { }\n"));
	{
		FDreamUIAst Check;
		FDreamUIDiagnosticBag Diagnostics;
		if (!TestTrue(FString::Printf(TEXT("the author's edits parse: %s\n%s"), *Diagnostics.ToString(), *Text), FDreamUISourceFile::Parse(Text, TEXT("Card.dui"), Check, Diagnostics)))
		{
			return false;
		}
	}

	// Second: the title renamed Heading, the icon and the badge gone, a Glow added under the card, the background at 50 %.
	FDreamPsdWriteDocument V2 = V1;
	V2.Layers = { Square(TEXT("Background"), 1, 0), Square(TEXT("Glow"), 8, 120), GroupDivider(2), Square(TEXT("Heading"), 4, 30), GroupRecord(TEXT("Card"), 6) };
	V2.Layers[0].Opacity = 128;
	FDreamPsdDocument Read2;
	ReadBack(V2, Read2, Messages);
	FDreamPsdMapper Mapper2(Read2, Options(TEXT("Card")));
	FDreamPsdMapResult R2;
	Mapper2.Map(R2);
	FString Merged;
	FDreamPsdMergeReport Report;
	const bool bMerged = DreamPsdDuiMerge::Merge(Text, &Manifest, R2.Root, Merged, Report);
	if (!TestTrue(FString::Printf(TEXT("the reimport merges (%s)"), *FString::Join(Report.Errors, TEXT("; "))), bMerged))
	{
		return false;
	}
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(FString::Printf(TEXT("the merged file parses: %s\n%s"), *Diagnostics.ToString(), *Merged), FDreamUISourceFile::Parse(Merged, TEXT("Card.dui"), Ast, Diagnostics)))
	{
		return false;
	}
	const FDreamUINode* Heading = FindAstNode(Ast.Root, TEXT("Heading_psd4"));
	TestTrue(TEXT("the renamed layer's node is renamed, saying what it was"), Heading != nullptr && Heading->WasId == TEXT("Title_psd4"));
	TestTrue(TEXT("...and keeps the author's line and comment"), Heading != nullptr && Heading->Properties.ContainsByPredicate([](const FDreamUIProperty& P) { return P.Name == TEXT("ToolTipText"); })
		&& Merged.Contains(TEXT("// the heading")));
	const FDreamUIProperty* Position = Heading != nullptr ? Heading->Properties.FindByPredicate([](const FDreamUIProperty& P) { return P.Name == TEXT("AnchorData.AnchoredPosition"); }) : nullptr;
	TestTrue(TEXT("the importer's line the author edited is written over"), Position != nullptr && Position->Value.Elements.Num() == 2 && Position->Value.Elements[0] == TEXT("30"));
	TestTrue(FString::Printf(TEXT("...and that is listed (%s)"), *FString::Join(Report.Conflicts, TEXT("; "))),
		Report.Conflicts.ContainsByPredicate([](const FString& C) { return C.Contains(TEXT("AnchorData.AnchoredPosition")); }));
	TestNull(TEXT("the gone icon's node is gone"), FindAstNode(Ast.Root, TEXT("Icon_psd5")));
	TestNull(TEXT("the gone badge's node is gone"), FindAstNode(Ast.Root, TEXT("Badge_psd7")));
	FString ExtraParent;
	TestTrue(TEXT("the author's node inside the badge moved up to the root"), FindAstNode(Ast.Root, TEXT("Extra"), &ExtraParent) != nullptr && ExtraParent == Ast.Root.Id);
	TestTrue(TEXT("...and that is listed"), Report.Conflicts.ContainsByPredicate([](const FString& C) { return C.Contains(TEXT("Extra")); }));
	FString LabelParent;
	TestTrue(TEXT("the author's node in the card stays in the card"), FindAstNode(Ast.Root, TEXT("MyLabel"), &LabelParent) != nullptr && LabelParent == TEXT("Card_psd6"));
	int32 GlowAt = INDEX_NONE, CardAt = INDEX_NONE, BackgroundAt = INDEX_NONE;
	for (int32 i = 0; i < Ast.Root.Children.Num(); ++i)
	{
		const FString& Id = Ast.Root.Children[i].Id;
		GlowAt = Id == TEXT("Glow_psd8") ? i : GlowAt;
		CardAt = Id == TEXT("Card_psd6") ? i : CardAt;
		BackgroundAt = Id == TEXT("Background_psd1") ? i : BackgroundAt;
	}
	TestTrue(FString::Printf(TEXT("the new layer's node is where the layer is: after the background, before the card (%d, %d, %d)"), BackgroundAt, GlowAt, CardAt),
		BackgroundAt != INDEX_NONE && BackgroundAt < GlowAt && GlowAt < CardAt);
	const FDreamUINode* Background = FindAstNode(Ast.Root, TEXT("Background_psd1"));
	const FDreamUIProperty* Opacity = Background != nullptr ? Background->Properties.FindByPredicate([](const FDreamUIProperty& P) { return P.Name == TEXT("RenderOpacity"); }) : nullptr;
	TestTrue(TEXT("a value the layer now has is written"), Opacity != nullptr && Opacity->Value.Raw == TEXT("0.502"));

	// Third: the same document again -- nothing changes.
	FDreamPsdImportManifest Manifest2 = FDreamPsdImportManifest::FromTree(R2.Root);
	FString Again;
	FDreamPsdMergeReport Report2;
	DreamPsdDuiMerge::Merge(Merged, &Manifest2, R2.Root, Again, Report2);
	TestEqual(TEXT("importing the same document again changes nothing"), Again, Merged);
	TestEqual(TEXT("...and lists no conflict"), Report2.Conflicts.Num(), 0);
	// The manifest round-trips.
	FDreamPsdImportManifest Reread;
	TestTrue(TEXT("the manifest reads back"), FDreamPsdImportManifest::FromJson(Manifest2.ToJson(), Reread) && Reread.Nodes.Num() == Manifest2.Nodes.Num()
		&& Reread.Nodes.Contains(TEXT("Heading_psd4")) && Reread.Nodes[TEXT("Heading_psd4")].LayerId == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdPendingTest,
	"DreamGUI.Psd.EffectsAdjustmentsAndWarpsAreListedAsPending",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdPendingTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	using namespace DreamPsdDescriptor;
	FDreamPsdDescriptor Shadow;
	Shadow.ClassId = TEXT("DrSh");
	Shadow.Items.Emplace(TEXT("enab"), MakeBool(true));
	FDreamPsdDescriptor Root;
	Root.ClassId = TEXT("null");
	Root.Items.Emplace(TEXT("masterFXSwitch"), MakeBool(true));
	Root.Items.Emplace(TEXT("DrSh"), MakeDescriptor(Shadow));
	FDreamPsdWriteDocument Doc;
	Doc.Width = 50;
	Doc.Height = 50;
	FDreamPsdWriteLayer Styled = PixelLayer(TEXT("Styled"), 1, FIntRect(0, 0, 10, 10), [](int32, int32) { return FColor::White; });
	Styled.ExtraBlocks.Add(DreamPsdWriter::MakeEffectsBlock(Root));
	FDreamPsdWriteLayer Curves;
	Curves.Name = TEXT("Warm");
	Curves.LayerId = 2;
	Curves.ExtraBlocks.Add(DreamPsdWriter::MakeCurvesBlock({}));
	TSharedPtr<FDreamPsdText> T = MakeShared<FDreamPsdText>();
	T->Text = TEXT("Bent");
	T->Warp.Style = TEXT("warpFlag");
	T->StyleRuns.AddDefaulted();
	FDreamPsdWriteLayer Bent = PixelLayer(TEXT("Bent"), 3, FIntRect(0, 20, 30, 30), [](int32, int32) { return FColor::Black; });
	Bent.Text = T;
	Doc.Layers = { Styled, Curves, Bent };
	FDreamPsdDocument Read;
	FDreamPsdMessages Messages;
	ReadBack(Doc, Read, Messages);
	FDreamPsdMapper Mapper(Read, Options(TEXT("Pending")));
	FDreamPsdMapResult R;
	Mapper.Map(R);
	TestTrue(FString::Printf(TEXT("the effects are listed (%s)"), *FString::Join(R.PendingEffects, TEXT("; "))), R.PendingEffects.Num() == 1 && R.PendingEffects[0].Contains(TEXT("drop shadow")));
	TestTrue(TEXT("...and the layer itself is still an image"), FindNode(R.Root, TEXT("Styled_psd1")) != nullptr);
	TestTrue(FString::Printf(TEXT("the adjustment is listed (%s)"), *FString::Join(R.PendingAdjustments, TEXT("; "))), R.PendingAdjustments.Num() == 1 && R.PendingAdjustments[0].Contains(TEXT("curves")));
	TestNull(TEXT("...and makes no node yet"), FindNode(R.Root, TEXT("Warm_psd2")));
	TestTrue(TEXT("the warp is listed"), R.PendingWarps.Num() == 1 && R.PendingWarps[0].Contains(TEXT("warpFlag")));
	const FDreamPsdDuiNode* BentNode = FindNode(R.Root, TEXT("Bent_psd3"));
	TestTrue(TEXT("...and the warped text is drawn from its pixels"), BentNode != nullptr && BentNode->Type == TEXT("Image"));
	TestTrue(TEXT("the summary says all three"), R.Summarize().Contains(TEXT("effects")) && R.Summarize().Contains(TEXT("Adjustment")) && R.Summarize().Contains(TEXT("Warped")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdImportTest,
	"DreamGUI.Psd.AnImportWritesTheDuiTexturesManifestAndBlueprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPsdImportTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdTestLocal;
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("Psd")));
	const FString PsdFile = FPaths::Combine(Directory, TEXT("ImportGallery.psd"));
	const FString DuiFile = FPaths::Combine(Directory, TEXT("WBP_ImportGallery.dui"));
	IFileManager::Get().Delete(*DuiFile, false, true, true);
	IFileManager::Get().Delete(*FDreamPsdImportManifest::PathFor(DuiFile), false, true, true);
	FDreamPsdWriteDocument Doc = MakeGallery();
	TArray<uint8> Bytes;
	DreamPsdWriter::Write(Doc, Bytes);
	IFileManager::Get().MakeDirectory(*Directory, true);
	FFileHelper::SaveArrayToFile(Bytes, *PsdFile);

	FDreamPsdImportRequest Request;
	Request.SourceFile = PsdFile;
	Request.AssetFolder = TEXT("/Temp/DreamGUITests/PsdImport");
	Request.DuiFile = DuiFile;
	Request.bSave = false;
	Request.MapOptions = Options(TEXT("ImportGallery"));
	FDreamPsdImportReport Report;
	const bool bImported = DreamPsdImporter::Import(Request, Report);
	TestTrue(FString::Printf(TEXT("the import succeeds:\n%s"), *Report.ToString()), bImported);
	TestFalse(TEXT("a first import is not a reimport"), Report.bReimport);
	TestTrue(TEXT("the .dui is written"), FPaths::FileExists(DuiFile));
	TestTrue(TEXT("...with its manifest"), FPaths::FileExists(FDreamPsdImportManifest::PathFor(DuiFile)));
	TestTrue(TEXT("its textures are under the document's folder"), Report.Textures.Num() > 0 && Report.Textures[0].StartsWith(TEXT("/Temp/DreamGUITests/PsdImport/ImportGallery/")));
	UTexture2D* Texture = Report.Textures.Num() > 0 ? LoadObject<UTexture2D>(nullptr, *Report.Textures[0]) : nullptr;
	TestTrue(TEXT("...made, as UI textures"), Texture != nullptr && Texture->LODGroup == TEXTUREGROUP_UI && Texture->MipGenSettings == TMGS_NoMipmaps);
	UObject* Blueprint = LoadObject<UObject>(nullptr, *Report.BlueprintPath);
	TestNotNull(FString::Printf(TEXT("the Blueprint %s is made"), *Report.BlueprintPath), Blueprint);
	TestEqual(FString::Printf(TEXT("...and compiles from the .dui without a word: %s"), *FString::Join(Report.CompileMessages, TEXT("; "))), Report.CompileMessages.Num(), 0);

	// Again: a reimport, which finds nothing to change.
	FString Before;
	FFileHelper::LoadFileToString(Before, *DuiFile);
	FDreamPsdImportReport Again;
	DreamPsdImporter::Import(Request, Again);
	FString After;
	FFileHelper::LoadFileToString(After, *DuiFile);
	TestTrue(TEXT("a second import is a reimport"), Again.bReimport);
	TestEqual(TEXT("...that changes nothing in the file"), After, Before);
	TestEqual(TEXT("...and lists no conflict"), Again.Merge.Conflicts.Num(), 0);
	IFileManager::Get().Delete(*DuiFile, false, true, true);
	IFileManager::Get().Delete(*FDreamPsdImportManifest::PathFor(DuiFile), false, true, true);
	IFileManager::Get().Delete(*PsdFile, false, true, true);
	return true;
}

#endif
