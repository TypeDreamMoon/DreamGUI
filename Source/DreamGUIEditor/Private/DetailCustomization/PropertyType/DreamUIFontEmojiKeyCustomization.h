#pragma once
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/DreamUITextData.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/Char.h"
#include "Misc/Parse.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "DreamUIFontEmojiKeyCustomization"

/**
 * An emoji data key, in the row of its entry: the emoji as characters, and the code points it is found by -- its
 * Sequence, U+FE0E and U+FE0F left out -- in hex. Either can be typed into. The key is then made by
 * FDreamUIFontEmojiKey::ApplyEmoji, so it is always one cluster segmented the way texts segment them, and written
 * through the property system as one change to the key: the asset is dirtied, the edit can be undone, and the map is
 * rehashed (a key hashes its sequence) before the asset hears of it. A key another entry already has is refused, since
 * the map could reach only one of the two.
 */
class FDreamUIFontEmojiKeyCustomization : public IPropertyTypeCustomization
{
public:
	FDreamUIFontEmojiKeyCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FDreamUIFontEmojiKeyCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		// A map key's value content is drawn in the name column of its entry's row, so both boxes share that width.
		HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(160.0f)
		[
			SNew(SHorizontalBox)
			.IsEnabled(TAttribute<bool>(PropertyHandle, &IPropertyHandle::IsEditable))
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(52.0f)
				[
					SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.SelectAllTextWhenFocused(true)
					.RevertTextOnEscape(true)
					.HintText(LOCTEXT("CharactersHint", "Emoji"))
					.ToolTipText(LOCTEXT("Characters_Tooltip", "The emoji this entry draws: one emoji, a flag, a keycap, a ZWJ sequence or an emoji with a skin tone."))
					.Text_Lambda([PropertyHandle]() { return FText::FromString(GetCharactersText(PropertyHandle)); })
					.OnTextCommitted_Lambda([PropertyHandle](const FText& InText, ETextCommit::Type InCommitType)
					{
						const FString Characters = InText.ToString();
						// Leaving the box unchanged is not an edit.
						if (InCommitType == ETextCommit::OnCleared || Characters.Equals(GetCharactersText(PropertyHandle), ESearchCase::CaseSensitive))return;
						SetKeyFromCharacters(PropertyHandle, Characters);
					})
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.HintText(LOCTEXT("SequenceHint", "U+1F44D U+1F3FD"))
				.ToolTipText(LOCTEXT("Sequence_Tooltip", "The code points this entry is found by, in hex. U+FE0E and U+FE0F are left out: they choose between a character's text and emoji forms, not a picture."))
				.Text_Lambda([PropertyHandle]() { return FText::FromString(GetSequenceText(PropertyHandle)); })
				.OnTextCommitted_Lambda([PropertyHandle](const FText& InText, ETextCommit::Type InCommitType)
				{
					const FString Hex = InText.ToString();
					if (InCommitType == ETextCommit::OnCleared || Hex.Equals(GetSequenceText(PropertyHandle), ESearchCase::CaseSensitive))return;
					FString Characters;
					if (!ParseSequence(Hex, Characters))
					{
						Notify(FText::Format(LOCTEXT("NotASequence", "\"{0}\" is not a list of code points: type them in hex, as U+1F44D U+1F3FD or 1F44D 1F3FD."), InText));
						return;
					}
					SetKeyFromCharacters(PropertyHandle, Characters);
				})
			]
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}

private:
	/** The key in every object the row edits; false when one cannot be read. */
	static bool ReadKeys(const TSharedRef<IPropertyHandle>& InHandle, TArray<const FDreamUIFontEmojiKey*>& OutKeys)
	{
		TArray<const void*> RawData;
		InHandle->AccessRawData(RawData);
		OutKeys.Reset();
		for (const void* Raw : RawData)
		{
			if (Raw == nullptr)return false;
			OutKeys.Add(static_cast<const FDreamUIFontEmojiKey*>(Raw));
		}
		return OutKeys.Num() > 0;
	}

	/** The one key the row shows; null when the objects it edits disagree, and the boxes show nothing. */
	static const FDreamUIFontEmojiKey* GetSharedKey(const TSharedRef<IPropertyHandle>& InHandle)
	{
		TArray<const FDreamUIFontEmojiKey*> Keys;
		if (!InHandle->IsValidHandle() || !ReadKeys(InHandle, Keys))return nullptr;
		for (const FDreamUIFontEmojiKey* Key : Keys)
		{
			if (!(*Key == *Keys[0]) || !Key->EmojiChar.Equals(Keys[0]->EmojiChar, ESearchCase::CaseSensitive))return nullptr;
		}
		return Keys[0];
	}

