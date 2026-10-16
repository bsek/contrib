/*
 * Zune WebView class for WebKitLegacy (replaces MorphOS's Objective-C WkWebView).
 * Drives WebKit::WebPage directly: draw/input go straight to the page, and
 * WebKit's main-thread work runs from a signal plus a timer input handler.
 */

// WebKit headers first: AROS's exec/debug macros (Exception(), D(), ...) must
// not be defined while they are parsed.
#include "WebKit.h"
#include "WebPage.h"
#include "WebProcess.h"
#include <WebCore/AuthenticationChallenge.h>
#include <WebCore/AuthenticationClient.h>
#include <WebCore/Credential.h>
#include <WebCore/FindOptions.h>
#include <WebCore/PageIdentifier.h>
#include <WebCore/PointerTypesAROS.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/WindowFeatures.h>
#include <wtf/MonotonicTime.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringConcatenate.h>

#include <proto/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <zune/customclasses.h>
#include <cybergraphx/cybergraphics.h>
#include <devices/rawkeycodes.h>
#include <intuition/pointerclass.h>

#include <algorithm>
#include <stdlib.h>
#include <string.h>

#include "WebViewZune.h"
#include "WebViewZune_private.h"

// Private methods run through the application's event dispatch.
#define MUIM_WebView_Signal   (MUIB_WebView | 0x0201)
#define MUIM_WebView_LateDraw (MUIB_WebView | 0x0202)
#define MUIM_WebView_LateScroll (MUIB_WebView | 0x0203)
// Private attribute: the view's WebKit::WebPage, for pages opened by window.open.
#define MUIA_WebView_Page (MUIB_WebView | 0x0220)

// Return IDs of the view's modal windows.
enum { RID_Cancel = 0x57CB0100, RID_OK, RID_Select };

// Zune's input-handler timer is fixed-interval: poll WebKit timers at 50 Hz.
static const ULONG timerMillis = 20;

// Only OpenSSL's socket BIOs use the global bsdsocket base; curl does the
// networking with per-thread bases and feeds OpenSSL through its own BIO.
// Weak, so applications that open SocketBase themselves keep theirs.
extern "C" { __attribute__((weak)) struct Library* SocketBase = nullptr; }

static LONG s_sigBit = -1;

// Pages of all views, for settings that apply to every view.
static WTF::Vector<WebKit::WebPage*> s_pages;

void webkittyForEachPage(const WTF::Function<void(WebKit::WebPage&)>& function)
{
    for (auto* page : s_pages)
        function(*page);
}

// Modal windows run while WebKit is on the stack; its run loop must wait.
static int s_modal;

// Custom pointers; AROS only has the default and busy pointers built in.
enum { PointerHand, PointerText, PointerCount };
static Object* s_pointers[PointerCount];
static struct BitMap* s_pointerBitMaps[PointerCount];

// Load progress goes to serial (WTF's MorphOS-style debug printer).
extern "C" void wkAROSDebugPrintf(const char* format, ...);
#define WKVIEW_LOG(...) wkAROSDebugPrintf("[WebView] " __VA_ARGS__)

enum { HookCreateNew, HookAlert, HookConfirm, HookPrompt, HookPolicy, HookCredentials, HookClose, HookCount };

// The engine matches a current Safari; sites gate features on the version
static const char s_defaultUserAgent[] =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.6 Safari/605.1.15";

struct WebView_DATA {
    WebKit::WebPage* page;
    struct MUI_EventHandlerNode ehn;
    struct MUI_InputHandlerNode signalIhn;
    struct MUI_InputHandlerNode timerIhn;
    char* url;
    char* title;
    char* toolTip;
    char* errorText;
    char* userAgent; // NULL: s_defaultUserAgent
    IPTR state;
    LONG progress;
    BOOL canGoBack, canGoForward;
    BOOL active;
    BOOL sourceMode;
    float zoom;
    struct Hook* hooks[HookCount];
    // window.open features, from _fCanOpenWindow to _fDoOpenWindow
    WebCore::WindowFeatures* openFeatures;
    LONG docWidth, docHeight, visWidth, visHeight, scrollX, scrollY;
    int cursor;
    WTF::MonotonicTime loadStart;
    WTF::MonotonicTime navigationStart;
    BOOL setup;
    BOOL shown;
    BOOL fullScreen;
    BOOL closeKeyWasDisabled;
    BOOL redrawPending;
    BOOL scrollPending;
};

// Esc closes the window (MUIKEY_WINDOW_CLOSE) before the view sees it; while
// an element is full screen, Esc leaves full screen instead.
static void setFullScreen(Object* obj, WebView_DATA* data, BOOL on)
{
    if (on == data->fullScreen)
        return;
    data->fullScreen = on;
    if (!data->setup)
        return;
    const IPTR closeKey = 1UL << MUIKEY_WINDOW_CLOSE;
    IPTR keys = 0;
    GetAttr(MUIA_Window_DisableKeys, _win(obj), &keys);
    if (on) {
        data->closeKeyWasDisabled = (keys & closeKey) != 0;
        keys |= closeKey;
    } else if (!data->closeKeyWasDisabled)
        keys &= ~closeKey;
    SetAttrs(_win(obj), MUIA_Window_DisableKeys, keys, TAG_DONE);
}

static bool ensureWebProcess()
{
    if (s_sigBit >= 0)
        return true;
    s_sigBit = AllocSignal(-1);
    if (s_sigBit < 0)
        return false;
    WebKit::WebProcess::singleton().initialize(int(s_sigBit));
    // Load address, to symbolize crash dumps against the ELF file
    WKVIEW_LOG("ensureWebProcess at %p\n", (void*)&ensureWebProcess);
    return true;
}

// Notifying string setter. Listeners (e.g. a window title) may keep the
// pointer, so the old copy is freed only after the new one is published.
static void setString(Object* obj, char*& field, ULONG attr, const WTF::String& value)
{
    char* old = field;
    field = strdup(value.utf8().data());
    SetAttrs(obj, attr, (IPTR)field, TAG_DONE);
    free(old);
}

// IPTR, not LONG: the MUIV_ values have bit 31 set and must not sign-extend.
static void setState(Object* obj, IPTR state)
{
    SetAttrs(obj, MUIA_WebView_State, state, TAG_DONE);
}

static void updateHistory(Object* obj, WebView_DATA* data)
{
    if (!data->page)
        return;
    BOOL back = data->page->canGoBack(), forward = data->page->canGoForward();
    if (back != data->canGoBack || forward != data->canGoForward)
        SetAttrs(obj, MUIA_WebView_CanGoBack, back, MUIA_WebView_CanGoForward, forward, TAG_DONE);
}

