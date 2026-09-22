// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriver.h"

#include "Core/Components/DreamWidget.h"

FDreamDriver::FDreamDriver(FDreamDriverContext& InContext)
	: Context(&InContext)
{
}

FDreamElementRef FDreamDriver::Find(const FDreamLocatorRef& InLocator)
{
	return MakeShared<FDreamDriverElement>(AsWeak(), InLocator);
}

TArray<FDreamElementRef> FDreamDriver::FindAll(const FDreamLocatorRef& InLocator)
{
	TArray<FDreamElementRef> Elements;
	if (Context == nullptr || !IsValid(Context->Root))
	{
		return Elements;
	}
	TArray<UDreamWidget*> Found;
	InLocator->Locate(Context->Root, Found);
	Elements.Reserve(Found.Num());
	for (UDreamWidget* Widget : Found)
	{
		// Pinned to the widget rather than carrying the original locator: an element from a list of
		// several has to keep meaning the one it came from, and re-asking a locator that matched three
		// widgets would not.
		Elements.Add(MakeShared<FDreamDriverElement>(AsWeak(), FDreamBy::Widget(Widget)));
	}
	return Elements;
}

FDreamDriverSequence FDreamDriver::Sequence()
{
	return FDreamDriverSequence(*Context);
}

bool FDreamDriver::Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout, const FString& InDescription)
{
	return Sequence().Wait(InWaitDelegate, InTimeout, InDescription).Perform();
}

bool FDreamDriver::Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout)
{
	return Sequence().Wait(InWaitDelegate, InTimeout).Perform();
}

bool FDreamDriver::Wait(const FDriverWaitDelegate& InWaitDelegate)
{
	return Sequence().Wait(InWaitDelegate).Perform();
}

void FDreamDriver::PumpFrames(int32 InFrameCount)
{
	if (Context != nullptr)
	{
		Context->PumpFrames(InFrameCount);
	}
}
