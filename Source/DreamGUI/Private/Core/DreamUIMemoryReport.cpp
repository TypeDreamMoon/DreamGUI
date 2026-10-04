// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIManager.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIDynamicSpriteAtlasData.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "DreamGUI.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "PixelFormat.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

/*
 * DreamGUI.Memory: what DreamGUI holds in memory, as each part that holds it says -- every font's glyph atlas
 * (UDreamUIFontData_BaseObject::GetMemoryInfo), the sprite atlas pages, and per world the canvas meshes' sections
 * (UDreamUIMeshComponent::GetMemoryInfo) and the paint rows (UDreamUIManagerWorldSubsystem::GetPaintRowsMemoryInfo).
 *
 * Read only. It looks the objects up rather than asking the canvases for their meshes, because a canvas makes its mesh
 * when it is asked for one it has not got (UDreamCanvas::GetUIMesh): a report that made what it reports on would count
 * itself. Every world with a manager is reported, whichever world the command was typed in, since fonts and sprite
 * atlases are shared by all of them and a play session's UI is beside the editor's.
 *
 * The JSON form is what the packaged text smoke test keeps (Tools/TestHost): it runs the command with "Json" and an
 * output device of its own. It is written by hand -- the runtime module does not link Json, and integers, strings,
 * objects and arrays are all a report holds.
 */
namespace DreamUIMemoryReportLocal
{
	struct FFontRow
	{
		FString Path;
		FString ClassName;
		FDreamUIFontMemoryInfo Info;
	};

	struct FSpriteAtlasRow
	{
		/** "Dynamic" (a packing tag of UDreamUIDynamicSpriteAtlasManager) or "Static" (a UDreamUIStaticSpriteAtlasData). */
		FString Kind;
		FString Name;
		int32 Pages = 0;
		/** The widest page's side, in texels. */
		int32 PageSize = 0;
		/** The pages' top mips, in bytes. */
		int64 Bytes = 0;
		/** Sprites packed into the pages; INDEX_NONE where the atlas does not say (a static one, outside the editor). */
		int32 Sprites = INDEX_NONE;
	};

	struct FWorldRow
	{
		FString Name;
		FString Path;
		FString Type;
		FString NetMode;
		int32 Canvases = 0;
		int32 Widgets = 0;
		int32 Meshes = 0;
		int32 Sections = 0;
		int32 PooledSections = 0;
		int64 VertexBytes = 0;
		int64 IndexBytes = 0;
		int32 PaintTextureRows = 0;
		int32 PaintTextRows = 0;
		int32 PaintGradientRows = 0;
		int64 PaintTextureBytes = 0;
	};

	struct FReport
	{
		TArray<FFontRow> Fonts;
		TArray<FSpriteAtlasRow> SpriteAtlases;
		TArray<FWorldRow> Worlds;
	};

	struct FTotals
	{
		int64 FontGPUBytes = 0;
		int64 FontCPUBytes = 0;
		/** Each font counts only its own face's file bytes (a fallback or style face reports its own), so the sum counts each file once. */
		int64 FontFaceBytes = 0;
		int32 SpriteAtlasPages = 0;
		int64 SpriteAtlasBytes = 0;
		int64 GeometryBytes = 0;
		int64 PaintRowBytes = 0;
	};

	/** The top mip of a texture, in bytes; 0 for one that has no size or format yet. */
	int64 GetMemoryReportTextureBytes(const UTexture2D* InTexture)
	{
		if (!IsValid(InTexture))
		{
			return 0;
		}
		const int32 Width = InTexture->GetSizeX();
		const int32 Height = InTexture->GetSizeY();
		const EPixelFormat Format = InTexture->GetPixelFormat();
		if (Width <= 0 || Height <= 0 || Format == PF_Unknown || Format >= PF_MAX)
		{
			return 0;
		}
		return (int64)GPixelFormats[Format].Get2DImageSizeInBytes((uint32)Width, (uint32)Height);
	}

	void AddMemoryReportPage(FSpriteAtlasRow& InOutRow, const UTexture2D* InPage)
	{
		if (!IsValid(InPage))
		{
			return;
		}
		++InOutRow.Pages;
		InOutRow.Bytes += GetMemoryReportTextureBytes(InPage);
		InOutRow.PageSize = FMath::Max(InOutRow.PageSize, InPage->GetSizeX());
	}