static IPTR callHook(Object* obj, WebView_DATA* data, int hook, APTR msg)
{
    return CallHookPkt(data->hooks[hook], obj, msg);
}

// Opens a view through CreateNewHook: spec NULL asks for a tab.
static Object* openNewView(Object* obj, WebView_DATA* data, struct WindowSpecification* spec)
{
    if (!data->hooks[HookCreateNew])
        return nullptr;
    return (Object*)callHook(obj, data, HookCreateNew, &spec);
}

// WebKit may ask for a repaint while it is laying out; paint from the
// application loop instead of recursing into MUI_Redraw.
static void scheduleRedraw(Object* obj, WebView_DATA* data)
{
    if (!data->setup || data->redrawPending)
        return;
    data->redrawPending = TRUE;
    DoMethod(_app(obj), MUIM_Application_PushMethod, (IPTR)obj, 1, MUIM_WebView_LateDraw);
}

// Runs dlg until it returns one of the RID_* IDs. The view's window sleeps
// meanwhile; a quit request is handed back to the application's loop.
static IPTR runModal(Object* obj, Object* dlg)
{
    Object* app = _app(obj);
    Object* win = _win(obj);
    IPTR result = RID_Cancel;
    bool quit = false;

    DoMethod(app, OM_ADDMEMBER, (IPTR)dlg);
    SetAttrs(win, MUIA_Window_Sleep, TRUE, TAG_DONE);
    s_modal++;
    SetAttrs(dlg, MUIA_Window_Open, TRUE, TAG_DONE);
    IPTR open = FALSE;
    GetAttr(MUIA_Window_Open, dlg, &open);
    ULONG sigs = 0;
    while (open) {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (id == MUIV_Application_ReturnID_Quit) {
            quit = true;
            break;
        }
        if (id >= RID_Cancel && id <= RID_Select) {
            result = id;
            break;
        }
        if (sigs)
            sigs = Wait(sigs);
    }
    SetAttrs(dlg, MUIA_Window_Open, FALSE, TAG_DONE);
    s_modal--;
    SetAttrs(win, MUIA_Window_Sleep, FALSE, TAG_DONE);
    DoMethod(app, OM_REMMEMBER, (IPTR)dlg);
    if (quit)
        DoMethod(app, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    return result;
}

// JavaScript alert/confirm/prompt; input, if given, gets a string field.
static bool runDialog(Object* obj, const WTF::String& message, bool cancel, WTF::String* input)
{
    auto text = message.latin1();
    auto initial = input ? input->latin1() : CString();
    Object *ok, *cancelButton = nullptr, *string = nullptr;
    Object* dlg = WindowObject,
        MUIA_Window_Title, (IPTR)"JavaScript",
        MUIA_Window_RefWindow, (IPTR)_win(obj),
        MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered,
        MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered,
        WindowContents, (IPTR)(VGroup,
            Child, (IPTR)(TextObject, MUIA_Text_Contents, (IPTR)text.data(), End),
            input ? Child : TAG_IGNORE, (IPTR)(string = input ? StringObject,
                StringFrame,
                MUIA_String_MaxLen, 1024,
                MUIA_String_Contents, (IPTR)initial.data(),
                MUIA_CycleChain, TRUE,
            End : nullptr),
            Child, (IPTR)(HGroup,
                Child, (IPTR)(ok = SimpleButton("_OK")),
                Child, (IPTR)HSpace(0),
                cancel ? Child : TAG_IGNORE, (IPTR)(cancelButton = cancel ? SimpleButton("_Cancel") : nullptr),
            End),
        End),
    End;
    if (!dlg)
        return false;

    Object* app = _app(obj);
    DoMethod(ok, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, RID_OK);
    if (cancelButton)
        DoMethod(cancelButton, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, RID_Cancel);
    if (string)
        DoMethod(string, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, (IPTR)app, 2, MUIM_Application_ReturnID, RID_OK);
    DoMethod(dlg, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2, MUIM_Application_ReturnID, RID_Cancel);
    SetAttrs(dlg, MUIA_Window_ActiveObject, (IPTR)(string ? string : ok), TAG_DONE);

    bool accepted = runModal(obj, dlg) == RID_OK;
    if (accepted && string) {
        IPTR contents = 0;
        GetAttr(MUIA_String_Contents, string, &contents);
        *input = WTF::String::fromLatin1((const char*)contents);
    }
    MUI_DisposeObject(dlg);
    return accepted;
}

// Popup list (<select>, context menu) below or above pos, in view
// coordinates. Returns the chosen index, -1 if dismissed.
static int runPopup(Object* obj, const WebCore::IntRect& pos, const WTF::Vector<WTF::String>& items)
{
    struct Window* w = _window(obj);
    struct Screen* scr = _screen(obj);
    const LONG maxRows = 16;
    LONG rows = std::min<LONG>(items.size(), maxRows);
    LONG height = rows * (_font(obj)->tf_YSize + 2) + 8;
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, _font(obj));
    LONG width = pos.width();
    for (auto& item : items) {
        auto text = item.latin1();
        width = std::max<LONG>(width, TextLength(&rp, text.data(), text.length()) + 16 + (rows < LONG(items.size()) ? 20 : 0));
    }
    LONG left = w->LeftEdge + _mleft(obj) + pos.x();
    LONG top = w->TopEdge + _mtop(obj) + pos.maxY();
    if (top + height > scr->Height)
        top = std::max<LONG>(0, top - pos.height() - height);

    Object* list;
    Object* dlg = WindowObject,
        MUIA_Window_Borderless, TRUE,
        MUIA_Window_CloseGadget, FALSE,
        MUIA_Window_DepthGadget, FALSE,
        MUIA_Window_SizeGadget, FALSE,
        MUIA_Window_DragBar, FALSE,
        MUIA_Window_LeftEdge, left,
        MUIA_Window_TopEdge, top,
        MUIA_Window_Width, width,
        MUIA_Window_Height, height,
        WindowContents, (IPTR)(ListviewObject,
            MUIA_Listview_ScrollerPos, LONG(items.size()) > maxRows ? MUIV_Listview_ScrollerPos_Default : MUIV_Listview_ScrollerPos_None,
            MUIA_Listview_List, (IPTR)(list = ListObject,
                InputListFrame,
                MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                MUIA_List_DestructHook, MUIV_List_DestructHook_String,
            End),
        End),
    End;
    if (!dlg)
        return -1;

    for (auto& item : items)
        DoMethod(list, MUIM_List_InsertSingle, (IPTR)item.latin1().data(), MUIV_List_Insert_Bottom);
    SetAttrs(list, MUIA_List_Active, MUIV_List_Active_Off, TAG_DONE);

    Object* app = _app(obj);
    DoMethod(list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (IPTR)app, 2, MUIM_Application_ReturnID, RID_Select);
    DoMethod(dlg, MUIM_Notify, MUIA_Window_Activate, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, RID_Cancel);
    DoMethod(dlg, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2, MUIM_Application_ReturnID, RID_Cancel);
    SetAttrs(dlg, MUIA_Window_ActiveObject, (IPTR)list, TAG_DONE);

    IPTR active = MUIV_List_Active_Off;
    if (runModal(obj, dlg) == RID_Select)
        GetAttr(MUIA_List_Active, list, &active);
    MUI_DisposeObject(dlg);
    return LONG(active) >= 0 ? int(active) : -1;
}

