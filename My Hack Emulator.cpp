#include <windows.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <thread>
#include <string>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <bitset>
#include <chrono>

// Types
using i16 = int16_t;

// Constants
constexpr int ROM_SIZE = 32768;
constexpr int RAM_SIZE = 24577;
constexpr int SCREEN_WIDTH = 512;
constexpr int SCREEN_HEIGHT = 256;
constexpr int SCREEN_WORDS = SCREEN_WIDTH * SCREEN_HEIGHT / 16; // 512*256 / 16 = 8192
uint16_t screenShadow[SCREEN_WORDS] = {};  // start with all zeros
int updatedScreenIndex = -1;  // index into RAM, not full screen update

// Global memory
i16 ROM[ROM_SIZE] = {};
i16 RAM[RAM_SIZE] = {};
//Registers
i16 PC = 0;
i16 A = 0;
i16 D = 0;
bool updatedScreen = false;
HWND clearButton = nullptr;
HWND stepButton = nullptr;
std::atomic<int> instructionsPerSecond = 1000000;  // Default: 1 million IPS

// Emulator control
std::atomic<bool> paused = false;
std::atomic<bool> running = true;

// Windows GUI
HWND hwnd = nullptr;
BITMAPINFO bmi = {};
uint32_t framebuffer[SCREEN_HEIGHT][SCREEN_WIDTH] = {};
bool fullscreen = false;
RECT windowedRect = { 0,0,1000,700 };
void InitBitmapInfo() {
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SCREEN_WIDTH;
    bmi.bmiHeader.biHeight = -SCREEN_HEIGHT;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
}
const char* compTable[2][64] = {};

const char* jumpTable[8] = {
    "", "JGT", "JEQ", "JGE", "JLT", "JNE", "JLE", "JMP"
};

void UpdateScreen() {
    if (!updatedScreen) return;
    if (updatedScreenIndex < 0 || updatedScreenIndex >= SCREEN_WORDS) return;

    uint16_t word = RAM[16384 + updatedScreenIndex];
    if (screenShadow[updatedScreenIndex] != word) {
        screenShadow[updatedScreenIndex] = word;

        int y = updatedScreenIndex / 32;
        int wordIndex = updatedScreenIndex % 32;

        for (int bit = 0; bit < 16; ++bit) {
            int x = wordIndex * 16 + bit;
            bool pixelOn = (word >> bit) & 1;
            framebuffer[y][x] = pixelOn ? 0xFF000000 : 0xFFFFFFFF;
        }
    }
    updatedScreen = false;
    updatedScreenIndex = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
}





void executeNextInstruction() {
    i16 instruction = ROM[PC];
    if ((instruction & 0b1110000000000000) == 0b1110000000000000) {
        // Valid C-instruction
        bool a_bit = (instruction >> 12) & 0b0000000000000001;
        i16 comp_bits = (instruction >> 6) & 0b0000000000111111;
        i16 dest_bits = (instruction >> 3) & 0b0000000000000111;
        i16 jump_bits = instruction & 0b0000000000000111;

        i16 x = D;
        i16 y = a_bit ? RAM[A] : A;

        // Decode ALU control bits with 16-bit constants
        if (comp_bits & 0b0000000000100000) x = 0;
        if (comp_bits & 0b0000000000010000) x = ~x;
        if (comp_bits & 0b0000000000001000) y = 0;
        if (comp_bits & 0b0000000000000100) y = ~y;

        i16 output = (comp_bits & 0b0000000000000010) ? (x + y) : (x & y);

        if (comp_bits & 0b0000000000000001) output = ~output;

        // Destination
        if (dest_bits & 0b0000000000000001) {
            RAM[A] = output;
            if (A >= 16384 && A < 24576) {
                int screenIndex = A - 16384;
                updatedScreenIndex = screenIndex;
                updatedScreen = true;
            }
        }


        if (dest_bits & 0b0000000000000100) {
            A = output;
        }
        if (dest_bits & 0b0000000000000010) D = output;
        // Jump
        bool jump = false;
        if ((jump_bits & 0b0000000000000001) && (output > 0)) {
            jump = true;
        }
        if ((jump_bits & 0b0000000000000010) && (output == 0)) {
            jump = true;
        }
        if ((jump_bits & 0b0000000000000100) && (output < 0)) {
            jump = true;
        }

        if (jump) {
            PC = A;
            return;  // do not increment PC
        }
    }
    else {
        // A-instruction
        A = instruction;
    }

    PC++;
    //if (PC >= ROM_SIZE) PC = 0;
}

bool LoadROM(const char* filename) {
    std::ifstream file;
    const char* ext = strrchr(filename, '.');
    std::string extension = (ext ? ext : "");

    if (extension == ".bin") {
        file.open(filename, std::ios::binary);
        if (!file) return false;
        file.read(reinterpret_cast<char*>(ROM), sizeof(ROM));
        return true;
    }
    else if (extension == ".hack") {
        file.open(filename);
        if (!file) return false;

        std::string line;
        int address = 0;
        while (std::getline(file, line) && address < ROM_SIZE) {
            if (line.length() != 16) continue;
            int value = 0;
            bool valid = true;
            for (char c : line) {
                value <<= 1;
                if (c == '1') value |= 1;
                else if (c != '0') {
                    valid = false;
                    break;
                }
            }
            if (valid) ROM[address++] = static_cast<i16>(value);
        }
        return true;
    }

    return false;
}


