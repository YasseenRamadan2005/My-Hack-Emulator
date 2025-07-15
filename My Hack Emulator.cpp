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
// Types
using i16 = int16_t;

// Constants
constexpr int ROM_SIZE = 32768;
constexpr int RAM_SIZE = 24577;
constexpr int SCREEN_WIDTH = 512;
constexpr int SCREEN_HEIGHT = 256;
// Global memory
i16 ROM[ROM_SIZE] = {};
i16 RAM[RAM_SIZE] = {};
//Registers
i16 PC = 0;
i16 A = 0;
i16 D = 0;
bool updatedScreen = false;
HWND debugWindow = nullptr;
HWND debugEditBox = nullptr;
HWND clearButton = nullptr;
HWND stepButton = nullptr;

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
const char* compTable[2][64] = {};

const char* jumpTable[8] = {
    "", "JGT", "JEQ", "JGE", "JLT", "JNE", "JLE", "JMP"
};
void DebugInstruction(int16_t instruction, uint16_t PC) {
    static bool initialized = false;
    if (!initialized) {
        // a = 0 (A used)
        compTable[0][42] = "0";
        compTable[0][63] = "1";
        compTable[0][58] = "-1";
        compTable[0][12] = "D";
        compTable[0][48] = "A";
        compTable[0][13] = "!D";
        compTable[0][49] = "!A";
        compTable[0][15] = "-D";
        compTable[0][51] = "-A";
        compTable[0][31] = "D+1";
        compTable[0][55] = "A+1";
        compTable[0][14] = "D-1";
        compTable[0][50] = "A-1";
        compTable[0][2] = "D+A";
        compTable[0][19] = "D-A";
        compTable[0][7] = "A-D";
        compTable[0][0] = "D&A";
        compTable[0][21] = "D|A";

        // a = 1 (M used)
        compTable[1][42] = "0";
        compTable[1][63] = "1";
        compTable[1][58] = "-1";
        compTable[1][12] = "D";
        compTable[1][48] = "M";
        compTable[1][13] = "!D";
        compTable[1][49] = "!M";
        compTable[1][15] = "-D";
        compTable[1][51] = "-M";
        compTable[1][31] = "D+1";
        compTable[1][55] = "M+1";
        compTable[1][14] = "D-1";
        compTable[1][50] = "M-1";
        compTable[1][2] = "D+M";
        compTable[1][19] = "D-M";
        compTable[1][7] = "M-D";
        compTable[1][0] = "D&M";
        compTable[1][21] = "D|M";

        initialized = true;
    }

    char buffer[64];

    if (instruction >= 0) {
        // A-instruction
        sprintf_s(buffer, sizeof(buffer), "@%d", instruction & 0x7FFF);
    }
    else {
        // C-instruction
        uint8_t a = (instruction >> 12) & 0b1;
        uint8_t comp = (instruction >> 6) & 0b111111;
        uint8_t dest = (instruction >> 3) & 0b111;
        uint8_t jump = instruction & 0b111;

        const char* compStr = (comp < 64) ? compTable[a][comp] : "";
        const char* jumpStr = jumpTable[jump];

        char destStr[4] = "";
        if (dest & 0b100) strcat_s(destStr, sizeof(destStr), "A");
        if (dest & 0b010) strcat_s(destStr, sizeof(destStr), "D");
        if (dest & 0b001) strcat_s(destStr, sizeof(destStr), "M");

        if (destStr[0] && jumpStr[0])
            sprintf_s(buffer, sizeof(buffer), "%s=%s;%s", destStr, compStr, jumpStr);
        else if (destStr[0])
            sprintf_s(buffer, sizeof(buffer), "%s=%s", destStr, compStr);
        else if (jumpStr[0])
            sprintf_s(buffer, sizeof(buffer), "%s;%s", compStr, jumpStr);
        else
            sprintf_s(buffer, sizeof(buffer), "%s", compStr);
    }

    // Get RAM[A] safely
    int16_t ramA = (A >= 0 && A < RAM_SIZE) ? RAM[A] : 0;

    char full[160];
    sprintf_s(full, sizeof(full),
        "PC: %d | 0x%04X => %-6s | A: %6d | D: %6d | RAM[A]: %6d\n",
        PC, instruction, buffer, A, D, ramA);

    //OutputDebugStringA(full);  // Console (optional)

    // Append to debug window
    if (debugEditBox) {
        int len = GetWindowTextLengthA(debugEditBox);

        // Limit debug window to prevent memory bloat
        const int MAX_DEBUG_TEXT = 10000; // chars
        if (len > MAX_DEBUG_TEXT) {
            SetWindowTextA(debugEditBox, "(Truncated...)\r\n");
            len = GetWindowTextLengthA(debugEditBox);
            SendMessageA(debugEditBox, EM_SETSEL, len, len);
        }

        SendMessageA(debugEditBox, EM_SETSEL, len, len);
        SendMessageA(debugEditBox, EM_REPLACESEL, FALSE, (LPARAM)full);
        SendMessageA(debugEditBox, EM_SCROLLCARET, 0, 0);
    }
}


