// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIRender/DreamUIRendererSettings.h"

namespace DreamUIRendererSettingsLocal
{
	TFunction<FDreamUIRendererSettings()>& Provider()
	{
		static TFunction<FDreamUIRendererSettings()> Instance;
		return Instance;
	}
}

void DreamUIRendererSettings::SetProvider(TFunction<FDreamUIRendererSettings()> InProvider)
{
	check(IsInGameThread());
	DreamUIRendererSettingsLocal::Provider() = MoveTemp(InProvider);
}

bool DreamUIRendererSettings::HasProvider()
{
	return static_cast<bool>(DreamUIRendererSettingsLocal::Provider());
}

FDreamUIRendererSettings DreamUIRendererSettings::Get()
{
	const TFunction<FDreamUIRendererSettings()>& Provider = DreamUIRendererSettingsLocal::Provider();
	return Provider ? Provider() : FDreamUIRendererSettings();
}
