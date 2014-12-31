#pragma once

namespace XboxSampleFramework
{
    using namespace Windows::Xbox::Input;
    using namespace Windows::Foundation;

    class GamepadReading
    {
    private:
        IGamepadReading^                        m_gamepadReading;
        Windows::Xbox::Input::GamepadButtons    m_lastGamepadButtons;
        UINT                                    m_pressedButtons;
        UINT                                    m_releasedButtons;
        float                                   m_lastLeftTrigger;
        float                                   m_lastRightTrigger;
        float                                   m_leftThumbstickDeadzone;
        float                                   m_rightThumbstickDeadzone;
       
        inline static FLOAT NormalizeThumbstickValue( const float thumbstickValue, const float deadZone )
        {
            if ( thumbstickValue > +deadZone )
            {
                return ( thumbstickValue - deadZone ) / ( 1.0f - deadZone );
            }

            if ( thumbstickValue < -deadZone )
            {
                return ( thumbstickValue + deadZone ) / ( 1.0f - deadZone );
            }

            return 0.0f;
        }

    public:
        // All the properties from the base IGamepadReading class
        float LeftThumbstickX() const           { return IsValid() ? m_gamepadReading->LeftThumbstickX : 0.0f; }
        float LeftThumbstickY() const           { return IsValid() ? m_gamepadReading->LeftThumbstickY : 0.0f; }
        float RightThumbstickX() const          { return IsValid() ? m_gamepadReading->RightThumbstickX : 0.0f; }
        float RightThumbstickY() const          { return IsValid() ? m_gamepadReading->RightThumbstickY : 0.0f; }
        bool IsAPressed() const                 { return IsValid() ? m_gamepadReading->IsAPressed : false; }
        bool IsViewPressed() const              { return IsValid() ? m_gamepadReading->IsViewPressed : false; }
        bool IsBPressed() const                 { return IsValid() ? m_gamepadReading->IsBPressed : false; }
        bool IsDPadDownPressed() const          { return IsValid() ? m_gamepadReading->IsDPadDownPressed : false; }
        bool IsDPadLeftPressed() const          { return IsValid() ? m_gamepadReading->IsDPadLeftPressed : false; }
        bool IsDPadRightPressed() const         { return IsValid() ? m_gamepadReading->IsDPadRightPressed : false; }
        bool IsDPadUpPressed() const            { return IsValid() ? m_gamepadReading->IsDPadUpPressed : false; }
        bool IsLeftShoulderPressed() const      { return IsValid() ? m_gamepadReading->IsLeftShoulderPressed : false; }
        bool IsLeftThumbstickPressed() const    { return IsValid() ? m_gamepadReading->IsLeftThumbstickPressed : false; }
        bool IsRightShoulderPressed() const     { return IsValid() ? m_gamepadReading->IsRightShoulderPressed : false; }
        bool IsRightThumbstickPressed() const   { return IsValid() ? m_gamepadReading->IsRightThumbstickPressed : false; }
        bool IsMenuPressed() const             { return IsValid() ? m_gamepadReading->IsMenuPressed : false; }
        bool IsXPressed() const                 { return IsValid() ? m_gamepadReading->IsXPressed : false; }
        bool IsYPressed() const                 { return IsValid() ? m_gamepadReading->IsYPressed : false; }
        float LeftTrigger() const               { return IsValid() ? m_gamepadReading->LeftTrigger : 0.0f; }
        float RightTrigger() const              { return IsValid() ? m_gamepadReading->RightTrigger : 0.0f; }

        // Extra properties to our class
        bool IsValid() const                        { return ( m_gamepadReading != nullptr ); }
        float NormalizedLeftThumbstickX() const     { return IsValid() ? NormalizeThumbstickValue( m_gamepadReading->LeftThumbstickX, m_leftThumbstickDeadzone ) : 0.0f; }
        float NormalizedLeftThumbstickY() const     { return IsValid() ? NormalizeThumbstickValue( m_gamepadReading->LeftThumbstickY, m_leftThumbstickDeadzone ) : 0.0f; }
        float NormalizedRightThumbstickX() const    { return IsValid() ? NormalizeThumbstickValue( m_gamepadReading->RightThumbstickX, m_rightThumbstickDeadzone ) : 0.0f; }
        float NormalizedRightThumbstickY() const    { return IsValid() ? NormalizeThumbstickValue( m_gamepadReading->RightThumbstickY, m_rightThumbstickDeadzone ) : 0.0f; }
        float LastLeftTrigger() const               { return m_lastLeftTrigger; }
        float LastRightTrigger() const              { return m_lastRightTrigger; }
        bool IsLeftTriggerPressed() const           { return IsValid() ? ( m_gamepadReading->LeftTrigger > 0.1f ) : false; }
        bool IsRightTriggerPressed() const          { return IsValid() ? ( m_gamepadReading->RightTrigger > 0.1f ) : false; }

