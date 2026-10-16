#ifndef WEBKITTY_WEBVIEWZUNE_PRIVATE_H
#define WEBKITTY_WEBVIEWZUNE_PRIVATE_H

/* Shared by the webkitty classes; not installed. */

#include <wtf/Function.h>

namespace WebKit { class WebPage; }
namespace WebCore { class ResourceHandle; class ResourceRequest; class ResourceResponse; }
namespace WTF { class URL; class String; }

/* Every page of every WebView object (WebViewZune.cpp). */
void webkittyForEachPage(const WTF::Function<void(WebKit::WebPage&)>& function);

/* Applies the WebPreferences settings to a page (WebPreferencesZune.cpp). */
void webkittyApplyPreferences(WebKit::WebPage& page);

/* Hand a download to the DownloadDelegate object (DownloadDelegateZune.cpp).
   false: the application has none. */
bool webkittyStartDownload(const WTF::URL& url, const WTF::String& suggestedName);
bool webkittyStartDownload(WebCore::ResourceHandle* handle, const WebCore::ResourceRequest& request,
    const WebCore::ResourceResponse& response);

#endif /* WEBKITTY_WEBVIEWZUNE_PRIVATE_H */
