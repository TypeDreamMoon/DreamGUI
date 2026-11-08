// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Import/DreamPsdDuiMerge.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextPatcher.h"

// ---------------------------------------------------------------------------------------------------------------------
// The manifest
// ---------------------------------------------------------------------------------------------------------------------

FDreamPsdImportManifest FDreamPsdImportManifest::FromTree(const FDreamPsdDuiNode& InRoot)
{
	FDreamPsdImportManifest M;
	InRoot.ForEach([&M](const FDreamPsdDuiNode& Node, const FString&)
	{
		if (Node.LayerId < 0)
		{
			return;
		}
		FNode& Entry = M.Nodes.Add(Node.Id);
		Entry.LayerId = Node.LayerId;
		Entry.Type = Node.Type;
		for (const FDreamPsdDuiProperty& P : Node.Properties)
		{
			Entry.Properties.Add(P.Name, P.Value);
		}
		for (const FDreamPsdDuiComponent& C : Node.Components)
		{
			TMap<FString, FString>& Values = Entry.Components.Add(C.ClassName);
			for (const FDreamPsdDuiProperty& P : C.Properties)
			{
				Values.Add(P.Name, P.Value);
			}
		}
	});
	return M;
}

FString FDreamPsdImportManifest::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetStringField(TEXT("source"), SourceFile);
	Root->SetStringField(TEXT("textureFolder"), TextureFolder);
	Root->SetStringField(TEXT("blueprint"), BlueprintPath);
	TArray<TSharedPtr<FJsonValue>> TextureValues;
	for (const FString& T : Textures)
	{
		TextureValues.Add(MakeShared<FJsonValueString>(T));
	}
	Root->SetArrayField(TEXT("textures"), TextureValues);
	TSharedRef<FJsonObject> NodesObject = MakeShared<FJsonObject>();
	TArray<FString> Ids;
	Nodes.GetKeys(Ids);
	Ids.Sort();
	for (const FString& Id : Ids)
	{
		const FNode& N = Nodes[Id];
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("layer"), N.LayerId);
		Object->SetStringField(TEXT("type"), N.Type);
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& P : N.Properties)
		{
			Properties->SetStringField(P.Key, P.Value);
		}
		Object->SetObjectField(TEXT("properties"), Properties);
		TSharedRef<FJsonObject> Components = MakeShared<FJsonObject>();
		for (const TPair<FString, TMap<FString, FString>>& C : N.Components)
		{
			TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
			for (const TPair<FString, FString>& P : C.Value)
			{
				Values->SetStringField(P.Key, P.Value);
			}
			Components->SetObjectField(C.Key, Values);
		}
		Object->SetObjectField(TEXT("components"), Components);
		NodesObject->SetObjectField(Id, Object);
	}
	Root->SetObjectField(TEXT("nodes"), NodesObject);
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

bool FDreamPsdImportManifest::FromJson(const FString& InJson, FDreamPsdImportManifest& Out)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	Out = FDreamPsdImportManifest();
	Out.SourceFile = Root->GetStringField(TEXT("source"));
	Out.TextureFolder = Root->GetStringField(TEXT("textureFolder"));
	Out.BlueprintPath = Root->GetStringField(TEXT("blueprint"));
	const TArray<TSharedPtr<FJsonValue>>* Textures = nullptr;
	if (Root->TryGetArrayField(TEXT("textures"), Textures))
	{
		for (const TSharedPtr<FJsonValue>& V : *Textures)
		{
			Out.Textures.Add(V->AsString());
		}
	}
	const TSharedPtr<FJsonObject>* Nodes = nullptr;
	if (Root->TryGetObjectField(TEXT("nodes"), Nodes))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Nodes)->Values)
		{
			const TSharedPtr<FJsonObject> Object = Entry.Value->AsObject();
			if (!Object.IsValid())
			{
				continue;
			}
			FNode& N = Out.Nodes.Add(Entry.Key);
			N.LayerId = (int32)Object->GetNumberField(TEXT("layer"));
			N.Type = Object->GetStringField(TEXT("type"));
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			if (Object->TryGetObjectField(TEXT("properties"), Properties))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Properties)->Values)
				{
					N.Properties.Add(P.Key, P.Value->AsString());
				}
			}
			const TSharedPtr<FJsonObject>* Components = nullptr;
			if (Object->TryGetObjectField(TEXT("components"), Components))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& C : (*Components)->Values)
				{
					TMap<FString, FString>& Values = N.Components.Add(C.Key);
					const TSharedPtr<FJsonObject> ValuesObject = C.Value->AsObject();
					if (ValuesObject.IsValid())
					{
						for (const TPair<FString, TSharedPtr<FJsonValue>>& P : ValuesObject->Values)
						{
							Values.Add(P.Key, P.Value->AsString());
						}
					}
				}
			}
		}
	}
	return true;
}

