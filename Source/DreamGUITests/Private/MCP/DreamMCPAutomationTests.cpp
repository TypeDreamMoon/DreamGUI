// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "MCP/DreamGUIMCPActions.h"
#include "MCP/DreamGUIMCPCompile.h"
#include "MCP/DreamGUIMCPEndpoint.h"
#include "MCP/DreamGUIMCPLayout.h"
#include "MCP/DreamGUIMCPPreviewStats.h"
#include "MCP/DreamGUIMCPResult.h"
#include "Text/DreamUIDiagnostics.h"

#include "AssetCompilingManager.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpServerModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "IPAddress.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "ShaderCompiler.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

/*
 * DreamGUI.MCP.*: the DreamMCP endpoint (protocol 1) and what `layout` and `preview` say about a screen.
 *
 * Every test but the socket ones drives FEndpoint::Handle() with no socket: the real manifest, the real actions, the real
 * compiler, and TickJobs() stepped by hand so a long action's states can be watched one at a time. The Socket tests bind a
 * real listener and talk HTTP to it, which is also where the loopback-only promise and the port two plugins share are
 * checked. The layout rules are tested twice: on snapshots made by hand (one case that must be reported and one that must
 * not, per rule) and end to end on compiled screens whose rects are known. DreamGUI.MCP.Render.* draws and needs a real RHI.
 */
namespace DreamMCPTestLocal
{
	using namespace DreamGUIMCP;

	static const TCHAR* const TestToken = TEXT("dreammcp-test-token-0123456789abcdef");

	/** The real manifest and actions, no socket, no discovery file, no ticker: Handle() and TickJobs() by hand. */
	struct FTestEndpoint
	{
		FEndpoint Endpoint;

		FTestEndpoint(FAutomationTestBase& Test, const EEndpointMode Mode, const bool bCanRender = false)
		{
			RegisterDreamGUIMCPActions(Endpoint);
			FEndpointOptions Options;
			Options.Mode = Mode;
			Options.Token = TestToken;
			Options.bListen = false;
			Options.bPublishDiscovery = false;
			Options.bRegisterTicker = false;
			Options.bCanRender = bCanRender;
			Options.StepBudgetSeconds = 0.0;
			FString Error;
			Test.TestTrue(FString::Printf(TEXT("the endpoint starts (%s)"), *Error), Endpoint.Start(Options, Error));
		}

		~FTestEndpoint()
		{
			Endpoint.Stop();
			ResetDreamGUIMCPActionState();
		}
	};

	static FHttpReply Send(FEndpoint& Endpoint, const TCHAR* Verb, const FString& Route, const FString& Body = FString(),
		const TCHAR* Token = TestToken, const bool bOrigin = false)
	{
		FHttpExchange Exchange;
		Exchange.Verb = Verb;
		Exchange.Path = FString(TEXT("/dream/gui")) + Route;
		if (Token)
		{
			Exchange.Headers.Add(TEXT("x-dream-token"), Token);
		}
		if (bOrigin)
		{
			Exchange.Headers.Add(TEXT("origin"), TEXT("http://localhost:3000"));
		}
		Exchange.Body = Body;
		return Endpoint.Handle(Exchange);
	}

	static FString CallBody(const FString& Action, const FString& ArgsJson = TEXT("{}"), const FString& Id = TEXT("t-1"))
	{
		return FString::Printf(TEXT("{\"id\":\"%s\",\"action\":\"%s\",\"args\":%s}"), *Id, *Action, *ArgsJson);
	}

	static FString JsonString(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}

	static TSharedPtr<FJsonObject> ParseReply(FAutomationTestBase& Test, const FHttpReply& Reply)
	{
		TSharedPtr<FJsonObject> Object = ParseJsonObject(Reply.Body);
		Test.TestTrue(FString::Printf(TEXT("the reply is a JSON object: %s"), *Reply.Body.Left(300)), Object.IsValid());
		return Object.IsValid() ? Object : MakeShared<FJsonObject>();
	}