	static void AppendCodepoint(FString& OutText, uint32 InCodepoint)
	{
		if (InCodepoint >= FDreamUIText_CodePoint::UNICODE_PLANE01_START)
		{
			const uint32 Offset = InCodepoint - FDreamUIText_CodePoint::UNICODE_PLANE01_START;
			OutText.AppendChar((TCHAR)(FDreamUIText_CodePoint::HIGH_SURROGATE_START + (Offset >> 10)));
			OutText.AppendChar((TCHAR)(FDreamUIText_CodePoint::LOW_SURROGATE_START + (Offset & 0x3FF)));
		}
		else
		{
			OutText.AppendChar((TCHAR)InCodepoint);
		}
	}

	/** What the key was typed as; for one that never was (made in code, or loaded from before EmojiChar), its sequence. */
	static FString GetCharactersText(const TSharedRef<IPropertyHandle>& InHandle)
	{
		const FDreamUIFontEmojiKey* Key = GetSharedKey(InHandle);
		if (Key == nullptr)return FString();
		if (!Key->EmojiChar.IsEmpty())return Key->EmojiChar;
		FString Characters;
		for (int32 Index = 0; Index < Key->GetSequenceLength(); Index++)
		{
			const int32 Codepoint = Key->GetSequenceAt(Index);
			if (Codepoint > 0)
			{
				AppendCodepoint(Characters, (uint32)Codepoint);
			}
		}
		return Characters;
	}

	/** "U+1F44D U+1F3FD": the sequence the key is found by; empty for a key with none. */
	static FString GetSequenceText(const TSharedRef<IPropertyHandle>& InHandle)
	{
		const FDreamUIFontEmojiKey* Key = GetSharedKey(InHandle);
		if (Key == nullptr)return FString();
		TArray<FString> Codepoints;
		for (int32 Index = 0; Index < Key->GetSequenceLength(); Index++)
		{
			const int32 Codepoint = Key->GetSequenceAt(Index);
			if (Codepoint > 0)
			{
				Codepoints.Add(FString::Printf(TEXT("U+%04X"), Codepoint));
			}
		}
		return FString::Join(Codepoints, TEXT(" "));
	}

	/** Code points in hex -- "U+1F44D U+1F3FD", "1F44D 1F3FD", "0x1F44D, 0x1F3FD" -- as the text they spell. None at all is an empty text. */
	static bool ParseSequence(const FString& InHex, FString& OutCharacters)
	{
		FString Normalized = InHex.ToUpper();
		Normalized.ReplaceInline(TEXT("U+"), TEXT(" "), ESearchCase::CaseSensitive);
		Normalized.ReplaceInline(TEXT("0X"), TEXT(" "), ESearchCase::CaseSensitive);
		for (int32 Index = 0; Index < Normalized.Len(); Index++)
		{
			TCHAR& Char = Normalized[Index];
			if (Char == TEXT(',') || Char == TEXT(';') || Char == TEXT('+'))
			{
				Char = TEXT(' ');
			}
		}
		TArray<FString> Tokens;
		Normalized.ParseIntoArrayWS(Tokens);
		OutCharacters.Reset();
		for (const FString& Token : Tokens)
		{
			if (Token.Len() > 6)return false;
			for (int32 Index = 0; Index < Token.Len(); Index++)
			{
				if (!FChar::IsHexDigit(Token[Index]))return false;
			}
			const uint32 Codepoint = FParse::HexNumber(Token);
			if (Codepoint == 0 || Codepoint > 0x10FFFF || (Codepoint >= FDreamUIText_CodePoint::HIGH_SURROGATE_START && Codepoint <= FDreamUIText_CodePoint::LOW_SURROGATE_END))return false;
			AppendCodepoint(OutCharacters, Codepoint);
		}
		return true;
	}

	/** What ApplyEmoji leaves when the characters are not one emoji cluster: a key standing for nothing. */
	static bool IsEmptyKey(const FDreamUIFontEmojiKey& InKey)
	{
		return InKey.EmojiCode == 0 || InKey.EmojiChar.IsEmpty();
	}

