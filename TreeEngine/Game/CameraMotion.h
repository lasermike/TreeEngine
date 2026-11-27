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
    static constexpr float SprintFactor = 1.3f;
    static constexpr float CrouchFactor = 0.3f;
    static constexpr float FlySpeed = 11.0f;
    static constexpr float FlyBoost = 3.0f;
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
    const DirectX::XMFLOAT3& Velocity() const { return m_velocity; }

    // Advances the eye position and rotation by seconds of the keys held (indexed by virtual-key code).
    void Step(const bool keys[512], float seconds, DirectX::XMFLOAT3& position, DirectX::XMFLOAT4& rotation);

private:
    Mode m_mode = Mode::Walk;
    DirectX::XMFLOAT3 m_velocity = DirectX::XMFLOAT3(0, 0, 0);
    float m_eyeHeight = EyeHeight;
    float m_lastGround = 0;
    bool m_onGround = false;
    bool m_spaceWasDown = false;
    double m_clock = 0;
    double m_lastSpacePress = -1;
    GroundHeight m_ground;
};
