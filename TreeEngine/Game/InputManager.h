#pragma once



struct FrameInputData
{
    UINT frame;
    bool key[512];

    FrameInputData()
    {
        frame = 0;
        memset(key, 0, sizeof(bool) * _countof(key));
    }
};


interface IInputManager
{
	virtual FrameInputData& GetFrameInput(UINT frame) = 0;
};

class InputManager : public IInputManager
{
	FrameInputData g_inputData[1]; // TODO: expand to 2 or more for double buffered input?

public:
	FrameInputData& GetFrameInput(UINT /*frame*/)
	{
		return g_inputData[0]; // TODO: multi frame input
	}
};

