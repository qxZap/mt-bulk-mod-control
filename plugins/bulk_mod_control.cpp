// Bulk Mod Control -- plugins\mt_plugin_BulkModControl.dll (was Mod Switch, renamed 2026-11-20).
//
// Main menu > Mods [Review] gets two more buttons beside Keep Disabled / Restore Previous Set: Enable All and
// Disable All (the user, 2026-11-20). The list is native (MTLoadedModListWidget, rows MTLoadedModRowWidget) and keeps
// its state in Saved\Config\Windows\CustomPak.ini [CustomPakStateV2]; this changes nothing there itself. A click
// presses each row's own button (ActionButton) whose next-launch state is the other one, the way a player would, so
// the game does its own bookkeeping. Rows are re-read after each pass: the list may rebuild itself after a press.
//
// The two buttons are copies of the list's own button class, added to the row Keep Disabled sits in.
// Settings: plugins\bulk_mod_control.ini, [General] Disabled.

#include <string>
#include <vector>
#include "../include/mt_plugin.hpp"

using namespace mt;

static const wchar_t* INI = L"bulk_mod_control.ini";
struct Ref { int index; uintptr_t ptr; };
static bool alive(const Ref& r) { return r.ptr && api->object(r.index) == r.ptr; }
static Ref refOf(uintptr_t o) { return {o ? get<int>(o + L().obj_index) : 0, o}; }
static bool usable(const Ref& r) { return alive(r) && !(get<uint32_t>(r.ptr + L().obj_flags) & 0x18000); }
struct FStr { const wchar_t* data; int num, max; };
static FStr str(const wchar_t* s) { int n = (int)wcslen(s) + 1; return {s, n, n}; }

static uintptr_t cList, cRow, cButton, cTextBlock, cPanel, widgetCDO, textCDO;
static uintptr_t fCreate, fAddChild, fChildAt, fChildCount, fChildIndex, fPressed, fSetText, fToText, fToStr, fHPad;
static int oKeep, oListBox, oNext, oAction, oBtnText, oBtnName, oBtnInner, oBtnClicked, oSlot, oSlotParent, oTbText;
static int pTextIn, pTextRet, pStrIn, pStrRet, oUBtnClicked = -1;
static bool ready;

static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string textOf(uintptr_t ftext) {  // FText at an address -> string (Conv_TextToString; the FString leaks, a few per click)
    unsigned char p[0x40] = {0};
    if (!api->read(ftext, p + pStrIn, pStrRet - pStrIn)) return "";
    api->process_event(textCDO, fToStr, p);
    uintptr_t d; int n; memcpy(&d, p + pStrRet, 8); memcpy(&n, p + pStrRet + 8, 4);
    std::string out;
    if (!d || n <= 0 || n > 4096) return out;
    std::vector<wchar_t> w(n);
    if (!api->read(d, w.data(), (size_t)n * 2)) return out;
    for (int i = 0; i < n && w[i]; i++) out += w[i] < 128 ? (char)w[i] : '?';
    return out;
}
static std::string blockText(uintptr_t tb) { return tb ? textOf(tb + oTbText) : ""; }
static void setText(uintptr_t tb, const wchar_t* s) {
    if (!tb) return;
    unsigned char p[0x40] = {0};
    FStr t = str(s);
    memcpy(p + pTextIn, &t, sizeof t);
    api->process_event(textCDO, fToText, p);
    api->process_event(tb, fSetText, p + pTextRet);
}
static bool flag(uintptr_t obj, uintptr_t fn) { unsigned char p[0x10] = {0}; api->process_event(obj, fn, p); return p[0] != 0; }
static int childCount(uintptr_t panel) { int n = 0; api->process_event(panel, fChildCount, &n); return n; }
static uintptr_t childAt(uintptr_t panel, int i) { struct { int i; uintptr_t ret; } a = {i, 0}; api->process_event(panel, fChildAt, &a); return a.ret; }
static int childIndex(uintptr_t panel, uintptr_t w) { struct { uintptr_t w; int ret; } a = {w, -1}; if (w) api->process_event(panel, fChildIndex, &a); return a.ret; }

