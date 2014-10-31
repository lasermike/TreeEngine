
#include <iostream>

#if defined(DEBUG) | defined(_DEBUG)
#ifndef HR
#define HR(x)                                              \
	{                                                          \
	HRESULT hr = (x);                                      \
if (FAILED(hr))                                         \
		{                                                      \
		std::cout << "ERROR: " << __FILE__ << ": " << (DWORD)__LINE__ << ", HR:" << hr << ", " << L#x << "\n"; \
		assert(SUCCEEDED(hr)); \
		}                                                      \
	}
#endif

#else
#ifndef HR
#define HR(x) (x)
#endif
#endif 
