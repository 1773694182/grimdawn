using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace GrimDawnTeleporter;

public sealed class HotkeyService : IDisposable
{
    private const int WmHotkey = 0x0312;
    private readonly Window _window;
    private readonly HwndSource _source;
    private readonly Dictionary<int, Action> _actions = [];

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    public HotkeyService(Window window)
    {
        _window = window;
        _source = HwndSource.FromHwnd(new WindowInteropHelper(window).Handle);
        _source.AddHook(WndProc);
    }

    public void Register(int id, uint virtualKey, Action action)
    {
        RegisterHotKey(new WindowInteropHelper(_window).Handle, id, 0, virtualKey);
        _actions[id] = action;
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WmHotkey && _actions.TryGetValue(wParam.ToInt32(), out var action))
        {
            action();
            handled = true;
        }

        return IntPtr.Zero;
    }

    public void Dispose()
    {
        foreach (var id in _actions.Keys)
        {
            UnregisterHotKey(new WindowInteropHelper(_window).Handle, id);
        }

        _source.RemoveHook(WndProc);
    }
}
