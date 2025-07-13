#include <windows.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>

// Types
using i16 = int16_t;

// Constants
constexpr int ROM_SIZE = 32768;
constexpr int RAM_SIZE = 24576;
constexpr int SCREEN_WIDTH = 512;
constexpr int SCREEN_HEIGHT = 256;

// Global memory
i16 ROM[ROM_SIZE] = {};
i16 RAM[RAM_SIZE] = {};
uint16_t PC = 0;

// Emulator control
std::atomic<bool> paused = false;
std::atomic<bool> running = true;

// Windows GUI
HWND hwnd = nullptr;
BITMAPINFO bmi = {};
uint32_t framebuffer[SCREEN_HEIGHT][SCREEN_WIDTH] = {};

void InitBitmapInfo() {
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SCREEN_WIDTH;
    bmi.bmiHeader.biHeight = -SCREEN_HEIGHT;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        running = false;
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN: {
        bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        wchar_t buffer[64];
        swprintf(buffer, 64, L"Key pressed: VK_CODE %d\n", (int)wParam);
        OutputDebugStringW(buffer);
        if (ctrlPressed && wParam == 'C') {
            running = false;
            PostQuitMessage(0);
            return 0;
        }

        else if (wParam == VK_SPACE) {
            paused = !paused;
            if (paused) {
                MessageBox(hwnd, L"Paused", L"Emulator", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        else {
            RAM[RAM_SIZE - 1] = 0;
            return 0;
        }
    }


    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        StretchDIBits(hdc,
            0, 0, SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2,
            0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
            framebuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void UpdateScreen() {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            framebuffer[y][x] = 0xFFFFFFFF;  // white
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void executeNextInstruction() {
    PC++;
    if (PC >= ROM_SIZE) PC = 0;
}

DWORD WINAPI EmuThread(LPVOID) {
    while (running) {
        if (!paused) {
            executeNextInstruction();
            UpdateScreen();
        }
        Sleep(16);  // ~60Hz
    }
    return 0;
}

bool LoadROM(const char* filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(ROM), sizeof(ROM));
    return true;
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Parse command line arguments (wide char)
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW || argc < 2) {
        MessageBox(nullptr, L"Usage: MyHACKEmulator.exe program.bin", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Convert argvW[1] (wide char) to char* (narrow)
    char filename[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, argvW[1], -1, filename, MAX_PATH, nullptr, nullptr);
    LocalFree(argvW);

    if (!LoadROM(filename)) {
        MessageBox(nullptr, L"Failed to load ROM.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    const wchar_t CLASS_NAME[] = L"MyHACKCPUEmulator";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    hwnd = CreateWindowEx(0, CLASS_NAME, L"My HACK Emulator",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 512,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOWDEFAULT);

    InitBitmapInfo();
    CreateThread(nullptr, 0, EmuThread, nullptr, 0, nullptr);

    MSG msg = {};
    while (running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    MessageBox(hwnd, L"Terminated", L"Emulator", MB_OK | MB_ICONINFORMATION);
    return 0;
}

