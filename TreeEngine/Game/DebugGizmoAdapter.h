#pragma once
#include "DebugVectorMath.h"
#include "imgui.h"
#include <array>
#include <cfloat>

namespace ImViewGuizmo
{
    // Column-major storage as expected by the vendor API; DirectX interprets its transpose as a row matrix.
    struct Matrix
    {
        float data[4][4];
        constexpr Matrix(float d = 1) : data{{d, 0, 0, 0}, {0, d, 0, 0}, {0, 0, d, 0}, {0, 0, 0, d}}
        {
        }
        constexpr Matrix(float a, float b, float c, float d, float e, float f, float g, float h, float i, float j,
                         float k, float l, float m, float n, float o, float p)
            : data{{a, b, c, d}, {e, f, g, h}, {i, j, k, l}, {m, n, o, p}}
        {
        }
        float* operator[](int i)
        {
            return data[i];
        }
        const float* operator[](int i) const
        {
            return data[i];
        }
    };
    namespace GizmoMath
    {
        using namespace DirectX;
        inline XMFLOAT3 V3(FXMVECTOR v)
        {
            XMFLOAT3 r;
            XMStoreFloat3(&r, v);
            return r;
        }
        inline XMFLOAT4 V4(FXMVECTOR v)
        {
            XMFLOAT4 r;
            XMStoreFloat4(&r, v);
            return r;
        }
        inline XMMATRIX Load(const Matrix& m)
        {
            return XMMATRIX(&m.data[0][0]);
        }
        inline Matrix Store(FXMMATRIX m)
        {
            XMFLOAT4X4 f;
            XMStoreFloat4x4(&f, m);
            Matrix result;
            std::copy(&f.m[0][0], &f.m[0][0] + 16, &result.data[0][0]);
            return result;
        }
        inline float get_vec_comp(const XMFLOAT3& v, int i)
        {
            return (&v.x)[i];
        }
        inline float get_vec_comp(const XMFLOAT4& v, int i)
        {
            return (&v.x)[i];
        }
        inline XMFLOAT3 make_vec3(float x, float y, float z)
        {
            return XMFLOAT3(x, y, z);
        }
        inline XMFLOAT4 make_vec4(float x, float y, float z, float w)
        {
            return XMFLOAT4(x, y, z, w);
        }
        inline XMFLOAT4 angleAxis(float a, const XMFLOAT3& axis)
        {
            return V4(XMQuaternionRotationAxis(XMLoadFloat3(&axis), a));
        }
        inline XMFLOAT4 quatLookAt(const XMFLOAT3& direction, const XMFLOAT3& up)
        {
            XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&direction));
            XMVECTOR u = XMLoadFloat3(&up);
            if (std::abs(XMVectorGetX(XMVector3Dot(d, u))) > 0.999f)
            {
                u = std::abs(XMVectorGetY(d)) < 0.9f ? g_XMIdentityR1 : g_XMIdentityR0;
            }
            return V4(XMQuaternionRotationMatrix(XMMatrixTranspose(XMMatrixLookToRH(g_XMZero, d, u))));
        }
        inline Matrix mat4_identity()
        {
            return Matrix();
        }
        inline XMFLOAT3 cross(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            return V3(XMVector3Cross(XMLoadFloat3(&a), XMLoadFloat3(&b)));
        }
        inline float dot(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            return XMVectorGetX(XMVector3Dot(XMLoadFloat3(&a), XMLoadFloat3(&b)));
        }
        inline float dot(const XMFLOAT4& a, const XMFLOAT4& b)
        {
            return XMVectorGetX(XMVector4Dot(XMLoadFloat4(&a), XMLoadFloat4(&b)));
        }
        inline float length2(const XMFLOAT3& v)
        {
            return dot(v, v);
        }
        inline float length(const XMFLOAT3& v)
        {
            return std::sqrt(length2(v));
        }
        inline XMFLOAT3 normalize(const XMFLOAT3& v)
        {
            return V3(XMVector3Normalize(XMLoadFloat3(&v)));
        }
        inline XMFLOAT3 mix(const XMFLOAT3& a, const XMFLOAT3& b, float t)
        {
            return V3(XMVectorLerp(XMLoadFloat3(&a), XMLoadFloat3(&b), t));
        }
        inline XMFLOAT3 add_vv(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            return V3(XMLoadFloat3(&a) + XMLoadFloat3(&b));
        }
        inline XMFLOAT3 subtract_vv(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            return V3(XMLoadFloat3(&a) - XMLoadFloat3(&b));
        }
        inline XMFLOAT3 multiply_vf(const XMFLOAT3& v, float f)
        {
            return V3(XMLoadFloat3(&v) * f);
        }
        inline Matrix mat4_cast(const XMFLOAT4& q)
        {
            return Store(XMMatrixRotationQuaternion(XMLoadFloat4(&q)));
        }
        inline Matrix transpose(const Matrix& m)
        {
            return Store(XMMatrixTranspose(Load(m)));
        }
        inline XMFLOAT3 get_matrix_col(const Matrix& m, int i)
        {
            return XMFLOAT3(m[i][0], m[i][1], m[i][2]);
        }
        inline void set_matrix_col(Matrix& m, int i, const XMFLOAT4& v)
        {
            std::copy(&v.x, &v.x + 4, m[i]);
        }
        inline XMFLOAT4 multiply_qq(const XMFLOAT4& a, const XMFLOAT4& b)
        {
            return V4(XMQuaternionMultiply(XMLoadFloat4(&b), XMLoadFloat4(&a)));
        }
        inline XMFLOAT3 multiply_qv(const XMFLOAT4& q, const XMFLOAT3& v)
        {
            return V3(XMVector3Rotate(XMLoadFloat3(&v), XMLoadFloat4(&q)));
        }
        inline Matrix multiply_mm(const Matrix& a, const Matrix& b)
        {
            return Store(Load(b) * Load(a));
        }
        inline XMFLOAT4 multiply_mv4(const Matrix& m, const XMFLOAT4& v)
        {
            return V4(XMVector4Transform(XMLoadFloat4(&v), Load(m)));
        }
    } // namespace GizmoMath
} // namespace ImViewGuizmo
#define IMVIEWGUIZMO_VEC3 DirectX::XMFLOAT3
#define IMVIEWGUIZMO_VEC4 DirectX::XMFLOAT4
#define IMVIEWGUIZMO_QUAT DirectX::XMFLOAT4
#define IMVIEWGUIZMO_MAT4 ImViewGuizmo::Matrix
#define IMVIEWGUIZMO_IMPLEMENTATION
#include "../ThirdParty/ImViewGuizmo/ImViewGuizmo.h"
#undef IMVIEWGUIZMO_IMPLEMENTATION
#undef IMVIEWGUIZMO_VEC3
#undef IMVIEWGUIZMO_VEC4
#undef IMVIEWGUIZMO_QUAT
#undef IMVIEWGUIZMO_MAT4
