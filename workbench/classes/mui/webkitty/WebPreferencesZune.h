#ifndef WEBKITTY_WEBPREFERENCESZUNE_H
#define WEBKITTY_WEBPREFERENCESZUNE_H

/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Settings for all WebView objects of the application; subclassable.
    IDs follow OWB's WebPreferences class. Attributes are [ISG]; those
    marked "stored" are kept but have no effect yet.
*/

#include <exec/types.h>
#include <libraries/mui.h>

#ifdef __cplusplus
extern "C" {
#endif
extern struct MUI_CustomClass *WebPreferences_CLASS;
#ifdef __cplusplus
}
#endif

#define WebPreferencesObject (Object *)NewObject(WebPreferences_CLASS->mcc_Class, NULL

#define MUIB_WebPreferences (TAG_USER | 0x10000000)

/*** Attributes *************************************************************/
/* Strings are UTF-8. */
#define MUIA_WebPreferences_StandardFontFamily      (MUIB_WebPreferences | 0x00000000) /* CONST_STRPTR */
#define MUIA_WebPreferences_FixedFontFamily         (MUIB_WebPreferences | 0x00000001) /* CONST_STRPTR */
#define MUIA_WebPreferences_SerifFontFamily         (MUIB_WebPreferences | 0x00000002) /* CONST_STRPTR */
#define MUIA_WebPreferences_SansSerifFontFamily     (MUIB_WebPreferences | 0x00000003) /* CONST_STRPTR */
#define MUIA_WebPreferences_CursiveFontFamily       (MUIB_WebPreferences | 0x00000004) /* CONST_STRPTR */
#define MUIA_WebPreferences_FantasyFontFamily       (MUIB_WebPreferences | 0x00000005) /* CONST_STRPTR */
#define MUIA_WebPreferences_DefaultFontSize         (MUIB_WebPreferences | 0x00000006) /* LONG, pixels */
#define MUIA_WebPreferences_DefaultFixedFontSize    (MUIB_WebPreferences | 0x00000007) /* LONG, pixels */
#define MUIA_WebPreferences_MinimumFontSize         (MUIB_WebPreferences | 0x00000008) /* LONG, pixels */
#define MUIA_WebPreferences_JavaScriptEnabled       (MUIB_WebPreferences | 0x0000000e) /* BOOL */
#define MUIA_WebPreferences_AllowsAnimatedImages    (MUIB_WebPreferences | 0x00000011) /* BOOL, stored */
#define MUIA_WebPreferences_AllowAnimatedImageLooping (MUIB_WebPreferences | 0x00000012) /* BOOL, stored */
#define MUIA_WebPreferences_LoadsImagesAutomatically (MUIB_WebPreferences | 0x00000013) /* BOOL */
#define MUIA_WebPreferences_DecodesBMPWithDatatypes (MUIB_WebPreferences | 0x0000001f) /* BOOL, stored */
#define MUIA_WebPreferences_DecodesPNGWithDatatypes (MUIB_WebPreferences | 0x00000020) /* BOOL, stored */
#define MUIA_WebPreferences_DecodesGIFWithDatatypes (MUIB_WebPreferences | 0x00000021) /* BOOL, stored */
#define MUIA_WebPreferences_DecodesJPGWithDatatypes (MUIB_WebPreferences | 0x00000022) /* BOOL, stored */
/* SQLite cookie database; default PROGDIR:Cache/cookie.jar.db */
#define MUIA_WebPreferences_CookieJarFileName       (MUIB_WebPreferences | 0x00000023) /* CONST_STRPTR */

#endif /* WEBKITTY_WEBPREFERENCESZUNE_H */
