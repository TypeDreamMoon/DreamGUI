// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamBackgroundBlur.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisualDirectMesh.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIDynamicSpriteAtlasData.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUITextData.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Extensions/DreamStaticMesh.h"
#include "TextureResource.h"
#include "DreamSpriteRegistrationTestTypes.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Utils/DreamUIUtils.h"
#include "DreamScopedWorld.h"

/*
 * The render-side paths that are reachable without a picture.
 *
 * The suite runs -nullrhi, so nothing here asserts a pixel. What it does assert is the arithmetic and
 * the bookkeeping that decide WHICH pixels the GPU would be asked for: which sprite a visual is
 * registered with when the brush changes (a repack notification goes to the registration list, and a
 * visual missing from it draws from a stale atlas rectangle forever), how big a full-size post-process
 * claims to be (the batching overlap test and the culling volume are both built from that answer), and
 * whether a CPU-side texture read stays inside its allocation.
 *
 * Three of the defects below were invisible in every existing test because each one fails as a LOOK
 * rather than as a crash or a wrong number: a sprite that keeps drawing yesterday's rectangle, a
 * full-screen blur that is culled because its widget is small, a hit test that reads a neighbouring
 * row of texels. The assertions are therefore on the bookkeeping, which is the last place the truth
 * is still visible from inside the process.
 */

namespace DreamVisualRenderPathTestLocal
{
	using DreamTests::FScopedGameWorld;

	/** A widget owning one visual, in an authoring tree with no world. Torn down by leaving scope. */
	struct FScopedVisualWidget
	{
		UDreamWidgetTree* Tree = nullptr;
		UDreamWidget* Widget = nullptr;

