// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIBridgeService.h"

#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace DreamUIBridgeLocal
{
	constexpr int32 ProtocolVersion = 1;
	/** Requests are checked this often; the cost of an empty poll is one directory listing. */
	constexpr float PollIntervalSeconds = 0.25f;
	constexpr double HeartbeatIntervalSeconds = 2.0;

	FTSTicker::FDelegateHandle GTicker;
	double GLastHeartbeat = 0.0;

	FString Root()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUI"), TEXT("Bridge")));
	}

	bool WriteJsonAtomically(const TSharedRef<FJsonObject>& InObject, const FString& InFinalPath)
	{
		FString Serialized;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
		FJsonSerializer::Serialize(InObject, Writer);

		const FString TempPath = InFinalPath + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(Serialized, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return false;
		}
		return IFileManager::Get().Move(*InFinalPath, *TempPath, /*Replace*/true, /*EvenIfReadOnly*/true);
	}

	void WriteStatus(const FString& InRoot, bool bBusy, const FString& InBusyAction)
	{
		TSharedRef<FJsonObject> Status = MakeShared<FJsonObject>();
		Status->SetNumberField(TEXT("protocol"), ProtocolVersion);
		Status->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
		Status->SetStringField(TEXT("project"), FApp::GetProjectName());
		Status->SetBoolField(TEXT("busy"), bBusy);
		if (!InBusyAction.IsEmpty())
		{
			Status->SetStringField(TEXT("busyAction"), InBusyAction);
		}
		Status->SetStringField(TEXT("heartbeatUtc"), FDateTime::UtcNow().ToIso8601());
		WriteJsonAtomically(Status, FPaths::Combine(InRoot, TEXT("status.json")));
	}

	/** `/Game/UI/WBP_X` -> the blueprint asset, or null with a reason. */
	UDreamWidgetBlueprint* LoadBlueprintByClassPath(const FString& InClassPath, FString& OutWhyNot)
	{
		if (!InClassPath.StartsWith(TEXT("/")))
		{
			OutWhyNot = FString::Printf(TEXT("'%s' is not an asset path"), *InClassPath);
			return nullptr;
		}
		FString PackagePath = InClassPath;
		FString Leaf;
		if (!InClassPath.Split(TEXT("."), &PackagePath, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			Leaf = FPackageName::GetShortName(InClassPath);
		}
		const FString ObjectPath = PackagePath + TEXT(".") + Leaf;
		UDreamWidgetBlueprint* Blueprint = LoadObject<UDreamWidgetBlueprint>(nullptr, *ObjectPath);
		if (Blueprint == nullptr)
		{
			OutWhyNot = FString::Printf(TEXT("no DreamGUI widget blueprint at '%s'"), *ObjectPath);
		}
		return Blueprint;
	}

	int32 InputParameterCount(const UFunction* InFunction)
	{
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(InFunction); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				++Count;
			}
		}
		return Count;
	}

	/**
	 * A property's C++ spelling WITH its template arguments: `TArray<FVector>`, not `TArray`.
	 *
	 * GetCPPType puts the arguments in its out-parameter and returns only the bare template name,
	 * so the obvious `GetCPPType()` call answers "TArray" for every array in the project -- which
	 * is exactly the string the `members` action cannot do anything with. Every type string the
	 * bridge emits goes through here, so what a `variables` reply prints is a string the client can
	 * hand straight back as a `typePath`; two spellings of one type is how a client asks about a
	 * type the editor just told it about and is told there is no such type.
	 */
	FString CppTypeOf(const FProperty* InProperty)
	{
		if (InProperty == nullptr)
		{
			return FString();
		}
		FString Extended;
		const FString Base = InProperty->GetCPPType(&Extended, /*CPPExportFlags*/0);
		return Base + Extended;
	}

	/** The editor's tooltip for a field, flattened to one string; empty when it has none. */
	FString ToolTipOf(const UFunction* InFunction)
	{
		return InFunction != nullptr ? InFunction->GetToolTipText().ToString() : FString();
	}

	FString ToolTipOf(const FProperty* InProperty)
	{
		return InProperty != nullptr ? InProperty->GetToolTipText().ToString() : FString();
	}

	/** Written only when there is something to write: an empty tooltip is absence, not "". */
	void SetOptionalString(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, const FString& InValue)
	{
		if (!InValue.IsEmpty())
		{
			InObject->SetStringField(InField, InValue);
		}
	}

	/**
	 * One FunctionInfo: name, return type, parameters in DECLARATION order, purity, tooltip.
	 *
	 * Parameters are always emitted, the empty array included, because "this function takes none"
	 * and "this editor is too old to say" have to look different from the other end -- the same
	 * reason unknown actions are answered rather than dropped.
	 */
	TSharedPtr<FJsonObject> MakeFunctionInfo(const UFunction* InFunction)
	{
		TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
		Info->SetStringField(TEXT("name"), InFunction->GetName());

		TArray<TSharedPtr<FJsonValue>> Params;
		for (TFieldIterator<FProperty> It(InFunction); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
			Param->SetStringField(TEXT("name"), It->GetName());
			Param->SetStringField(TEXT("type"), CppTypeOf(*It));
			Params.Add(MakeShared<FJsonValueObject>(Param));
		}
		// Counted from the array rather than walked a second time: two counts of one thing is one
		// count too many, and the one that drifts is always the one a client trusted.
		Info->SetNumberField(TEXT("paramCount"), Params.Num());
		Info->SetArrayField(TEXT("params"), Params);

		if (const FProperty* Return = InFunction->GetReturnProperty())
		{
			Info->SetStringField(TEXT("returnType"), CppTypeOf(Return));
		}
		Info->SetBoolField(TEXT("pure"), InFunction->HasAnyFunctionFlags(FUNC_BlueprintPure));
		SetOptionalString(Info, TEXT("tooltip"), ToolTipOf(InFunction));
		return Info;
	}

	/**
	 * Whether a binding EXPRESSION may call this -- the `Greeting()` in `Text <- Greeting() + Name`.
	 *
	 * The thunk pass lowers such a call with FindFunctionByName on the class and nothing else, so
	 * the honest list is every Blueprint-reachable function, inherited ones included, with three
	 * kinds struck out for reasons that are not taste:
	 *   - an EVENT is entered, not called (its body is a graph and its caller is the dispatcher),
	 *     so offering one produces a call the compiler will not make;
	 *   - a DELEGATE SIGNATURE is a type wearing a function's clothes -- a name and parameters with
	 *     no implementation anywhere;
	 *   - an EDITOR-ONLY function is absent from a packaged build, so a file that compiles here
	 *     would fail to at exactly the moment nobody is watching.
	 */
	bool IsCallableFromExpression(const UFunction* InFunction)
	{
		if (InFunction->HasAnyFunctionFlags(FUNC_Delegate | FUNC_MulticastDelegate | FUNC_Event | FUNC_EditorOnly))
		{
			return false;
		}
		return InFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure);
	}

	/**
	 * The class a `classPath` names, whichever of the three spellings it is.
	 *
	 * A widget Blueprint first, because that is what a `class` line writes and what every existing
	 * caller sends. Falling through to a plain class lookup is what lets the actions answer for
	 * `/Script/DreamGUI.DreamUserWidget` and for a `_C` path -- a native base class has bindable
	 * functions and blueprint-visible variables exactly like a generated one does, and refusing to
	 * say so only because no .uasset was involved would blank completion out on the base class
	 * every text-backed widget derives from.
	 */
	UClass* ResolveClassByPath(const FString& InClassPath, FString& OutWhyNot)
	{
		if (InClassPath.IsEmpty())
		{
			OutWhyNot = TEXT("no classPath was given");
			return nullptr;
		}

		// A `/Script/` path is a native class and a `_C` path is a generated one; neither can ever
		// be a .uasset, and the Blueprint load that discovers so is not free -- it is a
		// StaticLoadObject miss, which warns into the log, once per completion request. Skipping
		// the attempt is the difference between a quiet editor and a log that scrolls while
		// somebody types.
		FString BlueprintWhyNot = FString::Printf(TEXT("'%s' is not a widget Blueprint path"), *InClassPath);
		const bool bCouldBeBlueprintAsset = !InClassPath.StartsWith(TEXT("/Script/"))
			&& !InClassPath.EndsWith(TEXT("_C"));
		if (bCouldBeBlueprintAsset)
		{
			if (UDreamWidgetBlueprint* Blueprint = LoadBlueprintByClassPath(InClassPath, BlueprintWhyNot))
			{
				if (Blueprint->GeneratedClass == nullptr)
				{
					OutWhyNot = FString::Printf(TEXT("'%s' has never compiled; compile it once first"), *InClassPath);
					return nullptr;
				}
				return Blueprint->GeneratedClass;
			}
		}

		if (InClassPath.StartsWith(TEXT("/")))
		{
			// LOAD_NoWarn, because a path that is a class rather than a Blueprint is an ordinary
			// case here and the load that discovers so must not narrate it into the log on every
			// keystroke of an author's completion.
			if (UObject* Loaded = LoadObject<UObject>(nullptr, *InClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				if (UClass* AsClass = Cast<UClass>(Loaded))
				{
					return AsClass;
				}
				if (const UBlueprint* AsBlueprint = Cast<UBlueprint>(Loaded))
				{
					if (AsBlueprint->GeneratedClass != nullptr)
					{
						return AsBlueprint->GeneratedClass;
					}
				}
			}
		}
		else if (UClass* ByName = FindFirstObject<UClass>(*InClassPath, EFindFirstObjectOptions::None))
		{
			return ByName;
		}

		OutWhyNot = FString::Printf(TEXT("'%s' names no class: %s, and it is no other loadable class either"),
			*InClassPath, *BlueprintWhyNot);
		return nullptr;
	}

	/**
	 * The UStruct a `typePath` names, and -- when the spelling was a container -- the element type
	 * to report back so the client knows what to ask about next.
	 *
	 * The spellings are the ones CppTypeOf prints, because the client got the string from an
	 * earlier reply: `TArray<FFoo>`, `TSet<FFoo>`, `FFoo`, `UFoo*`, `TObjectPtr<UFoo>`, an asset
	 * path, or a bare reflected name. A container is peeled ONCE rather than to the bottom -- the
	 * element is what the next question is about, and answering `FFoo` for `TArray<TArray<FFoo>>`
	 * would describe a type the file has no way to name.
	 *
	 * Prefixes are tried and then dropped because reflection carries none -- `FVector` is a
	 * UScriptStruct called `Vector` -- and the client is quoting C++, not reflection.
	 */
	UStruct* ResolveTypePath(const FString& InTypePath, FString& OutElementType, FString& OutWhyNot)
	{
		FString Type = InTypePath.TrimStartAndEnd();
		if (Type.IsEmpty())
		{
			OutWhyNot = TEXT("no typePath was given");
			return nullptr;
		}

		auto PeelTemplate = [](FString& InOutType, const TCHAR* InTemplateName) -> bool
		{
			const FString Prefix = FString(InTemplateName) + TEXT("<");
			if (!InOutType.StartsWith(Prefix, ESearchCase::CaseSensitive) || !InOutType.EndsWith(TEXT(">")))
			{
				return false;
			}
			InOutType = InOutType.Mid(Prefix.Len(), InOutType.Len() - Prefix.Len() - 1).TrimStartAndEnd();
			return true;
		};

		if (PeelTemplate(Type, TEXT("TArray")) || PeelTemplate(Type, TEXT("TSet")))
		{
			OutElementType = Type;
		}
		// Every pointer spelling means the same class. Which one a header happened to use is not a
		// distinction the language has, so it is not one this answer should carry.
		static const TCHAR* PointerTemplates[] =
		{
			TEXT("TObjectPtr"), TEXT("TSubclassOf"), TEXT("TSoftObjectPtr"),
			TEXT("TSoftClassPtr"), TEXT("TWeakObjectPtr"), TEXT("TScriptInterface"),
		};
		for (const TCHAR* PointerTemplate : PointerTemplates)
		{
			if (PeelTemplate(Type, PointerTemplate))
			{
				break;
			}
		}
		Type.RemoveFromEnd(TEXT("*"));
		Type.TrimStartAndEndInline();
		if (Type.IsEmpty())
		{
			OutWhyNot = FString::Printf(TEXT("'%s' peels down to nothing"), *InTypePath);
			return nullptr;
		}

		if (Type.StartsWith(TEXT("/")))
		{
			// `/Game/UI/Row` and `/Game/UI/Row.Row` name the asset; `/Game/UI/Row.Row_C` names the
			// class it generates. All three are things a .dui writes, so all three resolve.
			FString ObjectPath = Type;
			FString PackagePath;
			FString Leaf;
			if (!Type.Split(TEXT("."), &PackagePath, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				ObjectPath = Type + TEXT(".") + FPackageName::GetShortName(Type);
			}
			UObject* Loaded = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (UScriptStruct* AsStruct = Cast<UScriptStruct>(Loaded))
			{
				return AsStruct;
			}
			if (UClass* AsClass = Cast<UClass>(Loaded))
			{
				return AsClass;
			}
			if (const UBlueprint* AsBlueprint = Cast<UBlueprint>(Loaded))
			{
				if (AsBlueprint->GeneratedClass != nullptr)
				{
					return AsBlueprint->GeneratedClass;
				}
			}
			OutWhyNot = FString::Printf(TEXT("nothing at '%s' is a struct or a class"), *ObjectPath);
			return nullptr;
		}

		auto FindByReflectedName = [](const FString& InName) -> UStruct*
		{
			if (UScriptStruct* AsStruct = FindFirstObject<UScriptStruct>(*InName, EFindFirstObjectOptions::None))
			{
				return AsStruct;
			}
			return FindFirstObject<UClass>(*InName, EFindFirstObjectOptions::None);
		};
		if (UStruct* Found = FindByReflectedName(Type))
		{
			return Found;
		}
		if (Type.Len() > 1 && (Type[0] == TEXT('F') || Type[0] == TEXT('U') || Type[0] == TEXT('A'))
			&& FChar::IsUpper(Type[1]))
		{
			if (UStruct* Found = FindByReflectedName(Type.RightChop(1)))
			{
				return Found;
			}
		}
		OutWhyNot = FString::Printf(TEXT("'%s' names no struct or class this editor has loaded"), *InTypePath);
		return nullptr;
	}

	/** Name, type and tooltip for one property: the shape `members` and `variables` share. */
	TSharedPtr<FJsonObject> MakePropertyInfo(const FProperty* InProperty)
	{
		TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
		Info->SetStringField(TEXT("name"), InProperty->GetName());
		Info->SetStringField(TEXT("type"), CppTypeOf(InProperty));
		SetOptionalString(Info, TEXT("tooltip"), ToolTipOf(InProperty));
		return Info;
	}

	// ---- actions -------------------------------------------------------------------------------

	void HandlePing(const TSharedRef<FJsonObject>& OutResponse)
	{
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"),
			FString::Printf(TEXT("DreamGUI bridge, %s"), FApp::GetProjectName()));
	}

	/**
	 * What `<-`, `->` and a binding expression can name on a class.
	 *
	 * Bindable = callable, no inputs, returns something (a binding PULLS a value every frame; the
	 * compiler's own check is FindFunctionByName plus the parameter rules, so this list can offer
	 * nothing the compiler then refuses for shape). Handlers = callable functions; signature
	 * compatibility with the event stays the compiler's verdict -- completion offers candidates,
	 * not promises. Both of those stop at the DreamGUI widget base: the engine layers above it
	 * would drown the author's own functions in noise.
	 *
	 * Callable does NOT stop there, and the difference is not an oversight. The first two lists
	 * answer "what may stand alone on the right of an arrow", which is a small authored set and a
	 * menu an author reads. Callable answers "what may appear anywhere inside an expression",
	 * which the thunk pass resolves with a bare FindFunctionByName over the whole class -- so a
	 * list that stopped at UDreamUserWidget would hide functions the compiler accepts, and the
	 * author would learn they exist only by guessing. The set stays small in practice because a
	 * DreamGUI widget descends from UObject rather than from UUserWidget; there is no engine
	 * widget hierarchy above it to flood the list.
	 */
	void HandleFunctions(const FString& InClassPath, const TSharedRef<FJsonObject>& OutResponse)
	{
		FString WhyNot;
		UClass* Class = ResolveClassByPath(InClassPath, WhyNot);
		if (Class == nullptr)
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"), WhyNot);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Bindable;
		TArray<TSharedPtr<FJsonValue>> Handlers;
		TArray<TSharedPtr<FJsonValue>> Callable;
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UFunction* Function = *It;
			const UClass* Owner = Function->GetOwnerClass();
			const bool bAuthorFacing = Owner != nullptr && Owner->IsChildOf(UDreamUserWidget::StaticClass());

			// Built once and shared by whichever lists claim it: three copies of one function's
			// tooltip is three times the JSON for no extra fact.
			TSharedPtr<FJsonObject> Info;
			auto EnsureInfo = [&Info, Function]() -> TSharedPtr<FJsonObject>
			{
				if (!Info.IsValid())
				{
					Info = MakeFunctionInfo(Function);
				}
				return Info;
			};

			if (bAuthorFacing
				&& !Function->HasAnyFunctionFlags(FUNC_Delegate)
				&& Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
			{
				Handlers.Add(MakeShared<FJsonValueObject>(EnsureInfo()));
				if (InputParameterCount(Function) == 0 && Function->GetReturnProperty() != nullptr)
				{
					Bindable.Add(MakeShared<FJsonValueObject>(EnsureInfo()));
				}
			}
			if (IsCallableFromExpression(Function))
			{
				Callable.Add(MakeShared<FJsonValueObject>(EnsureInfo()));
			}
		}

		TSharedPtr<FJsonObject> Functions = MakeShared<FJsonObject>();
		Functions->SetArrayField(TEXT("bindable"), Bindable);
		Functions->SetArrayField(TEXT("handlers"), Handlers);
		Functions->SetArrayField(TEXT("callable"), Callable);
		OutResponse->SetObjectField(TEXT("functions"), Functions);
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"), FString());
	}

	/**
	 * What a `<->` may name and what a binding expression may read: the class's blueprint-visible
	 * variables, inherited ones included.
	 *
	 * `fieldNotify` is reported and not required, because the compiler does not require it. The
	 * thunk pass accepts any variable of the right type for a `<->` (FindVariablePinType asks the
	 * Blueprint's own variable lists and then the class, and nothing else); FieldNotify decides
	 * something else entirely -- whether the runtime SUBSCRIBES to the variable or falls back to
	 * polling it every frame. So a client that treated a false here as an error would refuse
	 * bindings that compile and work, and one that ignored it would never be able to explain why
	 * a screen updates a frame late.
	 *
	 * The judge is the runtime's own: UDreamUserWidget resolves a field through the generated
	 * class's FieldNotify roster, so that roster is what is asked. The metadata is checked too,
	 * for the native case a generated class has no entry for.
	 */
	void HandleVariables(const FString& InClassPath, const TSharedRef<FJsonObject>& OutResponse)
	{
		FString WhyNot;
		UClass* Class = ResolveClassByPath(InClassPath, WhyNot);
		if (Class == nullptr)
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"), WhyNot);
			return;
		}

		TSet<FName> NotifyFields;
		if (const UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Class))
		{
			GeneratedClass->ForEachFieldNotify([&NotifyFields](UE::FieldNotification::FFieldId FieldId)
			{
				NotifyFields.Add(FieldId.GetName());
				return true;
			}, /*bIncludeSuper*/true);
		}

		TArray<TSharedPtr<FJsonValue>> Variables;
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Info = MakePropertyInfo(Property);
			Info->SetBoolField(TEXT("fieldNotify"),
				NotifyFields.Contains(Property->GetFName())
					|| Property->HasMetaData(FBlueprintMetadata::MD_FieldNotify));
			Variables.Add(MakeShared<FJsonValueObject>(Info));
		}

		OutResponse->SetArrayField(TEXT("variables"), Variables);
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"), FString());
	}

	/**
	 * What a `.` reaches inside a type: the blueprint-visible properties of the struct or class a
	 * `typePath` names, with the element type reported when the path was a container.
	 *
	 * The container step is the point of the action. An `each Row in Rows()` block writes
	 * `Text = Row.Label`, and to offer `Label` the client has to get from `TArray<FRowData>` to
	 * `FRowData` -- which is a reflection question no amount of parsing the .dui can answer.
	 * elementType comes back so the client can ask the follow-up itself rather than inventing a
	 * string-peeling rule of its own that this side would then have to keep matching.
	 */
	void HandleMembers(const FString& InTypePath, const TSharedRef<FJsonObject>& OutResponse)
	{
		FString ElementType;
		FString WhyNot;
		UStruct* Struct = ResolveTypePath(InTypePath, ElementType, WhyNot);
		if (Struct == nullptr)
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"), WhyNot);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Members;
		for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
			{
				continue;
			}
			Members.Add(MakeShared<FJsonValueObject>(MakePropertyInfo(Property)));
		}

		OutResponse->SetArrayField(TEXT("members"), Members);
		if (!ElementType.IsEmpty())
		{
			OutResponse->SetStringField(TEXT("elementType"), ElementType);
		}
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"), FString());
	}

	/**
	 * Find an asset in the content browser and open its editor -- the `reveal` action's answer for
	 * the other half of a .dui, the asset paths written as values.
	 *
	 * Both halves, in that order, because either alone is half a gesture: syncing without opening
	 * leaves the author looking at an icon, and opening without syncing leaves them with a window
	 * and no idea where the thing lives. The sync goes first so the browser is already showing the
	 * asset behind whatever editor lands on top of it.
	 */
	void HandleRevealAsset(const FString& InAssetPath, const TSharedRef<FJsonObject>& OutResponse)
	{
		if (InAssetPath.IsEmpty() || !InAssetPath.StartsWith(TEXT("/")))
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"),
				FString::Printf(TEXT("'%s' is not an asset path"), *InAssetPath));
			return;
		}

		// `/Game/UI/Tex` is the spelling a .dui writes; the object inside it repeats the leaf.
		FString ObjectPath = InAssetPath;
		FString PackagePath;
		FString Leaf;
		if (!InAssetPath.Split(TEXT("."), &PackagePath, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			ObjectPath = InAssetPath + TEXT(".") + FPackageName::GetShortName(InAssetPath);
		}

		UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Asset == nullptr)
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"),
				FString::Printf(TEXT("no asset at '%s'"), *ObjectPath));
			return;
		}

		FContentBrowserModule& ContentBrowser =
			FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowser.Get().SyncBrowserToAssets(TArray<UObject*>{ Asset });

		FString Message = FString::Printf(TEXT("found '%s' in the content browser"), *Asset->GetName());
		if (GEditor != nullptr)
		{
			if (UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				Editors->OpenEditorForAsset(Asset);
				Message += TEXT(" and opened it");
			}
		}
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"), Message);
	}

	void HandleAssets(const FString& InClassFilter, const TSharedRef<FJsonObject>& OutResponse)
	{
		const FAssetRegistryModule& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		FTopLevelAssetPath ClassPath = UDreamWidgetBlueprint::StaticClass()->GetClassPathName();
		if (!InClassFilter.IsEmpty() && InClassFilter != TEXT("DreamWidgetBlueprint"))
		{
			if (const UClass* Filter = FindFirstObject<UClass>(*InClassFilter, EFindFirstObjectOptions::None))
			{
				ClassPath = Filter->GetClassPathName();
			}
			else
			{
				OutResponse->SetBoolField(TEXT("ok"), false);
				OutResponse->SetStringField(TEXT("message"),
					FString::Printf(TEXT("no class named '%s'"), *InClassFilter));
				return;
			}
		}

		TArray<FAssetData> Assets;
		Registry.Get().GetAssetsByClass(ClassPath, Assets, /*bSearchSubClasses*/true);

		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FAssetData& Asset : Assets)
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("path"), Asset.PackageName.ToString());
			Info->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			Out.Add(MakeShared<FJsonValueObject>(Info));
		}
		OutResponse->SetArrayField(TEXT("assets"), Out);
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"), FString());
	}

	void HandleReveal(const FString& InClassPath, const FString& InWidgetId,
		const TSharedRef<FJsonObject>& OutResponse)
	{
		FString WhyNot;
		UDreamWidgetBlueprint* Blueprint = LoadBlueprintByClassPath(InClassPath, WhyNot);
		if (Blueprint == nullptr)
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"), WhyNot);
			return;
		}

		UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		Editors->OpenEditorForAsset(Blueprint);

		FString Message = FString::Printf(TEXT("opened '%s'"), *Blueprint->GetName());
		if (!InWidgetId.IsEmpty())
		{
			IAssetEditorInstance* Instance = Editors->FindEditorForAsset(Blueprint, /*bFocusIfOpen*/true);
			// The toolkit name is the type check: FindEditorForAsset hands back an interface, and
			// casting someone else's editor would be a crash wearing a feature's name.
			if (Instance != nullptr && Instance->GetEditorName() == TEXT("DreamWidgetBlueprintEditor"))
			{
				FDreamWidgetBlueprintEditor* Editor = static_cast<FDreamWidgetBlueprintEditor*>(Instance);
				if (UDreamWidget* Preview = Editor->GetPreviewRootWidget())
				{
					TArray<UDreamWidget*> Matches = Preview->GetDisplayName() == InWidgetId
						? TArray<UDreamWidget*>{ Preview }
						: Preview->FindChildArrayByDisplayName(InWidgetId, /*IncludeChildren*/true);
					if (Matches.Num() > 0)
					{
						Editor->SelectWidgets(TSet<UDreamWidget*>{ Matches[0] }, /*bAppendOrToggle*/false);
						Message += FString::Printf(TEXT(", selected '%s'"), *InWidgetId);
					}
					else
					{
						Message += FString::Printf(TEXT("; no widget named '%s' in the preview"), *InWidgetId);
					}
				}
			}
		}
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"), Message);
	}

	void HandleCompile(const FString& InClassPath, const TSharedRef<FJsonObject>& OutResponse)
	{
		// The watcher's own gates: compiling reinstances live widgets, which mid-PIE is a crash
		// report, and mid-GC/save is worse. Refusing loudly beats queueing quietly.
		if (GEditor == nullptr || GEditor->PlayWorld != nullptr || GIsSavingPackage || IsGarbageCollecting())
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"),
				TEXT("the editor is busy (PIE, saving or collecting); try again in a moment"));
			return;
		}

		FString WhyNot;
		UDreamWidgetBlueprint* Blueprint = LoadBlueprintByClassPath(InClassPath, WhyNot);
		if (Blueprint == nullptr)
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"), WhyNot);
			return;
		}

		// SkipGarbageCollection, like every other compile the plugin issues: an author did not ask
		// for a full GC by saving a file. Verdicts travel through the diagnostics mailbox.
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"),
			FString::Printf(TEXT("compiled '%s'; diagnostics are in the mailbox"), *Blueprint->GetName()));
	}

	void Dispatch(const TSharedPtr<FJsonObject>& InRequest, const TSharedRef<FJsonObject>& OutResponse)
	{
		const FString Action = InRequest->GetStringField(TEXT("action"));
		FString ClassPath;
		InRequest->TryGetStringField(TEXT("classPath"), ClassPath);

		if (Action == TEXT("ping"))
		{
			HandlePing(OutResponse);
		}
		else if (Action == TEXT("functions"))
		{
			HandleFunctions(ClassPath, OutResponse);
		}
		else if (Action == TEXT("variables"))
		{
			HandleVariables(ClassPath, OutResponse);
		}
		else if (Action == TEXT("members"))
		{
			FString TypePath;
			InRequest->TryGetStringField(TEXT("typePath"), TypePath);
			HandleMembers(TypePath, OutResponse);
		}
		else if (Action == TEXT("revealAsset"))
		{
			FString AssetPath;
			InRequest->TryGetStringField(TEXT("assetPath"), AssetPath);
			HandleRevealAsset(AssetPath, OutResponse);
		}
		else if (Action == TEXT("assets"))
		{
			FString ClassFilter;
			InRequest->TryGetStringField(TEXT("classFilter"), ClassFilter);
			HandleAssets(ClassFilter, OutResponse);
		}
		else if (Action == TEXT("reveal"))
		{
			FString WidgetId;
			InRequest->TryGetStringField(TEXT("widgetId"), WidgetId);
			HandleReveal(ClassPath, WidgetId, OutResponse);
		}
		else if (Action == TEXT("compile"))
		{
			HandleCompile(ClassPath, OutResponse);
		}
		else
		{
			// Answered, not dropped: a silent editor and a missing feature look identical from the
			// other end, and the difference is exactly what the client needs to report.
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"),
				FString::Printf(TEXT("unknown action '%s'"), *Action));
		}
	}
}