	static FString Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		FString Value;
		if (Object.IsValid())
		{
			Object->TryGetStringField(Name, Value);
		}
		return Value;
	}

	static double Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		double Value = 0.0;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(Name, Value);
		}
		return Value;
	}

	static TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		const TSharedPtr<FJsonObject>* Child = nullptr;
		if (Object.IsValid() && Object->TryGetObjectField(Name, Child) && Child)
		{
			return *Child;
		}
		return MakeShared<FJsonObject>();
	}

	static TArray<TSharedPtr<FJsonValue>> ArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Name, Values) && Values)
		{
			return *Values;
		}
		return TArray<TSharedPtr<FJsonValue>>();
	}

	/** The codes of a result's diagnostics, in order. */
	static TArray<FString> Codes(const TSharedPtr<FJsonObject>& Result)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(Result, TEXT("diagnostics")))
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Value->TryGetObject(Object))
			{
				Out.Add(Field(*Object, TEXT("code")));
			}
		}
		return Out;
	}

	static void ExpectRefused(FAutomationTestBase& Test, const TCHAR* What, const FHttpReply& Reply, const int32 Status, const TCHAR* Reason)
	{
		Test.TestEqual(FString::Printf(TEXT("%s: status"), What), Reply.Status, Status);
		const TSharedPtr<FJsonObject> Body = ParseReply(Test, Reply);
		bool bOk = true;
		Body->TryGetBoolField(TEXT("ok"), bOk);
		Test.TestFalse(FString::Printf(TEXT("%s: ok is false"), What), bOk);
		Test.TestEqual(FString::Printf(TEXT("%s: outcome"), What), Field(Body, TEXT("outcome")), FString(TEXT("refused")));
		Test.TestEqual(FString::Printf(TEXT("%s: reason"), What), Field(Body, TEXT("reason")), FString(Reason));
		Test.TestFalse(FString::Printf(TEXT("%s: has a summary"), What), Field(Body, TEXT("summary")).IsEmpty());
	}

	/** A long call run to its end: the 202, then TickJobs and a poll per tick until the job finishes. The job, or null. */
	static TSharedPtr<FJsonObject> RunToEnd(FAutomationTestBase& Test, FEndpoint& Endpoint, const FString& Body, TArray<FString>* OutStatesSeen = nullptr)
	{
		const FHttpReply Accepted = Send(Endpoint, TEXT("POST"), TEXT("/call"), Body);
		if (!Test.TestEqual(FString::Printf(TEXT("a long action answers 202 (%s)"), *Accepted.Body.Left(300)), Accepted.Status, 202))
		{
			return nullptr;
		}
		const FString Job = Field(ParseReply(Test, Accepted), TEXT("job"));
		const double Deadline = FPlatformTime::Seconds() + 240.0;
		while (FPlatformTime::Seconds() < Deadline)
		{
			const TSharedPtr<FJsonObject> Polled = ParseJsonObject(Send(Endpoint, TEXT("GET"), TEXT("/jobs/") + Job).Body);
			const FString State = Field(Polled, TEXT("state"));
			if (OutStatesSeen && (OutStatesSeen->IsEmpty() || OutStatesSeen->Last() != State))
			{
				OutStatesSeen->Add(State);
			}
			if (State == TEXT("done") || State == TEXT("failed") || State == TEXT("cancelled"))
			{
				return Polled;
			}
			Endpoint.TickJobs();
			// A preview waits for shaders that compile on worker processes: let their results land, as a frame would.
			FAssetCompilingManager::Get().ProcessAsyncTasks(true);
			if (GShaderCompilingManager != nullptr)
			{
				GShaderCompilingManager->ProcessAsyncResults(/*bLimitExecutionTime*/ false, /*bBlockOnGlobalShaderCompletion*/ false);
			}
			FPlatformProcess::Sleep(0.0f);
		}
		Test.AddError(FString::Printf(TEXT("The job %s never finished."), *Job));
		return nullptr;
	}

	static TSharedPtr<FJsonObject> ResultOf(const TSharedPtr<FJsonObject>& Job)
	{
		return ObjectField(Job, TEXT("result"));
	}

	/**
	 * .dui files of one test, under Saved/DreamGUITests/MCP_<guid>/, and the package root their class lines name: a mount of
	 * its own (a class line may not name /Temp, which is read-only to a compile, as /Engine is), taken down afterwards. The
	 * endpoints of these tests are editors, which keep what they compile in memory: nothing is saved under it.
	 */
	struct FScopedSources
	{
		FString Directory;
		FString PackageRoot;
		FString MountRoot;
		FString MountDirectory;

		FScopedSources()
		{
			const FString Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("MCP_") + Unique));
			FPaths::NormalizeDirectoryName(Directory);
			IFileManager::Get().MakeDirectory(*Directory, true);
			MountRoot = TEXT("/DreamMCPTests_") + Unique + TEXT("/");
			MountDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("MCPAssets_") + Unique)) + TEXT("/");
			IFileManager::Get().MakeDirectory(*MountDirectory, true);
			FPackageName::RegisterMountPoint(MountRoot, MountDirectory);
			PackageRoot = MountRoot.LeftChop(1);
		}

		~FScopedSources()
		{
			FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory);
			IFileManager::Get().DeleteDirectory(*MountDirectory, false, true);
			IFileManager::Get().DeleteDirectory(*Directory, false, true);
		}

		/** Writes Name.dui with InBody below a `class <root>/WBP_<Name>` line (unless InBody has its own). The absolute path. */
		FString Write(const FString& InName, const FString& InBody, const bool bClassLine = true) const
		{
			const FString Path = FPaths::Combine(Directory, InName + TEXT(".dui"));
			const FString Text = bClassLine ? FString::Printf(TEXT("class %s/WBP_%s\n\n%s"), *PackageRoot, *InName, *InBody) : InBody;
			FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			return Path;
		}
	};

	/** A full-screen root and the lines of its children. */
	static FString Screen(const FString& InChildren)
	{
		return FString::Printf(TEXT(
			"Widget Root {\n"
			"    AnchorData.AnchorMin = (0, 0)\n"
			"    AnchorData.AnchorMax = (1, 1)\n"
			"    AnchorData.SizeDelta = (0, 0)\n"
			"%s\n"
			"}\n"), *InChildren);
	}

	/** The screen whose rects are known (Docs/tools/dreammcp.md walks through the same arithmetic). */
	static const TCHAR* const GoldenScreen = TEXT(
		"    Image Panel {\n"
		"        AnchorData.SizeDelta = (400, 200)\n"
		"        Brush.TintColor = #FF0000\n"
		"    }\n"
		"    Image TopLeft {\n"
		"        AnchorData.AnchorMin = (0, 1)\n"
		"        AnchorData.AnchorMax = (0, 1)\n"
		"        AnchorData.Pivot = (0, 1)\n"
		"        AnchorData.SizeDelta = (100, 50)\n"
		"        AnchorData.AnchoredPosition = (20, -30)\n"
		"        Brush.TintColor = #00FF00\n"
		"    }\n"
		"    Widget Column {\n"
		"        AnchorData.AnchorMin = (1, 0)\n"
		"        AnchorData.AnchorMax = (1, 1)\n"
		"        AnchorData.Pivot = (1, 0.5)\n"
		"        AnchorData.SizeDelta = (300, 0)\n"
		"        + VerticalBox {\n"
		"            Spacing = 10\n"
		"            Padding = (20, 20, 20, 20)\n"
		"        }\n"
		"        Image RowA {\n"
		"            Brush.TintColor = #0000FF\n"
		"            @slot HorizontalAlignment = Fill\n"
		"            @slot SizeRule = Auto\n"
		"        }\n"
		"        Image RowB {\n"
		"            Brush.TintColor = #FFFFFF\n"
		"            @slot HorizontalAlignment = Fill\n"
		"            @slot SizeRule = Auto\n"
		"        }\n"
		"    }\n");

	/** A test that compiles sources meant to fail: their errors reach the log, and must not fail the test. */
	class FQuietTestBase : public FAutomationTestBase
	{
	public:
		FQuietTestBase(const FString& InName, const bool bInComplexTask) : FAutomationTestBase(InName, bInComplexTask) {}
		virtual bool SuppressLogErrors() override { return true; }
		virtual bool SuppressLogWarnings() override { return true; }
	};

	/** The node of a layout result's tree at a path, searched depth first. */
	static TSharedPtr<FJsonObject> FindTreeNode(const TSharedPtr<FJsonObject>& InNode, const FString& InPath)
	{
		if (Field(InNode, TEXT("path")) == InPath)
		{
			return InNode;
		}
		for (const TSharedPtr<FJsonValue>& Child : ArrayField(InNode, TEXT("children")))
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Child->TryGetObject(Object))
			{
				if (TSharedPtr<FJsonObject> Found = FindTreeNode(*Object, InPath))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	static void ExpectRect(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& InTree, const TCHAR* InPath, int32 X, int32 Y, int32 W, int32 H)
	{
		const TSharedPtr<FJsonObject> Node = FindTreeNode(InTree, InPath);
		if (!Test.TestTrue(FString::Printf(TEXT("the tree has %s"), InPath), Node.IsValid()))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Rect = ObjectField(Node, TEXT("rect"));
		Test.TestEqual(FString::Printf(TEXT("%s: x"), InPath), static_cast<int32>(Number(Rect, TEXT("x"))), X);
		Test.TestEqual(FString::Printf(TEXT("%s: y"), InPath), static_cast<int32>(Number(Rect, TEXT("y"))), Y);
		Test.TestEqual(FString::Printf(TEXT("%s: w"), InPath), static_cast<int32>(Number(Rect, TEXT("w"))), W);
		Test.TestEqual(FString::Printf(TEXT("%s: h"), InPath), static_cast<int32>(Number(Rect, TEXT("h"))), H);
	}

	// ------------------------------------------------------------------------------- snapshots made by hand

	/** A 1000x600 viewport with a root that fills it. Add returns the new node's index. */
	struct FSnapshotBuilder
	{
		FLayoutSnapshot Snapshot;

		FSnapshotBuilder()
		{
			Snapshot.Viewport = FIntPoint(1000, 600);
			Add(INDEX_NONE, TEXT("Root"), TEXT("Widget"), 0, 0, 1000, 600);
		}

		int32 Add(int32 Parent, const TCHAR* Name, const TCHAR* Type, double X, double Y, double W, double H)
		{
			const int32 Index = Snapshot.Nodes.AddDefaulted();
			FLayoutNode& Node = Snapshot.Nodes[Index];
			Node.Parent = Parent;
			Node.Depth = Parent == INDEX_NONE ? 0 : Snapshot.Nodes[Parent].Depth + 1;
			Node.Name = Name;
			Node.Type = Type;
			Node.Path = Parent == INDEX_NONE ? FString(Name) : Snapshot.Nodes[Parent].Path + TEXT("/") + Name;
			Node.bAuthored = true;
			Node.Line = Index + 1;
			Node.File = TEXT("DUI/Test.dui");
			Node.X = Node.VisX = X;
			Node.Y = Node.VisY = Y;
			Node.W = Node.VisW = W;
			Node.H = Node.VisH = H;
			Node.Order = Index;
			Node.Visual = FCString::Strcmp(Type, TEXT("Widget")) == 0 ? FString() : FString(Type);
			if (Parent != INDEX_NONE)
			{
				Snapshot.Nodes[Parent].Children.Add(Index);
			}
			return Index;
		}

		TArray<FLayoutIssue> Lint() const
		{
			TArray<FLayoutIssue> Issues;
			LintLayout(Snapshot, Issues);
			return Issues;
		}
	};

	static int32 CountCode(const TArray<FLayoutIssue>& Issues, EDreamUIDiagnosticCode Code)
	{
		int32 Count = 0;
		for (const FLayoutIssue& Issue : Issues)
		{
			Count += Issue.Code == static_cast<int32>(Code) ? 1 : 0;
		}
		return Count;
	}

	// ------------------------------------------------------------------------------------- a raw HTTP client

	struct FRawReply
	{
		int32 Status = 0;
		FString Body;
	};

	/** One HTTP/1.1 request over a real TCP connection to 127.0.0.1:Port, ticking the HTTP server between reads. */
	static bool SendRaw(const int32 Port, const FString& RequestText, FRawReply& OutReply, FString& OutError)
	{
		ISocketSubsystem* const Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		TSharedRef<FInternetAddr> Address = Sockets->CreateInternetAddr();
		bool bValid = false;
		Address->SetIp(TEXT("127.0.0.1"), bValid);
		Address->SetPort(Port);
		FSocket* const Socket = Sockets->CreateSocket(NAME_Stream, TEXT("DreamMCPTestClient"), Address->GetProtocolType());
		if (!Socket || !Socket->Connect(*Address))
		{
			OutError = TEXT("could not connect to 127.0.0.1");
			if (Socket) { Sockets->DestroySocket(Socket); }
			return false;
		}
		Socket->SetNonBlocking(true);
		const FTCHARToUTF8 Utf8(*RequestText);
		int32 Sent = 0;
		Socket->Send(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent);

		TArray<uint8> Received;
		bool bComplete = false;
		const double Deadline = FPlatformTime::Seconds() + 10.0;
		while (!bComplete && FPlatformTime::Seconds() < Deadline)
		{
			static_cast<FTSTickerObjectBase&>(FHttpServerModule::Get()).Tick(0.01f);
			uint8 Buffer[4096];
			int32 Read = 0;
			if (Socket->Recv(Buffer, sizeof(Buffer), Read) && Read > 0)
			{
				Received.Append(Buffer, Read);
			}
			const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Received.GetData()), Received.Num());
			const FString Text(Decoded.Length(), Decoded.Get());
			const int32 HeaderEnd = Text.Find(TEXT("\r\n\r\n"));
			if (HeaderEnd != INDEX_NONE)
			{
				TArray<FString> Lines;
				Text.Left(HeaderEnd).ParseIntoArrayLines(Lines);
				int32 ContentLength = -1;
				for (const FString& Line : Lines)
				{
					if (Line.StartsWith(TEXT("content-length:"), ESearchCase::IgnoreCase))
					{
						ContentLength = FCString::Atoi(*Line.RightChop(15).TrimStartAndEnd());
					}
				}
				const FString Body = Text.RightChop(HeaderEnd + 4);
				if (ContentLength >= 0 && FTCHARToUTF8(*Body).Length() >= ContentLength)
				{
					TArray<FString> StatusParts;
					Lines[0].ParseIntoArrayWS(StatusParts);
					OutReply.Status = StatusParts.Num() > 1 ? FCString::Atoi(*StatusParts[1]) : 0;
					OutReply.Body = Body;
					bComplete = true;
				}
			}
			FPlatformProcess::Sleep(0.001f);
		}
		Socket->Close();
		Sockets->DestroySocket(Socket);
		if (!bComplete)
		{
			OutError = TEXT("no complete answer within 10 s");
		}
		return bComplete;
	}

	static FString HelloRequest(const TCHAR* InRoute)
	{
		return FString::Printf(TEXT("GET %s/hello HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Dream-Token: %s\r\nConnection: close\r\n\r\n"), InRoute, TestToken);
	}

	/** Ticks the HTTP server and asks the endpoint to publish until it has, or 10 s. */
	static bool PumpUntilPublished(FEndpoint& Endpoint)
	{
		const double Deadline = FPlatformTime::Seconds() + 10.0;
		while (!Endpoint.IsDiscoveryPublished() && FPlatformTime::Seconds() < Deadline)
		{
			static_cast<FTSTickerObjectBase&>(FHttpServerModule::Get()).Tick(0.01f);
			Endpoint.UpdateDiscovery();
			FPlatformProcess::Sleep(0.005f);
		}
		return Endpoint.IsDiscoveryPublished();
	}
}

using namespace DreamGUIMCP;
using namespace DreamMCPTestLocal;

