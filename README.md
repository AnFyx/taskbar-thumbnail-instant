# Taskbar Thumbnail Instant Preview

A [Windhawk](https://windhawk.net/) mod that removes the hover delay before taskbar thumbnail previews appear on the Windows 11 XAML taskbar.

Hovering a taskbar icon normally waits before showing the preview. This mod makes that delay a setting, so previews can feel instant.

## Install

1. Install [Windhawk](https://windhawk.net/).
2. Create a new mod and paste the contents of [`mods/taskbar-thumbnail-instant.wh.cpp`](mods/taskbar-thumbnail-instant.wh.cpp), then compile and enable it.
3. Adjust **Hover delay (ms)** in the mod settings. The default is 80 ms.

The mod targets `explorer.exe` on x86-64 and needs no restart: Windhawk applies the hook to the running process.

## How it works

The Windows 11 taskbar is a XAML application living in `Taskbar.View.dll`, loaded by `explorer.exe`. The method that schedules a thumbnail preview is:

```
winrt::Taskbar::implementation::HoverFlyoutModel::TransitionToFlyoutVisiblePendingState(
    winrt::hstring, std::chrono::duration<__int64, std::ratio<1, 10000000>>)
```

Its second argument is the delay before the preview becomes visible, as a `TimeSpan` in 100 ns ticks. The mod hooks the method through Windhawk's symbol resolution (`HookSymbols`) and calls the original with the configured delay instead.

Two details make it work reliably:

- **`Taskbar.View.dll` may not be loaded yet** when the mod initialises, so the mod also hooks `LoadLibraryExW` and installs the symbol hook as soon as the DLL appears. An atomic flag makes sure the hook is installed exactly once, whichever path gets there first.
- **The hstring argument is passed through untouched** and never dereferenced, so the mod does not depend on its internal layout.

The delay setting is read from the hooked method on the UI thread and written from Windhawk's settings callback on another thread, so it is stored in an `std::atomic<int>`.

## Limitations

- **Version-bound.** The hook is resolved by symbol name. On a build where that symbol is absent or its signature changed, the hook fails, the mod refuses to load, and the taskbar is left as it was. Tested on Windows 11 24H2, build 26100.
- **The delay is overridden for every call** to that method, so any other transition going through it uses the same value.
- **A delay of 0 causes flicker** when the pointer merely crosses the taskbar on its way elsewhere. 50 to 100 ms feels instant without it.
- **This modifies the behaviour of a system process in memory.** Nothing is written to disk and no system file is patched: disabling the mod in Windhawk restores the original behaviour. Windhawk itself injects into the target process, which some security software flags.

## License

MIT, see [LICENSE](LICENSE).
