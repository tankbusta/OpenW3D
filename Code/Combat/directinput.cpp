/*
**	Command & Conquer Renegade(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Combat/directinput.cpp                       $*
 *                                                                                             *
 *                      $Author:: Patrick                                                     $*
 *                                                                                             *
 *                     $Modtime:: 1/15/02 5:32p                                               $*
 *                                                                                             *
 *                    $Revision:: 25                                                         $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "directinput.h"
#include "win.h"
#include "debug.h"
#include "timemgr.h"

#include <GameInput.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

ComPtr<IGameInput>				GIObject = nullptr;

// Previous state for detecting transitions
GameInputKeyState				PreviousKeyState[256] = {};
GameInputMouseState				PreviousMouseState = {};

char						DirectInput::DIKeyboardButtons[NUM_KEYBOARD_BUTTONS];
char						DirectInput::DIMouseButtons[NUM_MOUSE_BUTTONS];
long						DirectInput::DIMouseAxis[NUM_MOUSE_AXIS];
char						DirectInput::DIJoystickButtons[NUM_CONTROLLER_BUTTONS];
float						DirectInput::ButtonLastHitTime[NUM_KEYBOARD_BUTTONS];
long						DirectInput::DIJoystickAxis[NUM_JOYSTICK_AXIS];
Vector3						DirectInput::CursorPos(0, 0, 0);
bool						DirectInput::EatMouseHeld = false;
bool						DirectInput::Captured = false;
int							DirectInput::LastKeyPressed = 0;


struct ButtonMapping {
	int index;
	GameInputGamepadButtons mask;
};

ButtonMapping buttons[] = {
	{ DirectInput::BUTTON_CONTROLLER_A - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadA },
	{ DirectInput::BUTTON_CONTROLLER_B - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadB },
	{ DirectInput::BUTTON_CONTROLLER_X - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadX },
	{ DirectInput::BUTTON_CONTROLLER_Y - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadY },
	{ DirectInput::BUTTON_CONTROLLER_MENU - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadMenu },

	{ DirectInput::BUTTON_CONTROLLER_DPAD_UP - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadDPadUp },
	{ DirectInput::BUTTON_CONTROLLER_DPAD_DOWN - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadDPadDown },
	{ DirectInput::BUTTON_CONTROLLER_DPAD_LEFT - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadDPadLeft },
	{ DirectInput::BUTTON_CONTROLLER_DPAD_RIGHT - DirectInput::BUTTON_CONTROLLER_FIRST, GameInputGamepadDPadRight },
};

#define		LEFT_TRIGGER_OFFSET			DirectInput::BUTTON_CONTROLLER_RIGHT_TRIGGER - DirectInput::BUTTON_CONTROLLER_FIRST
#define		RIGHT_TRIGGER_OFFSET		DirectInput::BUTTON_CONTROLLER_RIGHT_TRIGGER - DirectInput::BUTTON_CONTROLLER_FIRST
#define		LSTICK_UP_OFFSET			DirectInput::BUTTON_CONTROLLER_LSTICK_UP - DirectInput::BUTTON_CONTROLLER_FIRST
#define		LSTICK_DOWN_OFFSET			DirectInput::BUTTON_CONTROLLER_LSTICK_DOWN - DirectInput::BUTTON_CONTROLLER_FIRST
#define		LSTICK_LEFT_OFFSET			DirectInput::BUTTON_CONTROLLER_LSTICK_LEFT - DirectInput::BUTTON_CONTROLLER_FIRST
#define		LSTICK_RIGHT_OFFSET			DirectInput::BUTTON_CONTROLLER_LSTICK_RIGHT - DirectInput::BUTTON_CONTROLLER_FIRST

#define		BUTTON_BIT_DOUBLE			8
#define		BUTTON_DOUBLE_THRESHHOLD	0.25f
#define		TRIGGER_THRESHOLD			0.5f
#define		STICK_THRESHOLD				0.3f
#define		AXIS_SCALE					1000.0f

#define UPDATE_BUTTON_STATE(table, idx, isPressed) \
	do { \
		bool wasPressed = (table[idx] & DI_BUTTON_HELD) != 0; \
		if (isPressed) { \
			if (!wasPressed) { \
				table[idx] |= DI_BUTTON_HIT; \
				table[idx] |= DI_BUTTON_HELD; \
			} else { \
				table[idx] |= DI_BUTTON_HELD; \
			} \
		} else { \
			if (wasPressed) { \
				table[idx] |= DI_BUTTON_RELEASED; \
				table[idx] &= ~DI_BUTTON_HELD; \
			} \
		} \
	} while(0)

/*
**
*/
void DirectInput::Init(void)
{
	HRESULT hr;

	WWDEBUG_SAY(("GameInput: Init\n"));

	// Create GameInput instance
	hr = GameInputCreate(&GIObject);
	if (FAILED(hr))
	{
		Debug_Say(("Failed to create GameInput instance: 0x%x\n", hr));
		return;
	}

	GIObject->SetFocusPolicy(GameInputDisableBackgroundInput);

	Debug_Say(("GameInput object created\n"));



	Captured = true;
	Flush();

	// Reset the double-click array entries
	for (int index = 0; index < NUM_KEYBOARD_BUTTONS; index++) {
		ButtonLastHitTime[index] = 1000;
	}

	return;
}

