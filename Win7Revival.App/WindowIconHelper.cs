using System;
using System.Runtime.InteropServices;

namespace Win7Revival.App
{
    internal static class WindowIconHelper
    {
        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr LoadImage(IntPtr hInst, string lpszName, uint uType,
            int cxDesired, int cyDesired, uint fuLoad);

        private const uint WM_SETICON = 0x0080;
        private const uint IMAGE_ICON = 1;
        private const uint LR_LOADFROMFILE = 0x0010;

        public static void SetWindowIcon(IntPtr hwnd, string icoPath)
        {
            var smallIcon = LoadImage(IntPtr.Zero, icoPath, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
            var bigIcon = LoadImage(IntPtr.Zero, icoPath, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);

            if (smallIcon != IntPtr.Zero)
                SendMessage(hwnd, WM_SETICON, (IntPtr)0, smallIcon);
            if (bigIcon != IntPtr.Zero)
                SendMessage(hwnd, WM_SETICON, (IntPtr)1, bigIcon);
        }
    }
}