// ------------------------------------------------------------------------------------------------ protocol

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPHelloTest,
	"DreamGUI.MCP.Protocol.HelloOffersEveryActionOfTheManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPHelloTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor);
	const FHttpReply Reply = Send(Test.Endpoint, TEXT("GET"), TEXT("/hello"));
	TestEqual(TEXT("hello answers 200"), Reply.Status, 200);
	const TSharedPtr<FJsonObject> Body = ParseReply(*this, Reply);
	TestEqual(TEXT("protocol 1"), static_cast<int32>(Number(Body, TEXT("protocol"))), 1);
	TestEqual(TEXT("id"), Field(Body, TEXT("id")), FString(TEXT("gui")));
	TestEqual(TEXT("mode"), Field(Body, TEXT("mode")), FString(TEXT("editor")));
	TestFalse(TEXT("plugin version"), Field(Body, TEXT("plugin")).IsEmpty());
	TestFalse(TEXT("engine version"), Field(Body, TEXT("engine")).IsEmpty());
	TestTrue(TEXT("ws is null without a WebSocket"), Body->HasTypedField<EJson::Null>(TEXT("ws")));
	TestEqual(TEXT("the route is /dream/gui"), Test.Endpoint.GetManifest().Route, FString(TEXT("/dream/gui")));

	TArray<FString> Offered;
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(Body, TEXT("actions")))
	{
		Offered.Add(Value->AsString());
	}
	TArray<FString> Declared;
	for (const FManifestAction& Action : Test.Endpoint.GetManifest().Actions)
	{
		Declared.Add(Action.Name);
	}
	TestEqual(TEXT("hello offers every action of the manifest"), FString::Join(Offered, TEXT(",")), FString::Join(Declared, TEXT(",")));
	TestEqual(TEXT("the actions, in order"), FString::Join(Declared, TEXT(",")), FString(TEXT("check,compile,diagnostics,lookup,layout,preview,info")));

	// `long` in the manifest and the shape of the answer agree: a short action answers 200, a long one 202.
	for (const FManifestAction& Action : Test.Endpoint.GetManifest().Actions)
	{
		const FString Args = Action.Name == TEXT("lookup") ? TEXT("{\"name\":\"Text\"}")
			: (Action.Name == TEXT("compile") || Action.Name == TEXT("layout") || Action.Name == TEXT("preview")) ? TEXT("{\"file\":\"DUI/DoesNotExist.dui\"}")
			: TEXT("{}");
		const FHttpReply Answer = Send(Test.Endpoint, TEXT("POST"), TEXT("/call"), CallBody(Action.Name, Args, Action.Name));
		TestEqual(FString::Printf(TEXT("%s answers %s"), *Action.Name, Action.bLong ? TEXT("202") : TEXT("200")), Answer.Status, Action.bLong ? 202 : 200);
	}
	// Layout reads no pixels: a process that cannot draw still lays screens out. Preview draws.
	TestEqual(TEXT("layout needs only an engine"), Test.Endpoint.GetManifest().FindAction(TEXT("layout"))->Needs, FString(TEXT("engine")));
	TestEqual(TEXT("preview needs a renderer"), Test.Endpoint.GetManifest().FindAction(TEXT("preview"))->Needs, FString(TEXT("render")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPAuthTest,
	"DreamGUI.MCP.Protocol.TokensAndOriginsAreChecked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPAuthTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor);
	ExpectRefused(*this, TEXT("no token"), Send(Test.Endpoint, TEXT("GET"), TEXT("/hello"), FString(), nullptr), 401, TEXT("bad-token"));
	ExpectRefused(*this, TEXT("a wrong token"), Send(Test.Endpoint, TEXT("GET"), TEXT("/hello"), FString(), TEXT("dreammcp-test-token-0123456789abcdeX")), 401, TEXT("bad-token"));
	ExpectRefused(*this, TEXT("a shorter token"), Send(Test.Endpoint, TEXT("GET"), TEXT("/hello"), FString(), TEXT("dreammcp")), 401, TEXT("bad-token"));
	ExpectRefused(*this, TEXT("a call without a token"), Send(Test.Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("info")), nullptr), 401, TEXT("bad-token"));
	ExpectRefused(*this, TEXT("an Origin with the right token"), Send(Test.Endpoint, TEXT("GET"), TEXT("/hello"), FString(), TestToken, true), 403, TEXT("origin-not-allowed"));
	ExpectRefused(*this, TEXT("an Origin without a token"), Send(Test.Endpoint, TEXT("GET"), TEXT("/hello"), FString(), nullptr, true), 403, TEXT("origin-not-allowed"));
	TestEqual(TEXT("the right token passes"), Send(Test.Endpoint, TEXT("GET"), TEXT("/hello")).Status, 200);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPRefusalTest,
	"DreamGUI.MCP.Protocol.RefusalsHaveTheirStatusAndReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPRefusalTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Editor(*this, EEndpointMode::Editor);
	FEndpoint& Endpoint = Editor.Endpoint;

	const FHttpReply Unknown = Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("explode")));
	ExpectRefused(*this, TEXT("an unknown action"), Unknown, 404, TEXT("unknown-action"));
	TArray<FString> Listed;
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(ObjectField(ParseReply(*this, Unknown), TEXT("data")), TEXT("actions")))
	{
		Listed.Add(Value->AsString());
	}
	TestTrue(TEXT("the unknown-action answer lists the actions"), Listed.Contains(TEXT("layout")));
	ExpectRefused(*this, TEXT("a body that is not JSON"), Send(Endpoint, TEXT("POST"), TEXT("/call"), TEXT("check please")), 400, TEXT("bad-request"));
	ExpectRefused(*this, TEXT("args that are not an object"), Send(Endpoint, TEXT("POST"), TEXT("/call"), TEXT("{\"action\":\"check\",\"args\":[1]}")), 400, TEXT("bad-args"));
	ExpectRefused(*this, TEXT("an argument the action does not take"), Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("check"), TEXT("{\"path\":\"x.dui\"}"))), 400, TEXT("bad-args"));
	ExpectRefused(*this, TEXT("an argument of the wrong type"), Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("check"), TEXT("{\"file\":3}"))), 400, TEXT("bad-args"));
	ExpectRefused(*this, TEXT("a required argument missing"), Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("layout"))), 400, TEXT("bad-args"));
	ExpectRefused(*this, TEXT("a value outside its enum"), Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("lookup"), TEXT("{\"name\":\"x\",\"kind\":\"teapot\"}"))), 400, TEXT("bad-args"));
	ExpectRefused(*this, TEXT("an integer that is not one"), Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("layout"), TEXT("{\"file\":\"a.dui\",\"width\":800.5}"))), 400, TEXT("bad-args"));
	ExpectRefused(*this, TEXT("a number below its minimum"), Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("layout"), TEXT("{\"file\":\"a.dui\",\"width\":10}"))), 400, TEXT("bad-args"));
	ExpectRefused(*this, TEXT("an unknown route"), Send(Endpoint, TEXT("GET"), TEXT("/status")), 404, TEXT("unknown-route"));
	ExpectRefused(*this, TEXT("a route under another prefix"), [&]() { FHttpExchange E; E.Verb = TEXT("GET"); E.Path = TEXT("/dream/shader/hello"); E.Headers.Add(TEXT("x-dream-token"), TestToken); return Endpoint.Handle(E); }(), 404, TEXT("unknown-route"));
	ExpectRefused(*this, TEXT("the wrong verb"), Send(Endpoint, TEXT("GET"), TEXT("/call")), 405, TEXT("bad-method"));
	ExpectRefused(*this, TEXT("an unknown job"), Send(Endpoint, TEXT("GET"), TEXT("/jobs/nope")), 404, TEXT("unknown-job"));
	ExpectRefused(*this, TEXT("cancelling an unknown job"), Send(Endpoint, TEXT("POST"), TEXT("/jobs/nope/cancel")), 404, TEXT("unknown-job"));
	ExpectRefused(*this, TEXT("shutdown of an editor"), Send(Endpoint, TEXT("POST"), TEXT("/shutdown")), 409, TEXT("not-a-host"));
	TestFalse(TEXT("an editor does not shut down"), Endpoint.IsShutdownRequested());

	// A long action stays long when it is refused before it starts: 202, and a job whose result is the refusal.
	const TSharedPtr<FJsonObject> NoRenderer = ResultOf(RunToEnd(*this, Endpoint, CallBody(TEXT("preview"), TEXT("{\"file\":\"DUI/a.dui\"}"), TEXT("r-1"))));
	TestEqual(TEXT("preview without a renderer is refused"), Field(NoRenderer, TEXT("reason")), FString(TEXT("render-unavailable")));
	const TSharedPtr<FJsonObject> Missing = ResultOf(RunToEnd(*this, Endpoint, CallBody(TEXT("layout"), TEXT("{\"file\":\"DUI/DoesNotExist.dui\"}"), TEXT("r-2"))));
	TestEqual(TEXT("layout of a file that is not there is refused"), Field(Missing, TEXT("reason")), FString(TEXT("not-found")));
	const TSharedPtr<FJsonObject> BadSizes = ResultOf(RunToEnd(*this, Endpoint, CallBody(TEXT("layout"), TEXT("{\"file\":\"DUI/a.dui\",\"sizes\":\"wide\"}"), TEXT("r-3"))));
	TestEqual(TEXT("sizes that do not read are refused"), Field(BadSizes, TEXT("reason")), FString(TEXT("bad-args")));

	FTestEndpoint Host(*this, EEndpointMode::Host);
	const FHttpReply Shutdown = Send(Host.Endpoint, TEXT("POST"), TEXT("/shutdown"));
	TestEqual(TEXT("a host accepts shutdown"), Shutdown.Status, 200);
	TestTrue(TEXT("and says so to its lifetime"), Host.Endpoint.IsShutdownRequested());
	return true;
}