// Flattens WebKit's context menu for runPopup(); submenu entries indented.
static void collectMenuItems(const WTF::Vector<WebCore::ContextMenuItem>& items, const char* indent,
    WTF::Vector<WTF::String>& titles, WTF::Vector<const WebCore::ContextMenuItem*>& picks)
{
    for (auto& item : items) {
        if (!item.enabled())
            continue;
        switch (item.type()) {
        case WebCore::ContextMenuItemType::Action:
        case WebCore::ContextMenuItemType::CheckableAction:
            titles.append(makeString(WTF::String::fromLatin1(indent), item.checked() ? "* "_s : ""_s, item.title()));
            picks.append(&item);
            break;
        case WebCore::ContextMenuItemType::Submenu:
            collectMenuItems(item.subMenuItems(), "   ", titles, picks);
            break;
        case WebCore::ContextMenuItemType::Separator:
            break;
        }
    }
}

// 16x16 ARGB pointer: 'x' black, 'o' white. Pen-based pointers would take
// the (user-set) pointer colours, so these carry their own.
static Object* makePointer(int index, const char* const rows[16], int hotX, int hotY)
{
    ULONG pixels[16 * 16];
    for (int i = 0; i < 16 * 16; i++) {
        char c = rows[i / 16][i % 16];
        pixels[i] = c == 'x' ? 0xFF000000 : c == 'o' ? 0xFFFFFFFF : 0;
    }
    struct BitMap* bm = AllocBitMap(16, 16, 32, BMF_CLEAR | BMF_SPECIALFMT | SHIFT_PIXFMT(PIXFMT_ARGB32), NULL);
    if (!bm)
        return nullptr;
    struct RastPort rp;
    InitRastPort(&rp);
    rp.BitMap = bm;
    // Little-endian ULONG 0xAARRGGBB is BGRA in memory.
    WritePixelArray(pixels, 0, 0, 16 * 4, &rp, 0, 0, 16, 16, RECTFMT_BGRA32);
    s_pointerBitMaps[index] = bm;
    return (Object*)NewObject(NULL, (UBYTE*)POINTERCLASS,
        POINTERA_BitMap, (IPTR)bm,
        POINTERA_XOffset, -hotX,
        POINTERA_YOffset, -hotY,
        TAG_DONE);
}

static Object* pointerFor(int type)
{
    static const char* const hand[16] = {
        ".....xx.........",
        "....xoox........",
        "....xoox........",
        "....xoox........",
        "....xooxxx......",
        "....xooxooxxx...",
        "....xooxooxooxx.",
        ".xx.xooxooxooxox",
        "xooxxoooooooooox",
        "xoooxoooooooooox",
        ".xooooooooooooox",
        "..xoooooooooooox",
        "..xooooooooooox.",
        "...xoooooooooox.",
        "....xoooooooox..",
        "....xxxxxxxxxx..",
    };
    static const char* const text[16] = {
        "xxx.xxx.........",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "...x............",
        "xxx.xxx.........",
        "................",
    };
    int index = type == POINTERTYPE_SELECTLINK ? PointerHand : PointerText;
    if (!s_pointers[index])
        s_pointers[index] = index == PointerHand ? makePointer(index, hand, 5, 0) : makePointer(index, text, 3, 7);
    return s_pointers[index];
}

static void setCursor(Object* obj, WebView_DATA* data, int type)
{
    if (!data->setup || type == data->cursor)
        return;
    data->cursor = type;
    switch (type) {
    case POINTERTYPE_BUSY:
    case POINTERTYPE_WORKING:
        SetWindowPointer(_window(obj), WA_BusyPointer, TRUE, TAG_DONE);
        break;
    case POINTERTYPE_SELECTLINK:
    case POINTERTYPE_SELECTTEXT:
        SetWindowPointer(_window(obj), WA_Pointer, (IPTR)pointerFor(type), TAG_DONE);
        break;
    default:
        SetWindowPointer(_window(obj), WA_Pointer, (IPTR)NULL, TAG_DONE);
        break;
    }
}

static void setHookAttr(WebView_DATA* data, struct TagItem* tag)
{
    switch (tag->ti_Tag) {
    case MUIA_WebView_CreateNewHook: data->hooks[HookCreateNew] = (struct Hook*)tag->ti_Data; break;
    case MUIA_WebView_AlertHook: data->hooks[HookAlert] = (struct Hook*)tag->ti_Data; break;
    case MUIA_WebView_ConfirmHook: data->hooks[HookConfirm] = (struct Hook*)tag->ti_Data; break;
    case MUIA_WebView_PromptHook: data->hooks[HookPrompt] = (struct Hook*)tag->ti_Data; break;
    case MUIA_WebView_PolicyHook: data->hooks[HookPolicy] = (struct Hook*)tag->ti_Data; break;
    case MUIA_WebView_CredentialsHook: data->hooks[HookCredentials] = (struct Hook*)tag->ti_Data; break;
    case MUIA_WebView_CloseHook: data->hooks[HookClose] = (struct Hook*)tag->ti_Data; break;
    case MUIA_WebView_SourceMode: data->sourceMode = tag->ti_Data; break;
    }
}

