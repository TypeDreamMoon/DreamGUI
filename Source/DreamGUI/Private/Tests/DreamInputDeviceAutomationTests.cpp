// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Event/DreamEventSystem.h"

/*
 * Which device the player has their hands on. Nothing tracked it before, so a key prompt had nothing
 * honest to draw itself from -- the closest thing available was the per-pointer Pointer/Navigation
 * split, which says how an event reached a widget, not what the player is holding.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputDeviceClassificationTest,
	"DreamGUI.Navigation.InputDevice.KeysClassifyByWhatProducedThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputDeviceClassificationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A letter is the keyboard"),
		UDreamEventSystem::GetInputDeviceForKey(EKeys::A), EDreamUIInputDevice::MouseAndKeyboard);
	// The mouse is filed with the keyboard on purpose: they are one device as far as the player's
	// hands and therefore as far as a key prompt is concerned.
	TestEqual(TEXT("A mouse button is filed with the keyboard"),
		UDreamEventSystem::GetInputDeviceForKey(EKeys::LeftMouseButton), EDreamUIInputDevice::MouseAndKeyboard);
	TestEqual(TEXT("A face button is the gamepad"),
		UDreamEventSystem::GetInputDeviceForKey(EKeys::Gamepad_FaceButton_Bottom), EDreamUIInputDevice::Gamepad);
	TestEqual(TEXT("A stick direction is the gamepad"),
		UDreamEventSystem::GetInputDeviceForKey(EKeys::Gamepad_LeftStick_Up), EDreamUIInputDevice::Gamepad);
	TestEqual(TEXT("A finger is touch"),
		UDreamEventSystem::GetInputDeviceForKey(EKeys::TouchKeys[0]), EDreamUIInputDevice::Touch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputDeviceChangeReportingTest,
	"DreamGUI.Navigation.InputDevice.OnlyAnActualChangeIsBroadcast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputDeviceChangeReportingTest::RunTest(const FString& Parameters)
{
	UDreamEventSystem* Events = NewObject<UDreamEventSystem>(GetTransientPackage());
	int32 BroadcastCount = 0;
	EDreamUIInputDevice LastSeen = EDreamUIInputDevice::Touch;
	Events->GetInputDeviceChangedEvent().AddLambda([&](EDreamUIInputDevice Device)
	{
		++BroadcastCount;
		LastSeen = Device;
	});

	TestEqual(TEXT("Starts on mouse and keyboard"), Events->GetCurrentInputDevice(), EDreamUIInputDevice::MouseAndKeyboard);
	// Every single key the actor sees is reported, so a report that changes nothing must stay silent:
	// a held direction or a resting axis would otherwise rebuild every prompt on screen each frame.
	TestFalse(TEXT("Reporting the device it is already on says nothing"),
		Events->ReportInputDevice(EDreamUIInputDevice::MouseAndKeyboard));
	TestEqual(TEXT("...and broadcasts nothing"), BroadcastCount, 0);

	TestTrue(TEXT("Switching to the gamepad is a change"), Events->ReportInputDevice(EDreamUIInputDevice::Gamepad));
	TestEqual(TEXT("...broadcast once"), BroadcastCount, 1);
	TestEqual(TEXT("...with the new device"), LastSeen, EDreamUIInputDevice::Gamepad);
	TestEqual(TEXT("...and remembered"), Events->GetCurrentInputDevice(), EDreamUIInputDevice::Gamepad);

	TestFalse(TEXT("A second gamepad key is not a change"), Events->ReportInputDevice(EDreamUIInputDevice::Gamepad));
	TestEqual(TEXT("...and stays silent"), BroadcastCount, 1);
	return true;
}

#endif
