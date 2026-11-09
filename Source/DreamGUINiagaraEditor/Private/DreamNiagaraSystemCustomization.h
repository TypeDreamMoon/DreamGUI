// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class UDreamNiagaraSystem;
struct FDreamNiagaraRendererInfo;

/**
 * The Niagara visual's details: what each of the system's renderers is drawn as -- or why it is not, a GPU emitter, a mesh or
 * light renderer -- and the button that converts the particle materials into UI materials.
 */
class FDreamNiagaraSystemCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	/** One line of the report. */
	static FText DescribeRenderer(const FDreamNiagaraRendererInfo& InInfo);

private:
	FReply ConvertMaterials();
	TArray<TWeakObjectPtr<UDreamNiagaraSystem>> Visuals;
};