	FReport CollectMemoryReport()
	{
		FReport Report;
		const EObjectFlags SkippedFlags = RF_ClassDefaultObject | RF_ArchetypeObject;
		// Gathered whole before anything is asked of them: what a font answers is its own business, and the object hash is
		// not to be held while it does.
		TArray<UObject*> Objects;

		GetObjectsOfClass(UDreamUIFontData_BaseObject::StaticClass(), Objects, true, SkippedFlags, EInternalObjectFlags::Garbage);
		for (UObject* Object : Objects)
		{
			const UDreamUIFontData_BaseObject* Font = Cast<UDreamUIFontData_BaseObject>(Object);
			if (!IsValid(Font))
			{
				continue;
			}
			FFontRow& Row = Report.Fonts.AddDefaulted_GetRef();
			Row.Path = Font->GetPathName();
			Row.ClassName = Font->GetClass()->GetName();
			Font->GetMemoryInfo(Row.Info);
		}
		Report.Fonts.Sort([](const FFontRow& A, const FFontRow& B) { return A.Path < B.Path; });

		if (UDreamUIDynamicSpriteAtlasManager* AtlasManager = UDreamUIDynamicSpriteAtlasManager::Get())
		{
			for (const TPair<FName, FDreamUIDynamicSpriteAtlasData>& Entry : AtlasManager->GetAtlasMap())
			{
				FSpriteAtlasRow& Row = Report.SpriteAtlases.AddDefaulted_GetRef();
				Row.Kind = TEXT("Dynamic");
				Row.Name = Entry.Key.ToString();
				Row.Sprites = Entry.Value.SpriteDataArray.Num();
				for (const TObjectPtr<UTexture2D>& Page : Entry.Value.AtlasTextureArray)
				{
					AddMemoryReportPage(Row, Page.Get());
				}
			}
		}
		// A static atlas keeps its pages to itself, and its public accessor builds them when they are missing. Read through
		// the property instead: an atlas nothing has drawn from yet reports no page rather than being made to have one.
		Objects.Reset();
		GetObjectsOfClass(UDreamUIStaticSpriteAtlasData::StaticClass(), Objects, true, SkippedFlags, EInternalObjectFlags::Garbage);
		const FArrayProperty* PagesProperty = FindFProperty<FArrayProperty>(UDreamUIStaticSpriteAtlasData::StaticClass(), TEXT("AtlasTextureArray"));
		const FObjectPropertyBase* PageProperty = PagesProperty != nullptr ? CastField<FObjectPropertyBase>(PagesProperty->Inner) : nullptr;
		for (UObject* Object : Objects)
		{
			if (!IsValid(Object) || PageProperty == nullptr)
			{
				continue;
			}
			FSpriteAtlasRow& Row = Report.SpriteAtlases.AddDefaulted_GetRef();
			Row.Kind = TEXT("Static");
			Row.Name = Object->GetPathName();
			FScriptArrayHelper Pages(PagesProperty, PagesProperty->ContainerPtrToValuePtr<void>(Object));
			for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
			{
				AddMemoryReportPage(Row, Cast<UTexture2D>(PageProperty->GetObjectPropertyValue(Pages.GetRawPtr(PageIndex))));
			}
		}

		TArray<UObject*> Meshes;
		GetObjectsOfClass(UDreamUIMeshComponent::StaticClass(), Meshes, true, SkippedFlags, EInternalObjectFlags::Garbage);
		Objects.Reset();
		GetObjectsOfClass(UDreamUIManagerWorldSubsystem::StaticClass(), Objects, true, SkippedFlags, EInternalObjectFlags::Garbage);
		for (UObject* Object : Objects)
		{
			const UDreamUIManagerWorldSubsystem* Manager = Cast<UDreamUIManagerWorldSubsystem>(Object);
			const UWorld* World = IsValid(Manager) && Manager->IsInitialized() && !Manager->HasTornDownWorld() ? Manager->GetWorld() : nullptr;
			if (World == nullptr)
			{
				continue;
			}
			FWorldRow& Row = Report.Worlds.AddDefaulted_GetRef();
			Row.Name = World->GetName();
			Row.Path = World->GetPathName();
			Row.Type = LexToString(World->WorldType.GetValue());
			Row.NetMode = ToString(World->GetNetMode());
			for (const TWeakObjectPtr<UDreamCanvas>& Canvas : Manager->GetAllCanvasArray())
			{
				Row.Canvases += Canvas.IsValid() ? 1 : 0;
			}
			Row.Widgets = Manager->GetRegisteredWidgets().Num();
			Manager->GetPaintRowsMemoryInfo(Row.PaintTextureRows, Row.PaintTextRows, Row.PaintGradientRows, Row.PaintTextureBytes);
			for (UObject* MeshObject : Meshes)
			{
				const UDreamUIMeshComponent* Mesh = Cast<UDreamUIMeshComponent>(MeshObject);
				if (!IsValid(Mesh) || Mesh->GetWorld() != World)
				{
					continue;
				}
				int32 Sections = 0;
				int32 PooledSections = 0;
				int64 VertexBytes = 0;
				int64 IndexBytes = 0;
				Mesh->GetMemoryInfo(Sections, PooledSections, VertexBytes, IndexBytes);
				++Row.Meshes;
				Row.Sections += Sections;
				Row.PooledSections += PooledSections;
				Row.VertexBytes += VertexBytes;
				Row.IndexBytes += IndexBytes;
			}
		}
		Report.Worlds.Sort([](const FWorldRow& A, const FWorldRow& B) { return A.Path < B.Path; });
		return Report;
	}

