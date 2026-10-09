// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIDynamicSpriteAtlasData.h"
#include "Core/DreamUISpriteData.h"
#include "Engine/Texture2D.h"
#include "Misc/Guid.h"
#include "PixelFormat.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCompressedSpriteAtlasFallbackTest,
	"DreamGUI.RHI.ACompressedRuntimeSpriteUsesItsOriginalTextureInsteadOfCopyingIntoTheBGRAAtlas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamCompressedSpriteAtlasFallbackTest::RunTest(const FString& Parameters)
{
	if (!GPixelFormats[PF_DXT1].Supported)
	{
		AddInfo(TEXT("This RHI has no BC1 support; the compressed-resource regression requires PF_DXT1."));
		return true;
	}

	// Four actual BC1 blocks, each solid red. This has platform data and an RHI resource, but no editor
	// source that could be re-encoded: the same constraint a cooked texture has. Merely changing
	// CompressionSettings on a BGRA fixture would not exercise the invalid-format copy.
	TArray<uint8> Blocks;
	Blocks.SetNumZeroed(4 * 8);
	for (int32 Block = 0; Block < 4; ++Block)
	{
		Blocks[Block * 8 + 1] = 0xf8;//RGB565 red, selected by every pixel's zero index
	}
	TStrongObjectPtr<UTexture2D> Texture(UTexture2D::CreateTransient(8, 8, PF_DXT1, NAME_None,
		TConstArrayView64<uint8>(Blocks.GetData(), Blocks.Num())));
	if (!TestNotNull(TEXT("The BC1 texture was created"), Texture.Get()))
	{
		return false;
	}
	FlushRenderingCommands();
	FTextureResource* const OriginalResource = Texture->GetResource();
	if (!TestNotNull(TEXT("The source has a live texture resource"), OriginalResource)
		|| !TestTrue(TEXT("The source has a live RHI texture"), OriginalResource->TextureRHI.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("The resource really is compressed"), static_cast<int32>(OriginalResource->TextureRHI->GetFormat()), static_cast<int32>(PF_DXT1));
	const TextureCompressionSettings OriginalCompression = Texture->CompressionSettings;
	const TextureGroup OriginalGroup = Texture->LODGroup;

	const FName PackingTag(*FString::Printf(TEXT("DreamGUITest_CompressedAtlas_%s"), *FGuid::NewGuid().ToString()));
	TStrongObjectPtr<UDreamUISpriteData> Sprite(UDreamUISpriteData::CreateDreamUISpriteData(
		GetTransientPackage(), Texture.Get(), FMargin(1.0f), PackingTag));
	if (!TestNotNull(TEXT("The public runtime factory creates a sprite"), Sprite.Get()))
	{
		return false;
	}
	AddExpectedMessage(TEXT("cannot be packed into the BGRA8 sprite atlas"), ELogVerbosity::Warning,
		EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("The very first texture query returns the source, rather than a blank atlas"), Sprite->GetAtlasTexture(), Texture.Get());
	TestTrue(TEXT("The unsupported format uses the individual render path"), Sprite->IsIndividual());
	TestFalse(TEXT("There is no atlas packing tag left to register this sprite against"), Sprite->HavePackingTag());
	TestNull(TEXT("No atlas was allocated for an incompatible source"), UDreamUIDynamicSpriteAtlasManager::Find(PackingTag));
	const FDreamUISpriteInfo& Info = Sprite->GetSpriteInfo();
	TestEqual(TEXT("The original width survives"), static_cast<int32>(Info.Width), 8);
	TestEqual(TEXT("The original height survives"), static_cast<int32>(Info.Height), 8);
	TestTrue(TEXT("The individual texture uses its full UV rectangle"), Info.MinUV.Equals(FVector2f::ZeroVector) && Info.MaxUV.Equals(FVector2f(1.0f, 1.0f)));
	TestTrue(TEXT("The border is measured against the original texture"), Info.BorderMinUV.Equals(FVector2f(0.125f, 0.125f)) && Info.BorderMaxUV.Equals(FVector2f(0.875f, 0.875f)));
	TestEqual(TEXT("The factory preserves the source's compression setting"), static_cast<int32>(Texture->CompressionSettings.GetValue()), static_cast<int32>(OriginalCompression));
	TestEqual(TEXT("The factory preserves the source's LOD group"), static_cast<int32>(Texture->LODGroup.GetValue()), static_cast<int32>(OriginalGroup));
	TestTrue(TEXT("The source resource was not rebuilt in an attempt to change its format"), Texture->GetResource() == OriginalResource);

	// The lower-level entry point must reject the same resource without growing a page or queuing a copy.
	FDreamUIDynamicSpriteAtlasData Atlas;
	Atlas.PackingTag = PackingTag;
	TestFalse(TEXT("Direct atlas packing rejects BC1"), Atlas.PackSprite(Sprite.Get()));
	TestEqual(TEXT("Rejected packing allocates no GPU pages"), Atlas.AtlasTextureArray.Num(), 0);
	TestEqual(TEXT("Rejected packing consumes no atlas rectangle"), Atlas.SpriteDataArray.Num(), 0);
	FlushRenderingCommands();
	TestTrue(TEXT("The fallback still samples the original compressed RHI texture"),
		Sprite->GetAtlasTexture()->GetResource()->TextureRHI == OriginalResource->TextureRHI);
	return true;
}

#endif
