#ifndef WEBKITTY_WEBVIEWZUNE_H
#define WEBKITTY_WEBVIEWZUNE_H

/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Zune WebView class on WebKit (webkitty, WebKitGTK 2.30 lineage).
    Names and values follow OWB's WebView class where they overlap.

    Link with -lwebkitty and nix=yes: WebKit's dependencies (fontconfig)
    use Unix paths.
*/

#include <exec/types.h>
#include <libraries/mui.h>

#ifdef __cplusplus
extern "C" {
#endif
extern struct MUI_CustomClass *WebView_CLASS;
/* Stops WebKit's threads; call once after the last view is disposed. */
void WebView_Terminate(void);
#ifdef __cplusplus
}
#endif

#define WebViewObject (Object *)NewObject(WebView_CLASS->mcc_Class, NULL

#define MUIB_WebView (TAG_USER | 0x10000000)

/*** Methods ****************************************************************/
#define MUIM_WebView_LoadURL        (MUIB_WebView | 0x00000002)
#define MUIM_WebView_GoBack         (MUIB_WebView | 0x00000003)
#define MUIM_WebView_GoForward      (MUIB_WebView | 0x00000004)
#define MUIM_WebView_StopLoading    (MUIB_WebView | 0x00000005)
#define MUIM_WebView_Reload         (MUIB_WebView | 0x00000006)
#define MUIM_WebView_SearchFor      (MUIB_WebView | 0x0000000c) /* returns TRUE if found */
#define MUIM_WebView_Zoom           (MUIB_WebView | 0x00000013)
#define MUIM_WebView_LoadHTMLString (MUIB_WebView | 0x00000014)

struct MUIP_WebView_LoadURL { STACKED ULONG MethodID; STACKED CONST_STRPTR url; };
struct MUIP_WebView_SearchFor { STACKED ULONG MethodID; STACKED CONST_STRPTR criteria; STACKED BYTE forward; STACKED BYTE caseSensitive; };
struct MUIP_WebView_Zoom { STACKED ULONG MethodID; STACKED ULONG mode; /* MUIV_WebView_Zoom* */ };
/* length (ULONG)-1: data is NUL-terminated. mime NULL: text/html. */
struct MUIP_WebView_LoadHTMLString { STACKED ULONG MethodID; STACKED CONST_STRPTR url; STACKED CONST_STRPTR data;
    STACKED ULONG length; STACKED CONST_STRPTR mime; STACKED CONST_STRPTR encoding; };

/*** Attributes *************************************************************/
/* Strings are UTF-8. */
#define MUIA_WebView_ToolTip           (MUIB_WebView | 0x00000002) /* [..G] CONST_STRPTR, link under the pointer, "" if none */
#define MUIA_WebView_State             (MUIB_WebView | 0x00000003) /* [..G] LONG, MUIV_WebView_State_* */
#define MUIA_WebView_EstimatedProgress (MUIB_WebView | 0x00000004) /* [..G] LONG, 0..100 */
#define MUIA_WebView_URL               (MUIB_WebView | 0x00000005) /* [..G] CONST_STRPTR */
#define MUIA_WebView_Title             (MUIB_WebView | 0x00000006) /* [..G] CONST_STRPTR */
#define MUIA_WebView_Active            (MUIB_WebView | 0x00000009) /* [ISG] BOOL, TRUE: takes the window's input focus */
#define MUIA_WebView_CanGoBack         (MUIB_WebView | 0x0000000a) /* [..G] BOOL */
#define MUIA_WebView_CanGoForward      (MUIB_WebView | 0x0000000b) /* [..G] BOOL */
#define MUIA_WebView_Data              (MUIB_WebView | 0x00000012) /* [..G] STRPTR, page HTML; caller FreeVec()s it */
#define MUIA_WebView_SourceMode        (MUIB_WebView | 0x00000013) /* [IS.] BOOL, LoadHTMLString shows the text as is */
#define MUIA_WebView_ErrorText         (MUIB_WebView | 0x00000015) /* [..G] CONST_STRPTR, reason of the last failed load */
#define MUIA_WebView_FullScreen        (MUIB_WebView | 0x00000016) /* [.SG] BOOL, an element (e.g. a video) fills the view; set FALSE to leave */

