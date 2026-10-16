/*
 * Zune DownloadDelegate class: runs downloads (WebCore's CurlDownload) and
 * reports them to the application's subclass, in OWB's order: DidBegin,
 * DecideDestination (answered with SetDestination), DidReceive*, then
 * DidFinish or DidFailWithError.
 */

// WebKit headers first, as in WebViewZune.cpp.
#include "WebKit.h"
#include "WebProcess.h"
#include <WebCore/CurlDownload.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceResponse.h>
#include <pal/text/TextEncoding.h>
#include <wtf/text/CString.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>
#include <zune/customclasses.h>

#include "DownloadDelegateZune.h"
#include "WebViewZune_private.h"

extern "C" void wkAROSDebugPrintf(const char* format, ...);

static Object* s_delegate;

namespace {

// The handle the application sees. It lives until its delegate goes away,
// so the application may still refer to finished downloads.
class Download final : public WebCore::CurlDownloadListener {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(Download);
public:
    explicit Download(Object* delegate)
        : m_delegate(delegate)
        , m_curl(adoptRef(new WebCore::CurlDownload()))
    {
    }

    void start(const WTF::URL& url, const WTF::String& suggestedName)
    {
        m_name = suggestedName;
        m_curl->init(*this, WTF::URL { url }, WebKit::WebProcess::singleton().networkingContext());
        begin();
    }

    void start(WebCore::ResourceHandle* handle, const WebCore::ResourceRequest& request, const WebCore::ResourceResponse& response)
    {
        m_name = response.suggestedFilename();
        m_curl->init(*this, handle, request, response);
        begin();
    }

    void setDestination(const char* destination)
    {
        if (m_done)
            return;
        if (destination)
            m_curl->setDestination(WTF::String::fromUTF8(destination));
        else
            cancel();
    }

    // m_curl stays referenced: this may run inside one of its callbacks.
    void cancel()
    {
        if (m_done)
            return;
        m_done = true;
        m_curl->setDeleteTmpFile(true);
        m_curl->setListener(nullptr);
        m_curl->cancel();
    }

private:
    void begin()
    {
        DoMethod(m_delegate, MUIM_DownloadDelegate_DidBeginDownload, (IPTR)this);
        m_curl->start();
    }

    void didReceiveResponse(const WebCore::ResourceResponse& response) override
    {
        if (response.httpStatusCode() >= 400) {
            fail();
            return;
        }
        WTF::String name = m_name;
        if (name.isEmpty())
            name = response.suggestedFilename();
        if (name.isEmpty())
            name = PAL::decodeURLEscapeSequences(response.url().lastPathComponent().toString());
        auto uname = name.utf8();
        // The application answers with SetDestination (NULL cancels).
        DoMethod(m_delegate, MUIM_DownloadDelegate_DecideDestinationWithSuggestedFilename, (IPTR)this, (IPTR)uname.data());
        if (!m_done && response.expectedContentLength() >= 0)
            DoMethod(m_delegate, MUIM_DownloadDelegate_DidReceiveExpectedContentLength, (IPTR)this, response.expectedContentLength());
    }

    void didReceiveDataOfLength(int size) override
    {
        DoMethod(m_delegate, MUIM_DownloadDelegate_DidReceiveDataOfLength, (IPTR)this, (IPTR)size);
    }

    void didFinish() override
    {
        m_done = true;
        m_curl->setListener(nullptr);
        DoMethod(m_delegate, MUIM_DownloadDelegate_DidFinishDownload, (IPTR)this);
    }

    void didFail(const WebCore::ResourceError& error) override
    {
        wkAROSDebugPrintf("[WebView] download failed: %s\n", error.localizedDescription().utf8().data());
        fail();
    }

    void fail()
    {
        cancel();
        DoMethod(m_delegate, MUIM_DownloadDelegate_DidFailWithError, (IPTR)this);
    }

    Object* m_delegate;
    RefPtr<WebCore::CurlDownload> m_curl;
    WTF::String m_name;
    bool m_done { false };
};

}

struct DownloadDelegate_DATA {
    WTF::Vector<std::unique_ptr<Download>>* downloads;
};

static Download* newDownload()
{
    if (!s_delegate)
        return nullptr;
    auto* data = (DownloadDelegate_DATA*)INST_DATA(DownloadDelegate_CLASS->mcc_Class, s_delegate);
    data->downloads->append(makeUnique<Download>(s_delegate));
    return data->downloads->last().get();
}

bool webkittyStartDownload(const WTF::URL& url, const WTF::String& suggestedName)
{
    Download* download = newDownload();
    if (download)
        download->start(url, suggestedName);
    return download;
}

bool webkittyStartDownload(WebCore::ResourceHandle* handle, const WebCore::ResourceRequest& request, const WebCore::ResourceResponse& response)
{
    Download* download = newDownload();
    if (download)
        download->start(handle, request, response);
    return download;
}

static IPTR DownloadDelegate__OM_NEW(Class* cl, Object* obj, struct opSet* msg)
{
    obj = (Object*)DoSuperMethodA(cl, obj, (Msg)msg);
    if (!obj)
        return 0;
    auto* data = (DownloadDelegate_DATA*)INST_DATA(cl, obj);
    data->downloads = new WTF::Vector<std::unique_ptr<Download>>;
    s_delegate = obj;
    return (IPTR)obj;
}

static IPTR DownloadDelegate__OM_DISPOSE(Class* cl, Object* obj, Msg msg)
{
    auto* data = (DownloadDelegate_DATA*)INST_DATA(cl, obj);
    if (s_delegate == obj)
        s_delegate = nullptr;
    for (auto& download : *data->downloads)
        download->cancel();
    delete data->downloads;
    return DoSuperMethodA(cl, obj, msg);
}

static IPTR DownloadDelegate__MUIM_CancelDownload(Class*, Object*, struct MUIP_DownloadDelegate_CancelDownload* msg)
{
    if (msg->download)
        static_cast<Download*>(msg->download)->cancel();
    return TRUE;
}

static IPTR DownloadDelegate__MUIM_SetDestination(Class*, Object*, struct MUIP_DownloadDelegate_SetDestination* msg)
{
    if (msg->download)
        static_cast<Download*>(msg->download)->setDestination(msg->destination);
    return TRUE;
}

extern "C" {
__ZUNE_CUSTOMCLASS_START(DownloadDelegate)
__ZUNE_CUSTOMCLASS_METHOD(DownloadDelegate__OM_NEW, OM_NEW, struct opSet*);
__ZUNE_CUSTOMCLASS_METHOD(DownloadDelegate__OM_DISPOSE, OM_DISPOSE, Msg);
__ZUNE_CUSTOMCLASS_METHOD(DownloadDelegate__MUIM_CancelDownload, MUIM_DownloadDelegate_CancelDownload, struct MUIP_DownloadDelegate_CancelDownload*);
__ZUNE_CUSTOMCLASS_METHOD(DownloadDelegate__MUIM_SetDestination, MUIM_DownloadDelegate_SetDestination, struct MUIP_DownloadDelegate_SetDestination*);
__ZUNE_CUSTOMCLASS_END(DownloadDelegate, NULL, MUIC_Window, NULL)
}