FString FDreamPsdImportManifest::PathFor(const FString& InDuiFile)
{
	return InDuiFile + TEXT(".psdimport.json");
}

// ---------------------------------------------------------------------------------------------------------------------
// The merge
// ---------------------------------------------------------------------------------------------------------------------

namespace DreamPsdDuiMergeLocal
{
	/** The tree of a parsed file, by id (ids compare without case). */
	struct FIndex
	{
		struct FEntry
		{
			const FDreamUINode* Node = nullptr;
			FString ParentId;
		};
		TMap<FString, FEntry> ById;
		/** Layer id -> node id, for the generated nodes. */
		TMap<int32, FString> ByLayer;
		FString RootId;

		void Build(const FDreamUIAst& InAst)
		{
			ById.Reset();
			ByLayer.Reset();
			RootId = InAst.Root.Id;
			Walk(InAst.Root, FString());
		}

		void Walk(const FDreamUINode& InNode, const FString& InParent)
		{
			if (!InNode.Id.IsEmpty())
			{
				ById.Add(InNode.Id.ToLower(), { &InNode, InParent });
				int32 Layer = -1;
				if (DreamPsdDui::ParseGeneratedId(InNode.Id, Layer))
				{
					ByLayer.Add(Layer, InNode.Id);
				}
			}
			for (const FDreamUINode& Child : InNode.Children)
			{
				Walk(Child, InNode.Id.IsEmpty() ? InParent : InNode.Id);
			}
		}

		const FEntry* Find(const FString& InId) const { return ById.Find(InId.ToLower()); }
	};

	/** What a .dui value looks like once parsed: no quotes, no '#', no spaces in a tuple. */
	FString Normalize(const FString& InValueText)
	{
		FString V = InValueText.TrimStartAndEnd();
		if (V.Len() >= 2 && V.StartsWith(TEXT("\"")) && V.EndsWith(TEXT("\"")))
		{
			V = V.Mid(1, V.Len() - 2);
			V.ReplaceInline(TEXT("\\\""), TEXT("\""));
			V.ReplaceInline(TEXT("\\n"), TEXT("\n"));
			V.ReplaceInline(TEXT("\\t"), TEXT("\t"));
			V.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
			return V;
		}
		if (V.StartsWith(TEXT("#")))
		{
			return V.Mid(1).ToUpper();
		}
		V.ReplaceInline(TEXT(" "), TEXT(""));
		return V;
	}

	FString CurrentText(const FDreamUIProperty& InProperty)
	{
		if (InProperty.Value.Kind == EDreamUIValueKind::Tuple)
		{
			return TEXT("(") + FString::Join(InProperty.Value.Elements, TEXT(",")) + TEXT(")");
		}
		if (InProperty.Value.Kind == EDreamUIValueKind::HexColor)
		{
			return InProperty.Value.Raw.ToUpper();
		}
		return InProperty.Value.Raw;
	}

	struct FMerger
	{
		FString& Text;
		const FDreamPsdImportManifest* Last;
		const FDreamPsdDuiNode& NewRoot;
		FDreamPsdMergeReport& Report;
		FDreamUIAst Ast;
		FIndex Index;

