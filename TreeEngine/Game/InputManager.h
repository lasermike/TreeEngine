#pragma once

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