// ------------------------------------------------------------------------------------------------------ jobs

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDreamMCPJobsTest,
	FQuietTestBase,
	"DreamGUI.MCP.Jobs.APolledJobRunsAndACancelledOneStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPJobsTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor);
	FEndpoint& Endpoint = Test.Endpoint;
	FScopedSources Sources;
	const FString File = Sources.Write(TEXT("Jobs"), Screen(TEXT("    Text Label { Text = \"Hi\" }")));
	const FString Args = FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(File));
	const auto Job = [this](const FHttpReply& Reply) { return Field(ParseReply(*this, Reply), TEXT("job")); };
	const auto State = [this, &Endpoint](const FString& Id) { return Field(ParseReply(*this, Send(Endpoint, TEXT("GET"), TEXT("/jobs/") + Id)), TEXT("state")); };
	const auto Busy = [this, &Endpoint]() { bool bBusy = false; ParseReply(*this, Send(Endpoint, TEXT("GET"), TEXT("/hello")))->TryGetBoolField(TEXT("busy"), bBusy); return bBusy; };

	const FString A = Job(Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("check"), Args, TEXT("job-a"))));
	TestEqual(TEXT("a repeated call id answers with the same job"), Job(Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("check"), Args, TEXT("job-a")))), A);
	const FString B = Job(Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("check"), Args, TEXT("job-b"))));
	TestFalse(TEXT("another call id is another job"), A == B);
	TestEqual(TEXT("a new job is queued"), State(A), FString(TEXT("queued")));
	TestTrue(TEXT("hello is busy while jobs wait"), Busy());

	bool bCancelled = false;
	ParseReply(*this, Send(Endpoint, TEXT("POST"), TEXT("/jobs/") + B + TEXT("/cancel")))->TryGetBoolField(TEXT("cancelled"), bCancelled);
	TestTrue(TEXT("a queued job is cancelled"), bCancelled);
	TestEqual(TEXT("and stays cancelled"), State(B), FString(TEXT("cancelled")));

	// A layout of several sizes is several steps: it can be stopped between them.
	const FString L = Job(Send(Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("layout"),
		FString::Printf(TEXT("{\"file\":\"%s\",\"sizes\":\"800x600, 1280x720\"}"), *JsonString(File)), TEXT("job-l"))));
	Endpoint.TickJobs(); // runs A to its end (one file is one step)
	TestEqual(TEXT("the check is done"), State(A), FString(TEXT("done")));
	Endpoint.TickJobs(); // starts the layout: its compile
	TestEqual(TEXT("the layout runs"), State(L), FString(TEXT("running")));
	const TSharedPtr<FJsonObject> Progress = ObjectField(ParseReply(*this, Send(Endpoint, TEXT("GET"), TEXT("/jobs/") + L)), TEXT("progress"));
	TestTrue(TEXT("a running job reports a progress message"), !Field(Progress, TEXT("message")).IsEmpty());
	bool bPending = false;
	ParseReply(*this, Send(Endpoint, TEXT("POST"), TEXT("/jobs/") + L + TEXT("/cancel")))->TryGetBoolField(TEXT("pending"), bPending);
	TestTrue(TEXT("cancelling a running job is pending"), bPending);
	Endpoint.TickJobs();
	TestEqual(TEXT("the next step stops it"), State(L), FString(TEXT("cancelled")));
	TestFalse(TEXT("hello is idle again"), Busy());

	// A layout is a compile, a step per size and the words: polled, it is seen queued, running, then done.
	TArray<FString> Seen;
	const TSharedPtr<FJsonObject> Done = RunToEnd(*this, Endpoint, CallBody(TEXT("layout"), Args, TEXT("job-c")), &Seen);
	TestEqual(TEXT("a polled job passes through queued, running and done"), FString::Join(Seen, TEXT(",")), FString(TEXT("queued,running,done")));
	TestEqual(TEXT("a job description echoes the call id"), Field(Done, TEXT("id")), FString(TEXT("job-c")));
	TestEqual(TEXT("and names its action"), Field(Done, TEXT("action")), FString(TEXT("layout")));
	TestEqual(TEXT("and carries its result"), Field(ResultOf(Done), TEXT("outcome")), FString(TEXT("ok")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPBusyEndpointsTest,
	"DreamGUI.MCP.Jobs.TheIdleFrameCapWaitsForEveryDreamEndpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPBusyEndpointsTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Shared = BusyEndpoints::Find();
	if (!TestNotNull(TEXT("DreamMCP.BusyEndpoints exists"), Shared))
	{
		return false;
	}
	TestTrue(TEXT("under the name every Dream plugin looks for"), IConsoleManager::Get().FindConsoleVariable(TEXT("DreamMCP.BusyEndpoints")) == Shared);
	TestTrue(TEXT("found again, not registered twice"), BusyEndpoints::Find() == Shared);
	const int32 Before = Shared->GetInt();
	Shared->Set(0, ECVF_SetByCode);
	ON_SCOPE_EXIT { Shared->Set(Before, ECVF_SetByCode); };
	TestEqual(TEXT("every endpoint idle: fifteen frames a second"), BusyEndpoints::HostFrameCap(), 15);

	// Another Dream plugin's endpoint in the process takes a job and counts itself in, as its own code does.
	Shared->Set(Shared->GetInt() + 1, ECVF_SetByCode);
	TestEqual(TEXT("another plugin busy: no cap, though DreamGUI's endpoint is idle"), BusyEndpoints::HostFrameCap(), 0);

	FScopedSources Sources;
	const FString File = Sources.Write(TEXT("Busy"), Screen(TEXT("    Text Label { Text = \"Busy\" }")));
	const FString Args = FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(File));
	{
		FTestEndpoint Test(*this, EEndpointMode::Editor);
		TestEqual(TEXT("a queued job is accepted"), Send(Test.Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("check"), Args, TEXT("b-1"))).Status, 202);
		TestEqual(TEXT("and counts DreamGUI in"), Shared->GetInt(), 2);
		for (int32 Tick = 0; Tick < 100 && Test.Endpoint.IsBusy(); ++Tick)
		{
			Test.Endpoint.TickJobs();
		}
		TestFalse(TEXT("the job ends"), Test.Endpoint.IsBusy());
		TestEqual(TEXT("and counts DreamGUI out"), Shared->GetInt(), 1);
		TestEqual(TEXT("the other plugin still busy: still no cap"), BusyEndpoints::HostFrameCap(), 0);

		Shared->Set(Shared->GetInt() - 1, ECVF_SetByCode);
		TestEqual(TEXT("the other plugin idle too: the cap comes back"), BusyEndpoints::HostFrameCap(), 15);

		TestEqual(TEXT("another job"), Send(Test.Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("check"), Args, TEXT("b-2"))).Status, 202);
		TestEqual(TEXT("counts DreamGUI in again"), Shared->GetInt(), 1);
	}
	TestEqual(TEXT("an endpoint stopped with a job queued takes its count back"), Shared->GetInt(), 0);
	BusyEndpoints::Add(-1);
	TestEqual(TEXT("and the count never goes below 0"), Shared->GetInt(), 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------- socket

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPSocketTest,
	"DreamGUI.MCP.Socket.AnswersOnLoopbackOnlyAndPublishesOnceItAnswers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPSocketTest::RunTest(const FString& Parameters)
{
	const FString DiscoveryDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("MCPDiscovery")));
	FEndpoint Endpoint;
	RegisterDreamGUIMCPActions(Endpoint);
	FEndpointOptions Options;
	Options.Mode = EEndpointMode::Host;
	Options.Port = 47960;
	Options.PortAttempts = 30;
	Options.Token = TestToken;
	Options.bRegisterTicker = false;
	Options.DiscoveryDirectory = DiscoveryDirectory;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("a real listener starts (%s)"), *Error), Endpoint.Start(Options, Error)))
	{
		return false;
	}
	ON_SCOPE_EXIT { Endpoint.Stop(); ResetDreamGUIMCPActionState(); IFileManager::Get().DeleteDirectory(*DiscoveryDirectory, false, true); };
	const int32 Port = Endpoint.GetPort();

	TestFalse(TEXT("no discovery file before it has answered"), IFileManager::Get().FileExists(*Endpoint.GetDiscoveryFilePath()));
	Endpoint.UpdateDiscovery();
	TestFalse(TEXT("nor while its own hello waits for the HTTP server to tick"), Endpoint.IsDiscoveryPublished());
	TestTrue(TEXT("published once its own hello is answered"), PumpUntilPublished(Endpoint));
	TestEqual(TEXT("as gui.json"), FPaths::GetCleanFilename(Endpoint.GetDiscoveryFilePath()), FString(TEXT("gui.json")));
	FString DiscoveryText;
	FFileHelper::LoadFileToString(DiscoveryText, *Endpoint.GetDiscoveryFilePath());
	const TSharedPtr<FJsonObject> Discovery = ParseJsonObject(DiscoveryText);
	if (TestTrue(TEXT("the discovery file is JSON"), Discovery.IsValid()))
	{
		TestEqual(TEXT("its pid"), static_cast<int64>(Number(Discovery, TEXT("pid"))), static_cast<int64>(FPlatformProcess::GetCurrentProcessId()));
		TestEqual(TEXT("its port"), static_cast<int32>(Number(Discovery, TEXT("port"))), Port);
		TestEqual(TEXT("its id"), Field(Discovery, TEXT("id")), FString(TEXT("gui")));
		TestEqual(TEXT("its route"), Field(Discovery, TEXT("route")), FString(TEXT("/dream/gui")));
		TestEqual(TEXT("its mode"), Field(Discovery, TEXT("mode")), FString(TEXT("host")));
		TestEqual(TEXT("its token"), Field(Discovery, TEXT("token")), FString(TestToken));
	}

	FRawReply Reply;
	TestTrue(FString::Printf(TEXT("hello over HTTP (%s)"), *Error), SendRaw(Port, HelloRequest(TEXT("/dream/gui")), Reply, Error));
	TestEqual(TEXT("answers 200"), Reply.Status, 200);
	FRawReply Denied;
	SendRaw(Port, TEXT("GET /dream/gui/hello HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"), Denied, Error);
	TestEqual(TEXT("a request without the token over HTTP is 401"), Denied.Status, 401);
	FRawReply Called;
	const FString Body = CallBody(TEXT("info"));
	SendRaw(Port, FString::Printf(TEXT("POST /dream/gui/call HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Dream-Token: %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"),
		TestToken, FTCHARToUTF8(*Body).Length(), *Body), Called, Error);
	TestEqual(TEXT("a short call over HTTP is 200"), Called.Status, 200);

	FString Exposed;
	TestTrue(FString::Printf(TEXT("nothing answers on a non-loopback address (%s)"), *Exposed), FEndpoint::ProveLoopbackOnly(Port, Exposed));
	const FString DiscoveryPath = Endpoint.GetDiscoveryFilePath();
	Endpoint.Stop();
	TestFalse(TEXT("stopping deletes the discovery file"), IFileManager::Get().FileExists(*DiscoveryPath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPSharedPortTest,
	"DreamGUI.MCP.Socket.SharesItsPortWithAnotherDreamPlugin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPSharedPortTest::RunTest(const FString& Parameters)
{
	// Another Dream plugin's endpoint in the same process, as DreamShader's is: its own manifest and route, the same port.
	static const TCHAR* const OtherManifest = TEXT(
		"{\"schema\":1,\"protocol\":1,\"id\":\"other\",\"plugin\":\"OtherPlugin\",\"route\":\"/dream/other\","
		"\"actions\":[{\"name\":\"info\",\"needs\":\"engine\",\"args\":{\"type\":\"object\",\"properties\":{}}}]}");
	const FString DiscoveryDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("MCPSharedPort")));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*DiscoveryDirectory, false, true); };

	FEndpoint Gui;
	RegisterDreamGUIMCPActions(Gui);
	FEndpoint Other;
	FActionHandler OtherInfo;
	OtherInfo.Run = [](const FActionCall&) { return FResult::MakeOk(TEXT("other")); };
	Other.RegisterAction(TEXT("info"), MoveTemp(OtherInfo));

	FEndpointOptions Options;
	Options.Mode = EEndpointMode::Host;
	Options.Port = 47990;
	Options.PortAttempts = 8;
	Options.Token = TestToken;
	Options.bRegisterTicker = false;
	Options.DiscoveryDirectory = DiscoveryDirectory;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("the other plugin's endpoint starts first (%s)"), *Error), [&]() { FEndpointOptions O = Options; O.ManifestText = OtherManifest; return Other.Start(O, Error); }()))
	{
		return false;
	}
	ON_SCOPE_EXIT { Other.Stop(); };
	if (!TestTrue(FString::Printf(TEXT("DreamGUI's starts beside it (%s)"), *Error), Gui.Start(Options, Error)))
	{
		return false;
	}
	ON_SCOPE_EXIT { Gui.Stop(); ResetDreamGUIMCPActionState(); };
	TestEqual(TEXT("both listen on one port"), Gui.GetPort(), Other.GetPort());
	TestTrue(TEXT("DreamGUI publishes"), PumpUntilPublished(Gui));
	TestTrue(TEXT("the other publishes"), PumpUntilPublished(Other));
	TestEqual(TEXT("two discovery files: gui.json"), FPaths::GetCleanFilename(Gui.GetDiscoveryFilePath()), FString(TEXT("gui.json")));
	TestEqual(TEXT("and other.json"), FPaths::GetCleanFilename(Other.GetDiscoveryFilePath()), FString(TEXT("other.json")));
	TestTrue(TEXT("both files are there"), IFileManager::Get().FileExists(*Gui.GetDiscoveryFilePath()) && IFileManager::Get().FileExists(*Other.GetDiscoveryFilePath()));

	FRawReply GuiHello;
	FRawReply OtherHello;
	SendRaw(Gui.GetPort(), HelloRequest(TEXT("/dream/gui")), GuiHello, Error);
	SendRaw(Gui.GetPort(), HelloRequest(TEXT("/dream/other")), OtherHello, Error);
	TestEqual(TEXT("the port answers DreamGUI's route"), Field(ParseJsonObject(GuiHello.Body), TEXT("id")), FString(TEXT("gui")));
	TestEqual(TEXT("and the other's"), Field(ParseJsonObject(OtherHello.Body), TEXT("id")), FString(TEXT("other")));

	// One stopping takes only its own route and file.
	const FString OtherFile = Other.GetDiscoveryFilePath();
	Other.Stop();
	TestFalse(TEXT("the other's file goes with it"), IFileManager::Get().FileExists(*OtherFile));
	TestTrue(TEXT("DreamGUI's stays"), IFileManager::Get().FileExists(*Gui.GetDiscoveryFilePath()));
	FRawReply StillThere;
	SendRaw(Gui.GetPort(), HelloRequest(TEXT("/dream/gui")), StillThere, Error);
	TestEqual(TEXT("and DreamGUI still answers"), StillThere.Status, 200);
	return true;
}