		/** New tree, by layer: the node and its parent's id. */
		TMap<int32, TPair<const FDreamPsdDuiNode*, FString>> NewByLayer;

		FMerger(FString& InText, const FDreamPsdImportManifest* InLast, const FDreamPsdDuiNode& InNewRoot, FDreamPsdMergeReport& InReport)
			: Text(InText), Last(InLast), NewRoot(InNewRoot), Report(InReport) {}

		bool Parse()
		{
			FDreamUIDiagnosticBag Diagnostics;
			Ast = FDreamUIAst();
			const bool bOk = FDreamUISourceFile::Parse(Text, TEXT("import"), Ast, Diagnostics, FDreamUISourceFile::MakeFileImportReader());
			if (!bOk || !Ast.bHasRoot)
			{
				Report.Errors.Add(FString::Printf(TEXT("the .dui does not parse, so it cannot be merged into: %s"), *Diagnostics.ToString()));
				return false;
			}
			Index.Build(Ast);
			return true;
		}

		/** The new parent of a node, as an id in the file: the file's own root id stands for the import's root. */
		FString FileId(const FString& InNewId) const
		{
			return InNewId == NewRoot.Id ? Index.RootId : InNewId;
		}

		bool Apply(const TArray<FDreamUIStructuralEdit>& InEdits)
		{
			if (InEdits.Num() == 0)
			{
				return true;
			}
			FDreamUIDiagnosticBag Diagnostics;
			if (!FDreamUITextPatcher::ApplyStructuralEdits(Text, Ast, InEdits, Diagnostics))
			{
				Report.Conflicts.Add(FString::Printf(TEXT("a change to the file's structure was refused: %s"), *Diagnostics.ToString()));
			}
			return Parse();
		}

		bool IsGenerated(const FString& InId) const
		{
			int32 Layer;
			return DreamPsdDui::ParseGeneratedId(InId, Layer);
		}

		/** What the last import wrote on the node of a layer -- by its layer, since a renamed layer's node has a new id. */
		const FDreamPsdImportManifest::FNode* FindLast(const FDreamPsdDuiNode& InNew) const
		{
			if (Last == nullptr || InNew.LayerId < 0)
			{
				return nullptr;
			}
			for (const TPair<FString, FDreamPsdImportManifest::FNode>& Entry : Last->Nodes)
			{
				if (Entry.Value.LayerId == InNew.LayerId)
				{
					return &Entry.Value;
				}
			}
			return nullptr;
		}

		bool Run()
		{
			if (!Parse())
			{
				return false;
			}
			NewRoot.ForEach([this](const FDreamPsdDuiNode& Node, const FString& Parent)
			{
				if (Node.LayerId >= 0)
				{
					NewByLayer.Add(Node.LayerId, TPair<const FDreamPsdDuiNode*, FString>(&Node, Parent));
				}
			});
			return RemoveGone() && RenameAndRetype() && Restructure() && UpdateComponents() && UpdateProperties();
		}

