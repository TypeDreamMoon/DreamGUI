// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIAst.h"

/*
 * The two walks every consumer of the tree would otherwise write for itself.
 *
 * They live here rather than in the parser because the AST outlives it: the builder resolves styles,
 * the write-back patcher hunts for the node holding a property, and a headless syntax check counts
 * nodes -- three callers, one traversal order. Three private copies of "recurse into Children" is
 * how one of them quietly forgets that loop bodies are children too.
 */

namespace DreamUIAstLocal
{
	void VisitDepthFirst(const FDreamUINode& InNode, TFunctionRef<void(const FDreamUINode&)> InPredicate)
	{
		InPredicate(InNode);
		for (const FDreamUINode& Child : InNode.Children)
		{
			VisitDepthFirst(Child, InPredicate);
		}
	}
}

const FDreamUIStyle* FDreamUIAst::FindStyle(const FString& InName) const
{
	// FString's own comparison, which is case insensitive, and that is the intended rule rather than
	// an oversight. Every name a .dui writes ends up as an FName somewhere downstream -- a node id
	// becomes a member variable, a style name is only ever matched against other names in the same
	// file -- and FName does not distinguish case. A parser that treated Card and card as two styles
	// would be stricter than the thing it feeds, which is the direction that produces "it resolved in
	// the editor and not in the build". Keywords are the opposite and are matched case sensitively:
	// they are grammar, not names.
	for (const FDreamUIStyle& Style : Styles)
	{
		if (Style.Name == InName)
		{
			return &Style;
		}
	}
	return nullptr;
}

void FDreamUIAst::ForEachNode(TFunctionRef<void(const FDreamUINode&)> InPredicate) const
{
	// bHasRoot rather than "is Root default constructed", because a parse that failed early leaves a
	// Root that is empty but not meaningfully absent, and callers that check emptiness instead would
	// each pick their own definition of empty.
	if (bHasRoot)
	{
		DreamUIAstLocal::VisitDepthFirst(Root, InPredicate);
	}
}