int32 FDreamUIBridgeService::ProcessPendingNow(const FString& InOverrideRoot)
{
	using namespace DreamUIBridgeLocal;

	const FString BridgeRoot = InOverrideRoot.IsEmpty() ? Root() : InOverrideRoot;
	const FString RequestsDir = FPaths::Combine(BridgeRoot, TEXT("Requests"));
	const FString ResponsesDir = FPaths::Combine(BridgeRoot, TEXT("Responses"));

	TArray<FString> RequestFiles;
	IFileManager::Get().FindFiles(RequestFiles, *(RequestsDir / TEXT("*.request.json")), /*Files*/true, /*Dirs*/false);
	if (RequestFiles.Num() == 0)
	{
		return 0;
	}
	// Ids sort oldest-first as strings; draining in send order is a sort, not a guess.
	RequestFiles.Sort();

	int32 Processed = 0;
	for (const FString& FileName : RequestFiles)
	{
		const FString FullPath = RequestsDir / FileName;
		FString Serialized;
		const bool bRead = FFileHelper::LoadFileToString(Serialized, *FullPath);

		// Take-then-delete BEFORE executing: a crash mid-action must not replay the request into
		// the next session.
		IFileManager::Get().Delete(*FullPath);

		FString RequestId = FileName;
		RequestId.RemoveFromEnd(TEXT(".request.json"));

		TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetNumberField(TEXT("protocol"), ProtocolVersion);
		Response->SetStringField(TEXT("requestId"), RequestId);

		const double Started = FPlatformTime::Seconds();
		TSharedPtr<FJsonObject> Request;
		if (bRead)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
			FJsonSerializer::Deserialize(Reader, Request);
		}
		if (Request.IsValid() && Request->HasTypedField<EJson::String>(TEXT("action")))
		{
			WriteStatus(BridgeRoot, /*bBusy*/true, Request->GetStringField(TEXT("action")));
			Dispatch(Request, Response);
		}
		else
		{
			Response->SetBoolField(TEXT("ok"), false);
			Response->SetStringField(TEXT("message"), TEXT("unreadable request"));
		}
		Response->SetNumberField(TEXT("durationMs"),
			FMath::RoundToInt((FPlatformTime::Seconds() - Started) * 1000.0));

		WriteJsonAtomically(Response, ResponsesDir / (RequestId + TEXT(".response.json")));
		++Processed;
	}

	WriteStatus(BridgeRoot, /*bBusy*/false, FString());
	GLastHeartbeat = FPlatformTime::Seconds();
	return Processed;
}

