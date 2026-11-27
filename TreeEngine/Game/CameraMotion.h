#pragma once
#include <DirectXMath.h>
#include <functional>

// The user's camera movement, apart from the player object so tests can drive it. Walk: on the ground with gravity,
// Space jumps, Shift crouches, Ctrl sprints. Fly: free, Space up, Ctrl down, Shift boosts. Double-tapping Space
// switches between them. W/A/S/D move level in both, in metres per second with easing; arrow keys look, Page Up/Down
// also fly up and down. Defaults are Minecraft-like (specs/usercontrols.md).
class CameraMotion
{
public:
    enum class Mode { Walk, Fly };
    // The ground height at (x, z); false where there is none (off a terrain's edge), where the walker keeps the last
    // height it stood on. A scene without terrain reports 0 everywhere.
    using GroundHeight = std::function<bool(float x, float z, float& height)>;

    static constexpr float EyeHeight = 1.6f;
    static constexpr float CrouchEyeHeight = 1.3f;
    static constexpr float JumpHeight = 1.25f;
    static constexpr float Gravity = 32.0f;
    static constexpr float WalkSpeed = 4.3f;
    static constexpr float SprintFactor = 3.0f;      // Much faster than Minecraft's 1.3: scenes are large.
    static constexpr float CrouchFactor = 0.3f;
    static constexpr float FlySpeed = 11.0f;
    static constexpr float FlyBoost = 3.0f;          // Shift's default; the scroll wheel sets it, MinBoost to MaxBoost.
    static constexpr float MinBoost = 1.0f;
    static constexpr float MaxBoost = 10.0f;
    static constexpr float MaxPitch = 1.5533f;       // 89 degrees: mouse look stops short of straight up or down.
    static constexpr float LookSpeed = 1.2f;         // Arrow keys, radians per second.
    static constexpr float DoubleTapSeconds = 0.3f;
    static constexpr float MaxStep = 0.1f;           // Longer steps (a load, a breakpoint) are cut to this.

    // Where the walk camera stands; without it, the ground is at height 0.
    void SetGround(GroundHeight ground) { m_ground = std::move(ground); }
    // A new scene, or a restart: the mode, at rest, with the eye at position.
    void Reset(Mode mode, const DirectX::XMFLOAT3& position);
    Mode GetMode() const { return m_mode; }
    void SetMode(Mode mode);
    bool OnGround() const { return m_onGround; }
    float GetFlyBoost() const { return m_flyBoost; }
    void SetFlyBoost(float boost);

    // Mouse look: turns the view by yaw (right) and pitch (down) radians, level (no roll), the pitch held within
    // MaxPitch, as in Minecraft.
    static void Look(float yaw, float pitch, DirectX::XMFLOAT4& rotation);
    const DirectX::XMFLOAT3& Velocity() const { return m_velocity; }

    // Advances the eye position and rotation by seconds of the keys held (indexed by virtual-key code). now is the
    // wall-clock time in seconds, for timing the double tap: the game's frame timer caps its steps at 0.1 s, so a slow
    // frame's steps understate real time. Without it (negative), the steps are the clock.
    void Step(const bool keys[512], float seconds, DirectX::XMFLOAT3& position, DirectX::XMFLOAT4& rotation,
        double now = -1);

private:
    Mode m_mode = Mode::Walk;
    DirectX::XMFLOAT3 m_velocity = DirectX::XMFLOAT3(0, 0, 0);
    float m_eyeHeight = EyeHeight;
    float m_lastGround = 0;
    bool m_onGround = false;
    float m_flyBoost = FlyBoost;
    bool m_spaceWasDown = false;
    double m_clock = 0;
    double m_lastSpacePress = -1;
    GroundHeight m_ground;
};
