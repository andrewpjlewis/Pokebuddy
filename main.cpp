#define UNICODE
#define _UNICODE

#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <fstream>
#include "json.hpp"
#include <vector>
#include <string>
#include <ctime>

using namespace Gdiplus;
using json = nlohmann::json;

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

struct PokemonGIF {
    std::wstring path;
    Image* img;
    int frameCount;
    int currentFrame;
    REAL width, height;
    std::string name;
};

struct BulbasaurSet {
    PokemonGIF idle;
    PokemonGIF walkLeft, walkRight;
    PokemonGIF sleepLeft, sleepRight;
    PokemonGIF wakeLeft, wakeRight;
    PokemonGIF tripLeft, tripRight;
    PokemonGIF pullUp;
    PokemonGIF* current = nullptr;
};

enum PetState {
    STATE_IDLE,
    STATE_WALK,
    STATE_SLEEP,
    STATE_WAKE,
    STATE_PULLUP,
    STATE_TRIP
};

std::string selectedPokemon = "bulbasaur";
BulbasaurSet bulbasaur;
NOTIFYICONDATA nid{};
ULONG_PTR gdiplusToken;

POINT petPosition = {-1, -1};
bool movingRight = true;
bool exploreMode = false;

int behaviorTimer = 0;
DWORD lastInteraction = 0;
DWORD sleepTimeout = 0.25 * 60 * 1000; // 15 seconds
int moveSpeed = 8;
int nudgeDistance = 50; // pixels to move on left-click
UINT baseTimerSpeed = 16; // smooth

DWORD lastAnimationTime = 0;
UINT animIntervalIdle = 250;
UINT animIntervalWalk = 150;
UINT animIntervalSleep = 800;
UINT animIntervalWake = 150;
UINT animIntervalTrip = 200;

PetState currentState = STATE_IDLE;
PetState previousState = STATE_IDLE;

PokemonGIF loadGifSafe(std::wstring path) {
    PokemonGIF pg{};
    pg.path = path;
    pg.img = new Image(pg.path.c_str());
    pg.frameCount = pg.img->GetFrameCount(&FrameDimensionTime);
    pg.currentFrame = 0;
    pg.width = pg.img->GetWidth();
    pg.height = pg.img->GetHeight();
    return pg;
}

bool advanceFrame(PokemonGIF& pg) {
    if (!pg.img) return false;
    pg.currentFrame++;
    if (pg.currentFrame >= pg.frameCount) {
        pg.currentFrame = 0;
        return true;
    }
    pg.img->SelectActiveFrame(&FrameDimensionTime, pg.currentFrame);
    return false;
}

void loadBulbasaur() {
    std::wstring base = L"assets\\";
    bulbasaur.idle       = loadGifSafe(base + L"bulbasaur-idle.gif");
    bulbasaur.walkLeft   = loadGifSafe(base + L"bulbasaur-walk-left.gif");
    bulbasaur.walkRight  = loadGifSafe(base + L"bulbasaur-walk-right.gif");
    bulbasaur.sleepLeft  = loadGifSafe(base + L"bulbasaur-sleep-left.gif");
    bulbasaur.sleepRight = loadGifSafe(base + L"bulbasaur-sleep-right.gif");
    bulbasaur.wakeLeft   = loadGifSafe(base + L"bulbasaur-wake-left.gif");
    bulbasaur.wakeRight  = loadGifSafe(base + L"bulbasaur-wake-right.gif");
    bulbasaur.tripLeft   = loadGifSafe(base + L"bulbasaur-trip-left.gif");
    bulbasaur.tripRight  = loadGifSafe(base + L"bulbasaur-trip-right.gif");
    bulbasaur.pullUp     = loadGifSafe(base + L"bulbasaur-pull-up.gif");
    bulbasaur.current    = &bulbasaur.idle;
}

void loadData() {
    std::ifstream f("data.json");
    if (f) {
        json j; f >> j;
        if (j.contains("posX")) petPosition.x = j["posX"];
        if (j.contains("posY")) petPosition.y = j["posY"];
        if (j.contains("exploreMode")) exploreMode = j["exploreMode"];
    }
}

void saveData() {
    json j;
    j["pokemon"] = selectedPokemon;
    j["posX"] = petPosition.x;
    j["posY"] = petPosition.y;
    j["exploreMode"] = exploreMode;
    std::ofstream f("data.json"); f << j.dump(4);
}

void ShowRightClickMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, 1, exploreMode ? L"Disable Explore Mode" : L"Enable Explore Mode");
    AppendMenu(hMenu, MF_STRING, 2, L"Feed");
    AppendMenu(hMenu, MF_STRING, 3, L"Heal");
    AppendMenu(hMenu, MF_STRING, 4, L"Switch Pokemon");
    AppendMenu(hMenu, MF_STRING, 5, L"Find Bulbasaur");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, 6, L"Return to Pokeball");

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_TOPALIGN | TPM_LEFTALIGN,
                             cursor.x, cursor.y, 0, hwnd, NULL);

    switch (cmd) {
        case 1: exploreMode = !exploreMode; break;
        case 2: MessageBox(hwnd, L"You fed your Pokemon!", L"Feed", MB_OK); break;
        case 3: MessageBox(hwnd, L"Your Pokemon is healed!", L"Heal", MB_OK); break;
        case 4: MessageBox(hwnd, L"Switching Pokemon...", L"Switch Pokemon", MB_OK); break;
        case 5:
            petPosition.x = GetSystemMetrics(SM_CXSCREEN) - (int)bulbasaur.current->width - 200;
            movingRight = false;
            MessageBox(hwnd, L"Bulbasaur found!", L"Find Bulbasaur", MB_OK);
            break;
        case 6: PostQuitMessage(0); break;
    }

    DestroyMenu(hMenu);
    saveData();
}

void renderPokemon(HWND hwnd) {
    PokemonGIF* pg = bulbasaur.current;
    if (!pg || !pg->img) return;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, (int)pg->width, (int)pg->height);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    Graphics g(mem);
    g.Clear(Color(0,0,0,0));
    g.DrawImage(pg->img, 0.0f, 0.0f, pg->width, pg->height);

    POINT ptDest = {petPosition.x, petPosition.y};
    SIZE sizeWnd = {(LONG)pg->width, (LONG)pg->height};
    POINT ptSrc = {0,0};

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, screen, &ptDest, &sizeWnd, mem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

