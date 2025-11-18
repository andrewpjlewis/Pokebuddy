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
#include <map>

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
};

struct BulbasaurSet {
    PokemonGIF idle;
    PokemonGIF walkLeft, walkRight;
    PokemonGIF sleepLeft, sleepRight;
    PokemonGIF wakeLeft, wakeRight;
    PokemonGIF tripLeft, tripRight;
    PokemonGIF findItem;
    PokemonGIF eat;
    PokemonGIF* current = nullptr;
};

enum PetState {
    STATE_IDLE,
    STATE_WALK,
    STATE_SLEEP,
    STATE_WAKE,
    STATE_TRIP,
    STATE_FINDITEM,
    STATE_EAT
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
DWORD sleepTimeout = 0.25 * 60 * 1000;
int moveSpeed = 8;
int nudgeDistance = 50;
UINT baseTimerSpeed = 16;

DWORD lastAnimationTime = 0;
UINT animIntervalIdle = 250;
UINT animIntervalWalk = 150;
UINT animIntervalSleep = 800;
UINT animIntervalWake = 150;
UINT animIntervalFindItem = 250; // faster find-item animation
UINT animIntervalEat = 200;

PetState currentState = STATE_IDLE;
PetState previousState = STATE_IDLE;

std::map<std::string, int> bag;
std::string selectedItemForFeeding = "";
Image* cursorImage = nullptr;

HWND hwndCursorOverlay = NULL;
bool cursorVisible = false;

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
    std::wstring base = L"assets\\bulbasaur\\";
    bulbasaur.idle       = loadGifSafe(base + L"bulbasaur-idle.gif");
    bulbasaur.walkLeft   = loadGifSafe(base + L"bulbasaur-walk-left.gif");
    bulbasaur.walkRight  = loadGifSafe(base + L"bulbasaur-walk-right.gif");
    bulbasaur.sleepLeft  = loadGifSafe(base + L"bulbasaur-sleep-left.gif");
    bulbasaur.sleepRight = loadGifSafe(base + L"bulbasaur-sleep-right.gif");
    bulbasaur.wakeLeft   = loadGifSafe(base + L"bulbasaur-wake-left.gif");
    bulbasaur.wakeRight  = loadGifSafe(base + L"bulbasaur-wake-right.gif");
    bulbasaur.tripLeft   = loadGifSafe(base + L"bulbasaur-trip-left.gif");
    bulbasaur.tripRight  = loadGifSafe(base + L"bulbasaur-trip-right.gif");
    bulbasaur.findItem   = loadGifSafe(base + L"bulbasaur-finditem.gif");
    bulbasaur.eat        = loadGifSafe(base + L"bulbasaur-eat.gif");
    bulbasaur.current = &bulbasaur.idle;
}

void loadData() {
    std::ifstream f("data.json");
    if (f) {
        json j; f >> j;
        if (j.contains("posX")) petPosition.x = j["posX"];
        if (j.contains("posY")) petPosition.y = j["posY"];
        if (j.contains("exploreMode")) exploreMode = j["exploreMode"];
        if (j.contains("bag")) {
            for (auto it = j["bag"].begin(); it != j["bag"].end(); ++it)
                bag[it.key()] = it.value();
        }
    }
}

void saveData() {
    json j;
    j["pokemon"] = selectedPokemon;
    j["posX"] = petPosition.x;
    j["posY"] = petPosition.y;
    j["exploreMode"] = exploreMode;
    j["bag"] = json::object();
    for (auto it = bag.begin(); it != bag.end(); ++it)
        j["bag"][it->first] = it->second;
    std::ofstream f("data.json"); f << j.dump(4);
}

std::wstring humanizeItem(std::string key) {
    std::wstring s(key.begin(), key.end());
    for (auto& c : s) if (c == '-') c = ' ';
    if (!s.empty()) s[0] = towupper(s[0]);
    return s;
}

void CreateCursorOverlay(HINSTANCE hInst) {
    WNDCLASS wc{};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"CursorOverlay";
    RegisterClass(&wc);

    hwndCursorOverlay = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"CursorOverlay", L"BerryOverlay",
        WS_POPUP,
        0, 0, 64, 64,
        NULL, NULL, hInst, NULL
    );
    ShowWindow(hwndCursorOverlay, SW_HIDE);
}

void renderCursorOverlay() {
    if (!cursorImage || !cursorVisible) return;

    POINT cursor;
    GetCursorPos(&cursor);

    int width = cursorImage->GetWidth();
    int height = cursorImage->GetHeight();

    int offsetX = 16; // offset near cursor
    int offsetY = 16;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    Graphics g(mem);
    g.Clear(Color(0, 0, 0, 0));
    g.DrawImage(cursorImage, (REAL)offsetX, (REAL)offsetY, (REAL)width, (REAL)height);

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    SIZE size = { width, height };
    POINT ptSrc = { 0, 0 };
    POINT ptDest = { cursor.x, cursor.y };

    UpdateLayeredWindow(hwndCursorOverlay, screen, &ptDest, &size, mem, &ptSrc, 0, &blend, ULW_ALPHA);
    ShowWindow(hwndCursorOverlay, SW_SHOW);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

void ShowRightClickMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    HMENU hBagMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, 1, exploreMode ? L"Disable Explore Mode" : L"Enable Explore Mode");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hBagMenu, L"Bag");

    int id = 100;
    for (auto it = bag.begin(); it != bag.end(); ++it) {
        std::wstring label = humanizeItem(it->first) + L" x " + std::to_wstring(it->second);
        AppendMenu(hBagMenu, MF_STRING, id++, label.c_str());
    }

    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, 5, L"Return to Pokeball");

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_TOPALIGN | TPM_LEFTALIGN,
        cursor.x, cursor.y, 0, hwnd, NULL);

    if (cmd == 1) exploreMode = !exploreMode;
    else if (cmd >= 100) {
        int idx = cmd - 100;
        int i = 0;
        for (auto it = bag.begin(); it != bag.end(); ++it, ++i) {
            if (i == idx) {
                if (it->second > 0) {
                    selectedItemForFeeding = it->first;
                    std::wstring berryPath = L"assets\\berries\\" + std::wstring(it->first.begin(), it->first.end()) + L".png";
                    if (cursorImage) delete cursorImage;
                    cursorImage = Image::FromFile(berryPath.c_str());
                    cursorVisible = true;
                }
                break;
            }
        }
    } else if (cmd == 5) PostQuitMessage(0);

    DestroyMenu(hMenu);
    saveData();
}