// Call what a dynamic multicast delegate is bound to (TArray<FScriptDelegate>, each {FWeakObjectPtr object (index,
// serial), FName function}).
static int fire(uintptr_t at) {
    uintptr_t d = get<uintptr_t>(at); int n = get<int>(at + 8), called = 0;
    for (int i = 0; i < n && i < 8; i++) {
        uintptr_t e = d + (uintptr_t)i * 16;
        uintptr_t obj = api->object(get<int>(e));
        std::string fn = fname(e + 8);
        if (!obj || fn.empty() || fn == "None") continue;
        uintptr_t f = 0;
        for (uintptr_t c = classOf(obj); c && !f; c = get<uintptr_t>(c + L().struct_super)) f = api->find_object("Function", fn.c_str(), objName(c).c_str());
        if (!f) { MT_LOG("press: %s.%s not found", objName(classOf(obj)).c_str(), fn.c_str()); continue; }
        unsigned char p[0x100] = {0};
        api->process_event(obj, f, p);
        called++;
    }
    return called;
}
// Press an MTButtonWidget the way a click does: its inner UButton's OnClicked (2026-11-20: the row listens to the
// widget through a native delegate, so the widget's own OnClicked list is empty and firing it pressed nothing).
static int press(uintptr_t btn) {
    uintptr_t in = get<uintptr_t>(btn + oBtnInner);
    int n = in && oUBtnClicked >= 0 ? fire(in + oUBtnClicked) : 0;
    if (!n) n = fire(btn + oBtnClicked);
    static bool told;
    if (!n && !told) { told = true; MT_LOG("press: nothing is bound to the row button's click"); }
    return n;
}

static Ref list;                      // the open mod list
static Ref made[2];                   // Enable All, Disable All
static uintptr_t inner[2]; static bool held[2]; static int labelFrames;
static int want = -1, passes, wait;   // 0 enable all, 1 disable all
static const wchar_t* const LABEL[2] = {L"Enable All", L"Disable All"};

static void buildButtons(uintptr_t l) {
    uintptr_t keep = get<uintptr_t>(l + oKeep);
    uintptr_t slot = keep ? get<uintptr_t>(keep + oSlot) : 0, row = slot ? get<uintptr_t>(slot + oSlotParent) : 0;
    if (!row) { MT_LOG("the row the review buttons sit in was not found -- no buttons"); return; }
    uintptr_t cls = classOf(keep);
    for (int b = 0; b < 2; b++) {
        if (usable(made[b]) && childIndex(row, made[b].ptr) >= 0) continue;
        struct { uintptr_t context, cls, player, ret; } c = {l, cls, 0, 0};
        api->process_event(widgetCDO, fCreate, &c);
        if (!c.ret) { MT_LOG("could not create a button"); return; }
        unsigned char p[0x40] = {0};
        FStr t = str(LABEL[b]);
        memcpy(p + pTextIn, &t, sizeof t);
        api->process_event(textCDO, fToText, p);
        wr(c.ret + oBtnText, p + pTextRet, 16);  // moved in raw: nothing destroys the buffer
        struct { uintptr_t content, slot; } add = {c.ret, 0};
        api->process_event(row, fAddChild, &add);
        float pad[4] = {8, 0, 0, 0};
        if (add.slot && fHPad) api->process_event(add.slot, fHPad, pad);
        made[b] = refOf(c.ret);
        inner[b] = get<uintptr_t>(c.ret + oBtnInner);
        held[b] = false;
    }
    labelFrames = 60;
    MT_LOG("Enable All / Disable All added to the mod list");
}

// One pass over the rows: press every row whose next-launch state is not the wanted one. Returns rows pressed.
static int pass(uintptr_t l, int w) {
    uintptr_t box = get<uintptr_t>(l + oListBox);
    if (!box) return 0;
    int pressed = 0, n = childCount(box);
    static bool told;
    for (int i = 0; i < n && i < 512; i++) {
        uintptr_t r = childAt(box, i);
        if (!r || !api->is_a(classOf(r), cRow)) continue;
        std::string next = lower(blockText(get<uintptr_t>(r + oNext)));
        uintptr_t btn = get<uintptr_t>(r + oAction);
        if (!told) MT_LOG("row %d: next launch \"%s\", button \"%s\"", i, next.c_str(), btn ? textOf(btn + oBtnText).c_str() : "-");
        bool off = next.find("disab") != std::string::npos || next == "off",
             on = !off && (next.find("enab") != std::string::npos || next == "on");
        if (!btn || !(w == 0 ? off : on)) continue;
        if (press(btn)) pressed++;
    }
    told = true;
    return pressed;
}

static void tick(void*) {
    if (!usable(list)) { list = {}; want = -1; return; }
    uintptr_t l = list.ptr;
    if (!usable(made[0]) || !usable(made[1])) buildButtons(l);
    if (labelFrames > 0) {  // the button writes its own name over the text for a while after it is built
        labelFrames--;
        for (int b = 0; b < 2; b++) if (usable(made[b])) setText(get<uintptr_t>(made[b].ptr + oBtnName), LABEL[b]);
    }
    for (int b = 0; b < 2; b++) {
        if (!usable(made[b]) || !inner[b]) continue;
        bool down = flag(inner[b], fPressed), up = held[b] && !down;
        held[b] = down;
        if (up) { want = b; passes = 0; wait = 0; MT_LOG("%ls", LABEL[b]); }
    }
    if (want < 0 || wait-- > 0) return;
    int n = pass(l, want);
    if (n) MT_LOG("%ls: pressed %d row(s)", LABEL[want], n);
    if (!n || ++passes >= 6) want = -1;  // done, or the list keeps disagreeing: stop rather than toggle forever
    wait = 10;                            // half a second for the rows to show their new state
}

