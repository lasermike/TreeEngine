using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using Microsoft.DirectX;
using Microsoft.DirectX.Direct3D;

namespace D3DImageSample
{
    public partial class Window1 : Window
    {
        [StructLayout(LayoutKind.Sequential)]
        public class SIZE
        {
            public int Width = 0;
            public int Height = 0;
        }

        [DllImport("TreeWin32Dll.dll")]
        private static extern IntPtr InitializeScene();

        [DllImport("TreeWin32Dll.dll")]
        private static extern void RenderScene([In, Out] SIZE size);

        [DllImport("TreeWin32Dll.dll")]
        private static extern void ReleaseScene();

        private D3DImage _di;
        private IntPtr _scene;

        public Window1()
        {
            // create a D3DImage to host the scene and
            // monitor it for changes in front buffer availability
            _di = new D3DImage();
            _di.IsFrontBufferAvailableChanged 
                += new DependencyPropertyChangedEventHandler(OnIsFrontBufferAvailableChanged);

            // make a brush of the scene available as a resource on the window
            Resources["RotatingTriangleScene"] = new ImageBrush(_di);

            // begin rendering the custom D3D scene into the D3DImage
            BeginRenderingScene();

            // parse the XAML
            InitializeComponent();
        }

        private void OnIsFrontBufferAvailableChanged(object sender, DependencyPropertyChangedEventArgs e)
        {
            // if the front buffer is available, then WPF has just created a new
            // D3D device, so we need to start rendering our custom scene
            if (_di.IsFrontBufferAvailable)
            {
                BeginRenderingScene();
            }
            else
            {
                // If the front buffer is no longer available, then WPF has lost its
                // D3D device so there is no reason to waste cycles rendering our
                // custom scene until a new device is created.
                StopRenderingScene();
            }
        }

        private void BeginRenderingScene()
        {
            if (_di.IsFrontBufferAvailable)
            {
                // create a custom D3D scene and get a pointer to its surface
                _scene = InitializeScene();

                // set the back buffer using the new scene pointer
                _di.Lock();
                _di.SetBackBuffer(D3DResourceType.IDirect3DSurface9, _scene);
                _di.Unlock();

                // leverage the Rendering event of WPF's composition target to
                // update the custom D3D scene
                CompositionTarget.Rendering += OnRendering;
            }
        }

        private void StopRenderingScene()
        {
            // This method is called when WPF loses its D3D device.
            // In such a circumstance, it is very likely that we have lost 
            // our custom D3D device also, so we should just release the scene.
            // We will create a new scene when a D3D device becomes 
            // available again.
            CompositionTarget.Rendering -= OnRendering;
            ReleaseScene();
            _scene = IntPtr.Zero;
        }

        private void OnRendering(object sender, EventArgs e)
        {
            // when WPF's composition target is about to render, we update our 
            // custom render target so that it can be blended with the WPF target
            UpdateScene();
        }

        private void UpdateScene()
        {
            if (_di.IsFrontBufferAvailable && _scene != IntPtr.Zero)
            {
                // lock the D3DImage
                _di.Lock();

                // update the scene (via a call into our custom library)
                SIZE size = new SIZE();
                RenderScene(size); 

                // invalidate the updated region of the D3DImage (in this case, the whole image)
                _di.AddDirtyRect(new Int32Rect(0, 0, size.Width, size.Height));

                // unlock the D3DImage
                _di.Unlock();
            }
        }
    }
}
