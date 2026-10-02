// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "DreamTextParityCorpus.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "Core/DreamUIFontData_Bitmap.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"

#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
THIRD_PARTY_INCLUDES_END
#endif

namespace DreamTextParity
{
	namespace CorpusLocal
	{
		void ReadStrings(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, TArray<FString>& OutStrings)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (InObject->TryGetArrayField(InField, Values) && Values != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					FString String;
					if (Value.IsValid() && Value->TryGetString(String))
					{
						OutStrings.Add(String);
					}
				}
			}
		}

		FIntPoint ReadPoint(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, FIntPoint InDefault)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (InObject->TryGetArrayField(InField, Values) && Values != nullptr && Values->Num() == 2 && (*Values)[0].IsValid() && (*Values)[1].IsValid())
			{
				return FIntPoint(FMath::RoundToInt32((*Values)[0]->AsNumber()), FMath::RoundToInt32((*Values)[1]->AsNumber()));
			}
			return InDefault;
		}

		float ReadFloat(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, float InDefault)
		{
			double Value = 0.0;
			return InObject->TryGetNumberField(InField, Value) ? static_cast<float>(Value) : InDefault;
		}

		FString ReadString(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, const FString& InDefault)
		{
			FString Value;
			return InObject->TryGetStringField(InField, Value) ? Value : InDefault;
		}

		/** "normal" is the font's own line height (0 here); "50%" is half of it. */
		float ParseLineHeight(const FString& InSpelling)
		{
			const FString Trimmed = InSpelling.TrimStartAndEnd();
			if (Trimmed.EndsWith(TEXT("%")))
			{
				return FCString::Atof(*Trimmed.LeftChop(1)) / 100.0f;
			}
			return 0.0f;
		}

		/** Appends a code point as UTF-16, a surrogate pair above the basic plane. */
		void AppendCodepoint(FString& OutText, uint32 InCodepoint)
		{
			if (InCodepoint < 0x10000u)
			{
				OutText.AppendChar(static_cast<TCHAR>(InCodepoint));
				return;
			}
			const uint32 Offset = InCodepoint - 0x10000u;
			OutText.AppendChar(static_cast<TCHAR>(0xD800u + (Offset >> 10)));
			OutText.AppendChar(static_cast<TCHAR>(0xDC00u + (Offset & 0x3FFu)));
		}

		/** The character a reference such as "lt" or "#x1F600" (the part between '&' and ';') stands for. */
		bool DecodeReference(const FString& InName, uint32& OutCodepoint)
		{
			if (InName == TEXT("lt")) { OutCodepoint = '<'; return true; }
			if (InName == TEXT("gt")) { OutCodepoint = '>'; return true; }
			if (InName == TEXT("amp")) { OutCodepoint = '&'; return true; }
			if (InName == TEXT("quot")) { OutCodepoint = '"'; return true; }
			if (InName == TEXT("apos")) { OutCodepoint = '\''; return true; }
			if (InName == TEXT("nbsp")) { OutCodepoint = 0xA0u; return true; }
			if (InName.StartsWith(TEXT("#x")) || InName.StartsWith(TEXT("#X")))
			{
				const FString Digits = InName.Mid(2);
				if (Digits.IsEmpty())
				{
					return false;
				}
				for (const TCHAR Char : Digits)
				{
					if (!FChar::IsHexDigit(Char))
					{
						return false;
					}
				}
				OutCodepoint = static_cast<uint32>(FParse::HexNumber64(*Digits));
				return OutCodepoint > 0u && OutCodepoint <= 0x10FFFFu;
			}
			if (InName.StartsWith(TEXT("#")))
			{
				const FString Digits = InName.Mid(1);
				if (Digits.IsEmpty() || !Digits.IsNumeric())
				{
					return false;
				}
				OutCodepoint = static_cast<uint32>(FCString::Atoi64(*Digits));
				return OutCodepoint > 0u && OutCodepoint <= 0x10FFFFu;
			}
			return false;
		}

		bool IsTagName(const FString& InName)
		{
			if (InName.IsEmpty())
			{
				return false;
			}
			for (const TCHAR Char : InName)
			{
				if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
				{
					return false;
				}
			}
			return true;
		}

		struct FOpenTag
		{
			FString Name;
			FString Value;
		};

		FRichStyle StyleOf(const TArray<FOpenTag>& InOpen, float InBaseSize)
		{
			FRichStyle Style;
			Style.Size = InBaseSize;
			for (const FOpenTag& Tag : InOpen)
			{
				const FString& Name = Tag.Name;
				if (Name.Equals(TEXT("b"), ESearchCase::IgnoreCase)) { Style.bBold = true; }
				else if (Name.Equals(TEXT("i"), ESearchCase::IgnoreCase)) { Style.bItalic = true; }
				else if (Name.Equals(TEXT("u"), ESearchCase::IgnoreCase)) { Style.bUnderline = true; }
				else if (Name.Equals(TEXT("s"), ESearchCase::IgnoreCase)) { Style.bStrikethrough = true; }
				else if (Name.Equals(TEXT("sup"), ESearchCase::IgnoreCase)) { Style.SupOrSub = 1; }
				else if (Name.Equals(TEXT("sub"), ESearchCase::IgnoreCase)) { Style.SupOrSub = 2; }
				else if (Name.Equals(TEXT("a"), ESearchCase::IgnoreCase)) { Style.bLink = true; }
				else if (Name.Equals(TEXT("size"), ESearchCase::IgnoreCase))
				{
					if (Tag.Value.StartsWith(TEXT("+")))
					{
						Style.Size += FCString::Atof(*Tag.Value.Mid(1));
					}
					else if (Tag.Value.StartsWith(TEXT("-")))
					{
						Style.Size -= FCString::Atof(*Tag.Value.Mid(1));
					}
					else
					{
						Style.Size = FCString::Atof(*Tag.Value);
					}
				}
				else if (Name.Equals(TEXT("color"), ESearchCase::IgnoreCase) && Tag.Value.StartsWith(TEXT("#")))
				{
					Style.bHasColor = true;
					Style.Color = FColor::FromHex(Tag.Value);
				}
			}
			return Style;
		}
	}

	FString GetResourcesDirectory()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
		const FString Base = Plugin.IsValid() ? Plugin->GetBaseDir() : FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("DreamGUI"));
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(Base, TEXT("Source"), TEXT("DreamGUITests"), TEXT("Resources")));
	}

	FString GetCorpusPath()
	{
		return FPaths::Combine(GetResourcesDirectory(), TEXT("TextParity"), TEXT("corpus.json"));
	}

	FString GetChromeReferenceDirectory()
	{
		return FPaths::Combine(GetResourcesDirectory(), TEXT("TextParity"), TEXT("Chrome"));
	}

	FString GetOutputDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITextParity")));
	}

	FString ResolveFontPath(const FString& InSpelling)
	{
		FString Path = InSpelling;
		FString EngineDirectory = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
		FPaths::NormalizeDirectoryName(EngineDirectory);
		Path.ReplaceInline(TEXT("$(EngineDir)"), *EngineDirectory, ESearchCase::CaseSensitive);
		if (Path.Contains(TEXT("$(WindowsFonts)")))
		{
			FString WindowsDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
			if (WindowsDirectory.IsEmpty())
			{
				WindowsDirectory = TEXT("C:/Windows");
			}
			Path.ReplaceInline(TEXT("$(WindowsFonts)"), *FPaths::Combine(WindowsDirectory, TEXT("Fonts")), ESearchCase::CaseSensitive);
		}
		FPaths::NormalizeFilename(Path);
		FPaths::RemoveDuplicateSlashes(Path);
		return Path;
	}

	FVector2D FCase::GetBox(int32 InPadding) const
	{
		const double BoxWidth = Width > 0.0f ? static_cast<double>(Width) : static_cast<double>(Canvas.X - 2 * InPadding);
		const double BoxHeight = static_cast<double>(Canvas.Y - 2 * InPadding);
		return FVector2D(FMath::Max(1.0, BoxWidth), FMath::Max(1.0, BoxHeight));
	}

	const FCase* FCorpus::FindCase(const FString& InId) const
	{
		return Cases.FindByPredicate([&InId](const FCase& Case) { return Case.Id == InId; });
	}

	bool FCorpus::FindFontKey(const FString& InKey, FFontKey& OutKey) const
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!Fonts.IsValid() || !Fonts->TryGetObjectField(InKey, Entry) || Entry == nullptr || !Entry->IsValid())
		{
			return false;
		}
		OutKey = FFontKey();
		OutKey.Key = InKey;
		TArray<FString> Spellings;
		CorpusLocal::ReadStrings(*Entry, TEXT("faces"), Spellings);
		for (const FString& Spelling : Spellings)
		{
			OutKey.Faces.Add(ResolveFontPath(Spelling));
		}
		FString Spelling;
		if ((*Entry)->TryGetStringField(TEXT("bold"), Spelling))
		{
			OutKey.Bold = ResolveFontPath(Spelling);
		}
		if ((*Entry)->TryGetStringField(TEXT("italic"), Spelling))
		{
			OutKey.Italic = ResolveFontPath(Spelling);
		}
		if ((*Entry)->TryGetStringField(TEXT("boldItalic"), Spelling))
		{
			OutKey.BoldItalic = ResolveFontPath(Spelling);
		}
		FString Kind;
		OutKey.bBitmap = (*Entry)->TryGetStringField(TEXT("kind"), Kind) && Kind.Equals(TEXT("bitmap"), ESearchCase::IgnoreCase);
		return OutKey.Faces.Num() > 0;
	}

	bool LoadCorpus(FCorpus& OutCorpus, FString& OutError)
	{
		using namespace CorpusLocal;
		OutCorpus = FCorpus();
		const FString Path = GetCorpusPath();
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("the corpus %s could not be read"), *Path);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("the corpus %s is not JSON"), *Path);
			return false;
		}
		double Padding = 16.0;
		Root->TryGetNumberField(TEXT("padding"), Padding);
		OutCorpus.Padding = FMath::Max(0, FMath::RoundToInt32(Padding));
		const FIntPoint DefaultCanvas = ReadPoint(Root, TEXT("canvas"), FIntPoint(1024, 160));
		const TSharedPtr<FJsonObject>* Fonts = nullptr;
		if (Root->TryGetObjectField(TEXT("fonts"), Fonts) && Fonts != nullptr)
		{
			OutCorpus.Fonts = *Fonts;
		}
		const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
		if (!Root->TryGetArrayField(TEXT("cases"), Cases) || Cases == nullptr)
		{
			OutError = FString::Printf(TEXT("the corpus %s has no cases"), *Path);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Cases)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Object.IsValid())
			{
				continue;
			}
			FCase Case;
			Case.Id = ReadString(Object, TEXT("id"), FString());
			if (Case.Id.IsEmpty())
			{
				continue;
			}
			Case.BaseId = Case.Id;
			Case.Text = ReadString(Object, TEXT("text"), FString());
			Case.Font = ReadString(Object, TEXT("font"), TEXT("Latin"));
			Case.Size = ReadFloat(Object, TEXT("size"), 16.0f);
			Case.Width = ReadFloat(Object, TEXT("width"), 0.0f);
			Case.LineHeight = ParseLineHeight(ReadString(Object, TEXT("lineHeight"), TEXT("normal")));
			Case.LetterSpacing = ReadFloat(Object, TEXT("letterSpacing"), 0.0f);
			bool bRich = false;
			Case.bRich = Object->TryGetBoolField(TEXT("rich"), bRich) && bRich;
			Case.Lang = ReadString(Object, TEXT("lang"), TEXT("en"));
			Case.Align = ReadString(Object, TEXT("align"), TEXT("start"));
			Case.Dir = ReadString(Object, TEXT("dir"), TEXT("ltr"));
			Case.Wrap = ReadString(Object, TEXT("wrap"), TEXT("anywhere"));
			Case.Overflow = ReadString(Object, TEXT("overflow"), FString());
			Case.MaxLines = FMath::RoundToInt32(ReadFloat(Object, TEXT("maxLines"), 0.0f));
			Case.Transform = ReadString(Object, TEXT("transform"), FString());
			const TSharedPtr<FJsonObject>* Outline = nullptr;
			if (Object->TryGetObjectField(TEXT("outline"), Outline) && Outline != nullptr && Outline->IsValid())
			{
				Case.OutlineEm = ReadFloat(*Outline, TEXT("width"), 0.0f);
				Case.OutlineColor = FColor::FromHex(ReadString(*Outline, TEXT("color"), TEXT("#000000")));
			}
			Case.Canvas = ReadPoint(Object, TEXT("canvas"), DefaultCanvas);
			ReadStrings(Object, TEXT("flags"), Case.Flags);
			TArray<FString> Variants;
			ReadStrings(Object, TEXT("variants"), Variants);
			OutCorpus.Cases.Add(Case);

			// The same two rules Make-ChromeReference.ps1 applies, so the two sides name and size every variant alike.
			for (const FString& Variant : Variants)
			{
				if (Variant.Equals(TEXT("narrow"), ESearchCase::IgnoreCase))
				{
					FCase Narrow = Case;
					Narrow.Id = Case.Id + TEXT("_1px");
					Narrow.Width = 1.0f;
					Narrow.Wrap = TEXT("normal");
					const int32 LinePitch = FMath::CeilToInt32(1.6 * static_cast<double>(Case.Size));
					Narrow.Canvas = FIntPoint(
						FMath::Max(512, 2 * OutCorpus.Padding + FMath::CeilToInt32(3.0 * static_cast<double>(Case.Size))),
						FMath::Min(4096, 2 * OutCorpus.Padding + (Case.Text.Len() + 1) * LinePitch));
					OutCorpus.Cases.Add(Narrow);
				}
				else if (Variant.Equals(TEXT("inverse"), ESearchCase::IgnoreCase))
				{
					FCase Inverse = Case;
					Inverse.Id = Case.Id + TEXT("_Inverse");
					Inverse.bInverse = true;
					OutCorpus.Cases.Add(Inverse);
				}
			}
		}
		return OutCorpus.Cases.Num() > 0;
	}

	UDreamUIFontData_FreeTypeRender* GetDreamFont(const FFontKey& InKey, FString& OutError)
	{
		// Weak, so that a font nobody draws with any more goes with the next collection; while a test holds one, every
		// later test that asks for the same key gets the same object rather than another atlas.
		static TMap<FString, TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> Cache;
		const FString CacheKey = FString::Printf(TEXT("%s|%s|%s|%s|%s|%s"), InKey.bBitmap ? TEXT("Bitmap") : TEXT("DistanceField"),
			*InKey.Key, *FString::Join(InKey.Faces, TEXT(";")), *InKey.Bold, *InKey.Italic, *InKey.BoldItalic);
		if (const TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>* Found = Cache.Find(CacheKey))
		{
			if (UDreamUIFontData_FreeTypeRender* Alive = Found->Get())
			{
				return Alive;
			}
		}
		TArray<FString> Missing;
		// One face of the key, of the kind the key asks for; null, noted, when its file is not there.
		auto MakeFace = [&InKey, &Missing](const FString& InFile) -> UDreamUIFontData_FreeTypeRender*
		{
			if (!FPaths::FileExists(InFile))
			{
				Missing.Add(InFile);
				return nullptr;
			}
			UDreamUIFontData_FreeTypeRender* Face = nullptr;
			if (InKey.bBitmap)
			{
				Face = NewObject<UDreamUIFontData_Bitmap>(GetTransientPackage(), NAME_None, RF_Transient);
			}
			else
			{
				Face = NewObject<UDreamUIFontData_DistanceField>(GetTransientPackage(), NAME_None, RF_Transient);
			}
			Face->SetFontFilePath(InFile, false);
			Face->InitFont();
			return Face;
		};
		TArray<UDreamUIFontData_FreeTypeRender*> Faces;
		for (const FString& File : InKey.Faces)
		{
			if (FPaths::GetBaseFilename(File).Contains(TEXT("ColorEmoji")))
			{
				continue;
			}
			if (UDreamUIFontData_FreeTypeRender* Face = MakeFace(File))
			{
				Faces.Add(Face);
			}
		}
		// The primary's true style faces, which Slate and Chrome draw <b> and <i> with as well; a style the key has no
		// file for is synthesized, as it is in Chrome.
		UDreamUIFontData_FreeTypeRender* BoldFace = InKey.Bold.IsEmpty() ? nullptr : MakeFace(InKey.Bold);
		UDreamUIFontData_FreeTypeRender* ItalicFace = InKey.Italic.IsEmpty() ? nullptr : MakeFace(InKey.Italic);
		UDreamUIFontData_FreeTypeRender* BoldItalicFace = InKey.BoldItalic.IsEmpty() ? nullptr : MakeFace(InKey.BoldItalic);
		if (Missing.Num() > 0)
		{
			OutError = FString::Printf(TEXT("font key %s: no file at %s"), *InKey.Key, *FString::Join(Missing, TEXT(", ")));
		}
		if (Faces.Num() == 0)
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("font key %s has no face DreamGUI can draw"), *InKey.Key);
			}
			return nullptr;
		}
		TArray<UDreamUIFontData_FreeTypeRender*> Fallbacks;
		for (int32 Index = 1; Index < Faces.Num(); ++Index)
		{
			Fallbacks.Add(Faces[Index]);
		}
		Faces[0]->SetFallbackFonts(Fallbacks);
		Faces[0]->SetStyleFonts(BoldFace, ItalicFace, BoldItalicFace);
		Cache.Add(CacheKey, Faces[0]);
		return Faces[0];
	}

	bool FRichStyle::operator==(const FRichStyle& Other) const
	{
		return Size == Other.Size && bBold == Other.bBold && bItalic == Other.bItalic && bUnderline == Other.bUnderline
			&& bStrikethrough == Other.bStrikethrough && SupOrSub == Other.SupOrSub && bLink == Other.bLink
			&& bHasColor == Other.bHasColor && (!bHasColor || Color == Other.Color);
	}

	void ParseRichText(const FString& InMarkup, float InBaseSize, TArray<FRichRun>& OutRuns, FString& OutPlain, TArray<int32>* OutSourceOffsets)
	{
		using namespace CorpusLocal;
		OutRuns.Reset();
		OutPlain.Reset();
		if (OutSourceOffsets != nullptr)
		{
			OutSourceOffsets->Reset();
		}
		TArray<FOpenTag> Open;
		FRichStyle Current = StyleOf(Open, InBaseSize);
		FString Pending;
		TArray<int32> PendingSources;
		auto Flush = [&OutRuns, &OutPlain, &Pending, &PendingSources, &Current, OutSourceOffsets]()
		{
			if (Pending.IsEmpty())
			{
				return;
			}
			if (OutRuns.Num() > 0 && OutRuns.Last().Style == Current)
			{
				OutRuns.Last().Text += Pending;
			}
			else
			{
				FRichRun Run;
				Run.Text = Pending;
				Run.Style = Current;
				OutRuns.Add(Run);
			}
			OutPlain += Pending;
			if (OutSourceOffsets != nullptr)
			{
				OutSourceOffsets->Append(PendingSources);
			}
			Pending.Reset();
			PendingSources.Reset();
		};

		const int32 Length = InMarkup.Len();
		int32 Index = 0;
		while (Index < Length)
		{
			const TCHAR Char = InMarkup[Index];
			if (Char == TEXT('<'))
			{
				const int32 Close = InMarkup.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
				if (Close != INDEX_NONE)
				{
					FString Tag = InMarkup.Mid(Index + 1, Close - Index - 1).TrimStartAndEnd();
					const bool bClosing = Tag.StartsWith(TEXT("/"));
					const bool bSelfClosing = !bClosing && Tag.EndsWith(TEXT("/"));
					if (bClosing)
					{
						Tag.RightChopInline(1);
					}
					else if (bSelfClosing)
					{
						Tag.LeftChopInline(1);
					}
					FString Name = Tag.TrimStartAndEnd();
					FString TagValue;
					int32 EqualsAt = INDEX_NONE;
					if (Name.FindChar(TEXT('='), EqualsAt))
					{
						TagValue = Name.Mid(EqualsAt + 1).TrimStartAndEnd();
						Name = Name.Left(EqualsAt).TrimStartAndEnd();
					}
					if (IsTagName(Name))
					{
						Flush();
						if (bClosing)
						{
							for (int32 OpenIndex = Open.Num() - 1; OpenIndex >= 0; --OpenIndex)
							{
								if (Open[OpenIndex].Name.Equals(Name, ESearchCase::IgnoreCase))
								{
									Open.RemoveAt(OpenIndex);
									break;
								}
							}
						}
						else if (!bSelfClosing)
						{
							FOpenTag Opened;
							Opened.Name = Name;
							Opened.Value = TagValue.TrimStartAndEnd();
							Open.Add(Opened);
						}
						Current = StyleOf(Open, InBaseSize);
						Index = Close + 1;
						continue;
					}
				}
			}
			else if (Char == TEXT('&'))
			{
				const int32 Semicolon = InMarkup.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
				uint32 Codepoint = 0;
				if (Semicolon != INDEX_NONE && Semicolon - Index <= 12 && DecodeReference(InMarkup.Mid(Index + 1, Semicolon - Index - 1), Codepoint))
				{
					const int32 UnitsBefore = Pending.Len();
					AppendCodepoint(Pending, Codepoint);
					for (int32 Unit = UnitsBefore; Unit < Pending.Len(); ++Unit)
					{
						PendingSources.Add(Index);
					}
					Index = Semicolon + 1;
					continue;
				}
			}
			Pending.AppendChar(Char);
			PendingSources.Add(Index);
			++Index;
		}
		Flush();
	}

	FString GetPlainText(const FCase& InCase)
	{
		if (!InCase.bRich)
		{
			return InCase.Text;
		}
		TArray<FRichRun> Runs;
		FString Plain;
		ParseRichText(InCase.Text, InCase.Size, Runs, Plain);
		return Plain;
	}

	void DecodeCodepoints(const FString& InText, TArray<uint32>& OutCodepoints, TArray<int32>& OutOffsets)
	{
		OutCodepoints.Reset();
		OutOffsets.Reset();
		const int32 Length = InText.Len();
		for (int32 Index = 0; Index < Length; ++Index)
		{
			const uint32 First = static_cast<uint32>(InText[Index]);
			OutOffsets.Add(Index);
			if (First >= 0xD800u && First <= 0xDBFFu && Index + 1 < Length)
			{
				const uint32 Second = static_cast<uint32>(InText[Index + 1]);
				if (Second >= 0xDC00u && Second <= 0xDFFFu)
				{
					OutCodepoints.Add(((First - 0xD800u) << 10) + (Second - 0xDC00u) + 0x10000u);
					++Index;
					continue;
				}
			}
			OutCodepoints.Add(First);
		}
	}

	bool IsDefaultIgnorable(uint32 InCodepoint)
	{
		return InCodepoint < 0x20u
			|| InCodepoint == 0x7Fu
			|| InCodepoint == 0xADu
			|| InCodepoint == 0x34Fu
			|| InCodepoint == 0x61Cu
			|| (InCodepoint >= 0x180Bu && InCodepoint <= 0x180Fu)
			|| (InCodepoint >= 0x200Bu && InCodepoint <= 0x200Fu)
			|| (InCodepoint >= 0x202Au && InCodepoint <= 0x202Eu)
			|| (InCodepoint >= 0x2060u && InCodepoint <= 0x206Fu)
			|| (InCodepoint >= 0xFE00u && InCodepoint <= 0xFE0Fu)
			|| InCodepoint == 0xFEFFu
			|| (InCodepoint >= 0xE0000u && InCodepoint <= 0xE0FFFu);
	}

	const FFaceCoverage& GetFaceCoverage(const FString& InFile)
	{
		// Shared, not held by value: a map that grows moves its values, and a caller may hold the last answer.
		static TMap<FString, TSharedRef<FFaceCoverage>> Cache;
		if (const TSharedRef<FFaceCoverage>* Found = Cache.Find(InFile))
		{
			return Found->Get();
		}
		TSharedRef<FFaceCoverage> Coverage = MakeShared<FFaceCoverage>();
#if WITH_FREETYPE
		TArray<uint8> Bytes;
		if (FFileHelper::LoadFileToArray(Bytes, *InFile, FILEREAD_Silent))
		{
			FT_Library Library = nullptr;
			if (FT_Init_FreeType(&Library) == 0)
			{
				FT_Face Face = nullptr;
				if (FT_New_Memory_Face(Library, Bytes.GetData(), static_cast<FT_Long>(Bytes.Num()), 0, &Face) == 0)
				{
					FT_Select_Charmap(Face, FT_ENCODING_UNICODE);
					FT_UInt GlyphIndex = 0;
					FT_ULong Code = FT_Get_First_Char(Face, &GlyphIndex);
					while (GlyphIndex != 0)
					{
						Coverage->Codepoints.Add(static_cast<uint32>(Code));
						Code = FT_Get_Next_Char(Face, Code, &GlyphIndex);
					}
					Coverage->bLoaded = true;
					FT_Done_Face(Face);
				}
				FT_Done_FreeType(Library);
			}
		}
#endif
		Cache.Add(InFile, Coverage);
		return Coverage.Get();
	}

	int32 FindCoveringFace(const FFontKey& InKey, uint32 InCodepoint)
	{
		for (int32 FaceIndex = 0; FaceIndex < InKey.Faces.Num(); ++FaceIndex)
		{
			if (GetFaceCoverage(InKey.Faces[FaceIndex]).Codepoints.Contains(InCodepoint))
			{
				return FaceIndex;
			}
		}
		return INDEX_NONE;
	}
}

#endif