/* Hooks, called with the view as object. Without one the view uses its own
   requester (alert/confirm/prompt) or the default action. */
/* msg: struct WindowSpecification ** (NULL: new tab). Returns the new view. */
#define MUIA_WebView_CreateNewHook     (MUIB_WebView | 0x00000007) /* [IS.] struct Hook * */
/* msg: STRPTR *message */
#define MUIA_WebView_AlertHook         (MUIB_WebView | 0x0000000c) /* [IS.] struct Hook * */
/* msg: STRPTR *message. Returns TRUE for OK. */
#define MUIA_WebView_ConfirmHook       (MUIB_WebView | 0x0000000d) /* [IS.] struct Hook * */
/* msg: STRPTR **{message, default}. Returns an AllocVec()ed answer or NULL. */
#define MUIA_WebView_PromptHook        (MUIB_WebView | 0x0000000e) /* [IS.] struct Hook * */
/* msg: CONST_STRPTR **{file name, MIME type}. Returns MUIV_WebView_Policy_*. */
#define MUIA_WebView_PolicyHook        (MUIB_WebView | 0x0000000f) /* [IS.] struct Hook * */
/* msg: IPTR **{host, realm, STRPTR *user, STRPTR *password}; the hook
   strdup()s user and password. Returns TRUE to log in. */
#define MUIA_WebView_CredentialsHook   (MUIB_WebView | 0x00000010) /* [IS.] struct Hook * */
/* msg: NULL. The page asked to close its window. */
#define MUIA_WebView_CloseHook         (MUIB_WebView | 0x00000011) /* [IS.] struct Hook * */

/* Scrolling, in pixels; for Prop_Entries/Visible/First of external
   scrollbars. All notify. */
#define MUIA_WebView_DocumentWidth  (MUIB_WebView | 0x00000100) /* [..G] LONG */
#define MUIA_WebView_DocumentHeight (MUIB_WebView | 0x00000101) /* [..G] LONG */
#define MUIA_WebView_VisibleWidth   (MUIB_WebView | 0x00000102) /* [..G] LONG */
#define MUIA_WebView_VisibleHeight  (MUIB_WebView | 0x00000103) /* [..G] LONG */
#define MUIA_WebView_ScrollX        (MUIB_WebView | 0x00000104) /* [.SG] LONG */
#define MUIA_WebView_ScrollY        (MUIB_WebView | 0x00000105) /* [.SG] LONG */
/* WebKit draws the main frame's scrollbars itself, instead of external ones */
#define MUIA_WebView_DrawScrollbars (MUIB_WebView | 0x00000106) /* [I..] BOOL, default FALSE */

/* Settings */
#define MUIA_WebView_JavaScriptEnabled (MUIB_WebView | 0x00000110) /* [ISG] BOOL, default TRUE */
#define MUIA_WebView_UserAgent         (MUIB_WebView | 0x00000111) /* [ISG] CONST_STRPTR, NULL = default (Safari 18) */

/*** Values *****************************************************************/
#define MUIV_WebView_State_Ready      (MUIB_WebView | 0x00000000)
#define MUIV_WebView_State_Connecting (MUIB_WebView | 0x00000001)
#define MUIV_WebView_State_Loading    (MUIB_WebView | 0x00000002)
#define MUIV_WebView_State_Error      (MUIB_WebView | 0x00000003)

#define MUIV_WebView_Policy_Ignore    (MUIB_WebView | 0x00000004)
#define MUIV_WebView_Policy_Use       (MUIB_WebView | 0x00000005)
#define MUIV_WebView_Policy_Download  (MUIB_WebView | 0x00000006)

#define MUIV_WebView_ZoomIn           (MUIB_WebView | 0x00000007)
#define MUIV_WebView_ZoomOut          (MUIB_WebView | 0x00000008)
#define MUIV_WebView_ZoomReset        (MUIB_WebView | 0x00000009)

/* For CreateNewHook: the window a page asked for (window.open features). */
struct WindowSpecification
{
    LONG left;
    LONG top;
    LONG width;
    LONG height;
    BOOL menubar;
    BOOL statusbar;
    BOOL toolbar;
    BOOL locationbar;
    BOOL scrollbars;
    BOOL resizable;
    BOOL fullscreen;
};

#endif /* WEBKITTY_WEBVIEWZUNE_H */
