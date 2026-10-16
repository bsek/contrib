/*
 * Zune WebPreferences class: settings shared by all WebView objects.
 * Only what was set is applied; the rest keeps WebKit's defaults.
 */

// WebKit headers first, as in WebViewZune.cpp.
#include "WebKit.h"
#include "WebPage.h"
#include <WebCore/CookieJarDB.h>
#include <WebCore/NetworkStorageSession.h>
#include <WebCore/Page.h>
#include <WebCore/Settings.h>
#include "NetworkStorageSessionMap.h"
#include <wtf/text/AtomString.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <zune/customclasses.h>

#include <stdlib.h>
#include <string.h>

#include "WebPreferencesZune.h"
#include "WebViewZune_private.h"

enum { FontStandard, FontFixed, FontSerif, FontSansSerif, FontCursive, FontFantasy, FontCount };

// One set for the whole application, as WebKit's page settings are applied
// to every view. Unset: NULL string, 0 size, -1 flag.
static struct {
    char* fonts[FontCount];
    LONG defaultSize, fixedSize, minimumSize;
    int javaScript = -1, loadsImages = -1;
    BOOL animatedImages = TRUE, animationLooping = TRUE;
    BOOL datatypes[4];
    char* cookieJar;
} s_prefs;

static void setString(char*& field, IPTR value)
{
    free(field);
    const char* s = (const char*)value;
    // An empty string (e.g. a cleared prefs field) means WebKit's default.
    field = s && *s ? strdup(s) : nullptr;
}

void webkittyApplyPreferences(WebKit::WebPage& page)
{
    auto* core = page.corePage();
    if (!core)
        return;
    auto& settings = core->settings();
    using FontSetter = void (WebCore::SettingsBase::*)(const WTF::String&, UScriptCode);
    static const FontSetter fontSetters[FontCount] = {
        &WebCore::SettingsBase::setStandardFontFamily, &WebCore::SettingsBase::setFixedFontFamily,
        &WebCore::SettingsBase::setSerifFontFamily, &WebCore::SettingsBase::setSansSerifFontFamily,
        &WebCore::SettingsBase::setCursiveFontFamily, &WebCore::SettingsBase::setFantasyFontFamily,
    };
    for (int i = 0; i < FontCount; i++) {
        if (s_prefs.fonts[i])
            (settings.*fontSetters[i])(WTF::AtomString::fromUTF8(s_prefs.fonts[i]), USCRIPT_COMMON);
    }
    if (s_prefs.defaultSize)
        settings.setDefaultFontSize(s_prefs.defaultSize);
    if (s_prefs.fixedSize)
        settings.setDefaultFixedFontSize(s_prefs.fixedSize);
    if (s_prefs.minimumSize)
        settings.setMinimumFontSize(s_prefs.minimumSize);
    if (s_prefs.loadsImages >= 0)
        settings.setLoadsImagesAutomatically(s_prefs.loadsImages);
    if (s_prefs.javaScript >= 0)
        page.setJavaScriptEnabled(s_prefs.javaScript);
}

// Cookies live in one database for all views (WebKit's default session).
static void setCookieJar(IPTR value)
{
    const char* path = (const char*)value;
    if (!path || !*path || (s_prefs.cookieJar && !strcmp(path, s_prefs.cookieJar)))
        return;
    setString(s_prefs.cookieJar, value);
    NetworkStorageSessionMap::defaultStorageSession().setCookieDatabase(
        makeUniqueRef<WebCore::CookieJarDB>(WTF::String::fromUTF8(path)));
}

