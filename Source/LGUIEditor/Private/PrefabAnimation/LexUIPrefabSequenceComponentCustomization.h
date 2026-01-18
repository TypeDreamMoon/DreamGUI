// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Widgets/Layout/SBox.h"
#include "Input/Reply.h"
#include "UObject/WeakObjectPtr.h"
#include "Framework/Docking/TabManager.h"

class IDetailsView;
class ULexUIPrefabSequence;
class ULexUIPrefabSequenceComponent;
class ISequencer;
class FSCSEditorTreeNode;
class IPropertyUtilities;

class FLexUIPrefabSequenceComponentCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	FReply InvokeSequencer();

	TWeakObjectPtr<ULexUIPrefabSequenceComponent> WeakSequenceComponent;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};