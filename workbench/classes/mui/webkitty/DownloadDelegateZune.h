#ifndef WEBKITTY_DOWNLOADDELEGATEZUNE_H
#define WEBKITTY_DOWNLOADDELEGATEZUNE_H

/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Runs the application's downloads and reports them; a Window subclass
    (e.g. a download list). Subclass it and implement the Did* and Decide*
    methods. The last object created is the one used.
    IDs follow OWB's DownloadDelegate class; download is an opaque handle.
*/

#include <exec/types.h>
#include <libraries/mui.h>

#ifdef __cplusplus
extern "C" {
#endif
extern struct MUI_CustomClass *DownloadDelegate_CLASS;
#ifdef __cplusplus
}
#endif

#define DownloadDelegateObject (Object *)NewObject(DownloadDelegate_CLASS->mcc_Class, NULL

#define MUIB_DownloadDelegate (TAG_USER | 0x10000000)

/*** Methods implemented by the subclass ***********************************/
#define MUIM_DownloadDelegate_DidCreateDownload            (MUIB_DownloadDelegate | 0x00000000)
#define MUIM_DownloadDelegate_DidDestroyDownload           (MUIB_DownloadDelegate | 0x00000001)
#define MUIM_DownloadDelegate_DidBeginDownload             (MUIB_DownloadDelegate | 0x00000002)
#define MUIM_DownloadDelegate_DidFinishDownload            (MUIB_DownloadDelegate | 0x00000003)
#define MUIM_DownloadDelegate_DidReceiveExpectedContentLength (MUIB_DownloadDelegate | 0x00000004)
#define MUIM_DownloadDelegate_DidReceiveDataOfLength       (MUIB_DownloadDelegate | 0x00000005)
/* Answer with MUIM_DownloadDelegate_SetDestination (NULL: cancel). */
#define MUIM_DownloadDelegate_DecideDestinationWithSuggestedFilename (MUIB_DownloadDelegate | 0x00000006)
#define MUIM_DownloadDelegate_DidFailWithError             (MUIB_DownloadDelegate | 0x00000009)

/*** Methods implemented by this class **************************************/
#define MUIM_DownloadDelegate_CancelDownload               (MUIB_DownloadDelegate | 0x00000007)
#define MUIM_DownloadDelegate_SetDestination               (MUIB_DownloadDelegate | 0x00000008)

struct MUIP_DownloadDelegate_DidCreateDownload { STACKED ULONG MethodID; STACKED void *download; };
struct MUIP_DownloadDelegate_DidDestroyDownload { STACKED ULONG MethodID; STACKED void *download; };
struct MUIP_DownloadDelegate_DidBeginDownload { STACKED ULONG MethodID; STACKED void *download; };
struct MUIP_DownloadDelegate_DidFinishDownload { STACKED ULONG MethodID; STACKED void *download; };
struct MUIP_DownloadDelegate_DidReceiveExpectedContentLength { STACKED ULONG MethodID; STACKED void *download; STACKED long long length; };
struct MUIP_DownloadDelegate_DidReceiveDataOfLength { STACKED ULONG MethodID; STACKED void *download; STACKED unsigned length; };
struct MUIP_DownloadDelegate_DecideDestinationWithSuggestedFilename { STACKED ULONG MethodID; STACKED void *download; STACKED char *filename; };
struct MUIP_DownloadDelegate_DidFailWithError { STACKED ULONG MethodID; STACKED void *download; };
struct MUIP_DownloadDelegate_CancelDownload { STACKED ULONG MethodID; STACKED void *download; };
struct MUIP_DownloadDelegate_SetDestination { STACKED ULONG MethodID; STACKED void *download; STACKED char *destination; };

#endif /* WEBKITTY_DOWNLOADDELEGATEZUNE_H */
