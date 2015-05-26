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
	ZeroMemory(m_key, ARRAYSIZE(m_key) * sizeof(bool));
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

		m_pGame->Tick(m_key);
		m_pGame->Render();
    }
}

void ApplicationView::HandleInput()
{
	m_input.Update();
	const XSF::GamepadReading& input = m_input.GetCurrentGamepadReading();

	m_key[VK_RIGHT] = input.IsDPadRightPressed();
	m_key[VK_LEFT] = input.IsDPadLeftPressed();
	m_key['0'] = input.IsYPressed();
	m_key['R'] = input.IsLeftShoulderPressed();
	m_key['Z'] = input.IsRightShoulderPressed();
	m_key['H'] = input.IsDPadUpPressed();
	m_key['P'] = input.IsAPressed();
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
