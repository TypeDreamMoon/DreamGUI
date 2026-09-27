// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/DreamLayoutPassContext.h"
#include "Core/DreamUIWorldService.h"
#include "Core/DreamWidgetLifecycle.h"
#include "Core/DreamWidgetTreeHost.h"
#include "DreamWidgetLifecycleTestTypes.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The contracts the core declares for the work ahead -- who owns a widget tree, the states a widget moves
 * through, the order a world's services tear down in, and the state one layout pass keeps -- held to what
 * their headers say before anything is built on them. Each header is compiled here, and nowhere else yet.
 */
namespace DreamCoreContractsTestLocal
{
	const TCHAR* StepName(EDreamWidgetLifecycleStep InStep)
	{
		switch (InStep)
		{
		case EDreamWidgetLifecycleStep::Register: return TEXT("Register");
		case EDreamWidgetLifecycleStep::BeginPlay: return TEXT("BeginPlay");
		case EDreamWidgetLifecycleStep::EndPlay: return TEXT("EndPlay");
		case EDreamWidgetLifecycleStep::Unregister: return TEXT("Unregister");
		case EDreamWidgetLifecycleStep::Destroy: return TEXT("Destroy");
		default: return TEXT("?");
		}
	}

	/** A service that writes its name into a shared log when it tears down. */
	class FLoggingService : public IDreamUIWorldService
	{
	public:
		FLoggingService(int32 InPriority, const TCHAR* InName)
			: Priority(InPriority)
			, Name(InName)
		{
		}
		virtual int32 GetTeardownPriority() const override
		{
			return Priority;
		}
		virtual void TeardownForWorld(UWorld& InWorld) override
		{
		}
		FString GetName() const
		{
			return Name;
		}

	private:
		int32 Priority;
		FString Name;
	};
}

