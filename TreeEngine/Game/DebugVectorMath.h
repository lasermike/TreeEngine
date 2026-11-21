#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace DebugVectorMath
{
    using namespace DirectX;

    inline bool Finite(const float* values, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            if (!std::isfinite(values[i]))
                return false;
        }
        return true;
    }

    inline bool Normalize(XMFLOAT4& value)
    {
        if (!Finite(&value.x, 4) || XMVectorGetX(XMVector4LengthSq(XMLoadFloat4(&value))) < 1e-12f)
            return false;
        XMStoreFloat4(&value, XMQuaternionNormalize(XMLoadFloat4(&value)));
        return true;
    }

    inline XMFLOAT4 FromDegrees(const XMFLOAT3& angles)
    {
        XMFLOAT4 result;
        XMStoreFloat4(&result,
                      XMQuaternionRotationRollPitchYaw(XMConvertToRadians(angles.x), XMConvertToRadians(angles.y),
                                                       XMConvertToRadians(angles.z)));
        return result;
    }

    inline XMFLOAT3 ToDegrees(XMFLOAT4 quaternion)
    {
        if (!Normalize(quaternion))
            return XMFLOAT3(0, 0, 0);
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, XMMatrixRotationQuaternion(XMLoadFloat4(&quaternion)));
        const float pitch = std::asin(std::max(-1.0f, std::min(1.0f, -m._32)));
        const bool pole = std::abs(std::cos(pitch)) < 1e-3f;
        return XMFLOAT3(XMConvertToDegrees(pitch),
                        XMConvertToDegrees(pole ? std::atan2(-m._13, m._11) : std::atan2(m._31, m._33)),
                        XMConvertToDegrees(pole ? 0.0f : std::atan2(m._12, m._22)));
    }

    inline bool ScaleLinked(float* values, int count, int axis, float next)
    {
        if (!std::isfinite(next) || std::abs(values[axis]) < 1e-6f)
            return false;
        const float factor = next / values[axis];
        float scaled[4];
        for (int i = 0; i < count; ++i)
            scaled[i] = values[i] * factor;
        if (!Finite(scaled, count))
            return false;
        std::copy(scaled, scaled + count, values);
        return true;
    }

    inline std::string Format(const char* label, const float* values, int count)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << label << ": [" << std::setprecision(9);
        for (int i = 0; i < count; ++i)
        {
            if (i)
                text << ", ";
            text << values[i];
        }
        text << ']';
        return text.str();
    }

    // Reflect Z between the engine's LH +Z-forward and the gizmo's RH -Z-forward coordinates.
    inline XMFLOAT3 GizmoPosition(XMFLOAT3 value)
    {
        return XMFLOAT3(value.x, value.y, -value.z);
    }
    inline XMFLOAT4 GizmoRotation(XMFLOAT4 value)
    {
        return XMFLOAT4(-value.x, -value.y, value.z, value.w);
    }
} // namespace DebugVectorMath