void takeKeyboardInput(UINT msg, WPARAM wParam) {
    if (msg == WM_KEYDOWN) {
        switch (wParam) {
        case VK_RETURN:       RAM[RAM_SIZE - 1] = 128; break;
        case VK_BACK:         RAM[RAM_SIZE - 1] = 129; break;
        case VK_LEFT:         RAM[RAM_SIZE - 1] = 130; break;
        case VK_UP:           RAM[RAM_SIZE - 1] = 131; break;
        case VK_RIGHT:        RAM[RAM_SIZE - 1] = 132; break;
        case VK_DOWN:         RAM[RAM_SIZE - 1] = 133; break;
        case VK_HOME:         RAM[RAM_SIZE - 1] = 134; break;
        case VK_END:          RAM[RAM_SIZE - 1] = 135; break;
        case VK_PRIOR:        RAM[RAM_SIZE - 1] = 136; break; // Page Up
        case VK_NEXT:         RAM[RAM_SIZE - 1] = 137; break; // Page Down
        case VK_INSERT:       RAM[RAM_SIZE - 1] = 138; break;
        case VK_DELETE:       RAM[RAM_SIZE - 1] = 139; break;
        case VK_ESCAPE:       RAM[RAM_SIZE - 1] = 140; break;
        case VK_F1:  case VK_F2:  case VK_F3:  case VK_F4:
        case VK_F5:  case VK_F6:  case VK_F7:  case VK_F8:
        case VK_F9:  case VK_F10: case VK_F11: case VK_F12:
            RAM[RAM_SIZE - 1] = 141 + (wParam - VK_F1);
            break;
        default:
            if (wParam < 128)
                RAM[RAM_SIZE - 1] = static_cast<i16>(wParam);
            break;
        }
    }
    else if (msg == WM_KEYUP) {
        RAM[RAM_SIZE - 1] = 0;
    }
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        running = false;
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        takeKeyboardInput(uMsg, wParam);
        if (wParam == VK_F11) {
            fullscreen = !fullscreen;
            if (fullscreen) {
                // Save windowed size and position
                GetWindowRect(hwnd, &windowedRect);
                SetWindowLong(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                MONITORINFO mi = { sizeof(mi) };
                if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
                    SetWindowPos(hwnd, HWND_TOP,
                        mi.rcMonitor.left, mi.rcMonitor.top,
                        mi.rcMonitor.right - mi.rcMonitor.left,
                        mi.rcMonitor.bottom - mi.rcMonitor.top,
                        SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER);
                }
            }
            else {
                // Restore windowed style and position
                SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                SetWindowPos(hwnd, HWND_NOTOPMOST,
                    windowedRect.left, windowedRect.top,
                    windowedRect.right - windowedRect.left,
                    windowedRect.bottom - windowedRect.top,
                    SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
            }
            return 0;
        }

        if ((GetKeyState(VK_CONTROL) & 0x8000)) {
            switch (wParam) {
            case 'C':  // Ctrl+C = exit
                running = false;
                PostQuitMessage(0);
                return 0;

            case 'R':  // Ctrl+R = Reset PC
                PC = 0;
                for (int y = 0; y < SCREEN_HEIGHT; ++y) {
                    for (int x = 0; x < SCREEN_WIDTH; ++x) {
                        framebuffer[y][x] = 0xFFFFFFFF;  // White
                    }
                }
                return 0;
            }
        }

        // Toggle pause with Shift
        if ((wParam == VK_SHIFT) || (wParam == VK_LSHIFT) || (wParam == VK_RSHIFT)) {
            paused = !paused;
            return 0;
        }

        return 0;


    case WM_KEYUP:
        takeKeyboardInput(uMsg, wParam);
        return 0;
    case WM_SIZE: {
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int clientWidth = rc.right - rc.left;
        int clientHeight = rc.bottom - rc.top;

        StretchDIBits(hdc,
            0, 0, clientWidth, clientHeight,
            0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
            framebuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
DWORD WINAPI EmuThread(LPVOID) {
    auto lastTime = std::chrono::high_resolution_clock::now();
    int executed = 0;

    while (running) {
        if (!paused) {
            executeNextInstruction();
            UpdateScreen();
            executed++;

            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastTime).count();

            if (elapsed >= 1'000'000) {
                executed = 0;
                lastTime = now;
            }
            else {
                std::this_thread::sleep_for(std::chrono::microseconds(0));
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    return 0;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Parse command line arguments
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW || argc < 2) {
        MessageBox(nullptr, L"Usage: MyHACKEmulator.exe program.bin or program.hack", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Convert wide string to narrow char*
    char filename[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, argvW[1], -1, filename, MAX_PATH, nullptr, nullptr);
    LocalFree(argvW);

    if (!LoadROM(filename)) {
        MessageBox(nullptr, L"Failed to load ROM file.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    const wchar_t CLASS_NAME[] = L"MyHACKCPUEmulator";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    hwnd = CreateWindowEx(
        0, CLASS_NAME, L"My HACK Emulator",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOWDEFAULT);


    InitBitmapInfo();
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            framebuffer[y][x] = 0xFFFFFFFF;  // White
        }
    }
    CreateThread(nullptr, 0, EmuThread, nullptr, 0, nullptr);

    MSG msg = {};
    char title[128];
    while (running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        static int lastIPS = -1;
        int currentIPS = instructionsPerSecond;
        if (currentIPS != lastIPS) {
            snprintf(title, sizeof(title), "My HACK Emulator");
            SetWindowTextA(hwnd, title);
            lastIPS = currentIPS;
        }
    }
    MessageBox(hwnd, L"Terminated", L"Emulator", MB_OK | MB_ICONINFORMATION);
    return 0;
}