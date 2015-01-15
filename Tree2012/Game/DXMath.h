#pragma once


__inline XMMATRIX MatrixTransformation
(
    FXMVECTOR ScalingOrigin, 
    FXMVECTOR ScalingOrientationQuaternion, 
    FXMVECTOR Scaling, 
    CXMVECTOR RotationOrigin, 
    CXMVECTOR RotationQuaternion, 
    CXMVECTOR Translation
)
{
    XMMATRIX M;
    XMVECTOR NegScalingOrigin;
    XMVECTOR VScalingOrigin;
    XMMATRIX MScalingOriginI;
    XMMATRIX MScalingOrientation;
    XMMATRIX MScalingOrientationT;
    XMMATRIX MScaling;
    XMVECTOR VRotationOrigin;
    XMMATRIX MRotation;
    XMVECTOR VTranslation;

    VScalingOrigin       = _mm_and_ps(ScalingOrigin,g_XMMask3);
    NegScalingOrigin     = XMVectorNegate(ScalingOrigin);

    MScalingOriginI      = XMMatrixTranslationFromVector(NegScalingOrigin);
    MScalingOrientation  = XMMatrixRotationQuaternion(ScalingOrientationQuaternion);
    MScalingOrientationT = XMMatrixTranspose(MScalingOrientation);
    MScaling             = XMMatrixScalingFromVector(Scaling);
    VRotationOrigin      = _mm_and_ps(RotationOrigin,g_XMMask3);
    MRotation            = XMMatrixRotationQuaternion(RotationQuaternion);
    VTranslation         = _mm_and_ps(Translation,g_XMMask3);

    M      = XMMatrixMultiply(MScalingOriginI, MScalingOrientationT);
    M      = XMMatrixMultiply(M, MScaling);
    M      = XMMatrixMultiply(M, MScalingOrientation);
    M.r[3] = XMVectorAdd(M.r[3], VScalingOrigin);
    M.r[3] = XMVectorSubtract(M.r[3], VRotationOrigin);
    M      = XMMatrixMultiply(M, MRotation);
    M.r[3] = XMVectorAdd(M.r[3], VRotationOrigin);
    M.r[3] = XMVectorAdd(M.r[3], VTranslation);

    return M;
}
