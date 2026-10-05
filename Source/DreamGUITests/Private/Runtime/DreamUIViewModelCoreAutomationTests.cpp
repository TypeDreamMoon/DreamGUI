// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamScopedGameInstanceWorld.h"
#include "DreamScopedWorld.h"
#include "DreamViewModelRuntimeTestTypes.h"
#include "DreamViewModelTestTypes.h"
#include "Core/DreamUIBindingObserver.h"
#include "ViewModel/DreamViewModel.h"
#include "ViewModel/DreamViewModelSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Templates/UniquePtr.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The view-model run time with nothing compiled: the observer that watches member paths, the view model base and its
 * setter macro, and the registry. Everything a widget's `Player.Stats.ClassName` binding stands on, driven by hand on
 * the test view models -- the counting ones, whose delegate count is how "every subscription came back off" is seen
 * from outside.
 */

namespace DreamUIViewModelCoreTestLocal
{
	using UE::FieldNotification::FFieldId;

	template <typename T>
	TStrongObjectPtr<T> MakeObject()
	{
		return TStrongObjectPtr<T>(NewObject<T>(GetTransientPackage()));
	}

	/** An observer whose reports land in Reports, client id by client id. */
	struct FRecordingObserver
	{
		FDreamUIBindingObserver Observer;
		TArray<int32> Reports;

		void Initialize(UObject* InRoot, UObject* InLifetimeOwner)
		{
			Observer.Initialize(InRoot, InLifetimeOwner, FDreamUIBindingObserver::FOnClientChanged::CreateLambda([this](int32 InClientId)
			{
				Reports.Add(InClientId);
			}));
		}
	};