bool FDreamUIBridgeService::WriteRevealToEditor(const FString& InAbsoluteFilePath, int32 InLine, int32 InColumn,
	const FString& InWidgetId, const FString& InOverrideRoot)
{
	using namespace DreamUIBridgeLocal;

	const FString BridgeRoot = InOverrideRoot.IsEmpty() ? Root() : InOverrideRoot;
	// The designer may be the first thing that ever writes here: a project whose author has not
	// opened the workspace yet still has a right-click, and a reveal that failed because a folder
	// was missing would be a feature that works only for people who did not need it.
	IFileManager::Get().MakeDirectory(*BridgeRoot, /*Tree*/true);

	TSharedRef<FJsonObject> Reveal = MakeShared<FJsonObject>();
	Reveal->SetNumberField(TEXT("protocol"), ProtocolVersion);
	// Absolute, always: the client is another process with its own working directory, and a
	// relative path here would resolve against whatever VSCode happened to be started from.
	Reveal->SetStringField(TEXT("file"), FPaths::ConvertRelativePathToFull(InAbsoluteFilePath));
	// 1-based at the floor, because both halves count from one and a 0 would land the cursor a
	// line above the one that was meant.
	Reveal->SetNumberField(TEXT("line"), FMath::Max(1, InLine));
	Reveal->SetNumberField(TEXT("column"), FMath::Max(1, InColumn));
	Reveal->SetStringField(TEXT("widgetId"), InWidgetId);
	Reveal->SetStringField(TEXT("stampUtc"), FDateTime::UtcNow().ToIso8601());

	return WriteJsonAtomically(Reveal, FPaths::Combine(BridgeRoot, TEXT("reveal-to-editor.json")));
}

