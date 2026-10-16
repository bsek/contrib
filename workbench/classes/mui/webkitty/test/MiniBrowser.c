/*
 * Minimal AROS test browser for the Zune WebView class:
 * a URL field, the view with scrollbars, and the window title bound to
 * the page title.
 *
 * Usage: MiniBrowser [-nojs] [url]
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>

#include <string.h>

#include <webkitty/WebViewZune.h>

int main(int argc, char** argv)
{
    const char* startURL = "about:blank";
    BOOL javaScript = TRUE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-nojs") == 0)
            javaScript = FALSE;
        else
            startURL = argv[i];
    }
    Object *window, *urlField, *view, *vbar, *hbar;

    Object* app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"WebKitty MiniBrowser",
        MUIA_Application_Base, (IPTR)"WKMINIBROWSER",
        SubWindow, (IPTR)(window = WindowObject,
            MUIA_Window_Title, (IPTR)"WebKitty",
            MUIA_Window_ID, MAKE_ID('W','K','M','B'),
            WindowContents, (IPTR)(VGroup,
                Child, (IPTR)(urlField = StringObject,
                    StringFrame,
                    MUIA_String_Contents, (IPTR)startURL,
                    MUIA_CycleChain, TRUE,
                End),
                Child, (IPTR)(HGroup,
                    MUIA_Group_Spacing, 0,
                    Child, (IPTR)(view = WebViewObject,
                        MUIA_WebView_JavaScriptEnabled, javaScript,
                    End),
                    Child, (IPTR)(vbar = ScrollbarObject, MUIA_Scrollbar_IncDecSize, 40, End),
                End),
                Child, (IPTR)(hbar = ScrollbarObject, MUIA_Group_Horiz, TRUE, MUIA_Scrollbar_IncDecSize, 40, End),
            End),
        End),
    End;

    if (!app) {
        WebView_Terminate();
        return 20;
    }

    DoMethod(window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
        (IPTR)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(urlField, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
        (IPTR)view, 2, MUIM_WebView_LoadURL, MUIV_TriggerValue);
    DoMethod(view, MUIM_Notify, MUIA_WebView_URL, MUIV_EveryTime,
        (IPTR)urlField, 3, MUIM_NoNotifySet, MUIA_String_Contents, MUIV_TriggerValue);
    DoMethod(view, MUIM_Notify, MUIA_WebView_Title, MUIV_EveryTime,
        (IPTR)window, 3, MUIM_Set, MUIA_Window_Title, MUIV_TriggerValue);

    static const ULONG scrollAttrs[2][3] = {
        { MUIA_WebView_DocumentHeight, MUIA_WebView_VisibleHeight, MUIA_WebView_ScrollY },
        { MUIA_WebView_DocumentWidth, MUIA_WebView_VisibleWidth, MUIA_WebView_ScrollX },
    };
    Object* bars[2] = { vbar, hbar };
    for (int i = 0; i < 2; i++) {
        DoMethod(view, MUIM_Notify, scrollAttrs[i][0], MUIV_EveryTime,
            (IPTR)bars[i], 3, MUIM_Set, MUIA_Prop_Entries, MUIV_TriggerValue);
        DoMethod(view, MUIM_Notify, scrollAttrs[i][1], MUIV_EveryTime,
            (IPTR)bars[i], 3, MUIM_Set, MUIA_Prop_Visible, MUIV_TriggerValue);
        DoMethod(view, MUIM_Notify, scrollAttrs[i][2], MUIV_EveryTime,
            (IPTR)bars[i], 3, MUIM_NoNotifySet, MUIA_Prop_First, MUIV_TriggerValue);
        DoMethod(bars[i], MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime,
            (IPTR)view, 3, MUIM_Set, scrollAttrs[i][2], MUIV_TriggerValue);
    }

    SetAttrs(window, MUIA_Window_Open, TRUE, TAG_DONE);
    DoMethod(view, MUIM_WebView_LoadURL, (IPTR)startURL);

    ULONG sigs = 0;
    while (DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs) != MUIV_Application_ReturnID_Quit) {
        if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C)
                break;
        }
    }

    SetAttrs(window, MUIA_Window_Open, FALSE, TAG_DONE);
    MUI_DisposeObject(app);
    WebView_Terminate();
    return 0;
}