/*
**
*/
void DirectInput::Shutdown(void)
{
	WWDEBUG_SAY(("GameInput: Shutdown\n"));

	GIObject.Reset();
}

/*
**
*/
void DirectInput::Flush(void)
{
	memset(DIKeyboardButtons, 0, sizeof(DIKeyboardButtons));
	memset(DIMouseButtons, 0, sizeof(DIMouseButtons));
	memset(DIMouseAxis, 0, sizeof(DIMouseAxis));
	memset(DIJoystickButtons, 0, sizeof(DIJoystickButtons));
	memset(PreviousKeyState, 0, sizeof(PreviousKeyState));
	memset(&PreviousMouseState, 0, sizeof(PreviousMouseState));
	memset(&DIJoystickAxis, 0, sizeof(DIJoystickAxis));
}

/*
** Acquire access to input devices
*/
void DirectInput::Acquire(void)
{
	if (!Captured) {
		Flush();

		POINT cursorPos;
		GetCursorPos(&cursorPos);
		ScreenToClient(MainWindow, &cursorPos);

		CursorPos.X = (float)cursorPos.x;
		CursorPos.Y = (float)cursorPos.y;

		ShowCursor(FALSE);

		Captured = true;
	}
}

/*
** Release access to input devices.
*/
void DirectInput::Unacquire(void)
{
	if (Captured) {
		POINT cursorPos;
		
		ShowCursor(TRUE);

		cursorPos.x = (LONG)CursorPos.X;
		cursorPos.y = (LONG)CursorPos.Y;
		ClientToScreen(MainWindow, &cursorPos);
		SetCursorPos(cursorPos.x, cursorPos.y);

		Captured = false;
	}
}

/*
**
*/
void DirectInput::ReadKeyboard(void)
{
	if (!GIObject) return;

	// Clear previous frame flags
	for (int i = 0; i < sizeof(DIKeyboardButtons); i++) {
		DIKeyboardButtons[i] &= DI_BUTTON_HELD;
	}

	// Get current keyboard reading
	ComPtr<IGameInputReading> reading;
	HRESULT hr = GIObject->GetCurrentReading(GameInputKindKeyboard, nullptr, &reading);

	if (SUCCEEDED(hr) && reading)
	{
		uint32_t keyCount = reading->GetKeyCount();

		if (keyCount > 0)
		{
			GameInputKeyState* keyStates = new GameInputKeyState[keyCount];
			reading->GetKeyState(keyCount, keyStates);

			// First, check which keys were released (were in previous state but not in current)
			for (int vkCode = 0; vkCode < NUM_KEYBOARD_BUTTONS; vkCode++) {
				if (PreviousKeyState[vkCode].virtualKey != 0) {
					bool stillPressed = false;
					for (uint32_t i = 0; i < keyCount; i++) {
						if (keyStates[i].virtualKey == PreviousKeyState[vkCode].virtualKey) {
							stillPressed = true;
							break;
						}
					}

					if (!stillPressed) {
						DIKeyboardButtons[vkCode] |= DI_BUTTON_RELEASED;
						DIKeyboardButtons[vkCode] &= ~DI_BUTTON_HELD;
						PreviousKeyState[vkCode].virtualKey = 0;
					}
				}
			}

			// Process each currently pressed key
			for (uint32_t i = 0; i < keyCount; i++)
			{
				int vk = keyStates[i].virtualKey;
				if (vk > 0 && vk < NUM_KEYBOARD_BUTTONS)
				{
					bool wasPressed = (PreviousKeyState[vk].virtualKey != 0);

					if (!wasPressed) {
						DIKeyboardButtons[vk] |= DI_BUTTON_HIT;
						DIKeyboardButtons[vk] |= DI_BUTTON_HELD;
						LastKeyPressed = vk;
					}
					else {
						DIKeyboardButtons[vk] |= DI_BUTTON_HELD;
					}

					PreviousKeyState[vk] = keyStates[i];
				}
			}

			delete[] keyStates;
		}
		else
		{
			// No keys pressed, mark all as released if they were held
			for (int vkCode = 0; vkCode < NUM_KEYBOARD_BUTTONS; vkCode++) {
				if (PreviousKeyState[vkCode].virtualKey != 0) {
					DIKeyboardButtons[vkCode] |= DI_BUTTON_RELEASED;
					DIKeyboardButtons[vkCode] &= ~DI_BUTTON_HELD;
					PreviousKeyState[vkCode].virtualKey = 0;
				}
			}
		}
	}

	// Set duplicate keys
	DIKeyboardButtons[VK_CONTROL] = DIKeyboardButtons[VK_CONTROL] | DIKeyboardButtons[VK_CONTROL];
	DIKeyboardButtons[VK_SHIFT] = DIKeyboardButtons[VK_LSHIFT] | DIKeyboardButtons[VK_RSHIFT];
	DIKeyboardButtons[VK_MENU] = DIKeyboardButtons[VK_LMENU] | DIKeyboardButtons[VK_RMENU];
	DIKeyboardButtons[VK_LWIN] = DIKeyboardButtons[VK_LWIN] | DIKeyboardButtons[VK_RWIN];
}

