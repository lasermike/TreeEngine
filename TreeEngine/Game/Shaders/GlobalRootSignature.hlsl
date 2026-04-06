//--------------------------------------------------------------------------------------
// GlobalRootSignature.hlsl
//
// The Global Root Signature for our Raytracing Pipeline State Object
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#define GlobalRootSignature  "RootFlags(0), SRV(t0), RootConstants(b0, num32bitconstants=4), DescriptorTable(UAV(u0)), CBV(b1), CBV(b2), SRV(t1), SRV(t2), SRV(t3), SRV(t4), SRV(t5), SRV(t6), SRV(t7), SRV(t8), DescriptorTable(SRV(t9, numDescriptors=4)), StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR)"


