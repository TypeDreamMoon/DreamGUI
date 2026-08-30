// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIBridgeService.h"

#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/AssetEditorSubsystem.h"
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

	void WriteJsonAtomically(const TSharedRef<FJsonObject>& InObject, const FString& InFinalPath)
	{
		FString Serialized;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
		FJsonSerializer::Serialize(InObject, Writer);

		const FString TempPath = InFinalPath + TEXT(".tmp");
		if (FFileHelper::SaveStringToFile(Serialized, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			IFileManager::Get().Move(*InFinalPath, *TempPath, /*Replace*/true, /*EvenIfReadOnly*/true);
		}
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

	// ---- actions -------------------------------------------------------------------------------

	void HandlePing(const TSharedRef<FJsonObject>& OutResponse)
	{
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"),
			FString::Printf(TEXT("DreamGUI bridge, %s"), FApp::GetProjectName()));
	}

	/**
	 * What `<-` and `->` can name on a class. Bindable = callable, no inputs, returns something
	 * (a binding PULLS a value every frame; the compiler's own check is FindFunctionByName plus
	 * the parameter rules, so this list can offer nothing the compiler then refuses for shape).
	 * Handlers = callable functions; signature compatibility with the event stays the compiler's
	 * verdict -- completion offers candidates, not promises. Both lists stop at the DreamGUI
	 * widget base: the engine layers above it would drown the author's own functions in noise.
	 */
	void HandleFunctions(const FString& InClassPath, const TSharedRef<FJsonObject>& OutResponse)
	{
		FString WhyNot;
		UDreamWidgetBlueprint* Blueprint = LoadBlueprintByClassPath(InClassPath, WhyNot);
		if (Blueprint == nullptr)
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"), WhyNot);
			return;
		}
		UClass* Class = Blueprint->GeneratedClass;
		if (Class == nullptr)
		{
			OutResponse->SetBoolField(TEXT("ok"), false);
			OutResponse->SetStringField(TEXT("message"),
				FString::Printf(TEXT("'%s' has never compiled; compile it once first"), *InClassPath));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Bindable;
		TArray<TSharedPtr<FJsonValue>> Handlers;
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UFunction* Function = *It;
			const UClass* Owner = Function->GetOwnerClass();
			if (Owner == nullptr || !Owner->IsChildOf(UDreamUserWidget::StaticClass()))
			{
				continue;
			}
			if (Function->HasAnyFunctionFlags(FUNC_Delegate))
			{
				continue;
			}
			if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
			{
				continue;
			}

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("name"), Function->GetName());
			const int32 ParamCount = InputParameterCount(Function);
			Info->SetNumberField(TEXT("paramCount"), ParamCount);
			if (const FProperty* Return = Function->GetReturnProperty())
			{
				Info->SetStringField(TEXT("returnType"), Return->GetCPPType());
			}

			Handlers.Add(MakeShared<FJsonValueObject>(Info));
			if (ParamCount == 0 && Function->GetReturnProperty() != nullptr)
			{
				Bindable.Add(MakeShared<FJsonValueObject>(Info));
			}
		}

		TSharedPtr<FJsonObject> Functions = MakeShared<FJsonObject>();
		Functions->SetArrayField(TEXT("bindable"), Bindable);
		Functions->SetArrayField(TEXT("handlers"), Handlers);
		OutResponse->SetObjectField(TEXT("functions"), Functions);
		OutResponse->SetBoolField(TEXT("ok"), true);
		OutResponse->SetStringField(TEXT("message"), FString());
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