/*
**
*/
void DirectInput::ReadMouse(void)
{
	if (!GIObject) return;

	// Clear previous frame data
	for (int i = 0; i < sizeof(DIMouseButtons); i++)
	{
		DIMouseButtons[i] &= DI_BUTTON_HELD;
	}

	for (int i = 0; i < (sizeof(DIMouseAxis) / sizeof(DIMouseAxis[0])); i++)
	{
		DIMouseAxis[i] = 0;
	}

	// Get current mouse reading
	ComPtr<IGameInputReading> reading;
	HRESULT hr = GIObject->GetCurrentReading(GameInputKindMouse, nullptr, &reading);
	if (SUCCEEDED(hr) && reading)
	{
		GameInputMouseState mouseState;
		if (reading->GetMouseState(&mouseState))
		{
			// Calculate deltas from previous position
			int deltaX = (int)(mouseState.positionX - PreviousMouseState.positionX);
			int deltaY = (int)(mouseState.positionY - PreviousMouseState.positionY);
			int wheelDelta = (int)(mouseState.wheelY - PreviousMouseState.wheelY);

			DIMouseAxis[0] = deltaX;
			DIMouseAxis[1] = deltaY;
			DIMouseAxis[2] = wheelDelta;
			CursorPos.X += (float)deltaX * 2;
			CursorPos.Y += (float)deltaY * 2;

			// Process mouse buttons (left, right, middle)
			for (int i = 0; i < 3; i++) {
				// GameInputMouseButtons uses bits 0, 1, 2 for left, right, middle
				GameInputMouseButtons buttonBit = (GameInputMouseButtons)(1 << i);

				bool wasPressed = (PreviousMouseState.buttons & buttonBit) != 0;
				bool isPressed = (mouseState.buttons & buttonBit) != 0;

				if (!wasPressed && isPressed)
				{
					DIMouseButtons[i] |= DI_BUTTON_HIT;
					DIMouseButtons[i] |= DI_BUTTON_HELD;
				}
				else if (wasPressed && !isPressed)
				{
					DIMouseButtons[i] |= DI_BUTTON_RELEASED;
					DIMouseButtons[i] &= ~DI_BUTTON_HELD;
					if (i == 0) EatMouseHeld = false;
				}
				else if (isPressed)
				{
					// Keep HELD state, don't set HIT again
					DIMouseButtons[i] |= DI_BUTTON_HELD;
				}
			}

			// Store state for next frame
			PreviousMouseState = mouseState;
		}
	}

	// "Eat" the left mouse button as necessary
	if (EatMouseHeld) {
		DIMouseButtons[BUTTON_MOUSE_LEFT & 0xFF] &= ~DI_BUTTON_HELD;
		DIMouseButtons[BUTTON_MOUSE_LEFT & 0xFF] &= ~DI_BUTTON_HIT;
		DIMouseButtons[BUTTON_MOUSE_LEFT & 0xFF] |= DI_BUTTON_RELEASED;
	}

}