static IPTR WebView__OM_NEW(Class* cl, Object* obj, struct opSet* msg)
{
    if (!ensureWebProcess())
        return 0;

    obj = (Object*)DoSuperNewTags(cl, obj, NULL,
        MUIA_FillArea, FALSE,
        MUIA_CycleChain, TRUE,
        // Makes Zune trap the right button over the view; see ContextMenuBuild.
        MUIA_ContextMenu, TRUE,
        TAG_MORE, (IPTR)msg->ops_AttrList);
    if (!obj)
        return 0;

    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    data->state = MUIV_WebView_State_Ready;
    data->zoom = 1.0f;
    {
        struct TagItem* tstate = msg->ops_AttrList;
        struct TagItem* tag;
        while ((tag = NextTagItem(&tstate)))
            setHookAttr(data, tag);
    }
    auto& process = WebKit::WebProcess::singleton();
    auto identifier = WebCore::PageIdentifier::generate();
    WebKit::WebPageCreationParameters parameters;
    process.createWebPage(identifier, WTF::move(parameters));
    data->page = process.webPage(identifier);
    if (!data->page) {
        CoerceMethod(cl, obj, OM_DISPOSE);
        return 0;
    }

    auto* page = data->page;
    page->_fInvalidate = [obj, data](bool) { scheduleRedraw(obj, data); };
    // Stored first, so OM_SET does not scroll the page back.
    page->_fScroll = [obj, data](int x, int y) {
        data->scrollX = x;
        data->scrollY = y;
        SetAttrs(obj, MUIA_WebView_ScrollX, x, MUIA_WebView_ScrollY, y, TAG_DONE);
        scheduleRedraw(obj, data);
    };
    page->_fSetDocumentSize = [obj, data](int width, int height) {
        data->docWidth = width;
        data->docHeight = height;
        SetAttrs(obj, MUIA_WebView_DocumentWidth, width, MUIA_WebView_DocumentHeight, height, TAG_DONE);
        scheduleRedraw(obj, data);
    };
    page->_fAlert = [obj, data](const WTF::String& message) {
        if (!data->setup)
            return;
        if (data->hooks[HookAlert]) {
            auto text = message.utf8();
            STRPTR arg = (STRPTR)text.data();
            s_modal++;
            callHook(obj, data, HookAlert, &arg);
            s_modal--;
        } else
            runDialog(obj, message, false, nullptr);
    };
    page->_fConfirm = [obj, data](const WTF::String& message) {
        if (!data->setup)
            return false;
        if (data->hooks[HookConfirm]) {
            auto text = message.utf8();
            STRPTR arg = (STRPTR)text.data();
            s_modal++;
            bool ok = callHook(obj, data, HookConfirm, &arg);
            s_modal--;
            return ok;
        }
        return runDialog(obj, message, true, nullptr);
    };
    page->_fPrompt = [obj, data](const WTF::String& message, const WTF::String& initial, WTF::String& result) {
        result = initial;
        if (!data->setup)
            return false;
        if (data->hooks[HookPrompt]) {
            auto text = message.utf8(), def = initial.utf8();
            STRPTR args[2] = { (STRPTR)text.data(), (STRPTR)def.data() };
            STRPTR* argp = args;
            s_modal++;
            STRPTR answer = (STRPTR)callHook(obj, data, HookPrompt, &argp);
            s_modal--;
            if (!answer)
                return false;
            result = WTF::String::fromUTF8(answer);
            FreeVec(answer);
            return true;
        }
        return runDialog(obj, message, true, &result);
    };
    page->_fPopup = [obj, data](const WebCore::IntRect& pos, const WTF::Vector<WTF::String>& items) {
        return data->setup ? runPopup(obj, pos, items) : -1;
    };
    page->_fSetCursor = [obj, data](int type) { setCursor(obj, data, type); };
    // Keyboard focus: WebKit only takes keys while the view is the window's
    // active object (MUIM_GoActive). A click in the page asks for it, Escape
    // gives it up, Tab past the last element moves on in the cycle chain.
    page->_fGoActive = [obj, data]() {
        if (data->setup)
            SetAttrs(_win(obj), MUIA_Window_ActiveObject, (IPTR)obj, TAG_DONE);
    };
    page->_fGoInactive = [obj, data]() {
        if (data->setup)
            SetAttrs(_win(obj), MUIA_Window_ActiveObject, MUIV_Window_ActiveObject_None, TAG_DONE);
    };
    page->_fActivateNext = [obj, data]() {
        if (data->setup)
            SetAttrs(_win(obj), MUIA_Window_ActiveObject, MUIV_Window_ActiveObject_Next, TAG_DONE);
    };
    page->_fActivatePrevious = [obj, data]() {
        if (data->setup)
            SetAttrs(_win(obj), MUIA_Window_ActiveObject, MUIV_Window_ActiveObject_Prev, TAG_DONE);
    };
    page->_fContextMenu = [obj, data](const WebCore::IntPoint& pos, const WTF::Vector<WebCore::ContextMenuItem>& items, const WebCore::HitTestResult&) {
        WTF::Vector<WTF::String> titles;
        WTF::Vector<const WebCore::ContextMenuItem*> picks;
        collectMenuItems(items, "", titles, picks);
        if (!data->setup || titles.isEmpty())
            return false;
        int index = runPopup(obj, WebCore::IntRect(pos, WebCore::IntSize()), titles);
        if (index >= 0 && data->page)
            data->page->onContextMenuItemSelected(ULONG(picks[index]->action()), picks[index]->title().utf8().data());
        return true;
    };
    page->_fChangedTitle = [obj, data](const WTF::String& title) {
        WKVIEW_LOG("title '%s'\n", title.utf8().data());
        setString(obj, data->title, MUIA_WebView_Title, title);
    };
    page->_fConsole = [](const WTF::String& url, const WTF::String& message, int level, unsigned int line, unsigned int) {
        WKVIEW_LOG("console(%d) %s:%u %s\n", level, url.utf8().data(), line, message.utf8().data());
    };
    page->_fEnterFullscreen = [obj, data]() {
        SetAttrs(obj, MUIA_WebView_FullScreen, TRUE, TAG_DONE);
        data->page->didEnterFullscreen(true);
    };
    page->_fExitFullscreen = [obj, data]() {
        // Cleared first so OM_SET does not ask the page to leave again
        setFullScreen(obj, data, FALSE);
        SetAttrs(obj, MUIA_WebView_FullScreen, FALSE, TAG_DONE);
    };
    page->_fChangedURL = [obj, data](const WTF::String& url) {
        setString(obj, data->url, MUIA_WebView_URL, url);
        updateHistory(obj, data);
    };
    page->_fHistoryChanged = [obj, data]() { updateHistory(obj, data); };
    page->_fHoveredURLChanged = [obj, data](const WTF::URL& url) {
        setString(obj, data->toolTip, MUIA_WebView_ToolTip, url.string());
    };
    page->_fProgressStarted = [obj]() { SetAttrs(obj, MUIA_WebView_EstimatedProgress, 0, TAG_DONE); };
    page->_fProgressUpdated = [obj, data](float progress) {
        LONG percent = LONG(progress * 100);
        if (percent != data->progress)
            SetAttrs(obj, MUIA_WebView_EstimatedProgress, percent, TAG_DONE);
    };
    page->_fProgressFinished = [obj]() { SetAttrs(obj, MUIA_WebView_EstimatedProgress, 100, TAG_DONE); };
    // window.open and target=_blank: WebKit wants the new view's page back.
    page->_fCanOpenWindow = [data](const WTF::String&, const WebCore::WindowFeatures& features) {
        if (!data->hooks[HookCreateNew])
            return false;
        delete data->openFeatures;
        data->openFeatures = new WebCore::WindowFeatures(features);
        return true;
    };
    page->_fDoOpenWindow = [obj, data]() -> WebCore::Page* {
        struct WindowSpecification spec, *specp = nullptr;
        if (auto* f = data->openFeatures; f && (f->width || f->height)) {
            spec = { LONG(f->x.value_or(MUIV_Window_LeftEdge_Centered)), LONG(f->y.value_or(MUIV_Window_TopEdge_Centered)),
                LONG(f->width.value_or(MUIV_Window_Width_Default)), LONG(f->height.value_or(MUIV_Window_Height_Default)),
                f->menuBarVisible.value_or(false), f->statusBarVisible.value_or(false), f->toolBarVisible.value_or(false),
                f->locationBarVisible.value_or(false), f->scrollbarsVisible.value_or(true), f->resizable.value_or(true),
                f->fullscreen.value_or(false) };
            specp = &spec;
        }
        delete data->openFeatures;
        data->openFeatures = nullptr;
        Object* view = openNewView(obj, data, specp);
        WebKit::WebPage* newPage = nullptr;
        if (view)
            GetAttr(MUIA_WebView_Page, view, (IPTR*)&newPage);
        return newPage ? newPage->corePage() : nullptr;
    };
    // Middle click / shift+click on a link.
    page->_fNewTabWindow = [obj, data](const WTF::URL& url, WebViewDelegateOpenWindowMode mode) {
        struct WindowSpecification spec = { MUIV_Window_LeftEdge_Centered, MUIV_Window_TopEdge_Centered,
            MUIV_Window_Width_Default, MUIV_Window_Height_Default, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE };
        Object* view = openNewView(obj, data, mode == WebViewDelegateOpenWindowMode::NewWindow ? &spec : nullptr);
        if (view)
            DoMethod(view, MUIM_WebView_LoadURL, (IPTR)url.string().utf8().data());
    };
    // A response WebKit cannot show: the PolicyHook decides.
    page->_fDownloadAsk = [obj, data](const WebCore::ResourceResponse& response, const WebCore::ResourceRequest&,
        const WTF::String&, WebCore::FramePolicyFunction&& function) {
        IPTR policy = MUIV_WebView_Policy_Ignore;
        if (data->hooks[HookPolicy]) {
            auto name = response.suggestedFilename().isEmpty() ? response.url().lastPathComponent().utf8() : response.suggestedFilename().utf8();
            auto mime = response.mimeType().utf8();
            CONST_STRPTR args[2] = { name.data(), mime.data() };
            CONST_STRPTR* argp = args;
            s_modal++;
            policy = callHook(obj, data, HookPolicy, &argp);
            s_modal--;
        }
        // Download goes on in _fDownloadFromResource.
        function(policy == MUIV_WebView_Policy_Use ? WebCore::PolicyAction::Use
            : policy == MUIV_WebView_Policy_Download ? WebCore::PolicyAction::Download
            : WebCore::PolicyAction::Ignore);
    };
    page->_fDownloadFromResource = [](WebCore::ResourceHandle* handle, const WebCore::ResourceRequest& request,
        const WebCore::ResourceResponse& response) {
        webkittyStartDownload(handle, request, response);
    };
    // Alt+click and the context menu's download entries.
    page->_fDownload = [](const WTF::URL& url, const WTF::String& suggestedName) {
        webkittyStartDownload(url, suggestedName);
    };
    // HTTP authentication, answered synchronously.
    page->_fAuthChallenge = [obj, data](const WebCore::AuthenticationChallenge& challenge) {
        if (!data->hooks[HookCredentials] || !challenge.authenticationClient())
            return false;
        auto host = challenge.protectionSpace().host().utf8();
        auto realm = challenge.protectionSpace().realm().utf8();
        STRPTR user = nullptr, password = nullptr;
        IPTR args[4] = { (IPTR)host.data(), (IPTR)realm.data(), (IPTR)&user, (IPTR)&password };
        IPTR* argp = args;
        s_modal++;
        bool login = callHook(obj, data, HookCredentials, &argp);
        s_modal--;
        if (login)
            challenge.authenticationClient()->receivedCredential(challenge, WebCore::Credential(WTF::String::fromUTF8(user ? user : ""),
                WTF::String::fromUTF8(password ? password : ""), WebCore::CredentialPersistence::ForSession));
        else
            challenge.authenticationClient()->receivedCancellation(challenge);
        free(user);
        free(password);
        return true;
    };
    page->_fCloseWindow = [obj, data]() {
        if (data->hooks[HookClose])
            callHook(obj, data, HookClose, nullptr);
    };
    // Plain media, HLS and Media Source; no VP9/HEVC (too slow to decode
    // in software, sites then offer H.264)
    if (auto* agent = (const char*)GetTagData(MUIA_WebView_UserAgent, 0, msg->ops_AttrList))
        data->userAgent = strdup(agent);
    page->_fUserAgentForURL = [data](const WTF::String&) {
        return WTF::String::fromUTF8(data->userAgent ? data->userAgent : s_defaultUserAgent);
    };
    page->_fMediaSupportCheck = [](WebViewDelegate::mediaType type) {
        return type == WebViewDelegate::mediaType::Media || type == WebViewDelegate::mediaType::MediaSource;
    };
    page->_fDidStartLoading = [obj, data]() {
        WKVIEW_LOG("load started\n");
        // Navigations not started by LoadURL (links, scripts) start here
        if (!data->loadStart)
            data->loadStart = data->navigationStart = WTF::MonotonicTime::now();
        setState(obj, MUIV_WebView_State_Loading);
    };
    page->_fDidStopLoading = [obj, data]() {
        updateHistory(obj, data);
        if (data->loadStart)
            WKVIEW_LOG("load finished (%ld ms)\n", long((WTF::MonotonicTime::now() - data->loadStart).milliseconds()));
        else
            WKVIEW_LOG("load finished\n");
        data->loadStart = { };
        setState(obj, MUIV_WebView_State_Ready);
    };
    page->_fDidFirstVisuallyNonEmptyLayout = [data]() {
        if (data->navigationStart)
            WKVIEW_LOG("first content (%ld ms)\n", long((WTF::MonotonicTime::now() - data->navigationStart).milliseconds()));
    };
    page->_fDidFailWithError = [obj, data](const WebCore::ResourceError& error) {
        WKVIEW_LOG("load failed: %s %d '%s' (%s)\n", error.domain().utf8().data(), error.errorCode(),
            error.localizedDescription().utf8().data(), error.failingURL().string().utf8().data());
        if (!error.isCancellation()) {
            // Before the state: listeners of the state read the text.
            setString(obj, data->errorText, MUIA_WebView_ErrorText, error.localizedDescription());
            setState(obj, MUIV_WebView_State_Error);
        }
    };
    // JavaScript is on unless WebPreferences or the view's own attribute
    // (which wins) turn it off.
    page->setJavaScriptEnabled(true);
    webkittyApplyPreferences(*page);
    if (auto* tag = FindTagItem(MUIA_WebView_JavaScriptEnabled, msg->ops_AttrList))
        page->setJavaScriptEnabled(tag->ti_Data);
    page->setDrawsMainFrameScrollbars(GetTagData(MUIA_WebView_DrawScrollbars, FALSE, msg->ops_AttrList));
    s_pages.append(page);
    return (IPTR)obj;
}

