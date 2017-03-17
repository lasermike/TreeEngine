// RenderPlatform12.cpp : Defines the exported functions for the DLL application.
//

#include "pch.h"
#include "RenderPlatform12.h"


// This is an example of an exported variable
RENDERPLATFORM12_API int nRenderPlatform12=0;

// This is an example of an exported function.
RENDERPLATFORM12_API int fnRenderPlatform12(void)
{
    return 42;
}

// This is the constructor of a class that has been exported.
// see RenderPlatform12.h for the class definition
CRenderPlatform12::CRenderPlatform12()
{
    return;
}