		/** Nodes whose layers are gone: what an author put inside them moves up, then they go. */
		bool RemoveGone()
		{
			TArray<FString> Gone;
			for (const TPair<int32, FString>& Entry : Index.ByLayer)
			{
				if (!NewByLayer.Contains(Entry.Key))
				{
					Gone.Add(Entry.Value);
				}
			}
			if (Gone.Num() == 0)
			{
				return true;
			}
			TSet<FString> GoneSet;
			for (const FString& Id : Gone)
			{
				GoneSet.Add(Id.ToLower());
			}
			// Rescue, one move at a time: an author's node inside a gone node, and a generated node that is still in the
			// document, go up to the nearest ancestor that stays.
			bool bMoved = true;
			while (bMoved)
			{
				bMoved = false;
				for (const FString& Id : Gone)
				{
					const FIndex::FEntry* Entry = Index.Find(Id);
					if (Entry == nullptr)
					{
						continue;
					}
					for (const FDreamUINode& Child : Entry->Node->Children)
					{
						if (Child.Id.IsEmpty() || GoneSet.Contains(Child.Id.ToLower()))
						{
							continue;
						}
						FString Destination = Entry->ParentId;
						while (GoneSet.Contains(Destination.ToLower()))
						{
							const FIndex::FEntry* Up = Index.Find(Destination);
							Destination = Up != nullptr ? Up->ParentId : Index.RootId;
						}
						FDreamUIStructuralEdit Move;
						Move.Kind = EDreamUIStructuralEditKind::MoveNode;
						Move.NodeId = Child.Id;
						Move.ParentId = Destination == Index.RootId ? FString() : Destination;
						Move.BeforeId = Id;
						if (!IsGenerated(Child.Id))
						{
							Report.Conflicts.Add(FString::Printf(TEXT("%s was inside %s, whose layer is gone: it is kept, moved up to %s"),
								*Child.Id, *Id, *Destination));
						}
						if (!Apply({ Move }))
						{
							return false;
						}
						bMoved = true;
						break;
					}
					if (bMoved)
					{
						break;
					}
				}
			}
			TArray<FDreamUIStructuralEdit> Removes;
			for (const FString& Id : Gone)
			{
				const FIndex::FEntry* Entry = Index.Find(Id);
				if (Entry == nullptr || GoneSet.Contains(Entry->ParentId.ToLower()))
				{
					continue;
				}
				FDreamUIStructuralEdit Remove;
				Remove.Kind = EDreamUIStructuralEditKind::RemoveNode;
				Remove.NodeId = Id;
				Removes.Add(Remove);
				Report.Changes.Add(FString::Printf(TEXT("removed %s: its layer is no longer in the document"), *Id));
			}
			return Apply(Removes);
		}

		bool RenameAndRetype()
		{
			TArray<FDreamUIStructuralEdit> Edits;
			for (const TPair<int32, TPair<const FDreamPsdDuiNode*, FString>>& Entry : NewByLayer)
			{
				const FString* OldId = Index.ByLayer.Find(Entry.Key);
				if (OldId == nullptr)
				{
					continue;
				}
				const FDreamPsdDuiNode& New = *Entry.Value.Key;
				const FIndex::FEntry* Old = Index.Find(*OldId);
				if (Old != nullptr && !Old->Node->TypeName.Equals(New.Type))
				{
					FDreamUIStructuralEdit Retype;
					Retype.Kind = EDreamUIStructuralEditKind::RetypeNode;
					Retype.NodeId = *OldId;
					Retype.TypeName = New.Type;
					Edits.Add(Retype);
					Report.Changes.Add(FString::Printf(TEXT("%s is now a %s (was a %s)"), *New.Id, *New.Type, *Old->Node->TypeName));
				}
				if (!OldId->Equals(New.Id, ESearchCase::CaseSensitive))
				{
					FDreamUIStructuralEdit Rename;
					Rename.Kind = EDreamUIStructuralEditKind::RenameNode;
					Rename.NodeId = *OldId;
					Rename.NewId = New.Id;
					Edits.Add(Rename);
					Report.Changes.Add(FString::Printf(TEXT("renamed %s to %s, its layer having been renamed (the file says was: %s)"), **OldId, *New.Id, **OldId));
				}
			}
			// One at a time: a retype and a rename of the same node touch the same header line.
			for (const FDreamUIStructuralEdit& Edit : Edits)
			{
				if (!Apply({ Edit }))
				{
					return false;
				}
			}
			return true;
		}