static IPTR WebView__OM_DISPOSE(Class* cl, Object* obj, Msg msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (auto* page = data->page) {
        // The process' page map may hold the last reference
        RefPtr<WebKit::WebPage> protectedPage(page);
        s_pages.removeFirst(page);
        page->clearDelegateCallbacks();
        WebKit::WebProcess::singleton().removeWebPage(page->pageID());
        page->willBeDisposed();
        data->page = nullptr;
    }
    free(data->url);
    free(data->title);
    free(data->toolTip);
    free(data->errorText);
    free(data->userAgent);
    delete data->openFeatures;
    return DoSuperMethodA(cl, obj, msg);
}

static IPTR WebView__OM_SET(Class* cl, Object* obj, struct opSet* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    struct TagItem* tstate = msg->ops_AttrList;
    struct TagItem* tag;
    bool scroll = false;
    while ((tag = NextTagItem(&tstate))) {
        switch (tag->ti_Tag) {
        case MUIA_WebView_State:
            data->state = tag->ti_Data;
            break;
        case MUIA_WebView_ScrollX:
            scroll |= LONG(tag->ti_Data) != data->scrollX;
            data->scrollX = LONG(tag->ti_Data);
            break;
        case MUIA_WebView_ScrollY:
            scroll |= LONG(tag->ti_Data) != data->scrollY;
            data->scrollY = LONG(tag->ti_Data);
            break;
        case MUIA_WebView_JavaScriptEnabled:
            if (data->page)
                data->page->setJavaScriptEnabled(tag->ti_Data);
            break;
        case MUIA_WebView_EstimatedProgress:
            data->progress = LONG(tag->ti_Data);
            break;
        case MUIA_WebView_UserAgent:
            free(data->userAgent);
            data->userAgent = tag->ti_Data ? strdup((const char*)tag->ti_Data) : nullptr;
            break;
        case MUIA_WebView_FullScreen:
            // Entering is up to the page; the application can only leave.
            if (!tag->ti_Data && data->fullScreen && data->page)
                data->page->exitFullscreen();
            setFullScreen(obj, data, tag->ti_Data ? TRUE : FALSE);
            break;
        case MUIA_WebView_CanGoBack:
            data->canGoBack = tag->ti_Data;
            break;
        case MUIA_WebView_CanGoForward:
            data->canGoForward = tag->ti_Data;
            break;
        case MUIA_WebView_Active:
            data->active = tag->ti_Data;
            if (data->active && data->setup)
                SetAttrs(_win(obj), MUIA_Window_ActiveObject, (IPTR)obj, TAG_DONE);
            break;
        default:
            setHookAttr(data, tag);
            break;
        }
    }
    // Scrollbar notifications can arrive while WebKit lays out; scroll later.
    if (scroll && data->setup && !data->scrollPending) {
        data->scrollPending = TRUE;
        DoMethod(_app(obj), MUIM_Application_PushMethod, (IPTR)obj, 1, MUIM_WebView_LateScroll);
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR WebView__OM_GET(Class* cl, Object* obj, struct opGet* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    switch (msg->opg_AttrID) {
    case MUIA_WebView_URL:
        *msg->opg_Storage = (IPTR)data->url;
        return TRUE;
    case MUIA_WebView_Title:
        *msg->opg_Storage = (IPTR)data->title;
        return TRUE;
    case MUIA_WebView_State:
        *msg->opg_Storage = data->state;
        return TRUE;
    case MUIA_WebView_DocumentWidth:
        *msg->opg_Storage = data->docWidth;
        return TRUE;
    case MUIA_WebView_DocumentHeight:
        *msg->opg_Storage = data->docHeight;
        return TRUE;
    case MUIA_WebView_VisibleWidth:
        *msg->opg_Storage = data->visWidth;
        return TRUE;
    case MUIA_WebView_VisibleHeight:
        *msg->opg_Storage = data->visHeight;
        return TRUE;
    case MUIA_WebView_ScrollX:
        *msg->opg_Storage = data->scrollX;
        return TRUE;
    case MUIA_WebView_ScrollY:
        *msg->opg_Storage = data->scrollY;
        return TRUE;
    case MUIA_WebView_JavaScriptEnabled:
        *msg->opg_Storage = data->page && data->page->javaScriptEnabled();
        return TRUE;
    case MUIA_WebView_ToolTip:
        *msg->opg_Storage = (IPTR)(data->toolTip ? data->toolTip : "");
        return TRUE;
    case MUIA_WebView_ErrorText:
        *msg->opg_Storage = (IPTR)(data->errorText ? data->errorText : "");
        return TRUE;
    case MUIA_WebView_UserAgent:
        *msg->opg_Storage = (IPTR)(data->userAgent ? data->userAgent : s_defaultUserAgent);
        return TRUE;
    case MUIA_WebView_EstimatedProgress:
        *msg->opg_Storage = data->progress;
        return TRUE;
    case MUIA_WebView_CanGoBack:
        *msg->opg_Storage = data->canGoBack;
        return TRUE;
    case MUIA_WebView_CanGoForward:
        *msg->opg_Storage = data->canGoForward;
        return TRUE;
    case MUIA_WebView_Active:
        *msg->opg_Storage = data->active;
        return TRUE;
    case MUIA_WebView_FullScreen:
        *msg->opg_Storage = data->fullScreen;
        return TRUE;
    case MUIA_WebView_Data:
        // The page's markup, as the old view's source view expects.
        *msg->opg_Storage = data->page ? (IPTR)data->page->getInnerHTML([](const char* html) -> void* {
            size_t length = strlen(html) + 1;
            void* copy = AllocVec(length, MEMF_ANY);
            if (copy)
                memcpy(copy, html, length);
            return copy;
        }) : 0;
        return TRUE;
    case MUIA_WebView_Page:
        *msg->opg_Storage = (IPTR)data->page;
        return TRUE;
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR WebView__MUIM_Setup(Class* cl, Object* obj, struct MUIP_Setup* msg)
{
    if (!DoSuperMethodA(cl, obj, (Msg)msg))
        return FALSE;

    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    data->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_INACTIVEWINDOW;
    data->ehn.ehn_Object = obj;
    data->ehn.ehn_Class = cl;
    // Ahead of Area's own handler, which eats the right button.
    data->ehn.ehn_Priority = 1;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&data->ehn);

    // Every view polls; WebProcess::handleSignals() is cheap when idle.
    data->signalIhn.ihn_Object = obj;
    data->signalIhn.ihn_Signals = 1UL << s_sigBit;
    data->signalIhn.ihn_Method = MUIM_WebView_Signal;
    DoMethod(_app(obj), MUIM_Application_AddInputHandler, (IPTR)&data->signalIhn);

    data->timerIhn.ihn_Object = obj;
    data->timerIhn.ihn_Flags = MUIIHNF_TIMER;
    data->timerIhn.ihn_Millis = timerMillis;
    data->timerIhn.ihn_Method = MUIM_WebView_Signal;
    DoMethod(_app(obj), MUIM_Application_AddInputHandler, (IPTR)&data->timerIhn);

    data->setup = TRUE;
    return TRUE;
}

static IPTR WebView__MUIM_Cleanup(Class* cl, Object* obj, struct MUIP_Cleanup* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    setCursor(obj, data, POINTERTYPE_NORMAL);
    data->setup = FALSE;
    DoMethod(_app(obj), MUIM_Application_RemInputHandler, (IPTR)&data->timerIhn);
    DoMethod(_app(obj), MUIM_Application_RemInputHandler, (IPTR)&data->signalIhn);
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&data->ehn);
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR WebView__MUIM_Show(Class* cl, Object* obj, struct MUIP_Show* msg)
{
    IPTR rc = DoSuperMethodA(cl, obj, (Msg)msg);
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (_mwidth(obj) != data->visWidth || _mheight(obj) != data->visHeight) {
        data->visWidth = _mwidth(obj);
        data->visHeight = _mheight(obj);
        SetAttrs(obj, MUIA_WebView_VisibleWidth, data->visWidth, MUIA_WebView_VisibleHeight, data->visHeight, TAG_DONE);
    }
    // Hidden pages (also the initial state) get no video and throttled timers
    if (!data->shown && data->page) {
        data->shown = TRUE;
        data->page->goVisible();
    }
    return rc;
}

static IPTR WebView__MUIM_Hide(Class* cl, Object* obj, struct MUIP_Hide* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (data->shown && data->page) {
        data->shown = FALSE;
        data->page->goHidden();
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR WebView__MUIM_GoActive(Class* cl, Object* obj, Msg msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (data->page)
        data->page->goActive();
    return DoSuperMethodA(cl, obj, msg);
}

static IPTR WebView__MUIM_GoInactive(Class* cl, Object* obj, Msg msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (data->page)
        data->page->goInactive();
    return DoSuperMethodA(cl, obj, msg);
}

// WebKit shows its own menu on the right button (see _fContextMenu).
static IPTR WebView__MUIM_ContextMenuBuild(Class*, Object*, Msg)
{
    return 0;
}

static IPTR WebView__MUIM_AskMinMax(Class* cl, Object* obj, struct MUIP_AskMinMax* msg)
{
    DoSuperMethodA(cl, obj, (Msg)msg);
    msg->MinMaxInfo->MinWidth += 100;
    msg->MinMaxInfo->MinHeight += 100;
    msg->MinMaxInfo->DefWidth += 1024;
    msg->MinMaxInfo->DefHeight += 740;
    msg->MinMaxInfo->MaxWidth = MUI_MAXMAX;
    msg->MinMaxInfo->MaxHeight = MUI_MAXMAX;
    return 0;
}

static IPTR WebView__MUIM_Draw(Class* cl, Object* obj, struct MUIP_Draw* msg)
{
    DoSuperMethodA(cl, obj, (Msg)msg);

    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (!data->page || !(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
        return 0;

    data->page->setVisibleSize(_mwidth(obj), _mheight(obj));
    data->page->draw(_rp(obj), _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj),
        (msg->flags & MADF_DRAWUPDATE) == MADF_DRAWUPDATE);
    return 0;
}

static IPTR WebView__MUIM_HandleEvent(Class* cl, Object* obj, struct MUIP_HandleEvent* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (!data->page)
        return 0;

    IPTR defaultObject = 0;
    GetAttr(MUIA_Window_DefaultObject, _win(obj), &defaultObject);
    bool isDefault = (Object*)defaultObject == obj;

    if (data->fullScreen && msg->imsg && msg->imsg->Class == IDCMP_RAWKEY && msg->imsg->Code == RAWKEY_ESCAPE) {
        data->page->exitFullscreen();
        return MUI_EventHandlerRC_Eat;
    }

    if (msg->muikey != MUIKEY_NONE && data->page->handleMUIKey(int(msg->muikey), isDefault))
        return MUI_EventHandlerRC_Eat;

    if (struct IntuiMessage* imsg = msg->imsg) {
        int x = imsg->MouseX - _mleft(obj);
        int y = imsg->MouseY - _mtop(obj);
        bool inside = x >= 0 && y >= 0 && x < _mwidth(obj) && y < _mheight(obj);
        if (data->page->handleIntuiMessage(imsg, x, y, inside, isDefault))
            return MUI_EventHandlerRC_Eat;
    }
    return 0;
}

static IPTR WebView__MUIM_WebView_Signal(Class*, Object*, Msg)
{
    if (s_modal)
        return 0;
    auto& process = WebKit::WebProcess::singleton();
    const uint32_t mask = 1UL << s_sigBit;
    process.handleSignals(mask);
    // Run timers that are already due, as the MorphOS view does.
    for (int i = 0; i < 5 && process.timeToNextTimerEvent() <= 0.001f; i++)
        process.handleSignals(mask);
    return 0;
}

static IPTR WebView__MUIM_WebView_LateDraw(Class* cl, Object* obj, Msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (data->redrawPending) {
        data->redrawPending = FALSE;
        if (data->setup)
            MUI_Redraw(obj, MADF_DRAWUPDATE);
    }
    return 0;
}

static IPTR WebView__MUIM_WebView_LateScroll(Class* cl, Object* obj, Msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    data->scrollPending = FALSE;
    if (data->page && data->setup)
        data->page->setScroll(data->scrollX, data->scrollY);
    return 0;
}

static IPTR WebView__MUIM_WebView_LoadURL(Class* cl, Object* obj, struct MUIP_WebView_LoadURL* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (data->page && msg->url) {
        WKVIEW_LOG("load '%s'\n", msg->url);
        data->loadStart = data->navigationStart = WTF::MonotonicTime::now();
        setState(obj, MUIV_WebView_State_Connecting);
        data->page->load(msg->url);
    }
    return 0;
}

static IPTR WebView__MUIM_WebView_LoadHTMLString(Class* cl, Object* obj, struct MUIP_WebView_LoadHTMLString* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (!data->page || !msg->data)
        return 0;
    size_t length = msg->length == (ULONG)-1 ? strlen(msg->data) : msg->length;
    const char* mime = data->sourceMode ? "text/plain" : msg->mime ? msg->mime : "text/html";
    data->page->loadData(msg->data, length, msg->url, mime);
    return 0;
}

static IPTR WebView__MUIM_WebView_SearchFor(Class* cl, Object* obj, struct MUIP_WebView_SearchFor* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (!data->page || !msg->criteria || !*msg->criteria)
        return FALSE;
    WebCore::FindOptions options { WebCore::FindOption::WrapAround };
    if (!msg->forward)
        options.add(WebCore::FindOption::Backwards);
    if (!msg->caseSensitive)
        options.add(WebCore::FindOption::CaseInsensitive);
    bool wrapped = false;
    return data->page->search(WTF::String::fromUTF8(msg->criteria), options, wrapped);
}

static IPTR WebView__MUIM_WebView_Zoom(Class* cl, Object* obj, struct MUIP_WebView_Zoom* msg)
{
    auto* data = (WebView_DATA*)INST_DATA(cl, obj);
    if (!data->page)
        return FALSE;
    switch (msg->mode) {
    case MUIV_WebView_ZoomIn:
        data->zoom = std::min(data->zoom * 1.2f, 4.0f);
        break;
    case MUIV_WebView_ZoomOut:
        data->zoom = std::max(data->zoom / 1.2f, 0.25f);
        break;
    default:
        data->zoom = 1.0f;
        break;
    }
    data->page->setPageAndTextZoomFactors(data->zoom, 1.0f);
    return TRUE;
}

// Stops WebKit's threads; call once after the last view is disposed.
extern "C" void WebView_Terminate(void)
{
    if (s_sigBit < 0)
        return;
    WebKit::WebProcess::singleton().terminate();
    for (int i = 0; i < PointerCount; i++) {
        DisposeObject(s_pointers[i]);
        FreeBitMap(s_pointerBitMaps[i]);
        s_pointers[i] = nullptr;
        s_pointerBitMaps[i] = nullptr;
    }
    FreeSignal(s_sigBit);
    s_sigBit = -1;
}

#define WKVIEW_PAGE_METHOD(name, call) \
static IPTR WebView__MUIM_WebView_##name(Class* cl, Object* obj, Msg) \
{ \
    auto* data = (WebView_DATA*)INST_DATA(cl, obj); \
    if (data->page) \
        data->page->call; \
    return 0; \
}
WKVIEW_PAGE_METHOD(GoBack, goBack())
WKVIEW_PAGE_METHOD(GoForward, goForward())
WKVIEW_PAGE_METHOD(Reload, reload(data->url ? data->url : ""))
WKVIEW_PAGE_METHOD(StopLoading, stop())

extern "C" {
__ZUNE_CUSTOMCLASS_START(WebView)
__ZUNE_CUSTOMCLASS_METHOD(WebView__OM_NEW, OM_NEW, struct opSet*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__OM_DISPOSE, OM_DISPOSE, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__OM_SET, OM_SET, struct opSet*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__OM_GET, OM_GET, struct opGet*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_Setup, MUIM_Setup, struct MUIP_Setup*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_Cleanup, MUIM_Cleanup, struct MUIP_Cleanup*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_Show, MUIM_Show, struct MUIP_Show*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_Hide, MUIM_Hide, struct MUIP_Hide*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_ContextMenuBuild, MUIM_ContextMenuBuild, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_GoActive, MUIM_GoActive, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_GoInactive, MUIM_GoInactive, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_AskMinMax, MUIM_AskMinMax, struct MUIP_AskMinMax*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_Draw, MUIM_Draw, struct MUIP_Draw*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_HandleEvent, MUIM_HandleEvent, struct MUIP_HandleEvent*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_Signal, MUIM_WebView_Signal, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_LateDraw, MUIM_WebView_LateDraw, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_LateScroll, MUIM_WebView_LateScroll, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_LoadURL, MUIM_WebView_LoadURL, struct MUIP_WebView_LoadURL*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_GoBack, MUIM_WebView_GoBack, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_GoForward, MUIM_WebView_GoForward, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_Reload, MUIM_WebView_Reload, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_StopLoading, MUIM_WebView_StopLoading, Msg);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_LoadHTMLString, MUIM_WebView_LoadHTMLString, struct MUIP_WebView_LoadHTMLString*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_SearchFor, MUIM_WebView_SearchFor, struct MUIP_WebView_SearchFor*);
__ZUNE_CUSTOMCLASS_METHOD(WebView__MUIM_WebView_Zoom, MUIM_WebView_Zoom, struct MUIP_WebView_Zoom*);
__ZUNE_CUSTOMCLASS_END(WebView, NULL, MUIC_Area, NULL)
}