// The table is constexpr, so it can be held at compile time too.
static_assert(DreamUI::NextLifecycle(EDreamWidgetLifecycle::Constructed, EDreamWidgetLifecycleStep::Register) == EDreamWidgetLifecycle::Registered);
static_assert(DreamUI::NextLifecycle(EDreamWidgetLifecycle::BegunPlay, EDreamWidgetLifecycleStep::Destroy) == EDreamWidgetLifecycle::Destroyed);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetLifecycleTableTest,
	"DreamGUI.Core.AWidgetsLifecycleStepsGoOnlyWhereTheTableSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetLifecycleTableTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoreContractsTestLocal;
	using L = EDreamWidgetLifecycle;
	using S = EDreamWidgetLifecycleStep;

	struct FRow
	{
		L From;
		S Step;
		L To;
	};
	const FRow Rows[] =
	{
		{ L::Constructed, S::Register, L::Registered }, { L::Registered, S::Register, L::Registered },
		{ L::BegunPlay, S::Register, L::BegunPlay }, { L::Destroyed, S::Register, L::Destroyed },

		{ L::Constructed, S::BeginPlay, L::Constructed }, { L::Registered, S::BeginPlay, L::BegunPlay },
		{ L::BegunPlay, S::BeginPlay, L::BegunPlay }, { L::Destroyed, S::BeginPlay, L::Destroyed },

		{ L::Constructed, S::EndPlay, L::Constructed }, { L::Registered, S::EndPlay, L::Registered },
		{ L::BegunPlay, S::EndPlay, L::Registered }, { L::Destroyed, S::EndPlay, L::Destroyed },

		{ L::Constructed, S::Unregister, L::Constructed }, { L::Registered, S::Unregister, L::Constructed },
		{ L::BegunPlay, S::Unregister, L::BegunPlay }, { L::Destroyed, S::Unregister, L::Destroyed },

		{ L::Constructed, S::Destroy, L::Destroyed }, { L::Registered, S::Destroy, L::Destroyed },
		{ L::BegunPlay, S::Destroy, L::Destroyed }, { L::Destroyed, S::Destroy, L::Destroyed },
	};
	for (const FRow& Row : Rows)
	{
		const L Next = DreamUI::NextLifecycle(Row.From, Row.Step);
		TestEqual(*FString::Printf(TEXT("%s, then %s"), LexToString(Row.From), StepName(Row.Step)),
			FString(LexToString(Next)), FString(LexToString(Row.To)));
		// Idempotent: asking the same step again changes nothing more.
		TestEqual(*FString::Printf(TEXT("%s, then %s twice"), LexToString(Row.From), StepName(Row.Step)),
			FString(LexToString(DreamUI::NextLifecycle(Next, Row.Step))), FString(LexToString(Next)));
		// And the one request that is a mistake rather than a no-op is registering what is destroyed.
		TestEqual(*FString::Printf(TEXT("%s, then %s, is a mistake to report"), LexToString(Row.From), StepName(Row.Step)),
			DreamUI::IsLifecycleMistake(Row.From, Row.Step), Row.From == L::Destroyed && Row.Step == S::Register);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutPassContextBalanceTest,
	"DreamGUI.Core.ALayoutPassContextIsBalancedOnlyOnceEveryScopeHasClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutPassContextBalanceTest::RunTest(const FString& Parameters)
{
	using FContext = FDreamLayoutPassContext;
	const TStrongObjectPtr<UDreamTreeHostProbe> First(NewObject<UDreamTreeHostProbe>());
	const TStrongObjectPtr<UDreamTreeHostProbe> Second(NewObject<UDreamTreeHostProbe>());
	const FContext::FDesiredSizeKey FirstWide{ FObjectKey(First.Get()), 1 };
	const FContext::FDesiredSizeKey FirstTall{ FObjectKey(First.Get()), 2 };
	const FContext::FDesiredSizeKey SecondWide{ FObjectKey(Second.Get()), 1 };

	FContext Context;
	TestTrue(TEXT("A new context is balanced"), Context.IsBalanced());
	TestFalse(TEXT("and not writing"), Context.IsWriting());
	{
		FContext::FPassScope Pass(Context);
		TestTrue(TEXT("A pass is writing"), Context.IsWriting());
		TestFalse(TEXT("and unbalanced while it runs"), Context.IsBalanced());
		{
			FContext::FMemoScope Memo(Context);
			Context.RecordDesiredSize(FirstWide, FVector2D(10.0, 20.0));
			Context.RecordDesiredSize(FirstTall, FVector2D(30.0, 40.0));
			Context.RecordDesiredSize(SecondWide, FVector2D(50.0, 60.0));
			const FVector2D* Answer = Context.FindDesiredSize(FirstWide);
			TestTrue(TEXT("The memo answers what was recorded in it"), Answer != nullptr && Answer->Equals(FVector2D(10.0, 20.0)));
			{
				FContext::FWriteScope Outer(Context, First.Get());
				TestTrue(TEXT("A writer is known while its scope is open"), Context.IsWriter(First.Get()));
				TestFalse(TEXT("and another widget is not a writer"), Context.IsWriter(Second.Get()));
				{
					FContext::FWriteScope Inner(Context, Second.Get());
					TestTrue(TEXT("Write scopes nest"), Context.IsWriter(First.Get()) && Context.IsWriter(Second.Get()));
				}
				TestFalse(TEXT("and the inner writer is gone once its scope closes"), Context.IsWriter(Second.Get()));
			}
			Context.ForgetDesiredSizes(First.Get());
			TestNull(TEXT("Forgetting a widget drops every answer for it"), Context.FindDesiredSize(FirstTall));
			TestNotNull(TEXT("and keeps the other widgets' answers"), Context.FindDesiredSize(SecondWide));
			{
				FContext::FMemoScope NestedMemo(Context);
				Context.RecordDesiredSize(FirstWide, FVector2D(1.0, 2.0));
			}
			TestNotNull(TEXT("A nested memo scope closing leaves the memo answering"), Context.FindDesiredSize(FirstWide));
		}
		TestNull(TEXT("and the outermost closing forgets everything"), Context.FindDesiredSize(SecondWide));
	}
	TestTrue(TEXT("With every scope closed the context is balanced again"), Context.IsBalanced());
	TestFalse(TEXT("and no longer writing"), Context.IsWriting());

	Context.RecordDesiredSize(FirstWide, FVector2D(7.0, 8.0));
	{
		FContext::FMemoScope Memo(Context);
		TestNull(TEXT("Nothing recorded outside a memo scope is there inside the next one"), Context.FindDesiredSize(FirstWide));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldServiceTeardownOrderTest,
	"DreamGUI.Core.WorldServicesTearDownHighestPriorityFirstAndInRegistrationOrderAmongEquals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldServiceTeardownOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoreContractsTestLocal;
	namespace Priority = DreamUI::WorldServiceTeardownPriority;

	FLoggingService Hosts(Priority::Hosts, TEXT("hosts"));
	FLoggingService Tooltip(Priority::Input, TEXT("tooltip"));
	FLoggingService Screen(Priority::Layers, TEXT("screen"));
	FLoggingService DragDrop(Priority::Input, TEXT("dragdrop"));
	FLoggingService Popups(Priority::Layers, TEXT("popups"));

	// Registration order, as services initialize: nothing about it is sorted.
	TArray<IDreamUIWorldService*> Services = { &Hosts, &Tooltip, &Screen, &DragDrop, &Popups };
	DreamUI::SortForTeardown(Services);

	TArray<FString> Order;
	for (const IDreamUIWorldService* Service : Services)
	{
		Order.Add(static_cast<const FLoggingService*>(Service)->GetName());
	}
	TestEqual(TEXT("Input first, then the layers, then the hosts; each band in the order it registered"),
		FString::Join(Order, TEXT(",")), FString(TEXT("tooltip,dragdrop,screen,popups,hosts")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetTreeHostContractTest,
	"DreamGUI.Core.TheTreeHostContractIsAnInterfaceAnyObjectCanImplement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetTreeHostContractTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("UDreamWidgetTreeHost is an interface class"),
		UDreamWidgetTreeHost::StaticClass()->HasAnyClassFlags(CLASS_Interface));

	const TStrongObjectPtr<UDreamTreeHostProbe> Probe(NewObject<UDreamTreeHostProbe>());
	TestTrue(TEXT("A class that implements it says so"), Probe->GetClass()->ImplementsInterface(UDreamWidgetTreeHost::StaticClass()));
	IDreamWidgetTreeHost* Host = Cast<IDreamWidgetTreeHost>(Probe.Get());
	if (!TestNotNull(TEXT("and an object of it casts to the interface"), Host))
	{
		return false;
	}
	TestEqual(TEXT("The tree outer comes from the host"), Host->GetTreeOuter(), static_cast<UObject*>(Probe.Get()));
	Host->ReleaseTree(EDreamTreeReleaseReason::Recompile);
	Host->RebuildTree();
	TestTrue(TEXT("and the calls reach it"), Probe->Releases.Num() == 1 && Probe->Releases[0] == EDreamTreeReleaseReason::Recompile && Probe->Rebuilds == 1);
	return true;
}

#endif