		FScopedVisualWidget(UClass* InVisualClass, const TCHAR* InName)
		{
			Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
			Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), FName(InName));
			if (Widget != nullptr)
			{
				Tree->RootWidget = Widget;
				Widget->SetWidth(120.0f);
				Widget->SetHeight(60.0f);
				if (InVisualClass != nullptr)
				{
					Widget->CreateNewVisual(InVisualClass);
				}
			}
		}
		~FScopedVisualWidget()
		{
			if (Widget != nullptr)
			{
				Widget->DestroyWidget();
			}
		}
		UDreamVisual* GetVisual() const { return Widget != nullptr ? Widget->GetVisual() : nullptr; }
	};

	UDreamSpriteRegistrationProbe* MakeProbe(UTexture2D* InAtlas, uint16 InWidth = 0, uint16 InHeight = 0)
	{
		UDreamSpriteRegistrationProbe* Probe = NewObject<UDreamSpriteRegistrationProbe>(GetTransientPackage());
		Probe->AtlasTexture = InAtlas;
		Probe->SpriteInfo.Width = InWidth;
		Probe->SpriteInfo.Height = InHeight;
		return Probe;
	}

	/** A CPU-readable texture: uncompressed BGRA8 with its top mip filled, and no RHI resource. */
	UTexture2D* MakeReadableTexture(int32 InSize, const FColor& InColor)
	{
		return FDreamUIUtils::CreateTexture(InSize, InColor, GetTransientPackage());
	}

	/**
	 * A word whose float exponent is all zeros and whose mantissa is not: the bit pattern a GPU with
	 * flush-to-zero turns into 0 on the load, before anything gets to read it back as an integer.
	 */
	bool IsDenormalBits(uint32 InBits)
	{
		const uint32 Exponent = (InBits >> 23) & 0xff;
		const uint32 Mantissa = InBits & 0x007fffff;
		return Exponent == 0 && Mantissa != 0;
	}

	/** The atlas page a sprite currently holds, read through reflection: the field is private. */
	UTexture2D* GetPackedAtlasTexture(UDreamUISpriteData* InSprite)
	{
		if (!IsValid(InSprite))return nullptr;
		if (FObjectProperty* Property = CastField<FObjectProperty>(
			UDreamUISpriteData::StaticClass()->FindPropertyByName(TEXT("AtlasTexture"))))
		{
			return Cast<UTexture2D>(Property->GetObjectPropertyValue_InContainer(InSprite));
		}
		return nullptr;
	}

	/**
	 * A texture whose top mip is NOT an FColor array: the shape a compressed UI texture has on disk.
	 * Built by hand rather than imported, because what the reader has to notice is the pixel format
	 * and the mip's size, and those are all this needs to carry.
	 */
	UTexture2D* MakeCompressedTexture(int32 InSize)
	{
		UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		FTexturePlatformData* PlatformData = new FTexturePlatformData();
		PlatformData->SizeX = InSize;
		PlatformData->SizeY = InSize;
		PlatformData->PixelFormat = PF_DXT1;
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		PlatformData->Mips.Add(Mip);
		Mip->SizeX = InSize;
		Mip->SizeY = InSize;
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		//a BC1 block is 8 bytes per 4x4, which is exactly the point: it is nothing like InSize^2 FColors
		const int32 BlockCount = FMath::Max(1, InSize / 4) * FMath::Max(1, InSize / 4);
		void* Data = Mip->BulkData.Realloc(BlockCount * 8);
		FMemory::Memzero(Data, BlockCount * 8);
		Mip->BulkData.Unlock();
		Texture->SetPlatformData(PlatformData);
		return Texture;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockSpriteHandOverTest,
	"DreamGUI.Render.ARectBlockUnregistersFromTheSpriteItLeavesAndRegistersWithTheOneItTakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockSpriteHandOverTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// SetBodySpriteTexture assigned the new sprite to the member BEFORE comparing, so every
	// comparison below it was the new sprite against itself: same atlas, therefore "nothing to do".
	// The consequence is not a wrong picture on that frame -- the UVs are re-read either way -- but a
	// registration list that no longer matches reality. The atlas broadcasts a repack to the list, so
	// this element stops hearing about repacks of the atlas it now draws from, and goes on sampling
	// the rectangle its old sprite used to occupy.
	FScopedVisualWidget Fixture(UDreamRectBlock::StaticClass(), TEXT("Block"));
	UDreamRectBlock* Block = Cast<UDreamRectBlock>(Fixture.GetVisual());
	if (!TestNotNull(TEXT("the rect block exists on a widget"), Block))
	{
		return false;
	}

	UTexture2D* AtlasOne = MakeReadableTexture(4, FColor::White);
	UTexture2D* AtlasTwo = MakeReadableTexture(4, FColor::White);
	UDreamSpriteRegistrationProbe* First = MakeProbe(AtlasOne);
	UDreamSpriteRegistrationProbe* Second = MakeProbe(AtlasTwo);

	Block->SetBodySpriteTexture(First);
	TestEqual(TEXT("taking a sprite registers with it"), First->AddCount, 1);
	TestEqual(TEXT("and the element is what registered"), (UObject*)First->LastAdded, (UObject*)Block);
	TestEqual(TEXT("the block holds the sprite it was given"),
		Block->GetBodySpriteTexture(), (UDreamUISpriteData_BaseObject*)First);

	// The hand-over. Two different atlas pages, which is the case the comparison was there to detect
	// and the case it could no longer see.
	Block->SetBodySpriteTexture(Second);
	TestEqual(TEXT("swapping sprites unregisters from the old one"), First->RemoveCount, 1);
	TestEqual(TEXT("and it is this element that left"), (UObject*)First->LastRemoved, (UObject*)Block);
	TestEqual(TEXT("and registers with the new one"), Second->AddCount, 1);
	TestEqual(TEXT("exactly once"), First->AddCount, 1);
	TestEqual(TEXT("the block holds the new sprite"),
		Block->GetBodySpriteTexture(), (UDreamUISpriteData_BaseObject*)Second);

	// Clearing the sprite has to let go as well. This is the same defect seen from the other side:
	// the old code assigned null first, so the "remove from old" branch had nothing left to remove
	// and the element stayed on the atlas's list after it had stopped drawing from it.
	Block->SetBodySpriteTexture(nullptr);
	TestEqual(TEXT("clearing the sprite unregisters from it"), Second->RemoveCount, 1);
	TestNull(TEXT("and the block holds nothing"), Block->GetBodySpriteTexture());

	// And setting the same sprite twice is not a hand-over at all.
	Block->SetBodySpriteTexture(First);
	Block->SetBodySpriteTexture(First);
	TestEqual(TEXT("re-assigning the same sprite registers once, not twice"), First->AddCount, 2);
	TestEqual(TEXT("and does not unregister from itself"), First->RemoveCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamImageSpriteHandOverTest,
	"DreamGUI.Render.AnImageHandsItsSpriteRegistrationOverEvenWhenTheTwoSpritesShareAnAtlas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamImageSpriteHandOverTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// SetBrush_DreamUISprite skipped the whole hand-over when the two sprites reported the same atlas
	// texture, on the theory that the registration is really about the atlas. It is not: the list is
	// keyed on the sprite data -- its packing tag, or its packing atlas asset -- and two sprites can
	// answer with the same page today and belong to different tags. The same branch also left
	// bHasAddToSprite alone, and that flag is what the geometry build reads to decide whether to take
	// UVs from the sprite or to fall back to the brush's own size, so an element that was never
	// registered kept drawing the whole atlas page instead of its own rectangle.
	FScopedVisualWidget Fixture(UDreamImage::StaticClass(), TEXT("Img"));
	UDreamImage* Image = Cast<UDreamImage>(Fixture.GetVisual());
	if (!TestNotNull(TEXT("the image exists on a widget"), Image))
	{
		return false;
	}

	// Start from a brush holding no sprite, so the constructor's default sprite is not in the way.
	Image->SetBrush_Texture(nullptr);

	// Both probes answer with the same atlas texture -- null, which is what an unpacked sprite
	// answers -- so this is exactly the case the old code took as "nothing to do".
	UDreamSpriteRegistrationProbe* First = MakeProbe(nullptr, 64, 48);
	UDreamSpriteRegistrationProbe* Second = MakeProbe(nullptr, 20, 10);

	Image->SetBrush_DreamUISprite(First);
	TestEqual(TEXT("taking a sprite registers with it"), First->AddCount, 1);

	Image->SetBrush_DreamUISprite(Second);
	TestEqual(TEXT("a shared atlas does not excuse the element from unregistering"), First->RemoveCount, 1);
	TestEqual(TEXT("nor from registering with the sprite it now draws"), Second->AddCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamImagePreferredSizeTest,
	"DreamGUI.Render.AnImageOverASpriteMeasuresAtTheSpritesAuthoredSizeRatherThanTheBrushDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamImagePreferredSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// GetPreferredWidth/Height answered Brush.ImageSize whatever the brush held. For a material or a
	// texture the brush deliberately draws at another size that IS the answer; for a sprite it is
	// the field nobody touched when the sprite was assigned, so every sprite in an Auto slot measured
	// at the brush default (32x32) with complete confidence.
	FScopedVisualWidget Fixture(UDreamImage::StaticClass(), TEXT("Img"));
	UDreamImage* Image = Cast<UDreamImage>(Fixture.GetVisual());
	if (!TestNotNull(TEXT("the image exists on a widget"), Image))
	{
		return false;
	}
	Image->SetBrush_Texture(nullptr);

	// A brush with no sprite still answers with its own size, which is the contract for a texture or
	// a material and the reason this is a fallback rather than a replacement.
	TestEqual(TEXT("a brush over no sprite measures at the brush size"),
		Image->GetPreferredWidth(), Image->GetBrush().ImageSize.X);

	UDreamSpriteRegistrationProbe* Probe = MakeProbe(nullptr, 64, 48);
	if (!TestNotEqual(TEXT("the sprite's size differs from the brush default, or this proves nothing"),
		(float)Probe->SpriteInfo.GetSourceWidth(), Image->GetBrush().ImageSize.X))
	{
		return false;
	}

	Image->SetBrush_DreamUISprite(Probe);
	TestEqual(TEXT("a brush over a sprite measures at the sprite's authored width"),
		Image->GetPreferredWidth(), 64.0f);
	TestEqual(TEXT("and its authored height"), Image->GetPreferredHeight(), 48.0f);

	// A sprite that has not been packed yet reports nothing, and nothing is an abstention rather than
	// a zero: the layout falls back to the authored rect instead of collapsing the element.
	UDreamSpriteRegistrationProbe* Unpacked = MakeProbe(nullptr, 0, 0);
	Image->SetBrush_DreamUISprite(Unpacked);
	TestEqual(TEXT("an unpacked sprite abstains and the brush size answers again"),
		Image->GetPreferredWidth(), Image->GetBrush().ImageSize.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPostProcessFullSizeBoundsTest,
	"DreamGUI.Render.AFullSizePostProcessIsMeasuredByTheRootCanvasRectItIsDrawnAt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPostProcessFullSizeBoundsTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// bUseFullSize builds the effect's quad from the ROOT CANVAS's rect while keeping this widget's
	// transform, and the bounds kept answering with the widget's own rect. Those bounds are what the
	// canvas overlap test batches by and what the mesh component's culling volume is built from, so a
	// full-screen blur behind a 40x20 widget was sorted and culled as if it covered 40x20 -- it
	// disappeared when the little widget left the frustum, and drew in the wrong order against
	// everything it actually overlapped.
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Root->SetDisplayName(TEXT("CanvasRoot"));
	Root->SetWidth(1600.0f);
	Root->SetHeight(900.0f);
	Root->OnRegister();
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("the root widget carries a canvas"), Canvas))
	{
		return false;
	}

	UDreamWidget* Child = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Child->SetDisplayName(TEXT("Blur"));
	Child->OnRegister();
	Child->TrySetParent(Root, false);
	// Sized after attaching and relative to the root, so the two rects cannot accidentally agree --
	// the canvas may re-size its own root when it registers, and a test that passed because both
	// rects were 2x2 would prove nothing.
	Child->SetWidth(Root->GetWidth() + 37.0f);
	Child->SetHeight(Root->GetHeight() + 19.0f);
	Child->CreateNewVisual(UDreamBackgroundBlur::StaticClass());

	UDreamVisualPostProcess* Blur = Cast<UDreamVisualPostProcess>(Child->GetVisual());
	if (!TestNotNull(TEXT("the blur visual exists"), Blur))
	{
		Root->DestroyWidget();
		return false;
	}

	FVector2D Min, Max;
	Blur->GetGeometryBoundsInLocalSpace(Min, Max);
	// The SPAN rather than the corners, so the assertion does not depend on where the pivot puts the
	// rect around the origin.
	TestEqual(TEXT("an ordinary post-process is measured by its own rect"), (float)(Max.X - Min.X), Child->GetWidth());
	TestEqual(TEXT("on both axes"), (float)(Max.Y - Min.Y), Child->GetHeight());

	Blur->SetUseFullSize(true);
	if (!TestNotNull(TEXT("the child resolved a render canvas"), Child->GetRenderCanvas()))
	{
		Root->DestroyWidget();
		return false;
	}
	UDreamWidget* SizeSource = Blur->GetSizeSourceWidget();
	TestEqual(TEXT("a full-size effect takes its rect from the root canvas's widget"), SizeSource, Root);

	Blur->GetGeometryBoundsInLocalSpace(Min, Max);
	TestEqual(TEXT("and is measured by that rect, not by its own"), (float)(Max.X - Min.X), Root->GetWidth());
	TestEqual(TEXT("on both axes"), (float)(Max.Y - Min.Y), Root->GetHeight());
	if (!TestNotEqual(TEXT("the two rects really do differ, or the assertion above proves nothing"),
		Root->GetWidth(), Child->GetWidth()))
	{
		Root->DestroyWidget();
		return false;
	}

	// The 3D bounds are what the mesh component's culling volume is built from, and they carry the
	// same rect in the (depth, across, up) order the base class documents.
	FVector Min3D, Max3D;
	Blur->GetGeometryBounds3DInLocalSpace(Min3D, Max3D);
	TestEqual(TEXT("the 3D bounds carry the same span across"), (float)(Max3D.Y - Min3D.Y), Root->GetWidth());
	TestEqual(TEXT("and up"), (float)(Max3D.Z - Min3D.Z), Root->GetHeight());

	Blur->SetUseFullSize(false);
	Blur->GetGeometryBoundsInLocalSpace(Min, Max);
	TestEqual(TEXT("turning it off returns the measurement to the element's own rect"),
		(float)(Max.X - Min.X), Child->GetWidth());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTexturePixelReadTest,
	"DreamGUI.Render.APixelReadStaysInsideTheTextureAndRefusesTheFormatsItCannotInterpret",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTexturePixelReadTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// The pixel-accurate hit test used to cast any top mip to FColor* and index it with an unclamped
	// UV. Both halves are wrong in a way that never crashes on the desktop: a compressed texture is a
	// quarter the size it is read as, so the read leaves the allocation somewhere down the image, and
	// UV 1.0 at the far edge indexes one whole row past the end even for an uncompressed one. What
	// comes back is another texture's memory, interpreted as a colour, and used to decide whether the
	// pointer hit the element.
	UTexture2D* Readable = MakeReadableTexture(8, FColor(10, 20, 30, 40));
	if (!TestNotNull(TEXT("the fixture texture exists"), Readable))
	{
		return false;
	}

	FColor Pixel = FColor::Black;
	TestTrue(TEXT("an uncompressed BGRA8 texture can be read"),
		FDreamUIUtils::ReadTexture2DPixel(Readable, FVector2D(0.5, 0.5), Pixel));
	TestEqual(TEXT("and it answers with what is in it"), Pixel, FColor(10, 20, 30, 40));

	// The far edge, which is where the old arithmetic left the allocation.
	Pixel = FColor::Black;
	TestTrue(TEXT("the far edge of the texture is still inside it"),
		FDreamUIUtils::ReadTexture2DPixel(Readable, FVector2D(1.0, 1.0), Pixel));
	TestEqual(TEXT("and reads the last texel rather than the first of whatever is next"),
		Pixel, FColor(10, 20, 30, 40));

	// And past both ends, because a UV comes from barycentric interpolation and is not bounded by
	// anything on the way in.
	Pixel = FColor::Black;
	TestTrue(TEXT("a UV past the end is clamped rather than followed"),
		FDreamUIUtils::ReadTexture2DPixel(Readable, FVector2D(4.0, -3.0), Pixel));
	TestEqual(TEXT("and still answers with this texture's colour"), Pixel, FColor(10, 20, 30, 40));

	// A format whose mip is not an FColor array at all. The answer has to be "cannot know", so that
	// the raycast falls back to its rectangle instead of testing against a reinterpreted BC1 block.
	AddExpectedMessage(TEXT("only PF_B8G8R8A8 can be read on the CPU"),
		ELogVerbosity::Warning, EAutomationExpectedErrorFlags::Contains, -1);
	UTexture2D* Compressed = MakeCompressedTexture(8);
	FColor Untouched = FColor(1, 2, 3, 4);
	TestFalse(TEXT("a compressed texture is refused rather than reinterpreted"),
		FDreamUIUtils::ReadTexture2DPixel(Compressed, FVector2D(0.5, 0.5), Untouched));
	TestEqual(TEXT("and the caller's colour is left alone"), Untouched, FColor(1, 2, 3, 4));

	// Null is what a brush with nothing in it hands over.
	TestFalse(TEXT("nothing at all is refused"),
		FDreamUIUtils::ReadTexture2DPixel(nullptr, FVector2D(0.5, 0.5), Untouched));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDynamicAtlasTrimTest,
	"DreamGUI.Render.ADynamicAtlasCanGiveItsPagesBackAndTheSpritesInItGoBackToUnpacked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDynamicAtlasTrimTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// The dynamic atlas only ever grew: the bin pack hands out a rectangle and never takes one back,
	// a page is added when the last one is full, and nothing but a process-wide reset released any of
	// it. A long session that shows many different sprites under one tag therefore climbs in video
	// memory and stays there. Rect-level eviction is still not implemented -- see the ledger -- but
	// the page set is now owned by the atlas entry rather than by the root set, so releasing the
	// entry's pages is enough to free them, and there is a call that does it.
	{
		FDreamUIDynamicSpriteAtlasData Data;
		Data.PackingTag = TEXT("DreamGUITest_Trim");
		Data.AtlasTextureArray.Add(MakeReadableTexture(4, FColor::Transparent));
		Data.AtlasBinPackArray.Add(rbp::MaxRectsBinPack(4, 4));

		Data.ReleaseAtlasTextures();
		TestEqual(TEXT("releasing an atlas drops its pages"), Data.AtlasTextureArray.Num(), 0);
		TestEqual(TEXT("and the bin packs that described them"), Data.AtlasBinPackArray.Num(), 0);
		TestEqual(TEXT("and the list of what was packed into them"), Data.SpriteDataArray.Num(), 0);
	}

	// The manager-level entry point, which is the one an application calls at a level transition. A
	// tag with no pages has nothing to give back, and that is a false rather than a no-op that claims
	// to have freed something.
	const FName TestTag = TEXT("DreamGUITest_TrimTag");
	TestFalse(TEXT("a tag nobody has ever used cannot be trimmed"),
		UDreamUIDynamicSpriteAtlasManager::TrimAtlasByPackingTag(TestTag));

	FDreamUIDynamicSpriteAtlasData* Entry = UDreamUIDynamicSpriteAtlasManager::FindOrAdd(TestTag);
	if (!TestNotNull(TEXT("the tag can be created"), Entry))
	{
		return false;
	}
	TestFalse(TEXT("an entry with no pages yet has nothing to give back either"),
		UDreamUIDynamicSpriteAtlasManager::TrimAtlasByPackingTag(TestTag));

	// A page put there by hand, so the test does not need a real packing run (which needs textures
	// with live RHI resources to copy between).
	Entry->AtlasTextureArray.Add(MakeReadableTexture(4, FColor::Transparent));
	Entry->AtlasBinPackArray.Add(rbp::MaxRectsBinPack(4, 4));
	TestTrue(TEXT("a tag nothing is drawing from gives its pages back"),
		UDreamUIDynamicSpriteAtlasManager::TrimAtlasByPackingTag(TestTag));

	FDreamUIDynamicSpriteAtlasData* AfterTrim = UDreamUIDynamicSpriteAtlasManager::Find(TestTag);
	if (!TestNotNull(TEXT("the entry itself survives, keeping the tag's settings"), AfterTrim))
	{
		return false;
	}
	TestEqual(TEXT("but holds no pages"), AfterTrim->AtlasTextureArray.Num(), 0);
	TestFalse(TEXT("and trimming again has nothing left to do"),
		UDreamUIDynamicSpriteAtlasManager::TrimAtlasByPackingTag(TestTag));

	UDreamUIDynamicSpriteAtlasManager::DisposeAtlasByPackingTag(TestTag);
	TestNull(TEXT("disposing removes the entry altogether"),
		UDreamUIDynamicSpriteAtlasManager::Find(TestTag));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDataTexturePackingTest,
	"DreamGUI.Render.EveryBitFieldInTheDataTextureIsANormalFloatSoFlushToZeroCannotEatIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDataTexturePackingTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// The data textures are R32_FLOAT and every reader gets a word through a float LOAD, so a word
	// that is really a bit pattern has to be a normal float. A GPU that flushes denormals to zero --
	// most mobile ones, and not optional there -- turns a denormal into 0 before asuint() runs, which
	// is why this is worth asserting on numbers rather than looking at a picture: the desktop the
	// author works on has flush-to-zero off and shows nothing wrong.
	//
	// These are the packings as they were: both were denormals in ordinary configurations.
	const uint32 OldOpaqueBlackAsRGBA = ((uint32)0 << 24) | ((uint32)0 << 16) | ((uint32)0 << 8) | (uint32)255;
	TestTrue(TEXT("an opaque black packed R,G,B,A used to be a denormal"), IsDenormalBits(OldOpaqueBlackAsRGBA));
	const uint32 OldBodyOnlyFlags = ((uint32)0x80 << 24) | ((uint32)1 << 16) | ((uint32)0 << 8);
	TestTrue(TEXT("and so did body-enabled with a non-default texture mode"), IsDenormalBits(OldBodyOnlyFlags));

	// Colours. Alpha on top means only a fully transparent colour can still flush, and that reads
	// back as the transparent it already was.
	const FColor Colors[] = {
		FColor(0, 0, 0, 255), FColor(255, 255, 255, 255), FColor(0, 0, 0, 0),
		FColor(0, 0, 1, 255), FColor(0, 1, 0, 1), FColor(12, 34, 56, 78), FColor(0, 0, 0, 1),
	};
	for (const FColor& Color : Colors)
	{
		const uint32 Packed = UDreamRectBlock::PackColorForDataTexture(Color);
		if (Color.A != 0)
		{
			TestFalse(*FString::Printf(TEXT("colour %s survives a flush-to-zero load"), *Color.ToString()),
				IsDenormalBits(Packed));
		}
		// ...and it is still the same colour: this is the mirror of DreamUI_UnpackUintColor.
		TestEqual(TEXT("alpha is the top byte"), (int32)((Packed >> 24) & 0xff), (int32)Color.A);
		TestEqual(TEXT("then red"), (int32)((Packed >> 16) & 0xff), (int32)Color.R);
		TestEqual(TEXT("then green"), (int32)((Packed >> 8) & 0xff), (int32)Color.G);
		TestEqual(TEXT("then blue"), (int32)(Packed & 0xff), (int32)Color.B);
	}

	// Flags. The payload is three bytes under a constant, so no combination of them can reach the
	// exponent -- including the all-zero one, which is a real configuration (everything disabled).
	for (int32 Bools = 0; Bools < 256; Bools += 1)
	{
		for (int32 Mode = 0; Mode < 4; Mode++)
		{
			const uint32 Packed = UDreamRectBlock::PackFlagsForDataTexture((uint8)Bools, (uint8)Mode, (uint8)(3 - Mode));
			if (!TestFalse(TEXT("no flag combination packs to a denormal"), IsDenormalBits(Packed)))
			{
				return false;
			}
			//the mirror of the decode in DreamUIRectBlock.ush
			if (!TestEqual(TEXT("the bools are the second byte"), (int32)((Packed >> 16) & 0xff), Bools)
				|| !TestEqual(TEXT("the scale mode the third"), (int32)((Packed >> 8) & 0xff), Mode)
				|| !TestEqual(TEXT("and the draw mode the fourth"), (int32)(Packed & 0xff), 3 - Mode))
			{
				return false;
			}
		}
	}

	// The widget-property marks pixel, which has the same shape and the same reason.
	for (int32 FontMark = 0; FontMark < 256; FontMark++)
	{
		const uint32 Packed = UDreamVisual::PackWidgetMarks((uint8)FontMark, 0);
		if (!TestFalse(TEXT("no font mark packs to a denormal"), IsDenormalBits(Packed)))
		{
			return false;
		}
		if (!TestEqual(TEXT("the font mark is where the shader looks"), (int32)((Packed >> 16) & 0xff), FontMark))
		{
			return false;
		}
	}
	TestEqual(TEXT("an extra mark lands in its own byte"),
		(int32)((UDreamVisual::PackWidgetMarks(0, 0x81) >> 8) & 0xff), 0x81);
	TestFalse(TEXT("and an extra mark with no font mark is a normal float too -- the case the old layout lost"),
		IsDenormalBits(UDreamVisual::PackWidgetMarks(0, 0x81)));

	// The numbers in that row are stored as float VALUES now, not as bit patterns, and the layout
	// says so: the size sits where it always did and the centre was appended AFTER the text style
	// block rather than widening in place, so the style pixels DreamUIText.ush indexes by hand did
	// not move.
	TestEqual(TEXT("the marks pixel is first"), UDreamVisual::WidgetMarksPixelStart, 0);
	TestEqual(TEXT("then the clip coordinate"), UDreamVisual::ClipDataCoordinatePixelStart, 1);
	TestEqual(TEXT("then width and height, one pixel each"), UDreamVisual::WidgetSizePixelStart, 2);
	TestEqual(TEXT("the text style block still starts at pixel 4"), FDreamTextStyle::PackedPixelStart, 4);
	TestEqual(TEXT("and the centre comes after it"), UDreamVisual::WidgetCenterPixelStart,
		FDreamTextStyle::PackedPixelStart + FDreamTextStyle::PackedPixelCount);
	TestEqual(TEXT("so the row is long enough to hold all of it"),
		UDreamVisual::WidgetPropertyDataLength / (int32)sizeof(float),
		UDreamVisual::WidgetCenterPixelStart + 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAtlasLruEvictionTest,
	"DreamGUI.Render.AFullAtlasReclaimsTheRectanglesOfSpritesNothingDrawsBeforeItAsksForAnotherPage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAtlasLruEvictionTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// A bin pack hands a rectangle out and cannot take it back, so an atlas that only ever inserts
	// grows forever: a long session that shows many different sprites under one tag keeps adding
	// pages and never gives one back. Reclaiming means rebuilding the pages around the sprites that
	// survive, and deciding who survives means asking what is actually being drawn.
	//
	// The pixel copies inside a rebuild need live RHI resources, which a headless run has none of;
	// the copy says so and skips, and the bookkeeping -- who was evicted, who stayed, who was told --
	// is what this asserts, because that is where the decision lives.
	AddExpectedMessage(TEXT("Resource is null"),
		ELogVerbosity::Warning, EAutomationExpectedErrorFlags::Contains, -1);

	const FName TestTag = TEXT("DreamGUITest_LRU");
	auto MakeSprite = [&]() -> UDreamUISpriteData*
	{
		UTexture2D* Texture = MakeReadableTexture(4, FColor::White);
		return UDreamUISpriteData::CreateDreamUISpriteData(GetTransientPackage(), Texture, FMargin(), TestTag);
	};

	{
		FDreamUIDynamicSpriteAtlasData Data;
		Data.PackingTag = TestTag;
		Data.AtlasTextureArray.Add(MakeReadableTexture(8, FColor::Transparent));
		Data.AtlasBinPackArray.Add(rbp::MaxRectsBinPack(Data.GetAtlasTextureSize(), Data.GetAtlasTextureSize()));

		UDreamUISpriteData* Oldest = MakeSprite();
		UDreamUISpriteData* Middle = MakeSprite();
		UDreamUISpriteData* Newest = MakeSprite();
		if (!TestNotNull(TEXT("the fixture sprites exist"), Oldest)
			|| !TestNotNull(TEXT("all of them"), Newest))
		{
			return false;
		}
		//touch order is what LRU means here: oldest first
		Data.TouchSprite(Oldest);
		Data.TouchSprite(Middle);
		Data.TouchSprite(Newest);
		TestEqual(TEXT("the list is in touch order"), Data.SpriteDataArray.Num(), 3);
		TestEqual(TEXT("with the oldest at the front"), Data.SpriteDataArray[0].Get(), Oldest);

		// Something is drawing the middle one. It is not the newest on purpose: "recently touched"
		// and "currently drawn" are different questions and only the second one protects a sprite.
		UDreamSpriteRenderProbe* Probe = NewObject<UDreamSpriteRenderProbe>(GetTransientPackage());
		Probe->DrawnSprite = Middle;
		Data.RenderSpriteArray.Add(Probe);

		const int32 EvictedCount = Data.EvictUnusedSprites();
		TestEqual(TEXT("the two nobody draws are reclaimed"), EvictedCount, 2);
		TestEqual(TEXT("and the one being drawn is still packed"), Data.SpriteDataArray.Num(), 1);
		if (Data.SpriteDataArray.Num() == 1)
		{
			TestEqual(TEXT("it is the one the probe draws"), Data.SpriteDataArray[0].Get(), Middle);
		}
		TestTrue(TEXT("whoever draws from the atlas is told its rectangle moved"), Probe->AtlasChangeCount > 0);
		TestNull(TEXT("an evicted sprite goes back to holding no atlas page"),
			GetPackedAtlasTexture(Oldest));
	}

	{
		// The LRU half: with an area to free, only as many as needed go, oldest first. The warmer
		// unused sprites keep their rectangles -- and their pixels, which is the point of asking for
		// an amount rather than evicting everything that is merely idle.
		FDreamUIDynamicSpriteAtlasData Data;
		Data.PackingTag = TestTag;
		Data.AtlasTextureArray.Add(MakeReadableTexture(8, FColor::Transparent));
		Data.AtlasBinPackArray.Add(rbp::MaxRectsBinPack(Data.GetAtlasTextureSize(), Data.GetAtlasTextureSize()));

		UDreamUISpriteData* Oldest = MakeSprite();
		UDreamUISpriteData* Middle = MakeSprite();
		UDreamUISpriteData* Newest = MakeSprite();
		Data.TouchSprite(Oldest);
		Data.TouchSprite(Middle);
		Data.TouchSprite(Newest);

		const int32 OneSpriteArea = Data.GetInsertAreaForSprite(Oldest);
		if (!TestTrue(TEXT("a sprite occupies some area"), OneSpriteArea > 0))
		{
			return false;
		}
		const int32 EvictedCount = Data.EvictUnusedSprites(OneSpriteArea);
		TestEqual(TEXT("asking for one sprite's worth of room evicts one sprite"), EvictedCount, 1);
		TestEqual(TEXT("and the other two are still packed"), Data.SpriteDataArray.Num(), 2);
		if (Data.SpriteDataArray.Num() == 2)
		{
			TestEqual(TEXT("the oldest is the one that went"), Data.SpriteDataArray[0].Get(), Middle);
			TestEqual(TEXT("in order"), Data.SpriteDataArray[1].Get(), Newest);
		}
	}

	{
		// And a page nothing landed in after a rebuild is handed back, which is where the memory
		// actually returns: one page is a whole atlas texture.
		FDreamUIDynamicSpriteAtlasData Data;
		Data.PackingTag = TestTag;
		for (int32 i = 0; i < 3; i++)
		{
			Data.AtlasTextureArray.Add(MakeReadableTexture(8, FColor::Transparent));
			Data.AtlasBinPackArray.Add(rbp::MaxRectsBinPack(Data.GetAtlasTextureSize(), Data.GetAtlasTextureSize()));
		}
		UDreamUISpriteData* Only = MakeSprite();
		Data.TouchSprite(Only);
		UDreamSpriteRenderProbe* Probe = NewObject<UDreamSpriteRenderProbe>(GetTransientPackage());
		Probe->DrawnSprite = Only;
		Data.RenderSpriteArray.Add(Probe);

		Data.RepackPages();
		TestEqual(TEXT("everything fits in the first page, so the empty ones are dropped"),
			Data.AtlasTextureArray.Num(), 1);
		TestEqual(TEXT("bin packs and pages stay in step"), Data.AtlasBinPackArray.Num(), 1);
		TestEqual(TEXT("and the sprite that was in use is still packed"), Data.SpriteDataArray.Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamImageDrawTypeGeometryTest,
	"DreamGUI.Render.TheBrushDrawTypesTileFillAndRoundTheImageInsteadOfQuietlyDrawingAPlainOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamImageDrawTypeGeometryTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// Tiled, Filled and RoundedBox were enumerators commented out of the brush: an author could not
	// pick them, and the note said each needed a geometry builder. Three of the four they need were
	// already there -- the sprite element has filled and tiled since the fork -- so what was missing
	// was the wiring and, for the rounded box, one shape.

	// The enumerator VALUES are what a saved brush carries, so the new ones had to be appended rather
	// than inserted: an insert would silently turn every saved Image into a Tiled one.
	TestEqual(TEXT("None is still 0"), (int32)EDreamUIImageBrushDrawType::None, 0);
	TestEqual(TEXT("Box is still 1"), (int32)EDreamUIImageBrushDrawType::Box, 1);
	TestEqual(TEXT("Border is still 2"), (int32)EDreamUIImageBrushDrawType::Border, 2);
	TestEqual(TEXT("Image is still 3"), (int32)EDreamUIImageBrushDrawType::Image, 3);
	TestEqual(TEXT("and the new ones come after"), (int32)EDreamUIImageBrushDrawType::Tiled, 4);
	TestEqual(TEXT("in the order the commented-out ones had"), (int32)EDreamUIImageBrushDrawType::Filled, 5);
	TestEqual(TEXT("with the rounded box last"), (int32)EDreamUIImageBrushDrawType::RoundedBox, 6);

	// Tiling counts, including the convention the builder depends on: the count INCLUDES the partial
	// tile at the far edge.
	{
		int32 Count = 0; float Remainder = 0.0f;
		UDreamImage::CalculateTileCount(100.0f, 32.0f, Count, Remainder);
		TestEqual(TEXT("three whole tiles and a partial one"), Count, 4);
		TestEqual(TEXT("the partial one is the remainder"), Remainder, 4.0f);

		UDreamImage::CalculateTileCount(64.0f, 32.0f, Count, Remainder);
		TestEqual(TEXT("an exact fit still reports the trailing tile"), Count, 3);
		TestEqual(TEXT("with nothing in it"), Remainder, 0.0f);

		UDreamImage::CalculateTileCount(100.0f, 0.0f, Count, Remainder);
		TestEqual(TEXT("a tile with no size tiles nothing rather than dividing by zero"), Count, 0);
		UDreamImage::CalculateTileCount(0.0f, 32.0f, Count, Remainder);
		TestEqual(TEXT("and neither does a rect with no size"), Count, 0);
	}

	// The rounded box, which is the one shape that had to be written. It is geometry rather than a
	// signed distance field -- UDreamRectBlock is the tool for an exact one -- so what matters is
	// that the outline is a rounded rect: inside the rect, on the arcs, wound the way every other
	// builder winds, and carrying the sprite's UV rect.
	{
		FDreamUISpriteInfo SpriteInfo;
		SpriteInfo.Width = 100;
		SpriteInfo.Height = 50;
		SpriteInfo.ApplyUV(0, 0, 100, 50, 1.0f / 100, 1.0f / 50);

		FDreamUIGeometry Geo;
		const int32 Segments = 4;
		const float Width = 100.0f, Height = 50.0f, Radius = 10.0f;
		UDreamImage::BuildRoundedBoxGeometry(&Geo, Width, Height, FVector2f(0.5f, 0.5f), SpriteInfo
			, FVector4f(Radius, Radius, Radius, Radius), Segments, FColor::White, false
			, true, true, true, true);

		const int32 RingCount = (Segments + 1) * 4;
		TestEqual(TEXT("a centre plus one ring of arc points"), Geo.Vertices.Num(), RingCount + 1);
		TestEqual(TEXT("and the origin vertices agree"), Geo.OriginVertices.Num(), RingCount + 1);
		TestEqual(TEXT("one triangle per ring segment, closing the loop"), Geo.Triangles.Num(), RingCount * 3);

		// Every point inside the rect, and the corners cut: a point in the corner square must be on
		// the arc rather than at the corner itself.
		const float HalfW = Width * 0.5f, HalfH = Height * 0.5f;
		bool bAllInside = true;
		bool bAnyCornerCut = false;
		double SignedArea = 0.0;
		for (int32 i = 0; i < RingCount; i++)
		{
			const FVector3f& P = Geo.OriginVertices[1 + i].Position;
			bAllInside &= (P.Y >= -HalfW - 0.01f && P.Y <= HalfW + 0.01f && P.Z >= -HalfH - 0.01f && P.Z <= HalfH + 0.01f);
			//the bottom-left corner square: anything in it must sit at Radius from the arc centre
			if (P.Y < -HalfW + Radius && P.Z < -HalfH + Radius)
			{
				const float DistanceToArcCenter = FVector2f(P.Y - (-HalfW + Radius), P.Z - (-HalfH + Radius)).Size();
				bAnyCornerCut = true;
				TestTrue(TEXT("a point in the corner square lies on the corner arc"),
					FMath::IsNearlyEqual(DistanceToArcCenter, Radius, 0.01f));
			}
			const FVector3f& Q = Geo.OriginVertices[1 + ((i + 1) % RingCount)].Position;
			SignedArea += (double)P.Y * Q.Z - (double)Q.Y * P.Z;
		}
		TestTrue(TEXT("the outline stays inside the rect"), bAllInside);
		TestTrue(TEXT("and the corners really are cut"), bAnyCornerCut);
		TestTrue(TEXT("the ring winds the way the rect builders wind"), SignedArea > 0.0);

		// UVs come from the position's place in the rect, and the sprite's V runs the other way from
		// Y -- the same relationship GetUV0..3 describe for a plain quad.
		const int32 BottomLeftish = 1;//first ring point: on the left edge, at the bottom-left arc
		TestTrue(TEXT("the UV of a left-edge point is at the left of the sprite rect"),
			FMath::IsNearlyEqual(Geo.Vertices[BottomLeftish].TextureCoordinate[0].X, SpriteInfo.MinUV.X, 0.001f));
		TestTrue(TEXT("and the centre samples the middle of it"),
			FMath::IsNearlyEqual(Geo.Vertices[0].TextureCoordinate[0].X, (SpriteInfo.MinUV.X + SpriteInfo.MaxUV.X) * 0.5f, 0.001f));

		// A radius past half the shorter side is a capsule, not an error.
		FDreamUIGeometry Capsule;
		UDreamImage::BuildRoundedBoxGeometry(&Capsule, Width, Height, FVector2f(0.5f, 0.5f), SpriteInfo
			, FVector4f(1000.0f, 1000.0f, 1000.0f, 1000.0f), Segments, FColor::White, false
			, true, true, true, true);
		bool bStillInside = true;
		for (int32 i = 1; i < Capsule.OriginVertices.Num(); i++)
		{
			const FVector3f& P = Capsule.OriginVertices[i].Position;
			bStillInside &= (P.Y >= -HalfW - 0.01f && P.Y <= HalfW + 0.01f && P.Z >= -HalfH - 0.01f && P.Z <= HalfH + 0.01f);
		}
		TestTrue(TEXT("an enormous radius is clamped to half the shorter side"), bStillInside);
	}

	// And the brush still describes a non-sprite resource by its own size, which is what the tiled
	// and filled builders measure with when the brush holds a texture or a material.
	{
		FScopedVisualWidget Fixture(UDreamImage::StaticClass(), TEXT("Img"));
		UDreamImage* Image = Cast<UDreamImage>(Fixture.GetVisual());
		if (!TestNotNull(TEXT("the image exists"), Image))
		{
			return false;
		}
		Image->SetBrush_Texture(nullptr);
		const FDreamUISpriteInfo Info = Image->GetBrushSpriteInfo();
		TestEqual(TEXT("the brush size is the tile size"), (int32)Info.Width, (int32)Image->GetBrush().ImageSize.X);
		TestEqual(TEXT("on both axes"), (int32)Info.Height, (int32)Image->GetBrush().ImageSize.Y);

		// The fill amount has its own setter because a progress bar writes it every frame.
		Image->SetBrushFillAmount(0.25f);
		TestEqual(TEXT("the fill amount round-trips"), Image->GetBrush().FillAmount, 0.25f);
		Image->SetBrushFillAmount(4.0f);
		TestEqual(TEXT("and is clamped to the fraction it is"), Image->GetBrush().FillAmount, 1.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDirectMeshRaycastTest,
	"DreamGUI.Render.ADirectMeshInMeshModeIsHitOnItsTrianglesRatherThanNotAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDirectMeshRaycastTest::RunTest(const FString& Parameters)
{
	using namespace DreamVisualRenderPathTestLocal;

	// The Mesh branch of UDreamVisualDirectMesh::LineTraceUI was commented out and returned false, so
	// choosing Mesh on a direct-mesh element meant it could not be clicked at all. The vertices it
	// has to test against live in CANVAS space -- the supplier transforms by ItemToCanvas before
	// writing them -- which is why this fixture needs a canvas even though the arithmetic does not.
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Root->SetDisplayName(TEXT("CanvasRoot"));
	Root->SetWidth(1000.0f);
	Root->SetHeight(1000.0f);
	Root->OnRegister();
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("the root widget carries a canvas"), Canvas))
	{
		return false;
	}

	UDreamWidget* Child = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Child->SetDisplayName(TEXT("Mesh"));
	Child->OnRegister();
	Child->TrySetParent(Root, false);
	Child->SetWidth(10.0f);
	Child->SetHeight(10.0f);
	Child->CreateNewVisual(UDreamStaticMesh::StaticClass());

	UDreamVisual* Visual = Child->GetVisual();
	auto DirectMesh = Cast<UDreamVisualDirectMesh>(Visual);
	if (!TestNotNull(TEXT("the direct-mesh visual exists"), DirectMesh))
	{
		Root->DestroyWidget();
		return false;
	}
	Visual->SetRaycastType(EDreamVisualRaycastType::Mesh);

	// One quad, deliberately far outside the 10x10 widget rect: a direct mesh is not bound by the
	// rect, which is exactly what the old rect pre-filter got wrong and what
	// GetHitGeometryFitsWidgetRect answers false to.
	TSharedPtr<FDreamUIRenderSection_DirectMesh> Section = MakeShared<FDreamUIRenderSection_DirectMesh>();
	Section->Vertices.SetNumZeroed(4);
	Section->Vertices[0].Position = FVector3f(0, 100, 100);
	Section->Vertices[1].Position = FVector3f(0, 200, 100);
	Section->Vertices[2].Position = FVector3f(0, 100, 200);
	Section->Vertices[3].Position = FVector3f(0, 200, 200);
	Section->ValidVerticesNum = 4;
	Section->TriangleIndices = { 0u, 3u, 2u, 0u, 1u, 3u };
	Section->ValidTriangleIndicesNum = 6;
	DirectMesh->OnSupplyMeshSection(TWeakObjectPtr<UDreamUIMeshComponent>(), Section);

	const FTransform& CanvasToWorld = Root->GetWorldTransform();
	auto WorldPoint = [&](float X, float Y, float Z) { return CanvasToWorld.TransformPosition(FVector(X, Y, Z)); };

	FDreamUIHitResult Hit;
	TestTrue(TEXT("a ray through the mesh hits it"),
		Visual->LineTraceUI(Hit, WorldPoint(100, 150, 150), WorldPoint(-100, 150, 150)));
	TestEqual(TEXT("and reports the element it hit"), Hit.Widget.Get(), Child);

	FDreamUIHitResult Miss;
	TestFalse(TEXT("a ray beside the mesh misses"),
		Visual->LineTraceUI(Miss, WorldPoint(100, 500, 500), WorldPoint(-100, 500, 500)));

	// Inside the widget's own rect but outside the mesh is a MISS: the geometry is the answer, not
	// the rect the element was authored at.
	FDreamUIHitResult RectOnly;
	TestFalse(TEXT("the widget rect is not a fallback once Mesh was chosen"),
		Visual->LineTraceUI(RectOnly, WorldPoint(100, 0, 0), WorldPoint(-100, 0, 0)));

	// With nothing supplied there is no mesh to test, and then the rect is all that is known.
	DirectMesh->OnSupplyMeshSection(TWeakObjectPtr<UDreamUIMeshComponent>(), TWeakPtr<FDreamUIRenderSection_DirectMesh>());
	FDreamUIHitResult RectFallback;
	TestTrue(TEXT("an element with no mesh yet falls back to its rect"),
		Visual->LineTraceUI(RectFallback, WorldPoint(100, 0, 0), WorldPoint(-100, 0, 0)));

	Root->DestroyWidget();
	return true;
}

#endif
