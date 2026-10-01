// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamPackageLoadCheck.h"

#include "Engine/Blueprint.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace DreamPackageLoadCheckLocal
{
	/** The category the Blueprint compiler reports its diagnostics in, other Blueprints' included. */
	const FName BlueprintCategory(TEXT("LogBlueprint"));

	/**
	 * Categories that log from threads of their own, about nothing a package is: what they say while a package loads is not
	 * the package's. The editor probes for a network connection on start (an HTTP request to a well-known address); behind
	 * a proxy that does not answer it, the request times out a few seconds in -- in the middle of whichever package is
	 * loading then -- and failed the test about that package, now and then, on a machine that has one.
	 */
	bool IsUnrelatedToLoading(const FName& InCategory)
	{
		static const FName Unrelated[] = { TEXT("LogHttp"), TEXT("LogHttpListener"), TEXT("LogOnline"), TEXT("LogAnalytics") };
		for (const FName& Name : Unrelated)
		{
			if (InCategory == Name)
			{
				return true;
			}
		}
		return false;
	}

	/** Every warning and error logged while it lives, from whichever thread logs it: problems, and the compiler's warnings. */
	class FProblemCapture : public FOutputDevice
	{
	public:
		FProblemCapture()
		{
			GLog->AddOutputDevice(this);
		}
		virtual ~FProblemCapture() override
		{
			GLog->RemoveOutputDevice(this);
		}
		virtual bool CanBeUsedOnAnyThread() const override
		{
			return true;
		}
		virtual bool CanBeUsedOnMultipleThreads() const override
		{
			return true;
		}
		virtual void Serialize(const TCHAR* InText, ELogVerbosity::Type InVerbosity, const FName& InCategory) override
		{
			const ELogVerbosity::Type Verbosity = (ELogVerbosity::Type)(InVerbosity & ELogVerbosity::VerbosityMask);
			if (Verbosity == ELogVerbosity::NoLogging || Verbosity > ELogVerbosity::Warning || IsUnrelatedToLoading(InCategory))
			{
				return;
			}
			const FString Line = FString::Printf(TEXT("%s: %s"), *InCategory.ToString(), InText);
			FScopeLock Lock(&Guard);
			(Verbosity == ELogVerbosity::Warning && InCategory == BlueprintCategory ? CompilerWarnings : Problems).Add(Line);
		}
		void TakeInto(DreamPackageLoadCheck::FResult& OutResult)
		{
			FScopeLock Lock(&Guard);
			OutResult.Problems.Append(MoveTemp(Problems));
			OutResult.OtherNotes.Append(MoveTemp(CompilerWarnings));
		}

	private:
		FCriticalSection Guard;
		TArray<FString> Problems;
		TArray<FString> CompilerWarnings;
	};
}

DreamPackageLoadCheck::FResult DreamPackageLoadCheck::LoadAndCompile(const FString& InPackageName)
{
	using namespace DreamPackageLoadCheckLocal;

	FResult Result;
	FProblemCapture Capture;
	Result.Package = LoadPackage(nullptr, *InPackageName, LOAD_None);
	if (Result.Package == nullptr)
	{
		Result.Problems.Add(FString::Printf(TEXT("%s did not load"), *InPackageName));
	}
	else
	{
		TArray<UBlueprint*> Blueprints;
		ForEachObjectWithPackage(Result.Package, [&Blueprints](UObject* InObject)
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(InObject))
			{
				Blueprints.Add(Blueprint);
			}
			return true;
		});
		for (UBlueprint* Blueprint : Blueprints)
		{
			FCompilerResultsLog Results;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
			{
				const FString Line = FString::Printf(TEXT("compiling %s: %s"), *Blueprint->GetName(), *Message->ToText().ToString());
				if (Message->GetSeverity() == EMessageSeverity::Error)
				{
					Result.Problems.Add(Line);
				}
				else if (Message->GetSeverity() == EMessageSeverity::Warning)
				{
					Result.CompilerWarnings.Add(Line);
				}
			}
		}
	}
	Capture.TakeInto(Result);
	return Result;
}