static int onConstruct(uintptr_t obj, uintptr_t, void*, void*) {
    if (obj && api->is_a(classOf(obj), cList) && !isCDO(obj)) { list = refOf(obj); made[0] = made[1] = {}; want = -1; }
    return 0;
}

static DWORD WINAPI run(LPVOID) {
    if (!api->wait_engine(INFINITE)) return 0;
    cList = api->find_object("Class", "MTLoadedModListWidget", "/Script/MotorTown");
    cRow = api->find_object("Class", "MTLoadedModRowWidget", "/Script/MotorTown");
    cButton = api->find_object("Class", "MTButtonWidget", "/Script/MotorTown");
    cTextBlock = api->find_object("Class", "TextBlock", "/Script/UMG");
    uintptr_t cWidget = api->find_object("Class", "Widget", "/Script/UMG"), cSlot = api->find_object("Class", "PanelSlot", "/Script/UMG");
    widgetCDO = api->find_object("WidgetBlueprintLibrary", "Default__WidgetBlueprintLibrary", "/Script/UMG");
    textCDO = api->find_object("KismetTextLibrary", "Default__KismetTextLibrary", "/Script/Engine");
    fCreate = api->find_object("Function", "Create", "WidgetBlueprintLibrary");
    fAddChild = api->find_object("Function", "AddChild", "PanelWidget");
    fChildAt = api->find_object("Function", "GetChildAt", "PanelWidget");
    fChildCount = api->find_object("Function", "GetChildrenCount", "PanelWidget");
    fChildIndex = api->find_object("Function", "GetChildIndex", "PanelWidget");
    fPressed = api->find_object("Function", "IsPressed", "Button");
    fSetText = api->find_object("Function", "SetText", "TextBlock");
    fToText = api->find_object("Function", "Conv_StringToText", "KismetTextLibrary");
    fToStr = api->find_object("Function", "Conv_TextToString", "KismetTextLibrary");
    fHPad = api->find_object("Function", "SetPadding", "HorizontalBoxSlot");
    ready = cList && cRow && cButton && cTextBlock && cWidget && cSlot && widgetCDO && textCDO && fCreate && fAddChild && fChildAt &&
            fChildCount && fChildIndex && fPressed && fSetText && fToText && fToStr;
    if (ready) {
        oKeep = off(cList, "KeepDisabledButton"); oListBox = off(cList, "ListBox");
        oNext = off(cRow, "NextLaunchStateTextBlock"); oAction = off(cRow, "ActionButton");
        oBtnText = off(cButton, "ButtonName"); oBtnName = off(cButton, "NameTextBlock"); oBtnInner = off(cButton, "MenuButton"); oBtnClicked = off(cButton, "OnClicked");
        if (uintptr_t cUB = api->find_object("Class", "Button", "/Script/UMG")) oUBtnClicked = off(cUB, "OnClicked");
        oSlot = off(cWidget, "Slot"); oSlotParent = off(cSlot, "Parent"); oTbText = off(cTextBlock, "Text");
        pTextIn = off(fToText, "InString"); pTextRet = off(fToText, "ReturnValue"); pStrIn = off(fToStr, "InText"); pStrRet = off(fToStr, "ReturnValue");
        for (int o : {oKeep, oListBox, oNext, oAction, oBtnText, oBtnName, oBtnInner, oBtnClicked, oSlot, oSlotParent, oTbText, pTextIn, pTextRet, pStrIn, pStrRet})
            ready = ready && o >= 0;
    }
    if (!ready) { MT_LOG("FAILED: the mod list's layout was not found -- no buttons"); return 0; }
    static const char* kC[] = {"Construct"};
    for (int i = 0; i < 120 && !api->watch_functions(kC, 1, onConstruct, nullptr); i++) Sleep(500);
    MT_LOG("ready: Enable All / Disable All go on Mods [Review]");
    while (!api->stopping()) {
        if (list.ptr) api->run_on_game_thread(tick, nullptr);
        Sleep(50);
    }
    return 0;
}

static void demo() {  // the state words, without a game
    auto isOff = [](std::string s) { s = lower(s); return s.find("disab") != std::string::npos || s == "off"; };
    if (!isOff("Disabled") || isOff("Enabled") || !isOff("OFF")) MT_LOG("SELF-CHECK FAILED: state words");
}

extern "C" __declspec(dllexport) int PluginInit(const MtApi* a) {
    if (!attach(a)) return 1;
    if (iniDisabled(INI)) return 0;
    demo();
    CloseHandle(CreateThread(nullptr, 0, run, nullptr, 0, nullptr));
    return 0;
}