        bool APreviouslyPressed() const             { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::A ) != 0; }
        bool ViewPreviouslyPressed() const          { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::View ) != 0; }
        bool BPreviouslyPressed() const             { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::B ) != 0; }
        bool DPadDownPreviouslyPressed() const      { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadDown ) != 0; }
        bool DPadLeftPreviouslyPressed() const      { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadLeft ) != 0; }
        bool DPadRightPreviouslyPressed() const     { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadRight ) != 0; }
        bool DPadUpPreviouslyPressed() const        { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadUp ) != 0; }
        bool LeftShoulderPreviouslyPressed() const  { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::LeftShoulder ) != 0; }
        bool LeftThumbstickPreviouslyPressed() const{ return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::LeftThumbstick ) != 0; }
        bool RightShoulderPreviouslyPressed() const { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::RightShoulder ) != 0; }
        bool RightThumbstickPreviouslyPressed() const{return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::RightThumbstick ) != 0; }
        bool MenuPreviouslyPressed() const         { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::Menu ) != 0; }
        bool XPreviouslyPressed() const             { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::X ) != 0; }
        bool YPreviouslyPressed() const             { return ( (UINT)m_lastGamepadButtons & (UINT)Windows::Xbox::Input::GamepadButtons::Y ) != 0; }
        
        // The difference between IsPressed and WasPressed is that IsPressed simply returns with every call the immediate state of the button,
        // but WasPressed indicates the state change of the button. IsPressed is usefull for incrementing/decrementing values, i.e. the increment will
        // keep going while you hold the button. WasPressed is useful for mode changes, i.e. it will only be valid on the call that there is a state change
        // even if you hold the button down.
        bool WasAPressed() const                    { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::A ) != 0; }
        bool WasViewPressed() const                 { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::View ) != 0; }
        bool WasBPressed() const                    { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::B ) != 0; }
        bool WasDPadDownPressed() const             { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadDown ) != 0; }
        bool WasDPadLeftPressed() const             { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadLeft ) != 0; }
        bool WasDPadRightPressed() const            { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadRight ) != 0; }
        bool WasDPadUpPressed() const               { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadUp ) != 0; }
        bool WasLeftShoulderPressed() const         { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::LeftShoulder ) != 0; }
        bool WasLeftThumbstickPressed() const       { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::LeftThumbstick ) != 0; }
        bool WasRightShoulderPressed() const        { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::RightShoulder ) != 0; }
        bool WasRightThumbstickPressed() const      { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::RightThumbstick ) != 0; }
        bool WasMenuPressed() const                { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::Menu) != 0; }
        bool WasXPressed() const                    { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::X ) != 0; }
        bool WasYPressed() const                    { return ( m_pressedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::Y ) != 0; }

        bool WasAReleased() const                   { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::A ) != 0; }
        bool WasViewReleased() const                { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::View ) != 0; }
        bool WasBReleased() const                   { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::B ) != 0; }
        bool WasDPadDownReleased() const            { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadDown ) != 0; }
        bool WasDPadLeftReleased() const            { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadLeft ) != 0; }
        bool WasDPadRightReleased() const           { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadRight ) != 0; }
        bool WasDPadUpReleased() const              { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::DPadUp ) != 0; }
        bool WasLeftShoulderReleased() const        { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::LeftShoulder ) != 0; }
        bool WasLeftThumbstickReleased() const      { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::LeftThumbstick ) != 0; }
        bool WasRightShoulderReleased() const       { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::RightShoulder ) != 0; }
        bool WasRightThumbstickReleased() const     { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::RightThumbstick ) != 0; }
        bool WasMenuReleased() const               { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::Menu) != 0; }
        bool WasXReleased() const                   { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::X ) != 0; }
        bool WasYReleased() const                   { return ( m_releasedButtons & (UINT)Windows::Xbox::Input::GamepadButtons::Y ) != 0; }

        UINT PressedButtons() const                 { return m_pressedButtons; }
        UINT ReleasedButtons() const                { return m_releasedButtons; }

        bool WasAnythingPressedOrReleased() const   { return m_lastGamepadButtons != Buttons(); }

    private:
        friend class Input;

        GamepadReading();

        void Update( IGamepad^ gamepad, IGamepadReading^ gamepadReading );

        // D specific
        Windows::Xbox::Input::GamepadButtons Buttons() const { return IsValid() ? m_gamepadReading->Buttons : Windows::Xbox::Input::GamepadButtons::None; }
        DateTime Timestamp() const { return IsValid() ? m_gamepadReading->Timestamp : DateTime(); }
        Windows::Xbox::Input::GamepadButtons LastButtons() const { return m_lastGamepadButtons; }
    };


    //--------------------------------------------------------------------------------------
    // Name: class Input
    // Desc: Class to manage input devices
    //--------------------------------------------------------------------------------------
    class Input
    {
    public:
        HRESULT    Initialize();
        VOID Update();
        const XSF::GamepadReading& GetCurrentGamepadReading() const { return *m_customGamepadReading; }
        IGamepad^ GetGamepad() const { return m_currentGamepad; }

    private:
        IGamepad^               m_currentGamepad;
        IGamepadReading^        m_currentGampadReading;
        XSF::GamepadReading*    m_customGamepadReading;

        static bool IsGamepadValid( Gamepad^ gamepad );
        static IGamepad^ GetMostRecentGamepad();
    };

} // namespace XboxSampleFramework