static void setPreferences(struct TagItem* tags)
{
    struct TagItem* tstate = tags;
    struct TagItem* tag;
    bool changed = false;
    while ((tag = NextTagItem(&tstate))) {
        ULONG id = tag->ti_Tag;
        if (id >= MUIA_WebPreferences_StandardFontFamily && id <= MUIA_WebPreferences_FantasyFontFamily) {
            setString(s_prefs.fonts[id - MUIA_WebPreferences_StandardFontFamily], tag->ti_Data);
            changed = true;
            continue;
        }
        switch (id) {
        case MUIA_WebPreferences_DefaultFontSize: s_prefs.defaultSize = LONG(tag->ti_Data); break;
        case MUIA_WebPreferences_DefaultFixedFontSize: s_prefs.fixedSize = LONG(tag->ti_Data); break;
        case MUIA_WebPreferences_MinimumFontSize: s_prefs.minimumSize = LONG(tag->ti_Data); break;
        case MUIA_WebPreferences_JavaScriptEnabled: s_prefs.javaScript = tag->ti_Data != 0; break;
        case MUIA_WebPreferences_LoadsImagesAutomatically: s_prefs.loadsImages = tag->ti_Data != 0; break;
        case MUIA_WebPreferences_AllowsAnimatedImages: s_prefs.animatedImages = tag->ti_Data; break;
        case MUIA_WebPreferences_AllowAnimatedImageLooping: s_prefs.animationLooping = tag->ti_Data; break;
        case MUIA_WebPreferences_DecodesBMPWithDatatypes:
        case MUIA_WebPreferences_DecodesPNGWithDatatypes:
        case MUIA_WebPreferences_DecodesGIFWithDatatypes:
        case MUIA_WebPreferences_DecodesJPGWithDatatypes:
            s_prefs.datatypes[id - MUIA_WebPreferences_DecodesBMPWithDatatypes] = tag->ti_Data;
            break;
        case MUIA_WebPreferences_CookieJarFileName: setCookieJar(tag->ti_Data); continue;
        default: continue;
        }
        changed = true;
    }
    if (changed)
        webkittyForEachPage([](WebKit::WebPage& page) { webkittyApplyPreferences(page); });
}

static IPTR WebPreferences__OM_NEW(Class* cl, Object* obj, struct opSet* msg)
{
    obj = (Object*)DoSuperMethodA(cl, obj, (Msg)msg);
    if (obj)
        setPreferences(msg->ops_AttrList);
    return (IPTR)obj;
}

static IPTR WebPreferences__OM_SET(Class* cl, Object* obj, struct opSet* msg)
{
    setPreferences(msg->ops_AttrList);
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR WebPreferences__OM_GET(Class* cl, Object* obj, struct opGet* msg)
{
    ULONG id = msg->opg_AttrID;
    IPTR* storage = msg->opg_Storage;
    if (id >= MUIA_WebPreferences_StandardFontFamily && id <= MUIA_WebPreferences_FantasyFontFamily) {
        const char* font = s_prefs.fonts[id - MUIA_WebPreferences_StandardFontFamily];
        *storage = (IPTR)(font ? font : "");
        return TRUE;
    }
    switch (id) {
    // Unset values report WebKit's defaults.
    case MUIA_WebPreferences_DefaultFontSize: *storage = s_prefs.defaultSize ? s_prefs.defaultSize : 16; return TRUE;
    case MUIA_WebPreferences_DefaultFixedFontSize: *storage = s_prefs.fixedSize ? s_prefs.fixedSize : 13; return TRUE;
    case MUIA_WebPreferences_MinimumFontSize: *storage = s_prefs.minimumSize; return TRUE;
    case MUIA_WebPreferences_JavaScriptEnabled: *storage = s_prefs.javaScript != 0; return TRUE;
    case MUIA_WebPreferences_LoadsImagesAutomatically: *storage = s_prefs.loadsImages != 0; return TRUE;
    case MUIA_WebPreferences_AllowsAnimatedImages: *storage = s_prefs.animatedImages; return TRUE;
    case MUIA_WebPreferences_AllowAnimatedImageLooping: *storage = s_prefs.animationLooping; return TRUE;
    case MUIA_WebPreferences_DecodesBMPWithDatatypes:
    case MUIA_WebPreferences_DecodesPNGWithDatatypes:
    case MUIA_WebPreferences_DecodesGIFWithDatatypes:
    case MUIA_WebPreferences_DecodesJPGWithDatatypes:
        *storage = s_prefs.datatypes[id - MUIA_WebPreferences_DecodesBMPWithDatatypes];
        return TRUE;
    case MUIA_WebPreferences_CookieJarFileName: *storage = (IPTR)(s_prefs.cookieJar ? s_prefs.cookieJar : ""); return TRUE;
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

// No instance data: the state is global.
struct WebPreferences_DATA { };

extern "C" {
__ZUNE_CUSTOMCLASS_START(WebPreferences)
__ZUNE_CUSTOMCLASS_METHOD(WebPreferences__OM_NEW, OM_NEW, struct opSet*);
__ZUNE_CUSTOMCLASS_METHOD(WebPreferences__OM_SET, OM_SET, struct opSet*);
__ZUNE_CUSTOMCLASS_METHOD(WebPreferences__OM_GET, OM_GET, struct opGet*);
__ZUNE_CUSTOMCLASS_END(WebPreferences, NULL, MUIC_Notify, NULL)
}
