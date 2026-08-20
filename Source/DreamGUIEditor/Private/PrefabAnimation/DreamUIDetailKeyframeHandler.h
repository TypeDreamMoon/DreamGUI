// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailKeyframeHandler.h"

class ISequencer;
class FDreamUIPrefabEditor;
class IPropertyHandle;

class FDreamUIDetailKeyframeHandler : public IDetailKeyframeHandler
{
public:
	FDreamUIDetailKeyframeHandler( TSharedPtr<FDreamUIPrefabEditor> InSequenceEditor );

	virtual bool IsPropertyKeyable(const UClass* InObjectClass, const class IPropertyHandle& PropertyHandle) const override;

	virtual bool IsPropertyKeyingEnabled() const override;

	virtual void OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle) override;

	virtual bool IsPropertyAnimated(const class IPropertyHandle& PropertyHandle, UObject *ParentObject) const override;

	virtual EPropertyKeyedStatus GetPropertyKeyedStatus(const IPropertyHandle& PropertyHandle) const override;

private:
	TSharedPtr<ISequencer> GetSequencer() const;
	TWeakPtr<FDreamUIPrefabEditor> PrefabEditor;
};