void UpdateScreen() {
    if (updatedScreen) {


        i16 screenBase = 16384;

        for (i16 y = 0; y < SCREEN_HEIGHT; ++y) {
            for (i16 wordIndex = 0; wordIndex < SCREEN_WIDTH / 16; ++wordIndex) {
                i16  ramIndex = screenBase + y * (SCREEN_WIDTH / 16) + wordIndex;
                i16 word = RAM[ramIndex];

                for (i16 bit = 0; bit < 16; ++bit) {
                    i16  x = wordIndex * 16 + (15 - bit);  // leftmost bit is highest bit
                    bool pixelOn = ((int16_t)word >> bit) & 1;  // cast to uint16_t for logical shift
                    framebuffer[y][x] = pixelOn ? 0xFF000000 : 0xFFFFFFFF;  // black if bit set, else white
                }
            }
        }

        InvalidateRect(hwnd, nullptr, FALSE);
    }
}


void executeNextInstruction() {
    i16 instruction = ROM[PC];
    DebugInstruction(instruction, PC);
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
            if (A >= 16384 && A <= 24576) {
                updatedScreen = TRUE;
            }
            else {
                updatedScreen = FALSE;
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


LRESULT CALLBACK DebugWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE:
        if (debugEditBox) {
            MoveWindow(debugEditBox, 0, 0, LOWORD(lParam), HIWORD(lParam) - 40, TRUE);
        }
        if (clearButton) {
            MoveWindow(clearButton, LOWORD(lParam) - 85, HIWORD(lParam) - 35, 80, 30, TRUE);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1 && debugEditBox) { // Clear button
            SetWindowTextA(debugEditBox, "");
            PC = 0;
            const char* resetMsg = "PC reset to 0\n";
            int len = GetWindowTextLengthA(debugEditBox);
            SendMessageA(debugEditBox, EM_SETSEL, len, len);
            SendMessageA(debugEditBox, EM_REPLACESEL, FALSE, (LPARAM)resetMsg);
            SendMessageA(debugEditBox, EM_SCROLLCARET, 0, 0);
            return 0;
        }
        else if (LOWORD(wParam) == 2) { // Step button
            if (!paused) paused = true; // pause automatic execution if running
            // Run one instruction step (mimic EmuThread loop for one cycle)
            UpdateScreen();
            executeNextInstruction();

            // Optionally, add a line to debug window so user knows step happened
            const char* stepMsg = "Step executed\n";
            int len = GetWindowTextLengthA(debugEditBox);
            SendMessageA(debugEditBox, EM_SETSEL, len, len);
            SendMessageA(debugEditBox, EM_REPLACESEL, FALSE, (LPARAM)stepMsg);
            SendMessageA(debugEditBox, EM_SCROLLCARET, 0, 0);
            return 0;
        }
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void CreateDebugWindow(HINSTANCE hInstance) {
    WNDCLASS wcDebug = {};
    wcDebug.lpfnWndProc = DebugWindowProc;
    wcDebug.hInstance = hInstance;
    wcDebug.lpszClassName = L"DebugWindowClass";
    RegisterClass(&wcDebug);

    debugWindow = CreateWindowEx(
        0, L"DebugWindowClass", L"Debug Output",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        nullptr, nullptr, hInstance, nullptr);

    debugEditBox = CreateWindowEx(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 620, 420,
        debugWindow, nullptr, hInstance, nullptr);

    clearButton = CreateWindow(
        L"BUTTON", L"RESET",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        540, 430, 80, 30,
        debugWindow, (HMENU)1, hInstance, nullptr);

    stepButton = CreateWindow(
        L"BUTTON", L"Step",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        440, 430, 80, 30,  // Positioned next to Clear button
        debugWindow, (HMENU)2, hInstance, nullptr);

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
        case VK_RETURN:       RAM[RAM_SIZE - 1 - 1] = 128; break;
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

        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'C') {
            running = false;
            PostQuitMessage(0);
            return 0;
        }
        else if ((wParam == VK_SHIFT) || (wParam == VK_LSHIFT) || (wParam == VK_RSHIFT)) { // Alt key for pause
            paused = !paused;
            return 0;
        }
        return 0;

    case WM_KEYUP:
        takeKeyboardInput(uMsg, wParam);
        return 0;

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

DWORD WINAPI EmuThread(LPVOID) {
    while (running) {
        if (!paused) {
            UpdateScreen();
            executeNextInstruction();
        }
        //Sleep(1);
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
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 512,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOWDEFAULT);


    InitBitmapInfo();
    InitBitmapInfo();
    //CreateDebugWindow(hInstance);
    CreateThread(nullptr, 0, EmuThread, nullptr, 0, nullptr);

    MSG msg = {};
    while (running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    MessageBox(hwnd, L"Terminated", L"Emulator", MB_OK | MB_ICONINFORMATION);
    return 0;
}