void FDreamUIBridgeService::Register()
{
	using namespace DreamUIBridgeLocal;

	const FString BridgeRoot = Root();
	IFileManager::Get().MakeDirectory(*(BridgeRoot / TEXT("Requests")), /*Tree*/true);
	IFileManager::Get().MakeDirectory(*(BridgeRoot / TEXT("Responses")), /*Tree*/true);

	// Yesterday's responses are nobody's answers: clients abandon on timeout, and a fresh editor
	// answering a stale id would only confuse a client that reuses nothing.
	TArray<FString> Stale;
	IFileManager::Get().FindFiles(Stale, *(BridgeRoot / TEXT("Responses") / TEXT("*.response.json")), true, false);
	for (const FString& FileName : Stale)
	{
		IFileManager::Get().Delete(*(BridgeRoot / TEXT("Responses") / FileName));
	}

	// And yesterday's REQUESTS are nobody's instructions, which is the half this did not do. A
	// request file survives the editor that failed to answer it -- a crash, a kill, a close mid-flight
	// -- and the first tick of the NEXT session picked it up and ran it: a compile, a reveal, a write
	// aimed at a workspace state that no longer exists, arriving out of nowhere seconds after
	// startup. The symmetry with responses is also the client contract: absence of status.json means
	// the editor is closed, so a correctly written request cannot already be here when this runs.
	TArray<FString> Abandoned;
	// The same glob ProcessPendingNow reads, so exactly the files that WOULD have been executed are
	// the files discarded -- a partially written `.tmp` a client is still producing is not one.
	IFileManager::Get().FindFiles(Abandoned, *(BridgeRoot / TEXT("Requests") / TEXT("*.request.json")), true, false);
	for (const FString& FileName : Abandoned)
	{
		UE_LOG(DreamGUIEditor, Verbose,
			TEXT("[%s].%d Discarding '%s': it was written for an editor session that never answered it."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FileName);
		IFileManager::Get().Delete(*(BridgeRoot / TEXT("Requests") / FileName));
	}

	WriteStatus(BridgeRoot, /*bBusy*/false, FString());
	GLastHeartbeat = FPlatformTime::Seconds();

	GTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		ProcessPendingNow();
		const double Now = FPlatformTime::Seconds();
		if (Now - GLastHeartbeat >= HeartbeatIntervalSeconds)
		{
			WriteStatus(Root(), /*bBusy*/false, FString());
			GLastHeartbeat = Now;
		}
		return true;
	}), PollIntervalSeconds);
}

void FDreamUIBridgeService::Unregister()
{
	using namespace DreamUIBridgeLocal;
	if (GTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GTicker);
		GTicker.Reset();
	}
	// Absence is the "closed" signal; leaving a stale heartbeat behind would make the client wait
	// out the full staleness budget instead.
	IFileManager::Get().Delete(*(Root() / TEXT("status.json")), /*RequireExists*/false);
}
