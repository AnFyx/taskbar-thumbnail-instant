// ==WindhawkMod==
// @id              taskbar-thumbnail-instant
// @name            Taskbar Thumbnail Instant Preview
// @description     Removes the hover delay before taskbar thumbnail previews appear (Windows 11 XAML taskbar, build 26100 and later)
// @version         1.0
// @author          AnFyx
// @github          https://github.com/AnFyx
// @include         explorer.exe
// @architecture    x86-64
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Thumbnail Instant Preview

On the Windows 11 XAML taskbar, hovering an icon waits about 400 ms before the
thumbnail preview appears. This mod makes that delay configurable, so previews
can feel instant.

## How it works

The mod hooks
`HoverFlyoutModel::TransitionToFlyoutVisiblePendingState` in `Taskbar.View.dll`,
the method that schedules the preview, and rewrites its duration argument with
the configured value.

## Notes

- The delay is overridden for **every** call to that method, so any other
  transition that goes through it uses the same value.
- Setting the delay to 0 makes previews flicker when the pointer crosses the
  taskbar on its way somewhere else. 50 to 100 ms feels instant without that.
- The hook is resolved from the symbol name. On a build where that symbol is
  absent or has a different signature, the mod fails to load and the taskbar is
  left untouched.

Tested on Windows 11 24H2, build 26100.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- hoverDelay: 80
  $name: Hover delay (ms)
  $description: >-
    Delay before the preview appears when hovering a taskbar icon.
    0 is instant, but previews flicker when the pointer merely crosses the
    taskbar. 50-100 feels instant without the flicker.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

// Borne haute : au-delà, la valeur n'a plus de sens et le calcul en ticks
// resterait inutilement grand (1 minute).
constexpr int kMaxHoverDelayMs = 60000;

// Lu depuis le thread UI (le hook) et écrit depuis le thread qui traite le
// changement de réglages : l'accès doit être atomique.
std::atomic<int> g_hoverDelayMs = 80;
std::atomic<bool> g_taskbarViewDllLoaded = false;

// HoverFlyoutModel::TransitionToFlyoutVisiblePendingState(hstring, TimeSpan)
// Le 3e argument est le délai avant affichage, en ticks de 100 ns. On le
// remplace par le délai configuré. `hstr` est repassé tel quel, jamais
// déréférencé : sa représentation exacte n'a pas d'importance ici.
using TransitionToFlyoutVisiblePendingState_t = void(*)(void* pThis,
                                                        void* hstr,
                                                        int64_t durationTicks);
TransitionToFlyoutVisiblePendingState_t
    TransitionToFlyoutVisiblePendingState_Original;

void TransitionToFlyoutVisiblePendingState_Hook(void* pThis,
                                                void* hstr,
                                                int64_t durationTicks) {
    const int64_t delayMs = g_hoverDelayMs.load(std::memory_order_relaxed);
    TransitionToFlyoutVisiblePendingState_Original(pThis, hstr,
                                                   delayMs * 10000);
}

HMODULE GetTaskbarViewModuleHandle() {
    HMODULE module = GetModuleHandle(L"Taskbar.View.dll");
    if (!module) {
        module = GetModuleHandle(L"ExplorerExtensions.dll");
    }
    return module;
}

bool HookTaskbarViewDllSymbols(HMODULE module) {
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(private: void __cdecl winrt::Taskbar::implementation::HoverFlyoutModel::TransitionToFlyoutVisiblePendingState(struct winrt::hstring,class std::chrono::duration<__int64,struct std::ratio<1,10000000> >))"},
            &TransitionToFlyoutVisiblePendingState_Original,
            TransitionToFlyoutVisiblePendingState_Hook,
        },
    };

    if (!HookSymbols(module, symbolHooks, ARRAYSIZE(symbolHooks))) {
        Wh_Log(L"HookSymbols failed: the taskbar is left untouched");
        return false;
    }

    return true;
}

void HandleLoadedModuleIfTaskbarView(HMODULE module) {
    if (!g_taskbarViewDllLoaded && GetTaskbarViewModuleHandle() == module &&
        !g_taskbarViewDllLoaded.exchange(true)) {
        if (HookTaskbarViewDllSymbols(module)) {
            Wh_ApplyHookOperations();
        }
    }
}

// Taskbar.View.dll peut être chargée après l'init du mod : on surveille les
// chargements de modules pour poser le hook dès qu'elle apparaît.
using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR lpLibFileName,
                                   HANDLE hFile,
                                   DWORD dwFlags) {
    HMODULE module = LoadLibraryExW_Original(lpLibFileName, hFile, dwFlags);
    if (module) {
        HandleLoadedModuleIfTaskbarView(module);
    }
    return module;
}

void LoadSettings() {
    const int delay = Wh_GetIntSetting(L"hoverDelay");
    g_hoverDelayMs.store(std::clamp(delay, 0, kMaxHoverDelayMs),
                         std::memory_order_relaxed);
}

BOOL Wh_ModInit() {
    LoadSettings();

    if (HMODULE module = GetTaskbarViewModuleHandle()) {
        g_taskbarViewDllLoaded = true;
        if (!HookTaskbarViewDllSymbols(module)) {
            return FALSE;
        }
    }

    HMODULE kernelBase = GetModuleHandle(L"kernelbase.dll");
    if (!kernelBase) {
        Wh_Log(L"kernelbase.dll not found");
        return FALSE;
    }

    auto pLoadLibraryExW =
        (LoadLibraryExW_t)GetProcAddress(kernelBase, "LoadLibraryExW");
    if (!pLoadLibraryExW) {
        Wh_Log(L"LoadLibraryExW not found in kernelbase.dll");
        return FALSE;
    }

    WindhawkUtils::Wh_SetFunctionHookT(pLoadLibraryExW, LoadLibraryExW_Hook,
                                       &LoadLibraryExW_Original);

    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_taskbarViewDllLoaded) {
        if (HMODULE module = GetTaskbarViewModuleHandle()) {
            HandleLoadedModuleIfTaskbarView(module);
        }
    }
}

void Wh_ModSettingsChanged() {
    LoadSettings();
}