LRESULT CALLBACK PetProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, 1, baseTimerSpeed, NULL);
            srand((unsigned)time(NULL));
            lastInteraction = GetTickCount();
            return 0;

        case WM_TIMER: {
            DWORD now = GetTickCount();

            if (!exploreMode && currentState == STATE_IDLE && (now - lastInteraction) > sleepTimeout) {
                previousState = currentState;
                currentState = STATE_SLEEP;
            }

            switch (currentState) {
                case STATE_IDLE:
                    if (bulbasaur.current != &bulbasaur.idle) {
                        bulbasaur.current = &bulbasaur.idle;
                        bulbasaur.current->currentFrame = 0;
                    }
                    if (now - lastAnimationTime >= animIntervalIdle) {
                        lastAnimationTime = now;
                        advanceFrame(*bulbasaur.current);
                        behaviorTimer++;
                        if (behaviorTimer > (exploreMode ? 10 : 30)) {
                            behaviorTimer = 0;
                            if (rand() % (exploreMode ? 2 : 3) == 0) {
                                currentState = STATE_WALK;
                                movingRight = rand() % 2;
                            }
                        }
                    }
                    break;

                case STATE_WALK:
                    if (bulbasaur.current != (movingRight ? &bulbasaur.walkRight : &bulbasaur.walkLeft)) {
                        bulbasaur.current = movingRight ? &bulbasaur.walkRight : &bulbasaur.walkLeft;
                        bulbasaur.current->currentFrame = 0;
                    }
                    if (now - lastAnimationTime >= animIntervalWalk) {
                        lastAnimationTime = now;
                        advanceFrame(*bulbasaur.current);

                        // Move pet
                        petPosition.x += movingRight ? moveSpeed : -moveSpeed;
                        if (petPosition.x < 0) { petPosition.x = 0; movingRight = true; }
                        if (petPosition.x + bulbasaur.current->width > GetSystemMetrics(SM_CXSCREEN)) {
                            petPosition.x = (int)(GetSystemMetrics(SM_CXSCREEN) - bulbasaur.current->width);
                            movingRight = false;
                        }
                        behaviorTimer++;
                        if (behaviorTimer > (exploreMode ? 15 : 30)) {
                            behaviorTimer = 0;
                            currentState = STATE_IDLE;
                        }
                    }
                    break;

                case STATE_SLEEP: {
                    PokemonGIF* targetSleep = movingRight ? &bulbasaur.sleepRight : &bulbasaur.sleepLeft;
                    if (bulbasaur.current != targetSleep) {
                        bulbasaur.current = targetSleep;
                        bulbasaur.current->currentFrame = 0;
                    }
                    if (now - lastAnimationTime >= animIntervalSleep) {
                        lastAnimationTime = now;
                        advanceFrame(*bulbasaur.current);
                    }
                    break;
                }

                case STATE_WAKE:
                    if (bulbasaur.current != (movingRight ? &bulbasaur.wakeRight : &bulbasaur.wakeLeft)) {
                        bulbasaur.current = movingRight ? &bulbasaur.wakeRight : &bulbasaur.wakeLeft;
                        bulbasaur.current->currentFrame = 0;
                    }
                    if (now - lastAnimationTime >= animIntervalWake) {
                        lastAnimationTime = now;
                        if (advanceFrame(*bulbasaur.current)) {
                            currentState = exploreMode ? STATE_WALK : STATE_IDLE;
                            bulbasaur.current = &bulbasaur.idle;
                        }
                    }
                    break;

                case STATE_TRIP:
                    if (bulbasaur.current != (movingRight ? &bulbasaur.tripRight : &bulbasaur.tripLeft)) {
                        bulbasaur.current = movingRight ? &bulbasaur.tripRight : &bulbasaur.tripLeft;
                        bulbasaur.current->currentFrame = 0;
                        behaviorTimer = 0;
                    }
                    if (now - lastAnimationTime >= animIntervalTrip) {
                        lastAnimationTime = now;
                        if (advanceFrame(*bulbasaur.current)) {
                            currentState = STATE_IDLE;
                            bulbasaur.current = &bulbasaur.idle;
                        }
                    }
                    break;
            }

            renderPokemon(hwnd);
            SetWindowPos(hwnd, HWND_TOPMOST, petPosition.x, petPosition.y, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            saveData();
            return 0;
        }

        case WM_LBUTTONDOWN:
            lastInteraction = GetTickCount();
            if (currentState == STATE_IDLE) {
                previousState = STATE_IDLE;
                movingRight = rand() % 2; // random nudge direction
                currentState = STATE_WALK;
            } else if (currentState == STATE_WALK) {
                currentState = STATE_TRIP; // trip when clicked while walking
            }
            break;

        case WM_RBUTTONDOWN:
            ShowRightClickMenu(hwnd);
            return 0;

        case WM_APP + 1:
            if (lParam == WM_RBUTTONUP)
                ShowRightClickMenu(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    loadData();
    loadBulbasaur();

    if (petPosition.x == -1 || petPosition.y == -1) {
        RECT taskbarRect;
        HWND taskbar = FindWindow(L"Shell_TrayWnd", NULL);
        GetWindowRect(taskbar, &taskbarRect);
        petPosition.x = taskbarRect.right - (int)bulbasaur.current->width - 80;
        int taskbarHeight = taskbarRect.bottom - taskbarRect.top;
        petPosition.y = taskbarRect.top + taskbarHeight - (int)bulbasaur.current->height + 2;
        saveData();
    }

    WNDCLASS wcPet{};
    wcPet.lpfnWndProc = PetProc;
    wcPet.hInstance = hInst;
    wcPet.lpszClassName = L"PetWindow";
    wcPet.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wcPet);

    HWND hwndPet = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetWindow", L"PokeBuddy", WS_POPUP,
        petPosition.x, petPosition.y,
        (int)bulbasaur.current->width,
        (int)bulbasaur.current->height,
        NULL, NULL, hInst, NULL
    );

    ShowWindow(hwndPet, SW_SHOW);
    UpdateWindow(hwndPet);

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwndPet;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP + 1;
    HICON hIcon = (HICON)LoadImage(NULL, L"assets\\tray-icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
    if (hIcon) nid.hIcon = hIcon;
    wcscpy_s(nid.szTip, ARRAYSIZE(nid.szTip), L"PokeBuddy");
    Shell_NotifyIcon(NIM_ADD, &nid);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    GdiplusShutdown(gdiplusToken);
    return 0;
}
