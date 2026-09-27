// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"

namespace DreamUI
{
	/**
	 * Flags for every object DreamGUI makes while running and keeps in a property: render targets,
	 * dynamic material instances, data textures, the canvas mesh, body setups.
	 *
	 * - RF_Transient: the level or asset that holds the owner does not save it.
	 * - RF_DuplicateTransient: a copy of the owner -- above all the world a play session duplicates --
	 *   gets null in its place instead of a clone. The clone is the dangerous part: a UTexture2DDynamic
	 *   comes out 0x0, and cloning an object clones its whole outer chain with it, which is how one
	 *   reference drags a widget tree into the copy.
	 * - RF_TextExportTransient: copy and paste leaves it out of the copied text, which would otherwise
	 *   paste it back as an ordinary, saved sub-object of the new actor.
	 *
	 * RF_Transient alone stops only the first of the three.
	 */
	inline constexpr EObjectFlags RuntimeObjectFlags = RF_Transient | RF_DuplicateTransient | RF_TextExportTransient;
}