		/** Every generated node under its parent, in the document's order, inserted where it is missing. */
		bool Restructure()
		{
			TArray<const FDreamPsdDuiNode*> Parents;
			NewRoot.ForEach([&Parents](const FDreamPsdDuiNode& Node, const FString&) { Parents.Add(&Node); });
			for (const FDreamPsdDuiNode* Parent : Parents)
			{
				const FString ParentId = FileId(Parent->Id);
				const int32 Count = Parent->Children.Num();
				// Last first: each child goes in front of the one after it, which is already where it belongs.
				for (int32 i = Count - 1; i >= 0; --i)
				{
					const FDreamPsdDuiNode& Child = Parent->Children[i];
					const FString NextId = i + 1 < Count ? Parent->Children[i + 1].Id : FString();
					const FIndex::FEntry* Existing = Index.Find(Child.Id);
					FDreamUIStructuralEdit Edit;
					Edit.ParentId = ParentId == Index.RootId ? FString() : ParentId;
					Edit.BeforeId = NextId;
					if (Existing == nullptr)
					{
						Edit.Kind = EDreamUIStructuralEditKind::InsertNode;
						Edit.TypeName = Child.Type;
						Edit.NewId = Child.Id;
						const bool bWasThere = FindLast(Child) != nullptr;
						if (bWasThere)
						{
							Report.Conflicts.Add(FString::Printf(TEXT("%s had been deleted from the file; it is back, as its layer is still in the document (put !skip in the layer's name to leave it out)"), *Child.Id));
						}
						else
						{
							Report.Changes.Add(FString::Printf(TEXT("added %s for a new layer"), *Child.Id));
						}
						if (!Apply({ Edit }))
						{
							return false;
						}
						continue;
					}
					const bool bRightParent = Existing->ParentId.Equals(ParentId, ESearchCase::IgnoreCase);
					bool bInOrder = bRightParent;
					if (bRightParent && !NextId.IsEmpty())
					{
						// In order when the next generated sibling follows it under the same parent.
						const FIndex::FEntry* Next = Index.Find(NextId);
						const FIndex::FEntry* ParentEntry = Index.Find(ParentId);
						if (Next != nullptr && ParentEntry != nullptr)
						{
							int32 Mine = INDEX_NONE, Theirs = INDEX_NONE;
							const TArray<FDreamUINode>& Siblings = ParentEntry->Node->Children;
							for (int32 s = 0; s < Siblings.Num(); ++s)
							{
								if (Siblings[s].Id.Equals(Child.Id, ESearchCase::IgnoreCase)) { Mine = s; }
								if (Siblings[s].Id.Equals(NextId, ESearchCase::IgnoreCase)) { Theirs = s; }
							}
							bInOrder = Mine != INDEX_NONE && Theirs != INDEX_NONE && Mine < Theirs;
						}
					}
					if (!bInOrder)
					{
						Edit.Kind = EDreamUIStructuralEditKind::MoveNode;
						Edit.NodeId = Existing->Node->Id;
						Report.Changes.Add(FString::Printf(TEXT("moved %s to where its layer now is"), *Child.Id));
						if (!Apply({ Edit }))
						{
							return false;
						}
					}
				}
			}
			return true;
		}

