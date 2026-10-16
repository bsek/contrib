#ifndef _BROWSERWINDOW_PRIVATE_H_
#define _BROWSERWINDOW_PRIVATE_H_

/*
    Copyright © 2009, The AROS Development Team. All rights reserved.
    $Id$
*/

#include <exec/types.h>
#include <utility/hooks.h>
#include <libraries/mui.h>

/*** Instance data **********************************************************/
struct BrowserWindow_DATA
{
    Object *tabContextMenu;
    Object *urlString;
    Object *tabs, *tabbed;
    Object *progressBar;
    Object *toolTips;
    Object *searchBar;
    Object *topBar, *bottomBar;
    BOOL fullScreen;
    BOOL barShown[4];       /* bars' MUIA_ShowMe before going full screen */
    struct Hook goHook;
    struct Hook webSearchHook;
    struct Hook webSearchAcknowledgeHook;
    char errorStatus[256];
    STRPTR title;           /* window title in the system codeset */
};

#endif /* _BROWSERWINDOW_PRIVATE_H_ */
