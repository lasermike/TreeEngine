//
// ApplicationView.cpp -
//

#include "pch.h"
#include "ApplicationView.h"
#include "InputXboxOne.h"

using namespace Windows::Foundation;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Core;

ApplicationView::ApplicationView()
{
    m_windowClosed = false;
	for (int i = 0; i < ARRAYSIZE(pressedButtons); i++)
	{
		pressedButtons[i] = false;
	}
}

// Called by the system.  Perform application initialization here,
// hooking application wide events, etc.
void ApplicationView::Initialize(CoreApplicationView^ applicationView)
{
    applicationView->Activated += ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &ApplicationView::OnActivated);
    CoreApplication::Suspending += ref new EventHandler<SuspendingEventArgs^>(this, &ApplicationView::OnSuspending);
    CoreApplication::Resuming += ref new EventHandler<Platform::Object^>(this, &ApplicationView::OnResuming);

    m_input.Initialize();

}

// Called when we are provided a window.
void ApplicationView::SetWindow(CoreWindow^ window)
{
    window->Closed += ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &ApplicationView::OnWindowClosed);

    //m_game = ref new Game();
	m_pGame = new Game();
	m_pGame->Initialize(window);
}

// The purpose of this method is to get the application entry point.
void ApplicationView::Load(Platform::String^ entryPoint)
{
}

// Called by the system after initialization is complete.  This
// implements the traditional game loop
void ApplicationView::Run()
{
    CoreDispatcher^ dispatcher = CoreWindow::GetForCurrentThread()->Dispatcher;

    while (!m_windowClosed)
    {
        dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

		HandleInput();

		m_pGame->Render();
    }
}

void ApplicationView::HandleInput()
{
	m_input.Update();
	const XSF::GamepadReading& input = m_input.GetCurrentGamepadReading();

	if (input.IsDPadRightPressed() && !pressedButtons[0])
	{
		m_pGame->OnKeydown(VK_RIGHT);
		pressedButtons[0] = true;
	}
	else
		pressedButtons[0] = false;

	if (input.IsDPadLeftPressed() && !pressedButtons[1])
	{
		m_pGame->OnKeydown(VK_LEFT);
		pressedButtons[1] = true;
	}
	else
		pressedButtons[1] = false;
	
	if (input.IsYPressed() && !pressedButtons[2])
	{
		m_pGame->OnKeydown('0');
		pressedButtons[2] = true;
	}
	else
		pressedButtons[2] = false;
	
	if (input.IsLeftShoulderPressed() && !pressedButtons[3])
	{
		m_pGame->OnKeydown('R');
		pressedButtons[3] = true;
	}
	else
		pressedButtons[3] = false;
	
	if (input.IsRightShoulderPressed() && !pressedButtons[4])
	{
		m_pGame->OnKeydown('Z');
		pressedButtons[4] = true;
	}
	else
		pressedButtons[4] = false;

	if (input.IsDPadUpPressed() && !pressedButtons[5])
	{
		m_pGame->OnKeydown('H');
		pressedButtons[5] = true;
	}
	else
		pressedButtons[5] = false;

	if (input.IsAPressed() && !pressedButtons[6])
	{
		m_pGame->OnKeydown('P');
		pressedButtons[6] = true;
	}
	else
		pressedButtons[6] = false;
}

void ApplicationView::Uninitialize()
{
}

// Called when the application is activated.
void ApplicationView::OnActivated(CoreApplicationView^ applicationView, IActivatedEventArgs^ args)
{
    CoreWindow::GetForCurrentThread()->Activate();
}

// Called when the application is suspending.
void ApplicationView::OnSuspending(Platform::Object^ sender, SuspendingEventArgs^ args)
{
    //m_game->Suspend();
}

// Called when the application is resuming from suspended.
void ApplicationView::OnResuming(Platform::Object^ sender, Platform::Object^ args)
{
    //m_game->Resume();
}

void ApplicationView::OnWindowClosed(CoreWindow^ sender, CoreWindowEventArgs^ args)
{
    m_windowClosed = true;
}

// Implements a IFrameworkView factory.
IFrameworkView^ ApplicationViewSource::CreateView()
{
    return ref new ApplicationView();
}

// Application entry point
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^)
{
    auto applicationViewSource = ref new ApplicationViewSource();

    CoreApplication::Run(applicationViewSource);

    return 0;
}