		/** The `+` blocks the importer owns: added where generated, removed where it made them last time and does not now. */
		bool UpdateComponents()
		{
			TArray<const FDreamPsdDuiNode*> Nodes;
			NewRoot.ForEach([&Nodes](const FDreamPsdDuiNode& Node, const FString&) { Nodes.Add(&Node); });
			for (const FDreamPsdDuiNode* New : Nodes)
			{
				const FIndex::FEntry* Old = Index.Find(FileId(New->Id));
				if (Old == nullptr)
				{
					continue;
				}
				const FDreamPsdImportManifest::FNode* LastNode = FindLast(*New);
				TArray<FDreamUIStructuralEdit> Edits;
				for (int32 c = Old->Node->Components.Num() - 1; c >= 0; --c)
				{
					const FString& Class = Old->Node->Components[c].ClassName;
					if (!DreamPsdDui::IsOwnedComponent(Class) || New->FindComponent(Class) != nullptr)
					{
						continue;
					}
					const bool bMadeLastTime = LastNode != nullptr && LastNode->Components.Contains(Class);
					if (bMadeLastTime)
					{
						FDreamUIStructuralEdit Remove;
						Remove.Kind = EDreamUIStructuralEditKind::RemoveComponent;
						Remove.NodeId = Old->Node->Id;
						Remove.ComponentIndex = c;
						Edits.Add(Remove);
						Report.Changes.Add(FString::Printf(TEXT("%s: removed + %s, which its layer no longer asks for"), *New->Id, *Class));
					}
				}
				for (const FDreamPsdDuiComponent& C : New->Components)
				{
					const bool bHas = Old->Node->Components.ContainsByPredicate([&C](const FDreamUIComponent& X) { return X.ClassName == C.ClassName; });
					if (!bHas)
					{
						FDreamUIStructuralEdit Insert;
						Insert.Kind = EDreamUIStructuralEditKind::InsertComponent;
						Insert.NodeId = Old->Node->Id;
						Insert.ComponentClassName = C.ClassName;
						Edits.Add(Insert);
					}
				}
				for (const FDreamUIStructuralEdit& Edit : Edits)
				{
					if (!Apply({ Edit }))
					{
						return false;
					}
					Old = Index.Find(FileId(New->Id));
					if (Old == nullptr)
					{
						break;
					}
				}
			}
			return true;
		}

