// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class IDreamDesignerHandleProvider;

/** The viewport handles of a warp's lattice and of a path's points (Designer/DreamDesignerHandles.h), which the editor module registers. */
namespace DreamDesignerPointHandles
{
	extern const FName LatticeProviderName;
	extern const FName PathProviderName;
	TSharedRef<IDreamDesignerHandleProvider> MakeLatticeProvider();
	TSharedRef<IDreamDesignerHandleProvider> MakePathProvider();
}
