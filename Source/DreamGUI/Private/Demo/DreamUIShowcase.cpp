// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Demo/DreamUIShowcase.h"

#include "DreamGUI.h"
#include "Interaction/DreamUIModal.h"

FText UDreamUIShowcasePanel::GetNowPlaying() const
{
	if (Tracks.Num() == 0)
	{
		return NSLOCTEXT("DreamUIShowcase", "NothingPlaying", "Nothing playing");
	}
	if (const UDreamUIShowcaseTrack* First = Cast<UDreamUIShowcaseTrack>(Tracks[0]))
	{
		return FText::Format(NSLOCTEXT("DreamUIShowcase", "NowPlaying", "Now playing · {0} — {1}"),
			First->Title, First->Artist);
	}
	return FText::GetEmpty();
}

TArray<UObject*> UDreamUIShowcasePanel::GetHistory() const
{
	TArray<UObject*> Out;
	Out.Reserve(History.Num());
	for (const TObjectPtr<UObject>& Entry : History)
	{
		Out.Add(Entry);
	}
	return Out;
}

void UDreamUIShowcasePanel::PopulateDemoData(int32 InTrackCount)
{
	static const TCHAR* Titles[] = { TEXT("Aurora Drift"), TEXT("Moon Harbor"), TEXT("Glass Rain"),
		TEXT("Signal Fire"), TEXT("Paper Planet"), TEXT("Night Market"), TEXT("Low Orbit"), TEXT("Blue Hour") };
	static const TCHAR* Artists[] = { TEXT("Kite Theory"), TEXT("Nocturne Bureau"), TEXT("The Lanterns"),
		TEXT("Vega Street"), TEXT("Field Notes"), TEXT("Harbor Lights"), TEXT("Cassette Moon"), TEXT("Delta Sleep") };

	Tracks.Reset();
	const int32 Count = FMath::Clamp(InTrackCount, 0, static_cast<int32>(UE_ARRAY_COUNT(Titles)));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UDreamUIShowcaseTrack* Track = NewObject<UDreamUIShowcaseTrack>(this);
		Track->Title = FText::FromString(Titles[Index]);
		Track->Artist = FText::FromString(Artists[Index]);
		Track->Label = FText::FromString(FString::Printf(TEXT("%s — %s"), Titles[Index], Artists[Index]));
		Tracks.Add(Track);
	}

	History.Reset();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UDreamUIShowcaseTrack* Entry = NewObject<UDreamUIShowcaseTrack>(this);
		Entry->Label = FText::FromString(FString::Printf(TEXT("Yesterday · %s"), Titles[(Index + 3) % UE_ARRAY_COUNT(Titles)]));
		History.Add(Entry);
	}
}

void UDreamUIShowcasePanel::HandleOpenModal()
{
	UDreamUIModalSubsystem* Modal = UDreamUIModalSubsystem::Get(this);
	if (Modal == nullptr)
	{
		return;
	}
	if (*ModalDialogClass == nullptr)
	{
		UE_LOG(DreamGUI, Log, TEXT("[Showcase] Modal button clicked, but no ModalDialogClass is set."));
		return;
	}
	Modal->ShowModalNative(ModalDialogClass, [WeakThis = TWeakObjectPtr<UDreamUIShowcasePanel>(this)](FName InResult)
	{
		if (UDreamUIShowcasePanel* Panel = WeakThis.Get())
		{
			Panel->LastModalResult = InResult;
		}
	});
}

void UDreamUIShowcasePanel::HandleConfirm()
{
	++ConfirmCount;
	UE_LOG(DreamGUI, Log, TEXT("[Showcase] Confirm clicked (%d)."), ConfirmCount);
}

void UDreamUIShowcasePanel::HandleChipDropped(UDreamDragDropOperation* InOperation)
{
	++DropCount;
	UE_LOG(DreamGUI, Log, TEXT("[Showcase] Chip dropped (%d)."), DropCount);
}