	FTotals SumMemoryReport(const FReport& InReport)
	{
		FTotals Totals;
		for (const FFontRow& Row : InReport.Fonts)
		{
			Totals.FontGPUBytes += Row.Info.AtlasGPUBytes;
			Totals.FontCPUBytes += Row.Info.AtlasCPUBytes;
			Totals.FontFaceBytes += Row.Info.FaceBytes;
		}
		for (const FSpriteAtlasRow& Row : InReport.SpriteAtlases)
		{
			Totals.SpriteAtlasPages += Row.Pages;
			Totals.SpriteAtlasBytes += Row.Bytes;
		}
		for (const FWorldRow& Row : InReport.Worlds)
		{
			Totals.GeometryBytes += Row.VertexBytes + Row.IndexBytes;
			Totals.PaintRowBytes += Row.PaintTextureBytes;
		}
		return Totals;
	}

	FString FormatMemoryReportBytes(int64 InBytes)
	{
		if (InBytes >= 1024 * 1024)
		{
			return FString::Printf(TEXT("%.2f MiB"), (double)InBytes / (1024.0 * 1024.0));
		}
		return FString::Printf(TEXT("%.1f KiB"), (double)InBytes / 1024.0);
	}

	FString GetMemoryReportSummary(const FReport& InReport)
	{
		const FTotals Totals = SumMemoryReport(InReport);
		return FString::Printf(TEXT("DreamGUI.Memory: %d font(s), atlases %s on the GPU and %s copied on the CPU, font files %s; %d sprite atlas page(s), %s; %d world(s), canvas geometry %s, paint rows %s."),
			InReport.Fonts.Num(), *FormatMemoryReportBytes(Totals.FontGPUBytes), *FormatMemoryReportBytes(Totals.FontCPUBytes), *FormatMemoryReportBytes(Totals.FontFaceBytes),
			Totals.SpriteAtlasPages, *FormatMemoryReportBytes(Totals.SpriteAtlasBytes), InReport.Worlds.Num(),
			*FormatMemoryReportBytes(Totals.GeometryBytes), *FormatMemoryReportBytes(Totals.PaintRowBytes));
	}

