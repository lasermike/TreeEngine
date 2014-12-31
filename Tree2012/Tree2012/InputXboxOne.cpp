//--------------------------------------------------------------------------------------
// Input.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "InputXboxOne.h"

using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation;
using namespace Windows::Xbox::Input;

//--------------------------------------------------------------------------------------
// Name: GamepadReading()
// Desc: Constructor
//--------------------------------------------------------------------------------------

XSF::GamepadReading::GamepadReading()
{
    m_gamepadReading            = nullptr;
    m_lastGamepadButtons        = GamepadButtons::None;
    m_pressedButtons            = 0;
    m_releasedButtons           = 0;
    m_lastLeftTrigger           = 0.0f;
    m_lastRightTrigger          = 0.0f;
    m_leftThumbstickDeadzone    = 0.0f;
    m_rightThumbstickDeadzone   = 0.0f;
}


//--------------------------------------------------------------------------------------
// Name: Update
// Desc: Update the gamepad reading each frame
//--------------------------------------------------------------------------------------

void XSF::GamepadReading::Update( IGamepad^ gamepad, IGamepadReading^ gamepadReading )
{
    if ( gamepad == nullptr )
    {
        m_leftThumbstickDeadzone    = 0.0f;
        m_rightThumbstickDeadzone   = 0.0f;
    }
    else
    {
        m_leftThumbstickDeadzone    = 0.24f; // 24% is the recommended value for thumbstick deadzones for Xbox One Gamepads
        m_rightThumbstickDeadzone   = 0.24f;
    }

    if ( m_gamepadReading == nullptr )
    {
        m_lastGamepadButtons        = GamepadButtons::None;
        m_pressedButtons            = 0;
        m_releasedButtons           = 0;
        m_lastLeftTrigger           = 0.0f;
        m_lastRightTrigger          = 0.0f;
    }
    else
    {
        m_pressedButtons            = ( (UINT)LastButtons() ^ (UINT)Buttons() ) & (UINT)Buttons();
        m_releasedButtons           = ( (UINT)LastButtons() ^ (UINT)Buttons() ) & ~(UINT)Buttons();
        m_lastLeftTrigger           = m_gamepadReading->LeftTrigger;
        m_lastRightTrigger          = m_gamepadReading->RightTrigger;
        m_lastGamepadButtons        = m_gamepadReading->Buttons;
    }

    m_gamepadReading    = gamepadReading;    
}


//--------------------------------------------------------------------------------------
// Name: Initialize
// Desc: Initializes the gamepad input system
//--------------------------------------------------------------------------------------

HRESULT XSF::Input::Initialize()
{
    m_currentGamepad = GetMostRecentGamepad();
    m_currentGampadReading = nullptr;
    m_customGamepadReading = new XSF::GamepadReading();

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Name: IsGamepadValid
// Desc: Returns true is the gamepad is connected to the system
//--------------------------------------------------------------------------------------

bool XSF::Input::IsGamepadValid( Gamepad^ gamepad )
{
    if( gamepad == nullptr )
    {
        return false;
    }

    auto allGamepads = Gamepad::Gamepads;
    auto gamepadCount = allGamepads->Size;
    for( unsigned i = 0; i < gamepadCount; ++ i )
    {
        if( gamepad->Id == allGamepads->GetAt( i )->Id )
        {
            return true;
        }
    }

    return false;
}


//--------------------------------------------------------------------------------------
// Name: GetMostRecentGamepad
// Desc: Get the most recent gamepad
//--------------------------------------------------------------------------------------

IGamepad^ XSF::Input::GetMostRecentGamepad()
{
    IGamepad^ gamepad = nullptr;

    auto allGamepads = Gamepad::Gamepads;
    if( allGamepads->Size > 0 )
    {
        gamepad = allGamepads->GetAt( 0 );
    }

    return gamepad;
}


//--------------------------------------------------------------------------------------
// Name: Update
// Desc: Update the input each frame
//--------------------------------------------------------------------------------------

VOID XSF::Input::Update()
{
    if( m_currentGamepad != GetMostRecentGamepad() )
    {
        m_currentGamepad = GetMostRecentGamepad();
    }

    if( m_currentGamepad == nullptr )
    {
        m_currentGampadReading = nullptr;
    }
    else
    {
        m_currentGampadReading = m_currentGamepad->GetCurrentReading();
    }

    m_customGamepadReading->Update( m_currentGamepad, m_currentGampadReading );
}