	/**
	 * The key these characters make, by ApplyEmoji as ever. A base that is an emoji only in its emoji form (U+2764, a
	 * digit before U+20E3) and comes without its U+FE0F gets one: a key leaves the selector out, so typing a sequence as
	 * the hex box shows it has to make the same key again.
	 */
	static FDreamUIFontEmojiKey MakeKey(const FDreamUIFontEmojiKey& InCurrent, const FString& InCharacters)
	{
		FDreamUIFontEmojiKey Key = InCurrent;
		Key.EmojiChar = InCharacters;
		Key.ApplyEmoji();
		if (IsEmptyKey(Key) && !InCharacters.IsEmpty())
		{
			int BaseUnits = 1;
			FDreamUIText_CodePoint::DecodeCodePointAt(InCharacters, InCharacters.Len(), 0, BaseUnits);
			Key = InCurrent;
			Key.EmojiChar = InCharacters.Left(BaseUnits) + FString::Chr((TCHAR)FDreamUIText_CodePoint::UNICODE_VS_COLOR) + InCharacters.Mid(BaseUnits);
			Key.ApplyEmoji();
		}
		if (IsEmptyKey(Key))
		{
			// One empty key, whatever the characters were: it hashes and compares as the sequence {0}.
			Key.EmojiChar.Reset();
			Key.EmojiCode = 0;
			Key.VariantSelector = 0;
			Key.Sequence.Reset();
		}
		return Key;
	}

	/** Whether the map that holds InKey has another key equal to InNewKey, as the map compares them: by sequence. */
	static bool HasOtherEqualKey(const FMapProperty* InMapProperty, const TArray<const void*>& InRawMaps, const FDreamUIFontEmojiKey* InKey, const FDreamUIFontEmojiKey& InNewKey)
	{
		for (const void* RawMap : InRawMaps)
		{
			if (RawMap == nullptr)continue;
			FScriptMapHelper MapHelper(InMapProperty, RawMap);
			bool bHoldsKey = false;
			bool bHasEqualKey = false;
			for (FScriptMapHelper::FIterator It(MapHelper); It; ++It)
			{
				const FDreamUIFontEmojiKey* OtherKey = reinterpret_cast<const FDreamUIFontEmojiKey*>(MapHelper.GetKeyPtr(It));
				if (OtherKey == InKey)
				{
					bHoldsKey = true;
				}
				else if (*OtherKey == InNewKey)
				{
					bHasEqualKey = true;
				}
			}
			if (bHoldsKey)return bHasEqualKey;
		}
		return false;
	}

	/**
	 * Writes the key these characters make into every object the row edits, as one change to the key property. The
	 * property system rehashes a map whose key it imports, so the entry is found by its new sequence by the time the
	 * asset's PostEditChangeProperty tells the texts using it. Nothing is written when any object refuses the key.
	 */
	static void SetKeyFromCharacters(const TSharedRef<IPropertyHandle>& InHandle, const FString& InCharacters)
	{
		const FProperty* KeyProperty = InHandle->GetProperty();
		TArray<const FDreamUIFontEmojiKey*> Keys;
		if (KeyProperty == nullptr || !ReadKeys(InHandle, Keys))return;
		const TSharedPtr<IPropertyHandle> MapHandle = InHandle->GetParentHandle();
		const FMapProperty* MapProperty = MapHandle.IsValid() ? CastField<FMapProperty>(MapHandle->GetProperty()) : nullptr;
		TArray<const void*> RawMaps;
		if (MapProperty != nullptr)
		{
			MapHandle->AccessRawData(RawMaps);
		}

		TArray<FString> PerObjectValues;
		for (const FDreamUIFontEmojiKey* Key : Keys)
		{
			const FDreamUIFontEmojiKey NewKey = MakeKey(*Key, InCharacters);
			if (IsEmptyKey(NewKey) && !InCharacters.IsEmpty())
			{
				Notify(FText::Format(LOCTEXT("NotAnEmoji", "\"{0}\" is not an emoji. An entry is one emoji: a pictograph, a flag, a keycap, a ZWJ sequence or an emoji with a skin tone."), FText::FromString(InCharacters)));
				return;
			}
			if (MapProperty != nullptr && HasOtherEqualKey(MapProperty, RawMaps, Key, NewKey))
			{
				Notify(IsEmptyKey(NewKey)
					? LOCTEXT("DuplicateEmptyEmoji", "This emoji data already has an entry with no emoji.")
					: FText::Format(LOCTEXT("DuplicateEmoji", "This emoji data already has an entry for {0}."), FText::FromString(NewKey.EmojiChar)));
				return;
			}
			KeyProperty->ExportText_Direct(PerObjectValues.AddDefaulted_GetRef(), &NewKey, &NewKey, nullptr, PPF_None);
		}
		InHandle->SetPerObjectValues(PerObjectValues);
	}

	static void Notify(const FText& InMessage)
	{
		FNotificationInfo Info(InMessage);
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
};
#undef LOCTEXT_NAMESPACE