/*
**
*/
void DirectInput::ReadJoystick(void)
{
	if (!GIObject) return;

	// Clear previous frame flags
	for (int i = 0; i < NUM_CONTROLLER_BUTTONS; i++) {
		DIJoystickButtons[i] &= DI_BUTTON_HELD;
	}

	IGameInputReading* reading = nullptr;
	HRESULT hr = GIObject->GetCurrentReading(GameInputKindGamepad, nullptr, &reading);
	if (FAILED(hr) || !reading) {
		// No gamepad reading, release all
		for (int i = 0; i < NUM_CONTROLLER_BUTTONS; i++)
		{
			if (DIJoystickButtons[i] & DI_BUTTON_HELD) {
				DIJoystickButtons[i] |= DI_BUTTON_RELEASED;
				DIJoystickButtons[i] &= ~DI_BUTTON_HELD;
			}
		}

		return;
	}

	GameInputGamepadState state;
	if (!reading->GetGamepadState(&state)) {
		// Failed to get state, release all
		for (int i = 0; i < NUM_CONTROLLER_BUTTONS; i++)
		{
			if (DIJoystickButtons[i] & DI_BUTTON_HELD)
			{
				DIJoystickButtons[i] |= DI_BUTTON_RELEASED;
				DIJoystickButtons[i] &= ~DI_BUTTON_HELD;
			}
		}

		return;
	}

	for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
	{
		UPDATE_BUTTON_STATE(DIJoystickButtons, buttons[i].index, (state.buttons & buttons[i].mask) != 0);
	}

	// Triggers
	UPDATE_BUTTON_STATE(DIJoystickButtons, LEFT_TRIGGER_OFFSET, state.leftTrigger > TRIGGER_THRESHOLD);
	UPDATE_BUTTON_STATE(DIJoystickButtons, RIGHT_TRIGGER_OFFSET, state.rightTrigger > TRIGGER_THRESHOLD);

	// Analog Sticks
	float leftX = state.leftThumbstickX;
	float leftY = state.leftThumbstickY;

	DIJoystickAxis[JOYSTICK_X_AXIS] = (long)(leftX * AXIS_SCALE);
	DIJoystickAxis[JOYSTICK_Y_AXIS] = (long)(leftY * AXIS_SCALE);

	// Treat these as buttons for movement
	UPDATE_BUTTON_STATE(DIJoystickButtons, LSTICK_UP_OFFSET, state.leftThumbstickY > STICK_THRESHOLD);
	UPDATE_BUTTON_STATE(DIJoystickButtons, LSTICK_DOWN_OFFSET, state.leftThumbstickY < -STICK_THRESHOLD);
	UPDATE_BUTTON_STATE(DIJoystickButtons, LSTICK_LEFT_OFFSET, state.leftThumbstickX < -STICK_THRESHOLD);
	UPDATE_BUTTON_STATE(DIJoystickButtons, LSTICK_RIGHT_OFFSET, state.leftThumbstickX > STICK_THRESHOLD);

	// Right (Currently Camera)

	float rightX = state.rightThumbstickX;
	float rightY = -state.rightThumbstickY;  // Invert Y axis for camera

	DIJoystickAxis[JOYSTICK_RX_AXIS] = (long)(rightX * AXIS_SCALE);
	DIJoystickAxis[JOYSTICK_RY_AXIS] = (long)(rightY * AXIS_SCALE);
}

/*
** Read all device input
*/
void DirectInput::Read(void)
{
	if (Captured) {
		ReadKeyboard();
		ReadMouse();
		ReadJoystick();

		Update_Double_Clicks();
	}

	return;
}

/*
**
*/
void DirectInput::Eat_Mouse_Held_States (void)
{
	if (	(DIMouseButtons[BUTTON_MOUSE_LEFT & 0xFF] & DI_BUTTON_HELD) ||
			(DIMouseButtons[BUTTON_MOUSE_LEFT & 0xFF] & DI_BUTTON_HIT))
	{
		EatMouseHeld = true;
	}

	return ;
}

/*
**
*/
long	DirectInput::Get_Joystick_Axis_State( JoystickAxis axis )
{
	if (axis >= 0 && axis < NUM_JOYSTICK_AXIS)
		return DIJoystickAxis[axis];

	return 0;
}


/*
**
*/
void	DirectInput::Update_Double_Clicks (void)
{
	float time_delta = TimeManager::Get_Frame_Real_Seconds();
	for ( int index = 0; index < NUM_KEYBOARD_BUTTONS; index++ ) {

		//
		// Bump time since last
		//
		ButtonLastHitTime[index] += time_delta;

		//
		// If the button is hit, check for double and reset time
		//
		if ( DIKeyboardButtons[index] & DI_BUTTON_HIT ) {
			if ( ButtonLastHitTime[index] <= BUTTON_DOUBLE_THRESHHOLD ) {
				DIKeyboardButtons[index] |= BUTTON_BIT_DOUBLE;
			}
			ButtonLastHitTime[index] = 0;
		}
	}

	return ;
}