bool isCursorOverBulbasaur() {
    POINT cursor; GetCursorPos(&cursor);
    return cursor.x >= petPosition.x && cursor.x <= petPosition.x + (int)bulbasaur.current->width &&
        cursor.y >= petPosition.y && cursor.y <= petPosition.y + (int)bulbasaur.current->height;
}

void handleFeeding() {
    if (!selectedItemForFeeding.empty() && isCursorOverBulbasaur()) {
        currentState = STATE_EAT;
        bulbasaur.current = &bulbasaur.eat;
        bulbasaur.current->currentFrame = 0;

        bag[selectedItemForFeeding]--;
        if (bag[selectedItemForFeeding] <= 0)
            bag.erase(selectedItemForFeeding);

        std::wstring eatPath = L"assets\\berries\\" +
            std::wstring(selectedItemForFeeding.begin(), selectedItemForFeeding.end()) +
            L"-eat.gif";

        if (cursorImage) delete cursorImage;
        cursorImage = Image::FromFile(eatPath.c_str());

        selectedItemForFeeding.clear();
    }
}

void trySpawnItem() {
    if (!exploreMode) return;
    int chance = rand() % 400;
    if (chance < 2 && currentState != STATE_FINDITEM) {
        std::vector<std::string> items = { "oran-berry", "sitrus-berry", "pecha-berry", "pokeball" };
        std::string item = items[rand() % items.size()];
        bag[item]++;
        currentState = STATE_FINDITEM;
        bulbasaur.current = &bulbasaur.findItem;
        bulbasaur.current->currentFrame = 0;
    }
}

void renderPokemon(HWND hwnd) {
    PokemonGIF* pg = bulbasaur.current;
    if (!pg || !pg->img) return;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, (int)pg->width, (int)pg->height);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    Graphics g(mem);
    g.Clear(Color(0, 0, 0, 0));
    g.DrawImage(pg->img, 0.0f, 0.0f, pg->width, pg->height);

    POINT ptDest = { petPosition.x, petPosition.y };
    SIZE sizeWnd = { (LONG)pg->width, (LONG)pg->height };
    POINT ptSrc = { 0,0 };

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
        trySpawnItem();
        handleFeeding();

        switch (currentState) {
        case STATE_IDLE:
            if (bulbasaur.current != &bulbasaur.idle) {
                bulbasaur.current = &bulbasaur.idle;
                bulbasaur.current->currentFrame = 0;
            }
            if (now - lastAnimationTime >= animIntervalIdle) {
                lastAnimationTime = now;
                advanceFrame(*bulbasaur.current);
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
                petPosition.x += movingRight ? moveSpeed : -moveSpeed;
            }
            break;

        case STATE_FINDITEM:
            if (now - lastAnimationTime >= animIntervalFindItem) {
                lastAnimationTime = now;
                bool done = advanceFrame(*bulbasaur.current);
                if (done) currentState = STATE_IDLE;
            }
            break;

        case STATE_EAT:
            if (now - lastAnimationTime >= animIntervalEat) {
                lastAnimationTime = now;
                bool done = advanceFrame(*bulbasaur.current);
                if (done) {
                    currentState = STATE_IDLE;
                    if (cursorImage) { delete cursorImage; cursorImage = nullptr; }
                    ShowWindow(hwndCursorOverlay, SW_HIDE);
                    cursorVisible = false;
                }
            }
            break;
        }

        renderPokemon(hwnd);
        if (cursorVisible) renderCursorOverlay();
        SetWindowPos(hwnd, HWND_TOPMOST, petPosition.x, petPosition.y, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        saveData();
        return 0;
    }

    case WM_LBUTTONDOWN:
        lastInteraction = GetTickCount();
        if (currentState == STATE_IDLE) {
            currentState = STATE_WALK;
            movingRight = rand() % 2;
        }
        break;

    case WM_RBUTTONDOWN:
        ShowRightClickMenu(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    GdiplusStartupInput gsi;
    GdiplusStartup(&gdiplusToken, &gsi, NULL);

    loadData();
    loadBulbasaur();

    if (petPosition.x == -1 || petPosition.y == -1) {
        RECT r;
        HWND taskbar = FindWindow(L"Shell_TrayWnd", NULL);
        GetWindowRect(taskbar, &r);
        petPosition.x = r.right - (int)bulbasaur.current->width - 80;
        int h = r.bottom - r.top;
        petPosition.y = r.top + h - (int)bulbasaur.current->height + 2;
        saveData();
    }

    WNDCLASS wc{};
    wc.lpfnWndProc = PetProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"PetWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetWindow", L"PokeBuddy", WS_POPUP, petPosition.x, petPosition.y,
        (int)bulbasaur.current->width, (int)bulbasaur.current->height,
        NULL, NULL, hInst, NULL);

    CreateCursorOverlay(hInst);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP + 1;
    HICON hIcon = (HICON)LoadImage(NULL, L"assets\\pokeball.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
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