	void PrintMemoryReport(const FReport& InReport, FOutputDevice& Ar)
	{
		Ar.Log(GetMemoryReportSummary(InReport));
		Ar.Log(TEXT("Fonts (atlas slices x side, GPU, CPU copy; cells: field / coverage / retired coverage / free of all; glyphs: field / colour / coverage; face bytes):"));
		for (const FFontRow& Row : InReport.Fonts)
		{
			const FDreamUIFontMemoryInfo& Info = Row.Info;
			Ar.Logf(TEXT("  %s [%s]: %d x %d at %d B/texel, %s, %s; cells %d / %d / %d / %d of %d; glyphs %d / %d (%lld texels) / %d; faces %s"),
				*Row.Path, *Row.ClassName, Info.AtlasSlices, Info.AtlasSliceSize, Info.AtlasBytesPerTexel,
				*FormatMemoryReportBytes(Info.AtlasGPUBytes), *FormatMemoryReportBytes(Info.AtlasCPUBytes),
				Info.FieldCells, Info.CoverageCells, Info.RetiredCoverageCells, Info.FreeCells, Info.CellsTotal,
				Info.FieldGlyphs, Info.ColorGlyphs, Info.ColorGlyphTexels, Info.CoverageGlyphs, *FormatMemoryReportBytes(Info.FaceBytes));
		}
		Ar.Log(TEXT("Sprite atlases:"));
		for (const FSpriteAtlasRow& Row : InReport.SpriteAtlases)
		{
			const FString Sprites = Row.Sprites >= 0 ? FString::Printf(TEXT(", %d sprite(s)"), Row.Sprites) : FString();
			Ar.Logf(TEXT("  %s %s: %d page(s) up to %d texels a side, %s%s"),
				*Row.Kind, *Row.Name, Row.Pages, Row.PageSize, *FormatMemoryReportBytes(Row.Bytes), *Sprites);
		}
		Ar.Log(TEXT("Worlds:"));
		for (const FWorldRow& Row : InReport.Worlds)
		{
			Ar.Logf(TEXT("  %s (%s, %s): %d canvas(es), %d widget(s); %d mesh(es), %d section(s) in use and %d pooled, vertices %s, indices %s; paint rows: %d text table(s) and %d gradient row(s) of %d, %s"),
				*Row.Path, *Row.Type, *Row.NetMode, Row.Canvases, Row.Widgets, Row.Meshes, Row.Sections, Row.PooledSections,
				*FormatMemoryReportBytes(Row.VertexBytes), *FormatMemoryReportBytes(Row.IndexBytes),
				Row.PaintTextRows, Row.PaintGradientRows, Row.PaintTextureRows, *FormatMemoryReportBytes(Row.PaintTextureBytes));
		}
	}

	/**
	 * A JSON document, one value to a line and indented, so that two reports diff line by line. Keys are the report's own
	 * and need no escaping; values are escaped as JSON asks.
	 */
	class FDreamUIMemoryReportJson
	{
	public:
		void BeginObject(const TCHAR* InKey = nullptr) { Open(InKey, TEXT('{')); }
		void EndObject() { Close(TEXT('}')); }
		void BeginArray(const TCHAR* InKey) { Open(InKey, TEXT('[')); }
		void EndArray() { Close(TEXT(']')); }
		void Number(const TCHAR* InKey, int64 InValue) { Value(InKey, FString::Printf(TEXT("%lld"), InValue)); }
		void String(const TCHAR* InKey, const FString& InValue) { Value(InKey, Quote(InValue)); }
		const FString& GetText() const { return Text; }

	private:
		FString Text;
		/** One entry per object or array still open: whether anything has been written into it yet. */
		TArray<bool> ScopeHasItems;

		void BeginItem(const TCHAR* InKey)
		{
			if (ScopeHasItems.Num() > 0)
			{
				if (ScopeHasItems.Last())
				{
					Text += TEXT(",");
				}
				ScopeHasItems.Last() = true;
				Text += TEXT("\n");
				Text += FString::ChrN(ScopeHasItems.Num() * 2, TEXT(' '));
			}
			if (InKey != nullptr)
			{
				Text += Quote(InKey);
				Text += TEXT(": ");
			}
		}

		void Value(const TCHAR* InKey, const FString& InValueText)
		{
			BeginItem(InKey);
			Text += InValueText;
		}

		void Open(const TCHAR* InKey, TCHAR InBracket)
		{
			BeginItem(InKey);
			Text.AppendChar(InBracket);
			ScopeHasItems.Add(false);
		}

