// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIEachBindingHandler.h"

namespace DreamUIEachBindingHandlerLocal
{
	// A plain pointer, zero-initialized before any code runs: the control library may register from a
	// static initializer, and that must not depend on this translation unit having been initialized first.
	IDreamUIEachBindingHandler* Handler = nullptr;
}

IDreamUIEachBindingHandler* DreamUI::GetEachBindingHandler()
{
	return DreamUIEachBindingHandlerLocal::Handler;
}

void DreamUI::SetEachBindingHandler(IDreamUIEachBindingHandler* InHandler)
{
	DreamUIEachBindingHandlerLocal::Handler = InHandler;
}