	TArray<FName> Path(std::initializer_list<const TCHAR*> InSegments)
	{
		TArray<FName> Segments;
		for (const TCHAR* Segment : InSegments)
		{
			Segments.Add(FName(Segment));
		}
		return Segments;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelObserverTwoHopTest,
	"DreamGUI.ViewModel.Core.TheObserverFollowsATwoHopPathAndReaimsWhenTheMiddleIsReplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `Stats.ClassName` from a player: one subscription on the player's Stats, one on the stats object's ClassName. The
 * leaf changing reports once. The middle being replaced takes the old stats object's delegate off, puts one on the
 * new one, and reports -- after which the old object's broadcasts reach nobody and the new one's do. Stop leaves
 * nothing behind on either.
 */
bool FDreamUIViewModelObserverTwoHopTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakeObject<UDreamTestCountingPlayerVM>();
	const TStrongObjectPtr<UDreamTestCountingStatsVM> OldStats = MakeObject<UDreamTestCountingStatsVM>();
	const TStrongObjectPtr<UDreamTestCountingStatsVM> NewStats = MakeObject<UDreamTestCountingStatsVM>();
	const TStrongObjectPtr<UDreamTestPlainModel> Owner = MakeObject<UDreamTestPlainModel>();
	Player->SetStats(OldStats.Get());

	FRecordingObserver Recorder;
	Recorder.Initialize(Player.Get(), Owner.Get());
	Recorder.Observer.AddPath(Path({ TEXT("Stats"), TEXT("ClassName") }), 7);
	TestEqual(TEXT("nothing is subscribed before Start"), Recorder.Observer.GetSubscriptionCount(), 0);
	Recorder.Observer.Start();
	TestTrue(TEXT("started"), Recorder.Observer.IsStarted());
	TestEqual(TEXT("one subscription per hop"), Recorder.Observer.GetSubscriptionCount(), 2);
	TestEqual(TEXT("the player is listened to once"), Player->LiveDelegateCount, 1);
	TestEqual(TEXT("and the stats object once"), OldStats->LiveDelegateCount, 1);
	TestEqual(TEXT("starting reports nothing"), Recorder.Reports.Num(), 0);

	OldStats->SetClassName(FText::FromString(TEXT("Mage")));
	TestEqual(TEXT("the leaf changing reports once"), Recorder.Reports, TArray<int32>({ 7 }));

	Recorder.Reports.Reset();
	Player->SetStats(NewStats.Get());
	TestEqual(TEXT("replacing the middle reports"), Recorder.Reports, TArray<int32>({ 7 }));
	TestEqual(TEXT("and takes the old object's delegate off"), OldStats->LiveDelegateCount, 0);
	TestEqual(TEXT("and puts one on the new one"), NewStats->LiveDelegateCount, 1);
	TestEqual(TEXT("still one per hop"), Recorder.Observer.GetSubscriptionCount(), 2);

	Recorder.Reports.Reset();
	OldStats->SetClassName(FText::FromString(TEXT("Old")));
	TestEqual(TEXT("the old object's broadcasts reach nobody"), Recorder.Reports.Num(), 0);
	NewStats->SetClassName(FText::FromString(TEXT("New")));
	TestEqual(TEXT("the new one's do"), Recorder.Reports, TArray<int32>({ 7 }));

	Recorder.Reports.Reset();
	Player->SetStats(nullptr);
	TestEqual(TEXT("the middle cleared reports too"), Recorder.Reports, TArray<int32>({ 7 }));
	TestEqual(TEXT("past an unset object nothing is subscribed"), Recorder.Observer.GetSubscriptionCount(), 1);
	TestEqual(TEXT("so the new stats object is let go"), NewStats->LiveDelegateCount, 0);

	Player->SetStats(OldStats.Get());
	Recorder.Observer.Stop();
	TestFalse(TEXT("stopped"), Recorder.Observer.IsStarted());
	TestEqual(TEXT("Stop leaves no subscription"), Recorder.Observer.GetSubscriptionCount(), 0);
	TestEqual(TEXT("and no delegate on the player"), Player->LiveDelegateCount, 0);
	TestEqual(TEXT("or on the stats object"), OldStats->LiveDelegateCount, 0);

	Recorder.Reports.Reset();
	OldStats->SetClassName(FText::FromString(TEXT("Quiet")));
	TestEqual(TEXT("a stopped observer reports nothing"), Recorder.Reports.Num(), 0);

	Recorder.Observer.Start();
	TestEqual(TEXT("started again, it subscribes along the path as it is now"), Recorder.Observer.GetSubscriptionCount(), 2);
	TestEqual(TEXT("on the object the player holds now"), OldStats->LiveDelegateCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelObserverSharingTest,
	"DreamGUI.ViewModel.Core.TheObserverSharesOneDelegatePerFieldAndReportsEachClientOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelObserverSharingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakeObject<UDreamTestCountingPlayerVM>();
	const TStrongObjectPtr<UDreamTestCountingStatsVM> Stats = MakeObject<UDreamTestCountingStatsVM>();
	const TStrongObjectPtr<UDreamTestCountingStatsVM> OtherStats = MakeObject<UDreamTestCountingStatsVM>();
	const TStrongObjectPtr<UDreamTestPlainModel> Owner = MakeObject<UDreamTestPlainModel>();
	Player->SetStats(Stats.Get());

	FRecordingObserver Recorder;
	Recorder.Initialize(Player.Get(), Owner.Get());
	// Client 1 reads two members of the stats object; client 2 reads one of them too, and the same path twice.
	Recorder.Observer.AddPath(Path({ TEXT("Stats"), TEXT("ClassName") }), 1);
	Recorder.Observer.AddPath(Path({ TEXT("Stats"), TEXT("Rank") }), 1);
	Recorder.Observer.AddPath(Path({ TEXT("Stats"), TEXT("ClassName") }), 2);
	Recorder.Observer.AddPath(Path({ TEXT("Stats"), TEXT("ClassName") }), 2);
	Recorder.Observer.Start();

	TestEqual(TEXT("Player.Stats, Stats.ClassName and Stats.Rank: three subscriptions, however many paths share them"),
		Recorder.Observer.GetSubscriptionCount(), 3);
	TestEqual(TEXT("one delegate on the player"), Player->LiveDelegateCount, 1);
	TestEqual(TEXT("one per field on the stats object"), Stats->LiveDelegateCount, 2);

	Stats->SetRank(5);
	TestEqual(TEXT("a field only client 1 reads reports client 1 only"), Recorder.Reports, TArray<int32>({ 1 }));

	Recorder.Reports.Reset();
	Stats->SetClassName(FText::FromString(TEXT("Rogue")));
	Recorder.Reports.Sort();
	TestEqual(TEXT("a field both read reports each once"), Recorder.Reports, TArray<int32>({ 1, 2 }));

	Recorder.Reports.Reset();
	Player->SetStats(OtherStats.Get());
	Recorder.Reports.Sort();
	TestEqual(TEXT("replacing the object all three paths pass through reports each client once"), Recorder.Reports, TArray<int32>({ 1, 2 }));
	TestEqual(TEXT("the old object is let go entirely"), Stats->LiveDelegateCount, 0);
	TestEqual(TEXT("the new one gets one delegate per field"), OtherStats->LiveDelegateCount, 2);

	Recorder.Observer.Stop();
	TestEqual(TEXT("Stop takes all of them back"), Player->LiveDelegateCount + OtherStats->LiveDelegateCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelObserverSetRootTest,
	"DreamGUI.ViewModel.Core.SetRootReaimsTheObserverWithoutReporting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelObserverSetRootTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> First = MakeObject<UDreamTestCountingPlayerVM>();
	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Second = MakeObject<UDreamTestCountingPlayerVM>();
	const TStrongObjectPtr<UDreamTestPlainModel> Owner = MakeObject<UDreamTestPlainModel>();

	FRecordingObserver Recorder;
	Recorder.Initialize(First.Get(), Owner.Get());
	Recorder.Observer.AddPath(Path({ TEXT("Name") }), 3);

	// Before Start: only the root changes.
	Recorder.Observer.SetRoot(Second.Get());
	TestFalse(TEXT("SetRoot does not start an observer that was not started"), Recorder.Observer.IsStarted());
	TestEqual(TEXT("and subscribes nothing"), Second->LiveDelegateCount, 0);
	Recorder.Observer.SetRoot(First.Get());

	Recorder.Observer.Start();
	TestEqual(TEXT("watching the first root"), First->LiveDelegateCount, 1);

	Recorder.Observer.SetRoot(Second.Get());
	TestTrue(TEXT("a started observer stays started"), Recorder.Observer.IsStarted());
	TestTrue(TEXT("and answers the new root"), Recorder.Observer.GetRoot() == Second.Get());
	TestEqual(TEXT("SetRoot reports nothing: the caller re-applies its values itself"), Recorder.Reports.Num(), 0);
	TestEqual(TEXT("the old root is let go"), First->LiveDelegateCount, 0);
	TestEqual(TEXT("the new one is watched"), Second->LiveDelegateCount, 1);

	First->SetName(FText::FromString(TEXT("old")));
	TestEqual(TEXT("the old root's change reaches nobody"), Recorder.Reports.Num(), 0);
	Second->SetName(FText::FromString(TEXT("new")));
	TestEqual(TEXT("the new root's does"), Recorder.Reports, TArray<int32>({ 3 }));

	Recorder.Observer.SetRoot(nullptr);
	TestEqual(TEXT("no root, nothing watched"), Recorder.Observer.GetSubscriptionCount(), 0);
	TestEqual(TEXT("and the last root let go"), Second->LiveDelegateCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelObserverReentrancyTest,
	"DreamGUI.ViewModel.Core.AReportMayStopReaimOrDestroyTheObserverOrBroadcastAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A report is a client running arbitrary code: it writes values (which broadcast on the very objects being watched),
 * re-aims the observer at another item, stops it, or destroys the thing that owns it. None of that may crash, and
 * once it stops or re-aims the observer, the rest of the change is not reported.
 */
bool FDreamUIViewModelObserverReentrancyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakeObject<UDreamTestCountingPlayerVM>();
	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Other = MakeObject<UDreamTestCountingPlayerVM>();
	const TStrongObjectPtr<UDreamTestPlainModel> Owner = MakeObject<UDreamTestPlainModel>();

	// Stop from inside a report, with a second client still to be told.
	{
		FDreamUIBindingObserver Observer;
		TArray<int32> Reports;
		Observer.Initialize(Player.Get(), Owner.Get(), FDreamUIBindingObserver::FOnClientChanged::CreateLambda([&Observer, &Reports](int32 InClientId)
		{
			Reports.Add(InClientId);
			Observer.Stop();
		}));
		Observer.AddPath(Path({ TEXT("Name") }), 1);
		Observer.AddPath(Path({ TEXT("Name") }), 2);
		Observer.Start();
		Player->SetName(FText::FromString(TEXT("stop")));
		TestEqual(TEXT("a client that stops the observer ends the report there"), Reports.Num(), 1);
		TestEqual(TEXT("and the stop took the delegate off"), Player->LiveDelegateCount, 0);
		Player->SetName(FText::FromString(TEXT("after")));
		TestEqual(TEXT("nothing is reported after it"), Reports.Num(), 1);
	}

	// SetRoot from inside a report.
	{
		FDreamUIBindingObserver Observer;
		TArray<int32> Reports;
		Observer.Initialize(Player.Get(), Owner.Get(), FDreamUIBindingObserver::FOnClientChanged::CreateLambda([&Observer, &Reports, &Other](int32 InClientId)
		{
			Reports.Add(InClientId);
			Observer.SetRoot(Other.Get());
		}));
		Observer.AddPath(Path({ TEXT("Name") }), 1);
		Observer.AddPath(Path({ TEXT("Name") }), 2);
		Observer.Start();
		Player->SetName(FText::FromString(TEXT("reaim")));
		TestEqual(TEXT("a client that re-aims the observer ends the report there"), Reports.Num(), 1);
		TestEqual(TEXT("the old root is let go"), Player->LiveDelegateCount, 0);
		TestEqual(TEXT("the new one is watched"), Other->LiveDelegateCount, 1);
		Other->SetName(FText::FromString(TEXT("next")));
		TestEqual(TEXT("and reports from it"), Reports.Num(), 2);
		Observer.Stop();
	}

	// The observer destroyed from inside a report.
	{
		TUniquePtr<FDreamUIBindingObserver> Observer = MakeUnique<FDreamUIBindingObserver>();
		int32 ReportCount = 0;
		Observer->Initialize(Player.Get(), Owner.Get(), FDreamUIBindingObserver::FOnClientChanged::CreateLambda([&Observer, &ReportCount](int32 InClientId)
		{
			++ReportCount;
			Observer.Reset();
		}));
		Observer->AddPath(Path({ TEXT("Name") }), 1);
		Observer->AddPath(Path({ TEXT("Name") }), 2);
		Observer->Start();
		Player->SetName(FText::FromString(TEXT("destroy")));
		TestEqual(TEXT("a client that destroys the observer ends the report there"), ReportCount, 1);
		TestFalse(TEXT("it is gone"), Observer.IsValid());
		TestEqual(TEXT("and its delegate with it"), Player->LiveDelegateCount, 0);
		Player->SetName(FText::FromString(TEXT("later")));
		TestEqual(TEXT("a later broadcast calls into nothing"), ReportCount, 1);
	}

	// A client that writes the field it watches: the nested broadcast is reported, the outer one finishes.
	{
		FDreamUIBindingObserver Observer;
		TArray<int32> Reports;
		Observer.Initialize(Player.Get(), Owner.Get(), FDreamUIBindingObserver::FOnClientChanged::CreateLambda([&Reports, &Player](int32 InClientId)
		{
			Reports.Add(InClientId);
			if (Reports.Num() == 1)
			{
				Player->SetName(FText::FromString(TEXT("echo")));
			}
		}));
		Observer.AddPath(Path({ TEXT("Name") }), 4);
		Observer.Start();
		Player->SetName(FText::FromString(TEXT("first")));
		TestEqual(TEXT("the write from inside the report is reported in turn"), Reports, TArray<int32>({ 4, 4 }));
		TestEqual(TEXT("with one delegate throughout"), Player->LiveDelegateCount, 1);
		Observer.Stop();
	}

	// A path added from inside a report, while started, is subscribed at once.
	{
		FDreamUIBindingObserver Observer;
		TArray<int32> Reports;
		Observer.Initialize(Player.Get(), Owner.Get(), FDreamUIBindingObserver::FOnClientChanged::CreateLambda([&Observer, &Reports](int32 InClientId)
		{
			Reports.Add(InClientId);
			Observer.AddPath(Path({ TEXT("Gold") }), 9);
		}));
		Observer.AddPath(Path({ TEXT("Name") }), 5);
		Observer.Start();
		Player->SetName(FText::FromString(TEXT("grow")));
		TestEqual(TEXT("the path added in the report is watched"), Observer.GetSubscriptionCount(), 2);
		Player->SetGold(10);
		TestTrue(TEXT("and reports"), Reports.Contains(9));
		Observer.Stop();
	}
	TestEqual(TEXT("nothing is left on the player"), Player->LiveDelegateCount, 0);
	TestEqual(TEXT("or on the other one"), Other->LiveDelegateCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelObserverNonNotifyingTest,
	"DreamGUI.ViewModel.Core.ObjectsThatCannotAnnounceAMemberAreNotSubscribed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Whether a binding may rely on the observer at all is ruled on before it watches (CanNotifyAlong); the observer
 * itself just skips what cannot be had -- an object that is not INotifyFieldValueChanged, a member it does not
 * announce -- and keeps watching what it can. An object that implements the interface by hand, with no
 * UDreamViewModel and no UHT descriptor, is watched like any other.
 */
bool FDreamUIViewModelObserverNonNotifyingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	const TStrongObjectPtr<UDreamTestPlainModel> Plain = MakeObject<UDreamTestPlainModel>();
	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakeObject<UDreamTestCountingPlayerVM>();
	const TStrongObjectPtr<UDreamTestHandNotifyModel> Head = MakeObject<UDreamTestHandNotifyModel>();
	const TStrongObjectPtr<UDreamTestHandNotifyModel> Tail = MakeObject<UDreamTestHandNotifyModel>();
	const TStrongObjectPtr<UDreamTestPlainModel> Owner = MakeObject<UDreamTestPlainModel>();

	{
		FRecordingObserver Recorder;
		Recorder.Initialize(Plain.Get(), Owner.Get());
		Recorder.Observer.AddPath(Path({ TEXT("Name") }), 1);
		Recorder.Observer.Start();
		TestEqual(TEXT("an object that is not INotifyFieldValueChanged is not subscribed"), Recorder.Observer.GetSubscriptionCount(), 0);
	}
	{
		FRecordingObserver Recorder;
		Recorder.Initialize(Player.Get(), Owner.Get());
		Recorder.Observer.AddPath(Path({ TEXT("Untracked") }), 1);
		Recorder.Observer.Start();
		TestEqual(TEXT("a member its descriptor does not list is not subscribed"), Recorder.Observer.GetSubscriptionCount(), 0);
		TestEqual(TEXT("so nothing is placed on it"), Player->LiveDelegateCount, 0);
	}
	{
		FRecordingObserver Recorder;
		Recorder.Initialize(Player.Get(), Owner.Get());
		Recorder.Observer.AddPath(Path({ TEXT("Stats"), TEXT("ClassName") }), 1);
		Recorder.Observer.Start();
		TestEqual(TEXT("past an unset member only the member itself is watched"), Recorder.Observer.GetSubscriptionCount(), 1);
	}
	TestEqual(TEXT("each observer took its delegates back as it went"), Player->LiveDelegateCount, 0);

	// By hand: Next is announced, Silent is not.
	TestTrue(TEXT("the hand-written descriptor's path can be announced"),
		DreamUIBindingPath::CanNotifyAlong(UDreamTestHandNotifyModel::StaticClass(), Path({ TEXT("Next"), TEXT("Value") })));
	TestFalse(TEXT("its unlisted member cannot"),
		DreamUIBindingPath::CanNotifyAlong(UDreamTestHandNotifyModel::StaticClass(), Path({ TEXT("Next"), TEXT("Silent") })));
	{
		FRecordingObserver Recorder;
		Recorder.Initialize(Head.Get(), Owner.Get());
		Recorder.Observer.AddPath(Path({ TEXT("Next"), TEXT("Value") }), 1);
		Recorder.Observer.AddPath(Path({ TEXT("Next"), TEXT("Silent") }), 2);
		Recorder.Observer.Start();
		TestEqual(TEXT("with nothing in Next, Next alone is watched"), Recorder.Observer.GetSubscriptionCount(), 1);

		Head->SetNext(Tail.Get());
		Recorder.Reports.Sort();
		TestEqual(TEXT("Next set reports both paths through it"), Recorder.Reports, TArray<int32>({ 1, 2 }));
		TestEqual(TEXT("and subscribes Value on the new object, not Silent"), Recorder.Observer.GetSubscriptionCount(), 2);
		TestEqual(TEXT("one delegate on it"), Tail->LiveDelegateCount, 1);

		Recorder.Reports.Reset();
		Tail->SetValue(3.f);
		TestEqual(TEXT("the hand-made object's broadcast is heard"), Recorder.Reports, TArray<int32>({ 1 }));
		Recorder.Observer.Stop();
		TestEqual(TEXT("and every delegate comes back off it"), Head->LiveDelegateCount + Tail->LiveDelegateCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelObserverDestructorTest,
	"DreamGUI.ViewModel.Core.DestroyingTheObserverTakesEveryDelegateBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelObserverDestructorTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakeObject<UDreamTestCountingPlayerVM>();
	const TStrongObjectPtr<UDreamTestCountingStatsVM> Stats = MakeObject<UDreamTestCountingStatsVM>();
	const TStrongObjectPtr<UDreamTestPlainModel> Owner = MakeObject<UDreamTestPlainModel>();
	Player->SetStats(Stats.Get());

	int32 ReportCount = 0;
	{
		FDreamUIBindingObserver Observer;
		Observer.Initialize(Player.Get(), Owner.Get(), FDreamUIBindingObserver::FOnClientChanged::CreateLambda([&ReportCount](int32)
		{
			++ReportCount;
		}));
		Observer.AddPath(Path({ TEXT("Stats"), TEXT("ClassName") }), 0);
		Observer.AddPath(Path({ TEXT("Name") }), 1);
		Observer.Start();
		TestEqual(TEXT("watching both objects"), Player->LiveDelegateCount + Stats->LiveDelegateCount, 3);
	}
	TestEqual(TEXT("the destructor takes the player's delegates back"), Player->LiveDelegateCount, 0);
	TestEqual(TEXT("and the stats object's"), Stats->LiveDelegateCount, 0);
	Stats->SetClassName(FText::FromString(TEXT("after")));
	Player->SetName(FText::FromString(TEXT("after")));
	TestEqual(TEXT("and nothing reports afterwards"), ReportCount, 0);

	// A lifetime owner that dies first: the delegates are bound weakly to it, so a broadcast after it is gone is dropped.
	{
		UDreamTestPlainModel* ShortLived = NewObject<UDreamTestPlainModel>(GetTransientPackage());
		FDreamUIBindingObserver Observer;
		Observer.Initialize(Player.Get(), ShortLived, FDreamUIBindingObserver::FOnClientChanged::CreateLambda([&ReportCount](int32)
		{
			++ReportCount;
		}));
		Observer.AddPath(Path({ TEXT("Name") }), 0);
		Observer.Start();
		ShortLived->MarkAsGarbage();
		Player->SetName(FText::FromString(TEXT("owner gone")));
		TestEqual(TEXT("a broadcast after the lifetime owner died is dropped"), ReportCount, 0);
	}
	TestEqual(TEXT("and its delegate still comes off"), Player->LiveDelegateCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelSetMacroTest,
	"DreamGUI.ViewModel.Core.DreamVmSetBroadcastsOnlyWhenTheValueChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * DREAM_VM_SET is the whole of a C++ setter: assign and announce on a change, and nothing at all otherwise -- FText
 * compared by what it shows, so a text rebuilt with the same words is no change. A setter that derives a second field
 * (SetGold writing GoldText) announces both, once.
 */
bool FDreamUIViewModelSetMacroTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	const TStrongObjectPtr<UDreamTestPlayerVM> Player = MakeObject<UDreamTestPlayerVM>();
	const TStrongObjectPtr<UDreamTestPlainModel> Owner = MakeObject<UDreamTestPlainModel>();
	TMap<FName, int32> Broadcasts;
	auto Listen = [&Broadcasts, &Player, &Owner](const FFieldId& InFieldId)
	{
		Player->AddFieldValueChangedDelegate(InFieldId, INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(Owner.Get(),
			[&Broadcasts](UObject*, FFieldId InChanged)
			{
				++Broadcasts.FindOrAdd(InChanged.GetName());
			}));
	};
	Listen(UDreamTestPlayerVM::FFieldNotificationClassDescriptor::Name);
	Listen(UDreamTestPlayerVM::FFieldNotificationClassDescriptor::Gold);
	Listen(UDreamTestPlayerVM::FFieldNotificationClassDescriptor::GoldText);
	Listen(UDreamTestPlayerVM::FFieldNotificationClassDescriptor::Health);
	Listen(UDreamTestPlayerVM::FFieldNotificationClassDescriptor::GetHealthPercent);

	const FText Hero = FText::FromString(TEXT("Hero"));
	Player->SetName(Hero);
	Player->SetName(Hero);
	TestEqual(TEXT("the same text twice announces once"), Broadcasts.FindRef(TEXT("Name")), 1);
	Player->SetName(Player->Name);
	TestEqual(TEXT("its own value handed back announces nothing"), Broadcasts.FindRef(TEXT("Name")), 1);
	Player->SetName(FText::FromString(TEXT("Hero")));
	TestEqual(TEXT("a text rebuilt with the same words announces nothing"), Broadcasts.FindRef(TEXT("Name")), 1);
	Player->SetName(FText::FromString(TEXT("Heroine")));
	TestEqual(TEXT("other words announce"), Broadcasts.FindRef(TEXT("Name")), 2);

	Player->SetGold(5);
	Player->SetGold(5);
	TestEqual(TEXT("a number set twice announces once"), Broadcasts.FindRef(TEXT("Gold")), 1);
	TestEqual(TEXT("and the field it derives, once"), Broadcasts.FindRef(TEXT("GoldText")), 1);
	TestEqual(TEXT("which holds the derived value"), Player->GoldText.ToString(), FString(TEXT("5")));

	Player->SetHealth(50.f);
	TestEqual(TEXT("a FieldNotify function is announced by the setter that changes what it reads"),
		Broadcasts.FindRef(TEXT("GetHealthPercent")), 1);

	int32 Gold = 7;
	TestFalse(TEXT("SetAndBroadcast answers false on no change"),
		DreamViewModel::SetAndBroadcast(Player.Get(), Gold, 7, UDreamTestPlayerVM::FFieldNotificationClassDescriptor::Gold));
	TestTrue(TEXT("and true on one"),
		DreamViewModel::SetAndBroadcast(Player.Get(), Gold, 8, UDreamTestPlayerVM::FFieldNotificationClassDescriptor::Gold));
	TestEqual(TEXT("having assigned it"), Gold, 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelBaseTest,
	"DreamGUI.ViewModel.Core.TheViewModelBaseResolvesFieldsByNameAndHasItsOutersWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelBaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	DreamTests::FScopedGameWorld TestWorld;
	TStrongObjectPtr<UDreamTestPlayerVM> Player(NewObject<UDreamTestPlayerVM>(TestWorld.World));
	const TStrongObjectPtr<UDreamTestPlainModel> Owner = MakeObject<UDreamTestPlainModel>();

	TestTrue(TEXT("a view model has the world of its outer"), Player->GetWorld() == TestWorld.World);
	TestNull(TEXT("the class default object has none"), GetDefault<UDreamTestPlayerVM>()->GetWorld());
	TestTrue(TEXT("so Blueprint subclasses get world-context nodes"), GetDefault<UDreamTestPlayerVM>()->ImplementsGetWorld());
	TStrongObjectPtr<UDreamTestPlayerVM> Loose(NewObject<UDreamTestPlayerVM>(GetTransientPackage()));
	TestNull(TEXT("and one outered to no world has none"), Loose->GetWorld());

	TestFalse(TEXT("not a design-time instance unless told"), Player->IsDesignTimeInstance());
	Player->SetDesignTimeInstance(true);
	TestTrue(TEXT("told"), Player->IsDesignTimeInstance());

	// The descriptor lists the C++ fields (UHT's) on top of the base's (none for a native class).
	const UE::FieldNotification::IClassDescriptor& Descriptor = Player->GetFieldNotificationDescriptor();
	TestTrue(TEXT("a FieldNotify property is a field"), Descriptor.GetField(Player->GetClass(), TEXT("Health")).IsValid());
	TestTrue(TEXT("so is a FieldNotify function"), Descriptor.GetField(Player->GetClass(), TEXT("GetHealthPercent")).IsValid());
	TestFalse(TEXT("an unannounced property is not"), Descriptor.GetField(Player->GetClass(), TEXT("Untracked")).IsValid());

	// The Blueprint-facing trio resolves by name.
	int32 Heard = 0;
	Player->AddFieldValueChangedDelegate(UDreamTestPlayerVM::FFieldNotificationClassDescriptor::Level,
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(Owner.Get(), [&Heard](UObject*, FFieldId)
		{
			++Heard;
		}));
	Player->K2_BroadcastFieldValueChanged(FFieldNotificationId(TEXT("Level")));
	TestEqual(TEXT("K2 broadcast by name reaches the field's listeners"), Heard, 1);
	Player->K2_BroadcastFieldValueChanged(FFieldNotificationId(TEXT("NoSuchField")));
	TestEqual(TEXT("and a name the class does not announce reaches nobody"), Heard, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRegistryTest,
	"DreamGUI.ViewModel.Core.TheRegistryRegistersFindsAndUnregisters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The game instance's registry, by its rules: a name finds the entry of that name; no name finds what was registered
 * by class first, then anything of the class; the same object again under the same name changes nothing and tells
 * nobody; a second object under a taken name replaces the first and tells everybody; FindOrCreate makes one outered
 * to the game instance and registers it; Deinitialize empties it.
 */
bool FDreamUIViewModelRegistryTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCoreTestLocal;

	DreamTests::FScopedGameInstanceWorld Game;
	UDreamViewModelSubsystem* Registry = UDreamViewModelSubsystem::Get(Game.World);
	if (!TestNotNull(TEXT("a game instance has a registry"), Registry))
	{
		return false;
	}
	TestTrue(TEXT("found from anything in its world"), UDreamViewModelSubsystem::Get(Game.GameInstance) == Registry);
	TestNull(TEXT("nothing from null"), UDreamViewModelSubsystem::Get(nullptr));
	const TStrongObjectPtr<UDreamTestPlainModel> Stray = MakeObject<UDreamTestPlainModel>();
	TestNull(TEXT("nothing from an object in no world"), UDreamViewModelSubsystem::Get(Stray.Get()));

	TArray<TPair<UObject*, FName>> Heard;
	Registry->OnRegisteredNative.AddLambda([&Heard](UObject* InViewModel, FName InName)
	{
		Heard.Emplace(InViewModel, InName);
	});

	UDreamTestPlayerVM* ByClass = NewObject<UDreamTestPlayerVM>(Game.GameInstance);
	UDreamTestPlayerVM* Stash = NewObject<UDreamTestPlayerVM>(Game.GameInstance);
	UDreamTestPlayerVM* NewStash = NewObject<UDreamTestPlayerVM>(Game.GameInstance);
	UDreamTestStatsVM* Stats = NewObject<UDreamTestStatsVM>(Game.GameInstance);

	Registry->Register(nullptr);
	TestEqual(TEXT("null is ignored, and nobody is told"), Heard.Num(), 0);

	Registry->Register(ByClass);
	TestEqual(TEXT("a registration is announced"), Heard.Num(), 1);
	TestTrue(TEXT("with the object and its name"), Heard.Num() == 1 && Heard[0].Key == ByClass && Heard[0].Value.IsNone());
	TestTrue(TEXT("found by its class"), Registry->Find(UDreamTestPlayerVM::StaticClass()) == ByClass);
	TestTrue(TEXT("and by a base of it"), Registry->Find(UDreamViewModel::StaticClass()) == ByClass);
	TestNull(TEXT("not by an unrelated class"), Registry->Find(UDreamTestStatsVM::StaticClass()));
	TestNull(TEXT("not by a name it was not registered under"), Registry->Find(UDreamTestPlayerVM::StaticClass(), TEXT("Stash")));

	Registry->Register(ByClass);
	TestEqual(TEXT("the same object again changes nothing and tells nobody"), Heard.Num(), 1);

	Registry->Register(Stats);
	TestTrue(TEXT("unnamed objects of different classes stand side by side"),
		Registry->Find(UDreamTestStatsVM::StaticClass()) == Stats && Registry->Find(UDreamTestPlayerVM::StaticClass()) == ByClass);

	Registry->Register(Stash, TEXT("Stash"));
	TestTrue(TEXT("a name finds its entry"), Registry->Find(UDreamTestPlayerVM::StaticClass(), TEXT("Stash")) == Stash);
	TestTrue(TEXT("no name still prefers what was registered by class"), Registry->Find(UDreamTestPlayerVM::StaticClass()) == ByClass);
	TestNull(TEXT("a name with the wrong class finds nothing"), Registry->Find(UDreamTestStatsVM::StaticClass(), TEXT("Stash")));

	const int32 HeardBeforeSwap = Heard.Num();
	Registry->Register(NewStash, TEXT("Stash"));
	TestTrue(TEXT("a second object under a taken name replaces the first"), Registry->Find(UDreamTestPlayerVM::StaticClass(), TEXT("Stash")) == NewStash);
	TestEqual(TEXT("and is announced"), Heard.Num(), HeardBeforeSwap + 1);

	TestTrue(TEXT("Unregister takes an object out"), Registry->Unregister(ByClass));
	TestFalse(TEXT("and says so only once"), Registry->Unregister(ByClass));
	TestTrue(TEXT("no name then falls back to a named entry of the class"), Registry->Find(UDreamTestPlayerVM::StaticClass()) == NewStash);
	TestFalse(TEXT("an object replaced under its name is registered no more"), Registry->Unregister(Stash));

	UObject* Made = Registry->FindOrCreate(UDreamTestItemVM::StaticClass(), TEXT("Made"));
	if (TestNotNull(TEXT("FindOrCreate makes what is not there"), Made))
	{
		TestTrue(TEXT("of the class asked"), Made->IsA<UDreamTestItemVM>());
		TestTrue(TEXT("outered to the game instance"), Made->GetOuter() == Game.GameInstance);
		TestTrue(TEXT("and registered under the name"), Registry->Find(UDreamTestItemVM::StaticClass(), TEXT("Made")) == Made);
		TestTrue(TEXT("a second call finds it"), Registry->FindOrCreate(UDreamTestItemVM::StaticClass(), TEXT("Made")) == Made);
		TestTrue(TEXT("and it has the game's world"), Made->GetWorld() == Game.World);
	}
	TestNull(TEXT("FindOrCreate refuses an abstract class"), Registry->FindOrCreate(UDreamViewModel::StaticClass(), TEXT("Abstract")));

	Registry->Deinitialize();
	TestNull(TEXT("Deinitialize empties the registry"), Registry->Find(UDreamTestPlayerVM::StaticClass(), TEXT("Stash")));
	return true;
}

#endif