		void Close(TCHAR InBracket)
		{
			const bool bScopeHadItems = ScopeHasItems.Num() > 0 && ScopeHasItems.Last();
			if (ScopeHasItems.Num() > 0)
			{
				ScopeHasItems.Pop();
			}
			if (bScopeHadItems)
			{
				Text += TEXT("\n");
				Text += FString::ChrN(ScopeHasItems.Num() * 2, TEXT(' '));
			}
			Text.AppendChar(InBracket);
		}

		static FString Quote(const FString& InValue)
		{
			FString Quoted;
			Quoted.Reserve(InValue.Len() + 2);
			Quoted.AppendChar(TEXT('"'));
			for (const TCHAR Character : InValue)
			{
				switch (Character)
				{
				case TEXT('"'): Quoted += TEXT("\\\""); break;
				case TEXT('\\'): Quoted += TEXT("\\\\"); break;
				case TEXT('\n'): Quoted += TEXT("\\n"); break;
				case TEXT('\r'): Quoted += TEXT("\\r"); break;
				case TEXT('\t'): Quoted += TEXT("\\t"); break;
				default:
					if ((uint32)Character < 0x20u)
					{
						Quoted += FString::Printf(TEXT("\\u%04x"), (uint32)Character);
					}
					else
					{
						Quoted.AppendChar(Character);
					}
					break;
				}
			}
			Quoted.AppendChar(TEXT('"'));
			return Quoted;
		}
	};