// --------------------------------------------------------------------------------------------------- actions

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDreamMCPCheckTest,
	FQuietTestBase,
	"DreamGUI.MCP.Actions.CheckReportsErrorsAtTheirLinesAndWritesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPCheckTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor);
	FScopedSources Sources;
	const FString Good = Sources.Write(TEXT("Good"), Screen(TEXT("    Text Label { Text = \"Hello\" }")));
	const FString Bad = Sources.Write(TEXT("Bad"), Screen(TEXT("    Text Label { Text = \"Hello\" }\n    Text Label { Text = \"Twice\" }\n    Wobble Nope { }")));

	const TSharedPtr<FJsonObject> Clean = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("check"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(Good)))));
	TestEqual(TEXT("a good file checks ok"), Field(Clean, TEXT("outcome")), FString(TEXT("ok")));
	TestEqual(TEXT("with its nodes counted"), static_cast<int32>(Number(ObjectField(Clean, TEXT("data")), TEXT("checked"))), 1);

	const TSharedPtr<FJsonObject> Broken = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("check"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(Bad)), TEXT("t-2"))));
	TestEqual(TEXT("a broken file is a source error"), Field(Broken, TEXT("outcome")), FString(TEXT("source-error")));
	const TArray<TSharedPtr<FJsonValue>> Diagnostics = ArrayField(Broken, TEXT("diagnostics"));
	if (TestTrue(TEXT("with diagnostics"), Diagnostics.Num() >= 1))
	{
		const TSharedPtr<FJsonObject> First = Diagnostics[0]->AsObject();
		TestTrue(TEXT("a diagnostic has a DUI code"), Field(First, TEXT("code")).StartsWith(TEXT("DUI")));
		TestEqual(TEXT("and is an error"), Field(First, TEXT("severity")), FString(TEXT("error")));
		TestTrue(TEXT("at a line"), Number(First, TEXT("line")) > 0);
		TestTrue(TEXT("with its page in the manual"), Field(First, TEXT("doc")).StartsWith(TEXT("https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui")));
		TestFalse(TEXT("and no absolute path in its file"), Field(First, TEXT("file")).Contains(TEXT(":/")));
	}
	TestTrue(TEXT("the duplicate id is DUI3001"), Codes(Broken).Contains(TEXT("DUI3001")));
	TestFalse(TEXT("check builds no Blueprint"), FPackageName::DoesPackageExist(Sources.PackageRoot + TEXT("/WBP_Good")) || FindPackage(nullptr, *(Sources.PackageRoot + TEXT("/WBP_Good"))) != nullptr);
	TestTrue(TEXT("and says so"), Field(Clean, TEXT("summary")).Contains(TEXT("Nothing was built")));
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDreamMCPCompileTest,
	FQuietTestBase,
	"DreamGUI.MCP.Actions.CompileBuildsTheComponentsAScreenPlacesFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPCompileTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor);
	FScopedSources Sources;
	const FString Card = Sources.Write(TEXT("Card"), TEXT(
		"Widget Root {\n"
		"    AnchorData.SizeDelta = (200, 100)\n"
		"    Image Face { AnchorData.AnchorMin = (0, 0)  AnchorData.AnchorMax = (1, 1)  AnchorData.SizeDelta = (0, 0) }\n"
		"}\n"));
	const FString Menu = Sources.Write(TEXT("Menu"), FString::Printf(TEXT("use \"%s\" as Card\n\n%s"), *Card,
		*Screen(TEXT("    Card First { AnchorData.AnchoredPosition = (0, 100) }\n    Card Second { AnchorData.AnchoredPosition = (0, -100) }"))));

	const TSharedPtr<FJsonObject> Built = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("compile"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(Menu)))));
	TestEqual(TEXT("the screen compiles"), Field(Built, TEXT("outcome")), FString(TEXT("ok")));
	TArray<FString> Order;
	for (const TSharedPtr<FJsonValue>& Asset : ArrayField(ObjectField(Built, TEXT("data")), TEXT("assets")))
	{
		Order.Add(FPackageName::GetLongPackageAssetName(Field(Asset->AsObject(), TEXT("asset"))));
	}
	TestEqual(TEXT("the component before the screen that places it"), FString::Join(Order, TEXT(",")), FString(TEXT("WBP_Card,WBP_Menu")));
	const TArray<TSharedPtr<FJsonValue>> Writes = ArrayField(Built, TEXT("writes"));
	TestEqual(TEXT("both are written"), Writes.Num(), 2);
	if (Writes.Num() > 0)
	{
		TestEqual(TEXT("in the user's editor, in memory"), Field(Writes[0]->AsObject(), TEXT("state")), FString(TEXT("memory")));
	}

	const TSharedPtr<FJsonObject> Again = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("compile"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(Menu)), TEXT("t-2"))));
	TestEqual(TEXT("unchanged sources are not compiled again"), ArrayField(Again, TEXT("writes")).Num(), 0);

	const FString NoClass = Sources.Write(TEXT("NoClass"), Screen(TEXT("    Text Label { Text = \"x\" }")), /*bClassLine*/ false);
	const TSharedPtr<FJsonObject> Refused = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("compile"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(NoClass)), TEXT("t-3"))));
	TestEqual(TEXT("a file with no class line is refused"), Field(Refused, TEXT("reason")), FString(TEXT("no-class-line")));
	TestFalse(TEXT("and told what to write"), ArrayField(Refused, TEXT("next")).IsEmpty());

	const FString Broken = Sources.Write(TEXT("Broken"), Screen(TEXT("    Text Label { Text = \"x\" ")));
	const TSharedPtr<FJsonObject> Failed = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("compile"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(Broken)), TEXT("t-4"))));
	TestEqual(TEXT("a file that does not parse is a source error"), Field(Failed, TEXT("outcome")), FString(TEXT("source-error")));

	// diagnostics: what the engine holds, errors first, without compiling.
	const FHttpReply Held = Send(Test.Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("diagnostics"), TEXT("{}"), TEXT("t-5")));
	TestEqual(TEXT("diagnostics is short"), Held.Status, 200);
	const TSharedPtr<FJsonObject> HeldBody = ParseReply(*this, Held);
	const TArray<TSharedPtr<FJsonValue>> HeldDiagnostics = ArrayField(HeldBody, TEXT("diagnostics"));
	if (TestTrue(TEXT("it holds the broken file's error"), HeldDiagnostics.Num() > 0))
	{
		TestEqual(TEXT("errors first"), Field(HeldDiagnostics[0]->AsObject(), TEXT("severity")), FString(TEXT("error")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPLookupTest,
	"DreamGUI.MCP.Actions.LookupFindsNodeTypesComponentsAndProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLookupTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor);
	const auto Lookup = [this, &Test](const FString& Args) { return ParseReply(*this, Send(Test.Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("lookup"), Args))); };

	const TSharedPtr<FJsonObject> Text = Lookup(TEXT("{\"name\":\"Text\"}"));
	const TSharedPtr<FJsonObject> TextData = ObjectField(Text, TEXT("data"));
	bool bExact = false;
	TextData->TryGetBoolField(TEXT("exact"), bExact);
	TestTrue(TEXT("Text is an exact node type"), bExact);
	TestEqual(TEXT("of the node kind"), Field(ObjectField(TextData, TEXT("entry")), TEXT("kind")), FString(TEXT("node")));
	bool bHasFontSize = false;
	for (const TSharedPtr<FJsonValue>& Line : ArrayField(ObjectField(TextData, TEXT("entry")), TEXT("properties")))
	{
		bHasFontSize |= Line->AsString().StartsWith(TEXT("FontSize:"));
	}
	TestTrue(TEXT("its properties include FontSize with its type"), bHasFontSize);
	const FString TextLines = Field(Text, TEXT("text"));
	TestTrue(TEXT("the text heads the entry with its name and class"), TextLines.StartsWith(TEXT("Text (DreamText)")));
	TestTrue(TEXT("and lists the properties, one a line"), TextLines.Contains(TEXT("\nproperties:\n")) && TextLines.Contains(TEXT("\n  FontSize: ")));

	const TSharedPtr<FJsonObject> One = ObjectField(Lookup(TEXT("{\"name\":\"Text.FontSize\"}")), TEXT("data"));
	const TArray<TSharedPtr<FJsonValue>> OneLines = ArrayField(ObjectField(One, TEXT("entry")), TEXT("properties"));
	TestTrue(TEXT("Type.Property narrows to that property"), OneLines.Num() >= 1 && OneLines.Num() <= 3);

	const TSharedPtr<FJsonObject> Box = ObjectField(Lookup(TEXT("{\"name\":\"VerticalBox\",\"kind\":\"component\"}")), TEXT("data"));
	TestEqual(TEXT("VerticalBox is a component"), Field(ObjectField(Box, TEXT("entry")), TEXT("kind")), FString(TEXT("component")));

	const TSharedPtr<FJsonObject> Partial = ObjectField(Lookup(TEXT("{\"name\":\"Anchor\",\"limit\":5}")), TEXT("data"));
	bool bPartialExact = true;
	Partial->TryGetBoolField(TEXT("exact"), bPartialExact);
	TestFalse(TEXT("part of a name lists matches"), bPartialExact);
	TestTrue(TEXT("at most the limit"), ArrayField(Partial, TEXT("matches")).Num() <= 5);
	TestTrue(TEXT("of a total that may be larger"), Number(Partial, TEXT("total")) >= ArrayField(Partial, TEXT("matches")).Num());
	TestTrue(TEXT("the text names each match and its kind"), Field(Lookup(TEXT("{\"name\":\"Anchor\",\"limit\":5}")), TEXT("text")).Contains(TEXT(" (")));

	// Wire text does not depend on the editor's language: the tooltips are their source's.
	const FString About = Field(ObjectField(TextData, TEXT("entry")), TEXT("about"));
	bool bAscii = true;
	for (const TCHAR Char : About)
	{
		bAscii &= Char < 0x2E80; // no CJK
	}
	TestTrue(FString::Printf(TEXT("the description is the source's, not a translation: %s"), *About.Left(80)), bAscii);

	const TSharedPtr<FJsonObject> Info = ParseReply(*this, Send(Test.Endpoint, TEXT("POST"), TEXT("/call"), CallBody(TEXT("info"))));
	TestEqual(TEXT("info is ok"), Field(Info, TEXT("outcome")), FString(TEXT("ok")));
	TestEqual(TEXT("info names the mode"), Field(ObjectField(Info, TEXT("data")), TEXT("mode")), FString(TEXT("editor")));
	TestFalse(TEXT("and the plugin's version"), Field(ObjectField(Info, TEXT("data")), TEXT("pluginVersion")).IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------------------------------- layout

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDreamMCPLayoutGoldenTest,
	FQuietTestBase,
	"DreamGUI.MCP.Layout.KnownScreensComeOutAtTheirKnownRects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLayoutGoldenTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor);
	FScopedSources Sources;
	const FString Golden = Sources.Write(TEXT("Golden"), Screen(GoldenScreen));
	const TSharedPtr<FJsonObject> Laid = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("layout"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(Golden)))));
	TestEqual(TEXT("the screen lays out"), Field(Laid, TEXT("outcome")), FString(TEXT("ok")));
	const TSharedPtr<FJsonObject> Tree = ObjectField(ObjectField(Laid, TEXT("data")), TEXT("tree"));
	// 1920x1080: a point-anchored 400x200 in the middle; a top-left anchor and pivot 20 px in and 30 down (Y is up in
	// DreamGUI, so -30 is down); a column stretched up the right edge, 300 wide, whose box pads its rows by 20 and spaces
	// them by 10, each row as tall as its texture (the default white, 4 px).
	ExpectRect(*this, Tree, TEXT("Root"), 0, 0, 1920, 1080);
	ExpectRect(*this, Tree, TEXT("Root/Panel"), 760, 440, 400, 200);
	ExpectRect(*this, Tree, TEXT("Root/TopLeft"), 20, 30, 100, 50);
	ExpectRect(*this, Tree, TEXT("Root/Column"), 1620, 0, 300, 1080);
	ExpectRect(*this, Tree, TEXT("Root/Column/RowA"), 1640, 20, 260, 4);
	ExpectRect(*this, Tree, TEXT("Root/Column/RowB"), 1640, 34, 260, 4);
	TestEqual(TEXT("the column says its box"), Field(FindTreeNode(Tree, TEXT("Root/Column")), TEXT("container")), FString(TEXT("VerticalBox")));
	TestEqual(TEXT("its rows say what places them"), Field(FindTreeNode(Tree, TEXT("Root/Column/RowA")), TEXT("placedBy")), FString(TEXT("VerticalBox")));
	TestTrue(TEXT("every node has its line"), Number(FindTreeNode(Tree, TEXT("Root/Panel")), TEXT("line")) > 0);
	const FString Summary = Field(Laid, TEXT("summary"));
	TestFalse(TEXT("the summary is one line"), Summary.Contains(TEXT("\n")));
	TestTrue(TEXT("and names the file and the viewport"), Summary.Contains(TEXT("at 1920x1080")));
	const FString Text = Field(Laid, TEXT("text"));
	TestTrue(TEXT("the text carries the tree, a node a line"), Text.Contains(TEXT("Image Panel  x=760 y=440 w=400 h=200")));
	TestTrue(TEXT("children indented under their parent"), Text.Contains(TEXT("\n    Image RowA  x=1640 y=20 w=260 h=4")));

	// Another viewport: the same file follows its anchors.
	const TSharedPtr<FJsonObject> Small = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("layout"),
		FString::Printf(TEXT("{\"file\":\"%s\",\"width\":1280,\"height\":720,\"root\":\"Column\",\"depth\":1}"), *JsonString(Golden)), TEXT("t-2"))));
	const TSharedPtr<FJsonObject> SmallTree = ObjectField(ObjectField(Small, TEXT("data")), TEXT("tree"));
	TestEqual(TEXT("root= starts the tree at that node"), Field(SmallTree, TEXT("path")), FString(TEXT("Root/Column")));
	ExpectRect(*this, SmallTree, TEXT("Root/Column"), 980, 0, 300, 720);
	ExpectRect(*this, SmallTree, TEXT("Root/Column/RowB"), 1000, 34, 260, 4);

	// Text: what it says and whether it fits.
	const FString Words = Sources.Write(TEXT("Words"), Screen(TEXT(
		"    Text Fits { AnchorData.SizeDelta = (600, 60)  Text = \"Short\"  FontSize = 24 }\n"
		"    Text Cramped { AnchorData.SizeDelta = (60, 30)  AnchorData.AnchoredPosition = (0, 200)  Text = \"A label far too long for this box\"  FontSize = 24 }")));
	const TSharedPtr<FJsonObject> WordsResult = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("layout"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(Words)), TEXT("t-3"))));
	const TSharedPtr<FJsonObject> WordsTree = ObjectField(ObjectField(WordsResult, TEXT("data")), TEXT("tree"));
	const TSharedPtr<FJsonObject> FitsText = ObjectField(FindTreeNode(WordsTree, TEXT("Root/Fits")), TEXT("text"));
	TestEqual(TEXT("a text node says what it says"), Field(FitsText, TEXT("text")), FString(TEXT("Short")));
	bool bOverflows = true;
	FitsText->TryGetBoolField(TEXT("overflows"), bOverflows);
	TestFalse(TEXT("a short text fits"), bOverflows);
	bool bCrampedOverflows = false;
	ObjectField(FindTreeNode(WordsTree, TEXT("Root/Cramped")), TEXT("text"))->TryGetBoolField(TEXT("overflows"), bCrampedOverflows);
	TestTrue(TEXT("a long text in a small box does not"), bCrampedOverflows);
	TestTrue(TEXT("and that is DUI8002"), Codes(WordsResult).Contains(TEXT("DUI8002")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPLintOffScreenTest,
	"DreamGUI.MCP.Layout.OffTheScreenIsReportedOncePerBranch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLintOffScreenTest::RunTest(const FString& Parameters)
{
	FSnapshotBuilder Inside;
	Inside.Add(0, TEXT("Panel"), TEXT("Image"), 100, 100, 800, 400);
	Inside.Add(0, TEXT("Edge"), TEXT("Image"), 900, 0, 100, 600);
	TestEqual(TEXT("a node inside the viewport, edge to edge, is not off the screen"), CountCode(Inside.Lint(), EDreamUIDiagnosticCode::LayoutOffScreen), 0);

	FSnapshotBuilder Outside;
	const int32 Panel = Outside.Add(0, TEXT("Panel"), TEXT("Image"), 900, 100, 300, 200);
	Outside.Add(Panel, TEXT("Child"), TEXT("Text"), 950, 120, 200, 40);
	Outside.Add(0, TEXT("Gone"), TEXT("Image"), -400, 100, 200, 200);
	const TArray<FLayoutIssue> Issues = Outside.Lint();
	TestEqual(TEXT("one finding for the panel past the right edge and one for the node wholly left of the screen, none for the child"),
		CountCode(Issues, EDreamUIDiagnosticCode::LayoutOffScreen), 2);
	TestTrue(TEXT("the message says by how much"), Issues.ContainsByPredicate([](const FLayoutIssue& I) { return I.Message.Contains(TEXT("200 px past the right edge")); }));

	FSnapshotBuilder Clipped;
	const int32 Scroll = Clipped.Add(0, TEXT("Scroll"), TEXT("Widget"), 100, 100, 400, 300);
	const int32 Item = Clipped.Add(Scroll, TEXT("Item"), TEXT("Image"), 100, 500, 400, 300);
	Clipped.Snapshot.Nodes[Item].bClipped = true;
	Clipped.Snapshot.Nodes[Item].VisW = 0.0;
	Clipped.Snapshot.Nodes[Item].VisH = 0.0;
	TestEqual(TEXT("what a clipping parent cuts away is not off the screen"), CountCode(Clipped.Lint(), EDreamUIDiagnosticCode::LayoutOffScreen), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPLintTextTest,
	"DreamGUI.MCP.Layout.TextThatDoesNotFitIsReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLintTextTest::RunTest(const FString& Parameters)
{
	FSnapshotBuilder Fits;
	const int32 Ok = Fits.Add(0, TEXT("Label"), TEXT("Text"), 100, 100, 300, 40);
	Fits.Snapshot.Nodes[Ok].bHasText = true;
	Fits.Snapshot.Nodes[Ok].Text = TEXT("Start");
	Fits.Snapshot.Nodes[Ok].TextNeeded = FVector2f(80, 30);
	Fits.Snapshot.Nodes[Ok].TextBox = FVector2f(300, 40);
	TestEqual(TEXT("a text that fits is not reported"), CountCode(Fits.Lint(), EDreamUIDiagnosticCode::LayoutTextOverflow), 0);

	FSnapshotBuilder Cut;
	const int32 Long = Cut.Add(0, TEXT("Label"), TEXT("Text"), 100, 100, 60, 30);
	Cut.Snapshot.Nodes[Long].bHasText = true;
	Cut.Snapshot.Nodes[Long].Text = TEXT("A label far too long");
	Cut.Snapshot.Nodes[Long].bTextOverflows = true;
	Cut.Snapshot.Nodes[Long].TextNeeded = FVector2f(60, 190);
	Cut.Snapshot.Nodes[Long].TextBox = FVector2f(60, 30);
	const TArray<FLayoutIssue> Issues = Cut.Lint();
	TestEqual(TEXT("a text past its box is reported"), CountCode(Issues, EDreamUIDiagnosticCode::LayoutTextOverflow), 1);
	TestTrue(TEXT("with what it needs and what it has"), Issues.Num() > 0 && Issues[0].Message.Contains(TEXT("needs 60x190 px and has 60x30")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPLintSizeTest,
	"DreamGUI.MCP.Layout.ZeroSizeAndSmallControlsAreReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLintSizeTest::RunTest(const FString& Parameters)
{
	FSnapshotBuilder Fine;
	const int32 Button = Fine.Add(0, TEXT("Play"), TEXT("Native.Button"), 100, 100, 200, 48);
	Fine.Snapshot.Nodes[Button].bInteractive = true;
	Fine.Add(0, TEXT("Marker"), TEXT("Widget"), 500, 300, 0, 0);
	TestEqual(TEXT("a 200x48 button and an empty zero-sized marker are not reported"), CountCode(Fine.Lint(), EDreamUIDiagnosticCode::LayoutTooSmall), 0);

	FSnapshotBuilder Small;
	const int32 Tiny = Small.Add(0, TEXT("Tiny"), TEXT("Native.Button"), 100, 100, 16, 16);
	Small.Snapshot.Nodes[Tiny].bInteractive = true;
	Small.Add(0, TEXT("Flat"), TEXT("Image"), 300, 100, 200, 0);
	const TArray<FLayoutIssue> Issues = Small.Lint();
	TestEqual(TEXT("a 16 px button and an image of no height are reported"), CountCode(Issues, EDreamUIDiagnosticCode::LayoutTooSmall), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPLintOverlapTest,
	"DreamGUI.MCP.Layout.ControlsOnTopOfEachOtherAreReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLintOverlapTest::RunTest(const FString& Parameters)
{
	FSnapshotBuilder Apart;
	const int32 A = Apart.Add(0, TEXT("A"), TEXT("Native.Button"), 100, 100, 200, 48);
	const int32 B = Apart.Add(0, TEXT("B"), TEXT("Native.Button"), 100, 158, 200, 48);
	Apart.Snapshot.Nodes[A].bInteractive = true;
	Apart.Snapshot.Nodes[B].bInteractive = true;
	const int32 Image = Apart.Add(0, TEXT("Under"), TEXT("Image"), 100, 100, 200, 48);
	(void)Image;
	TestEqual(TEXT("buttons 10 px apart, and an image under one, are not reported"), CountCode(Apart.Lint(), EDreamUIDiagnosticCode::LayoutOverlappingControls), 0);

	FSnapshotBuilder Stacked;
	const int32 C = Stacked.Add(0, TEXT("C"), TEXT("Native.Button"), 100, 100, 200, 48);
	const int32 D = Stacked.Add(0, TEXT("D"), TEXT("Native.Button"), 220, 100, 200, 48);
	Stacked.Snapshot.Nodes[C].bInteractive = true;
	Stacked.Snapshot.Nodes[D].bInteractive = true;
	const TArray<FLayoutIssue> Issues = Stacked.Lint();
	TestEqual(TEXT("buttons overlapping by 80 px are reported"), CountCode(Issues, EDreamUIDiagnosticCode::LayoutOverlappingControls), 1);
	TestTrue(TEXT("naming the one on top"), Issues.Num() > 0 && Issues.Last().Message.StartsWith(TEXT("Native.Button D lies over Native.Button C")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPLintAlignTest,
	"DreamGUI.MCP.Layout.EdgesThatNearlyLineUpAreReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLintAlignTest::RunTest(const FString& Parameters)
{
	FSnapshotBuilder Aligned;
	Aligned.Add(0, TEXT("A"), TEXT("Image"), 100, 100, 300, 80);
	Aligned.Add(0, TEXT("B"), TEXT("Image"), 100, 200, 300, 80);
	Aligned.Add(0, TEXT("C"), TEXT("Image"), 100, 300, 300, 80);
	Aligned.Add(0, TEXT("Far"), TEXT("Image"), 600, 100, 100, 100);
	TestEqual(TEXT("edges that line up exactly, or are far apart, are not reported"), CountCode(Aligned.Lint(), EDreamUIDiagnosticCode::LayoutNearlyAligned), 0);

	FSnapshotBuilder Missed;
	Missed.Add(0, TEXT("A"), TEXT("Image"), 100, 100, 300, 80);
	Missed.Add(0, TEXT("B"), TEXT("Image"), 102, 200, 300, 80);
	const TArray<FLayoutIssue> Issues = Missed.Lint();
	TestTrue(TEXT("left edges 2 px apart are reported"), CountCode(Issues, EDreamUIDiagnosticCode::LayoutNearlyAligned) >= 1);
	TestTrue(TEXT("as an info"), Issues.Num() > 0 && Issues[0].Severity == TEXT("info"));

	FSnapshotBuilder Gaps;
	Gaps.Add(0, TEXT("A"), TEXT("Image"), 100, 100, 300, 50);
	Gaps.Add(0, TEXT("B"), TEXT("Image"), 100, 166, 300, 50);
	Gaps.Add(0, TEXT("C"), TEXT("Image"), 100, 234, 300, 50);
	const TArray<FLayoutIssue> GapIssues = Gaps.Lint();
	TestTrue(TEXT("a column with gaps of 16 and 18 is reported"), GapIssues.ContainsByPredicate([](const FLayoutIssue& I) { return I.Message.Contains(TEXT("gaps of 16, 18")); }));
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDreamMCPLintSizesTest,
	FQuietTestBase,
	"DreamGUI.MCP.Layout.WhatBreaksAtOnlySomeSizesIsReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLintSizesTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor);
	FScopedSources Sources;
	// A panel 700 px right of the middle: inside at 1920 wide, off the right edge at 1280.
	const FString Fixed = Sources.Write(TEXT("Fixed"), Screen(TEXT("    Image Side { AnchorData.SizeDelta = (300, 200)  AnchorData.AnchoredPosition = (700, 0) }")));
	const TSharedPtr<FJsonObject> Breaks = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("layout"),
		FString::Printf(TEXT("{\"file\":\"%s\",\"sizes\":\"1280x720\"}"), *JsonString(Fixed)))));
	TestEqual(TEXT("a panel that leaves the screen at 1280 only is DUI8006, not DUI8001"), FString::Join(Codes(Breaks), TEXT(",")), FString(TEXT("DUI8006")));
	const TArray<TSharedPtr<FJsonValue>> Diagnostics = ArrayField(Breaks, TEXT("diagnostics"));
	TestTrue(TEXT("naming the size"), Diagnostics.Num() > 0 && Field(Diagnostics[0]->AsObject(), TEXT("message")).StartsWith(TEXT("at 1280x720 only")));

	// Anchored to the right edge instead: inside at both sizes.
	const FString Anchored = Sources.Write(TEXT("Anchored"), Screen(TEXT(
		"    Image Side { AnchorData.AnchorMin = (1, 0.5)  AnchorData.AnchorMax = (1, 0.5)  AnchorData.Pivot = (1, 0.5)  AnchorData.SizeDelta = (300, 200)  AnchorData.AnchoredPosition = (-20, 0) }")));
	const TSharedPtr<FJsonObject> Holds = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("layout"),
		FString::Printf(TEXT("{\"file\":\"%s\",\"sizes\":\"1280x720, 2560x1440\"}"), *JsonString(Anchored)), TEXT("t-2"))));
	TestEqual(TEXT("a panel anchored to its edge holds at every size"), Codes(Holds).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPLintContrastTest,
	"DreamGUI.MCP.Layout.TextThatDoesNotStandOutIsReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPLintContrastTest::RunTest(const FString& Parameters)
{
	// The picture is made by hand: the rule reads pixels around a text's box, wherever they came from.
	const auto Picture = [](FColor Background)
	{
		TArray<FColor> Pixels;
		Pixels.Init(Background, 1000 * 600);
		return Pixels;
	};
	FSnapshotBuilder Readable;
	const int32 White = Readable.Add(0, TEXT("Title"), TEXT("Text"), 100, 100, 400, 40);
	Readable.Snapshot.Nodes[White].bHasText = true;
	Readable.Snapshot.Nodes[White].Text = TEXT("Readable");
	Readable.Snapshot.Nodes[White].Color = FColor(230, 233, 240, 255);
	Readable.Snapshot.Nodes[White].FontSize = 16;
	TArray<FLayoutIssue> Fine;
	LintContrast(Readable.Snapshot, Picture(FColor(28, 30, 38, 255)), FIntPoint(1000, 600), Fine);
	TestEqual(TEXT("light text on a dark backdrop is not reported"), Fine.Num(), 0);

	FSnapshotBuilder Faint;
	const int32 Grey = Faint.Add(0, TEXT("Hint"), TEXT("Text"), 100, 100, 400, 40);
	Faint.Snapshot.Nodes[Grey].bHasText = true;
	Faint.Snapshot.Nodes[Grey].Text = TEXT("Barely visible");
	Faint.Snapshot.Nodes[Grey].Color = FColor(42, 46, 54, 255);
	Faint.Snapshot.Nodes[Grey].FontSize = 16;
	TArray<FLayoutIssue> Issues;
	LintContrast(Faint.Snapshot, Picture(FColor(32, 36, 44, 255)), FIntPoint(1000, 600), Issues);
	TestEqual(TEXT("grey text on a grey backdrop is reported"), CountCode(Issues, EDreamUIDiagnosticCode::LayoutLowContrast), 1);
	TestTrue(TEXT("as a warning, with the ratio"), Issues.Num() > 0 && Issues[0].Severity == TEXT("warning") && Issues[0].Message.Contains(TEXT("to 1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMCPPreviewStatsTest,
	"DreamGUI.MCP.Preview.ItsFiguresAndColourNamesReadTheirPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMCPPreviewStatsTest::RunTest(const FString& Parameters)
{
	const auto Name = [](uint8 R, uint8 G, uint8 B) { return NameColor(FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f)); };
	TestEqual(TEXT("pure red"), Name(255, 0, 0), FString(TEXT("red")));
	TestEqual(TEXT("near black"), Name(10, 10, 12), FString(TEXT("black")));
	TestEqual(TEXT("the default backdrop is a dark bluish grey"), Name(28, 30, 38), FString(TEXT("dark bluish grey")));
	TestEqual(TEXT("mid grey"), Name(128, 128, 128), FString(TEXT("grey")));
	TestEqual(TEXT("a dusty pink is a tinted grey"), Name(180, 134, 119), FString(TEXT("pinkish grey")));
	TestEqual(TEXT("dark orange is brown"), Name(107, 74, 42), FString(TEXT("brown")));

	// Half the picture covered by an opaque blue panel: the same over black and white there, the backdrop elsewhere.
	const int32 N = 100;
	TArray<FColor> Picture, OverBlack, OverWhite;
	for (int32 Index = 0; Index < N; ++Index)
	{
		const bool bPanel = Index < N / 2;
		Picture.Add(bPanel ? FColor(30, 60, 200, 255) : FColor(28, 30, 38, 255));
		OverBlack.Add(bPanel ? FColor(30, 60, 200, 255) : FColor::Black);
		OverWhite.Add(bPanel ? FColor(30, 60, 200, 255) : FColor::White);
	}
	FPreviewStats Stats;
	Stats.Add(Picture, OverBlack, OverWhite);
	TestEqual(TEXT("coverage is the panel's share"), Stats.GetCoverage(), 0.5, 1e-9);
	TestTrue(TEXT("one flat colour"), Stats.IsFlat());
	TestEqual(TEXT("its colour"), FormatHexColor(Stats.GetMeanColor()), FString(TEXT("#1E3CC8")));
	TArray<FString> Next;
	TestTrue(TEXT("and the sentence says so"), DescribePreview(Stats, Next).Contains(TEXT("#1E3CC8")));

	FPreviewStats Empty;
	TArray<FColor> Black;
	Black.Init(FColor::Black, N);
	TArray<FColor> Whites;
	Whites.Init(FColor::White, N);
	Empty.Add(Black, Black, Whites);
	TArray<FString> EmptyNext;
	TestTrue(TEXT("a screen that draws nothing is said to"), DescribePreview(Empty, EmptyNext).StartsWith(TEXT("The screen draws almost nothing")));
	TestFalse(TEXT("with what to do"), EmptyNext.IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------------------------------- render

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDreamMCPRenderPreviewTest,
	FQuietTestBase,
	"DreamGUI.MCP.Render.PreviewDrawsTheScreenWhereLayoutSaysItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamMCPRenderPreviewTest::RunTest(const FString& Parameters)
{
	FTestEndpoint Test(*this, EEndpointMode::Editor, /*bCanRender*/ true);
	FScopedSources Sources;
	const FString Golden = Sources.Write(TEXT("GoldenPicture"), Screen(GoldenScreen));
	const TSharedPtr<FJsonObject> Drawn = ResultOf(RunToEnd(*this, Test.Endpoint, CallBody(TEXT("preview"), FString::Printf(TEXT("{\"file\":\"%s\"}"), *JsonString(Golden)))));
	if (!TestEqual(TEXT("the screen is drawn"), Field(Drawn, TEXT("outcome")), FString(TEXT("ok"))))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>> Artifacts = ArrayField(Drawn, TEXT("artifacts"));
	if (!TestEqual(TEXT("one image"), Artifacts.Num(), 1))
	{
		return false;
	}
	const FString Relative = Field(Artifacts[0]->AsObject(), TEXT("path"));
	TestTrue(TEXT("under Saved/DreamMCP/artifacts"), Relative.StartsWith(TEXT("Saved/DreamMCP/artifacts/")));
	TestTrue(TEXT("and named in the summary"), Field(Drawn, TEXT("summary")).Contains(Relative));
	const FString File = FPaths::Combine(FPaths::ProjectDir(), Relative);
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*File, false, true, true); };

	TArray<uint8> Png;
	FFileHelper::LoadFileToArray(Png, *File);
	IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Reader = Wrappers.CreateImageWrapper(EImageFormat::PNG);
	TArray<uint8> Raw;
	if (!TestTrue(TEXT("the image reads as a PNG"), Reader.IsValid() && Reader->SetCompressed(Png.GetData(), Png.Num()) && Reader->GetRaw(ERGBFormat::BGRA, 8, Raw)))
	{
		return false;
	}
	TestEqual(TEXT("1920 wide"), static_cast<int32>(Reader->GetWidth()), 1920);
	TestEqual(TEXT("1080 high"), static_cast<int32>(Reader->GetHeight()), 1080);
	const auto At = [&Raw, &Reader](int32 X, int32 Y)
	{
		const int32 Offset = (Y * static_cast<int32>(Reader->GetWidth()) + X) * 4;
		return FColor(Raw[Offset + 2], Raw[Offset + 1], Raw[Offset + 0], Raw[Offset + 3]);
	};
	// The colours are the file's exactly (no second encoding), and the edges are where layout puts them.
	TestEqual(TEXT("the panel's middle is its red"), At(960, 540), FColor(255, 0, 0, 255));
	TestEqual(TEXT("its top left pixel is in"), At(760, 440), FColor(255, 0, 0, 255));
	TestEqual(TEXT("the pixel before it is the backdrop"), At(759, 440), FColor(28, 30, 38, 255));
	TestEqual(TEXT("its bottom right pixel is in"), At(1159, 639), FColor(255, 0, 0, 255));
	TestEqual(TEXT("the one after it is out"), At(1160, 640), FColor(28, 30, 38, 255));
	TestEqual(TEXT("the top-left block starts at 20, 30"), At(20, 30), FColor(0, 255, 0, 255));
	TestEqual(TEXT("and ends before 120, 80"), At(120, 80), FColor(28, 30, 38, 255));
	TestEqual(TEXT("the first row, 4 px from y=20"), At(1700, 23), FColor(0, 0, 255, 255));
	TestEqual(TEXT("not below it"), At(1700, 24), FColor(28, 30, 38, 255));

	const TSharedPtr<FJsonObject> Stats = ObjectField(ObjectField(Drawn, TEXT("data")), TEXT("stats"));
	// 400x200 + 100x50 + two 260x4 rows of a 1920x1080 picture.
	TestEqual(TEXT("coverage is what the screen draws"), Number(Stats, TEXT("coverage")), (400.0 * 200 + 100 * 50 + 2 * 260 * 4) / (1920.0 * 1080), 0.001);
	return true;
}

#endif