		/** The lines the importer owns, set or taken out; everything else is left as it is. */
		bool UpdateProperties()
		{
			TArray<const FDreamPsdDuiNode*> Nodes;
			NewRoot.ForEach([&Nodes](const FDreamPsdDuiNode& Node, const FString&) { Nodes.Add(&Node); });
			// Removals first (structure), then every value in one batch.
			TArray<FDreamUIStructuralEdit> Removes;
			for (const FDreamPsdDuiNode* New : Nodes)
			{
				const FIndex::FEntry* Old = Index.Find(FileId(New->Id));
				const FDreamPsdImportManifest::FNode* LastNode = FindLast(*New);
				if (Old == nullptr || LastNode == nullptr)
				{
					continue;
				}
				for (const FDreamUIProperty& P : Old->Node->Properties)
				{
					if (P.bSynthesized || New->Find(P.Name) != nullptr || !LastNode->Properties.Contains(P.Name) || !DreamPsdDui::IsOwnedProperty(New->Type, P.Name))
					{
						continue;
					}
					FDreamUIStructuralEdit Remove;
					Remove.Kind = EDreamUIStructuralEditKind::RemoveProperty;
					Remove.NodeId = Old->Node->Id;
					Remove.PropertyName = P.Name;
					Remove.PropertyTarget = EDreamUIPatchTarget::Node;
					Removes.Add(Remove);
				}
				for (int32 c = 0; c < Old->Node->Components.Num(); ++c)
				{
					const FDreamUIComponent& OldComponent = Old->Node->Components[c];
					const FDreamPsdDuiComponent* NewComponent = New->FindComponent(OldComponent.ClassName);
					const TMap<FString, FString>* LastValues = LastNode->Components.Find(OldComponent.ClassName);
					if (NewComponent == nullptr || LastValues == nullptr)
					{
						continue;
					}
					for (const FDreamUIProperty& P : OldComponent.Properties)
					{
						const bool bStillWritten = NewComponent->Properties.ContainsByPredicate([&P](const FDreamPsdDuiProperty& X) { return X.Name == P.Name; });
						if (bStillWritten || !LastValues->Contains(P.Name))
						{
							continue;
						}
						FDreamUIStructuralEdit Remove;
						Remove.Kind = EDreamUIStructuralEditKind::RemoveProperty;
						Remove.NodeId = Old->Node->Id;
						Remove.PropertyName = P.Name;
						Remove.PropertyTarget = EDreamUIPatchTarget::Component;
						Remove.ComponentIndex = c;
						Removes.Add(Remove);
					}
				}
			}
			for (const FDreamUIStructuralEdit& Remove : Removes)
			{
				if (!Apply({ Remove }))
				{
					return false;
				}
			}

			TArray<FDreamUIPropertyEdit> Edits;
			for (const FDreamPsdDuiNode* New : Nodes)
			{
				const FIndex::FEntry* Old = Index.Find(FileId(New->Id));
				if (Old == nullptr)
				{
					continue;
				}
				const FDreamPsdImportManifest::FNode* LastNode = FindLast(*New);
				auto Plan = [&](const FDreamPsdDuiProperty& P, const TArray<FDreamUIProperty>& InOldLines, EDreamUIPatchTarget InTarget, int32 InComponent,
					const TMap<FString, FString>* InLastValues)
				{
					const FDreamUIProperty* Current = InOldLines.FindByPredicate([&P](const FDreamUIProperty& X) { return X.Name == P.Name && !X.bSynthesized; });
					if (Current != nullptr && P.bSeedOnly)
					{
						return;
					}
					if (Current != nullptr && (Current->IsBinding() || Current->IsEventBinding()))
					{
						Report.Conflicts.Add(FString::Printf(TEXT("%s.%s is bound in the file; the binding is kept and the layer's value (%s) is not written"), *New->Id, *P.Name, *P.Value));
						return;
					}
					if (Current != nullptr && Normalize(CurrentText(*Current)) == Normalize(P.Value))
					{
						return;
					}
					if (Current != nullptr && InLastValues != nullptr)
					{
						const FString* Before = InLastValues->Find(P.Name);
						if (Before != nullptr && Normalize(*Before) != Normalize(CurrentText(*Current)))
						{
							Report.Conflicts.Add(FString::Printf(TEXT("%s.%s was changed in the file (to %s); the layer's value (%s) is written over it"),
								*New->Id, *P.Name, *CurrentText(*Current), *P.Value));
						}
					}
					FDreamUIPropertyEdit Edit;
					Edit.NodeId = Old->Node->Id;
					Edit.Target = InTarget;
					Edit.ComponentIndex = InComponent;
					Edit.PropertyName = P.Name;
					Edit.NewValueText = P.Value;
					Edits.Add(Edit);
				};
				for (const FDreamPsdDuiProperty& P : New->Properties)
				{
					Plan(P, Old->Node->Properties, EDreamUIPatchTarget::Node, INDEX_NONE, LastNode != nullptr ? &LastNode->Properties : nullptr);
				}
				for (const FDreamPsdDuiComponent& C : New->Components)
				{
					const int32 Index_ = Old->Node->Components.IndexOfByPredicate([&C](const FDreamUIComponent& X) { return X.ClassName == C.ClassName; });
					if (Index_ == INDEX_NONE)
					{
						continue;
					}
					const TMap<FString, FString>* LastValues = LastNode != nullptr ? LastNode->Components.Find(C.ClassName) : nullptr;
					for (const FDreamPsdDuiProperty& P : C.Properties)
					{
						Plan(P, Old->Node->Components[Index_].Properties, EDreamUIPatchTarget::Component, Index_, LastValues);
					}
				}
			}
			if (Edits.Num() > 0)
			{
				FDreamUIDiagnosticBag Diagnostics;
				if (!FDreamUITextPatcher::SetProperties(Text, Ast, Edits, Diagnostics))
				{
					Report.Conflicts.Add(FString::Printf(TEXT("some values were not written: %s"), *Diagnostics.ToString()));
				}
				Report.Changes.Add(FString::Printf(TEXT("%d values written"), Edits.Num()));
			}
			return Parse();
		}
	};
}

bool DreamPsdDuiMerge::Merge(const FString& InOldText, const FDreamPsdImportManifest* InLastImport, const FDreamPsdDuiNode& InNewRoot,
	FString& OutText, FDreamPsdMergeReport& OutReport)
{
	OutText = InOldText;
	DreamPsdDuiMergeLocal::FMerger Merger(OutText, InLastImport, InNewRoot, OutReport);
	if (!Merger.Run())
	{
		OutText = InOldText;
		return false;
	}
	return true;
}