	FString MemoryReportToJson(const FReport& InReport)
	{
		const FTotals Totals = SumMemoryReport(InReport);
		FDreamUIMemoryReportJson Json;
		Json.BeginObject();
		Json.Number(TEXT("version"), 1);
		Json.BeginObject(TEXT("totals"));
		Json.Number(TEXT("fontGpuBytes"), Totals.FontGPUBytes);
		Json.Number(TEXT("fontCpuBytes"), Totals.FontCPUBytes);
		Json.Number(TEXT("fontFaceBytes"), Totals.FontFaceBytes);
		Json.Number(TEXT("spriteAtlasPages"), Totals.SpriteAtlasPages);
		Json.Number(TEXT("spriteAtlasBytes"), Totals.SpriteAtlasBytes);
		Json.Number(TEXT("geometryBytes"), Totals.GeometryBytes);
		Json.Number(TEXT("paintRowBytes"), Totals.PaintRowBytes);
		Json.EndObject();

		Json.BeginArray(TEXT("fonts"));
		for (const FFontRow& Row : InReport.Fonts)
		{
			const FDreamUIFontMemoryInfo& Info = Row.Info;
			Json.BeginObject();
			Json.String(TEXT("path"), Row.Path);
			Json.String(TEXT("class"), Row.ClassName);
			Json.Number(TEXT("atlasSlices"), Info.AtlasSlices);
			Json.Number(TEXT("atlasSliceSize"), Info.AtlasSliceSize);
			Json.Number(TEXT("atlasBytesPerTexel"), Info.AtlasBytesPerTexel);
			Json.Number(TEXT("atlasGpuBytes"), Info.AtlasGPUBytes);
			Json.Number(TEXT("atlasCpuBytes"), Info.AtlasCPUBytes);
			Json.Number(TEXT("cellsTotal"), Info.CellsTotal);
			Json.Number(TEXT("fieldCells"), Info.FieldCells);
			Json.Number(TEXT("coverageCells"), Info.CoverageCells);
			Json.Number(TEXT("retiredCoverageCells"), Info.RetiredCoverageCells);
			Json.Number(TEXT("freeCells"), Info.FreeCells);
			Json.Number(TEXT("fieldGlyphs"), Info.FieldGlyphs);
			Json.Number(TEXT("colorGlyphs"), Info.ColorGlyphs);
			Json.Number(TEXT("colorGlyphTexels"), Info.ColorGlyphTexels);
			Json.Number(TEXT("coverageGlyphs"), Info.CoverageGlyphs);
			Json.Number(TEXT("faceBytes"), Info.FaceBytes);
			Json.EndObject();
		}
		Json.EndArray();

		Json.BeginArray(TEXT("spriteAtlases"));
		for (const FSpriteAtlasRow& Row : InReport.SpriteAtlases)
		{
			Json.BeginObject();
			Json.String(TEXT("kind"), Row.Kind);
			Json.String(TEXT("name"), Row.Name);
			Json.Number(TEXT("pages"), Row.Pages);
			Json.Number(TEXT("pageSize"), Row.PageSize);
			Json.Number(TEXT("bytes"), Row.Bytes);
			if (Row.Sprites >= 0)
			{
				Json.Number(TEXT("sprites"), Row.Sprites);
			}
			Json.EndObject();
		}
		Json.EndArray();

		Json.BeginArray(TEXT("worlds"));
		for (const FWorldRow& Row : InReport.Worlds)
		{
			Json.BeginObject();
			Json.String(TEXT("name"), Row.Name);
			Json.String(TEXT("path"), Row.Path);
			Json.String(TEXT("type"), Row.Type);
			Json.String(TEXT("netMode"), Row.NetMode);
			Json.Number(TEXT("canvases"), Row.Canvases);
			Json.Number(TEXT("widgets"), Row.Widgets);
			Json.Number(TEXT("meshes"), Row.Meshes);
			Json.Number(TEXT("sections"), Row.Sections);
			Json.Number(TEXT("pooledSections"), Row.PooledSections);
			Json.Number(TEXT("vertexBytes"), Row.VertexBytes);
			Json.Number(TEXT("indexBytes"), Row.IndexBytes);
			Json.BeginObject(TEXT("paintRows"));
			Json.Number(TEXT("textureRows"), Row.PaintTextureRows);
			Json.Number(TEXT("textRows"), Row.PaintTextRows);
			Json.Number(TEXT("gradientRows"), Row.PaintGradientRows);
			Json.Number(TEXT("textureBytes"), Row.PaintTextureBytes);
			Json.EndObject();
			Json.EndObject();
		}
		Json.EndArray();
		Json.EndObject();
		return Json.GetText();
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDreamUIMemoryCommand(
	TEXT("DreamGUI.Memory"),
	TEXT("What DreamGUI holds in memory: each font's glyph atlas (slices, GPU bytes, the CPU copy, cells, glyphs), the sprite atlas pages, ")
	TEXT("and in every world with a UI manager the canvas meshes' sections and the paint rows. DreamGUI.Memory prints it; ")
	TEXT("DreamGUI.Memory Json prints it as JSON instead; DreamGUI.Memory File=<path> writes the JSON to a file (relative to Saved)."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld*, FOutputDevice& Ar)
	{
		using namespace DreamUIMemoryReportLocal;
		bool bJson = false;
		FString FilePath;
		for (const FString& Arg : InArgs)
		{
			if (Arg.Equals(TEXT("Json"), ESearchCase::IgnoreCase))
			{
				bJson = true;
			}
			else if (Arg.StartsWith(TEXT("File="), ESearchCase::IgnoreCase))
			{
				FilePath = Arg.RightChop(5).TrimQuotes();
			}
			else
			{
				Ar.Logf(TEXT("DreamGUI.Memory: '%s' is not an argument it takes (Json, File=<path>)."), *Arg);
			}
		}

		const FReport Report = CollectMemoryReport();
		// The one line a log keeps whichever form was asked for, so a packaged run's log says what the UI held.
		UE_LOG(DreamGUI, Log, TEXT("%s"), *GetMemoryReportSummary(Report));
		if (!FilePath.IsEmpty())
		{
			FString Path = FilePath;
			if (FPaths::IsRelative(Path))
			{
				Path = FPaths::Combine(FPaths::ProjectSavedDir(), Path);
			}
			Path = FPaths::ConvertRelativePathToFull(Path);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
			if (FFileHelper::SaveStringToFile(MemoryReportToJson(Report), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(DreamGUI, Log, TEXT("DreamGUI.Memory wrote %s"), *Path);
			}
			else
			{
				UE_LOG(DreamGUI, Warning, TEXT("DreamGUI.Memory could not write %s"), *Path);
			}
		}
		if (bJson)
		{
			Ar.Log(MemoryReportToJson(Report));
		}
		else
		{
			PrintMemoryReport(Report, Ar);
		}
	}));
