/*
 * Copyright 2017 Stefan Dösinger for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* NOTE: The guest side uses mingw's headers. The host side uses Wine's headers. */

#include <windows.h>
#include <stdio.h>
#include <richedit.h>

#ifdef QEMU_DLL_GUEST

#include <initguid.h>
#include <richole.h>
DEFINE_GUID(IID_ITextServices, 0x8d33f740, 0xcf58, 0x11ce, 0xa8, 0x9d, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5);
DEFINE_GUID(IID_ITextHost, 0x13e670f4,0x1a5a,0x11cf,0xab,0xeb,0x00,0xaa,0x00,0xb6,0x5e,0xa1);
DEFINE_GUID(IID_ITextHost2, 0x13e670f5,0x1a5a,0x11cf,0xab,0xeb,0x00,0xaa,0x00,0xb6,0x5e,0xa1);

#endif

/* Separate because of <initguid.h>. */
#include <richole.h>

#include "windows-user-services.h"
#include "dll_list.h"
#include "qemu_riched20.h"

struct callback_impl
{
    /* Guest side */
    IRichEditOleCallback *guest;

    /* Host side */
    IRichEditOleCallback IRichEditOleCallback_iface;
    ULONG refcount;
};

struct qemu_set_ole_callbacks
{
    struct qemu_syscall super;
    uint64_t QueryInterface;
    uint64_t AddRef;
    uint64_t Release;
};

#ifdef QEMU_DLL_GUEST

static uint64_t __fastcall guest_ole_callback_AddRef(struct callback_impl *impl)
{
    return impl->guest->lpVtbl->AddRef(impl->guest);
}

static uint64_t __fastcall guest_ole_callback_Release(struct callback_impl *impl)
{
    return impl->guest->lpVtbl->Release(impl->guest);
}

BOOL WINAPI DllMainCRTStartup(HMODULE mod, DWORD reason, void *reserved)
{
    struct qemu_set_ole_callbacks call;

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            call.super.id = QEMU_SYSCALL_ID(CALL_SET_OLE_CALLBACKS);
            call.QueryInterface = 0;
            call.AddRef = (ULONG_PTR)guest_ole_callback_AddRef;
            call.Release = (ULONG_PTR)guest_ole_callback_Release;
            qemu_syscall(&call.super);
            break;
    }
    return TRUE;
}

#else

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(qemu_riched20);

const struct qemu_ops *qemu_ops;

static uint64_t guest_ole_callback_QueryInterface;
static uint64_t guest_ole_callback_AddRef;
static uint64_t guest_ole_callback_Release;

static void qemu_set_ole_callbacks(struct qemu_syscall *call)
{
    struct qemu_set_ole_callbacks *c = (struct qemu_set_ole_callbacks *)call;
    WINE_TRACE("\n");

    guest_ole_callback_QueryInterface = c->QueryInterface;
    guest_ole_callback_AddRef = c->AddRef;
    guest_ole_callback_Release = c->Release;
}

static const syscall_handler dll_functions[] =
{
    qemu_CreateTextServices,
    qemu_REExtendedRegisterClass,
    qemu_RichEdit10ANSIWndProc,
    qemu_RichEditANSIWndProc,
    qemu_set_ole_callbacks,
};

static inline struct callback_impl *impl_from_IRichEditOleCallback(IRichEditOleCallback *iface)
{
    return CONTAINING_RECORD(iface, struct callback_impl, IRichEditOleCallback_iface);
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_QueryInterface(IRichEditOleCallback *iface,
        REFIID riid, void **obj)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);

    WINE_FIXME("(%p, %s, %p)\n", impl, wine_dbgstr_guid(riid), obj);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRichEditOleCallback))
    {
        iface->lpVtbl->AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    WINE_FIXME("Unknown interface: %s\n", wine_dbgstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE RichEditOleCallback_AddRef(IRichEditOleCallback *iface)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    ULONG refcount = InterlockedIncrement(&impl->refcount);

    WINE_TRACE("%p increasing refcount to %u.\n", impl, refcount);
    return refcount;
}

static ULONG STDMETHODCALLTYPE RichEditOleCallback_Release(IRichEditOleCallback *iface)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    ULONG refcount = InterlockedDecrement(&impl->refcount);

    WINE_TRACE("%p decreasing refcount to %u.\n", impl, refcount);
    if (!refcount)
    {
        uint64_t ret;
        WINE_TRACE("Releasing guest inteface.\n");
        ret = qemu_ops->qemu_execute(QEMU_G2H(guest_ole_callback_Release), QEMU_H2G(impl));
        WINE_TRACE("Guest returned refcount %lu.\n", ret);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_GetNewStorage(IRichEditOleCallback* iface,
        LPSTORAGE *lplpstg)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %p)\n", impl, lplpstg);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_GetInPlaceContext(IRichEditOleCallback* iface,
        LPOLEINPLACEFRAME *lplpFrame, LPOLEINPLACEUIWINDOW *lplpDoc,
        LPOLEINPLACEFRAMEINFO lpFrameInfo)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %p, %p, %p) stub\n", impl, lplpFrame, lplpDoc, lpFrameInfo);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_ShowContainerUI(IRichEditOleCallback* iface,
        BOOL fShow)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %d)\n", impl, fShow);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_QueryInsertObject(IRichEditOleCallback* iface,
        LPCLSID lpclsid, LPSTORAGE lpstg, LONG cp)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %p, %p, %d)\n", impl, lpclsid, lpstg, cp);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_DeleteObject(IRichEditOleCallback* iface,
        LPOLEOBJECT lpoleobj)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %p)\n", impl, lpoleobj);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_QueryAcceptData(IRichEditOleCallback* iface,
        LPDATAOBJECT lpdataobj, CLIPFORMAT *lpcfFormat, DWORD reco, BOOL fReally,
        HGLOBAL hMetaPict)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %p, %p, %x, %d, %p)\n",
            impl, lpdataobj, lpcfFormat, reco, fReally, hMetaPict);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_ContextSensitiveHelp(IRichEditOleCallback* iface,
        BOOL fEnterMode)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %d)\n", impl, fEnterMode);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_GetClipboardData(IRichEditOleCallback* iface,
        CHARRANGE *lpchrg, DWORD reco, LPDATAOBJECT *lplpdataobj)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %p, %x, %p)\n", impl, lpchrg, reco, lplpdataobj);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_GetDragDropEffect( IRichEditOleCallback* iface, BOOL fDrag,
        DWORD grfKeyState, LPDWORD pdwEffect)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_FIXME("(%p, %d, %x, %p)\n", impl, fDrag, grfKeyState, pdwEffect);
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE RichEditOleCallback_GetContextMenu(IRichEditOleCallback* iface, WORD seltype,
        LPOLEOBJECT lpoleobj, CHARRANGE *lpchrg, HMENU *lphmenu)
{
    struct callback_impl *impl = impl_from_IRichEditOleCallback(iface);
    WINE_TRACE("(%p, %x, %p, %p, %p)\n", impl, seltype, lpoleobj, lpchrg, lphmenu);

    return E_FAIL;
}

static const struct IRichEditOleCallbackVtbl ole_callback_vtbl =
{
    RichEditOleCallback_QueryInterface,
    RichEditOleCallback_AddRef,
    RichEditOleCallback_Release,
    RichEditOleCallback_GetNewStorage,
    RichEditOleCallback_GetInPlaceContext,
    RichEditOleCallback_ShowContainerUI,
    RichEditOleCallback_QueryInsertObject,
    RichEditOleCallback_DeleteObject,
    RichEditOleCallback_QueryAcceptData,
    RichEditOleCallback_ContextSensitiveHelp,
    RichEditOleCallback_GetClipboardData,
    RichEditOleCallback_GetDragDropEffect,
    RichEditOleCallback_GetContextMenu
};

WNDPROC orig_proc_w, orig_proc_a;

static LRESULT handle_set_ole_callback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, BOOL unicode)
{
    struct callback_impl *cb;
    uint64_t ref;
    LRESULT ret;

    /* Note that injecting the wrapper COM interface here only works because no other Wine DLL uses
     * riched20. If a builtin library called riched20 we would need a way to figure out if the message
     * comes from the guest application (and thus needs the wrapper) or comes from a host DLL (and thus
     * already has ARM code, and can't be called inside the VM). That would require integration with
     * user32's wndproc wrappers.
     *
     * One hack that wouldn't involve breaking DLL separation would be to try to call QueryInterface
     * in a try/catch handler and see if it returns something useful or crashes. */

    cb = HeapAlloc(GetProcessHeap(), 0, sizeof(*cb));
    cb->guest = (void *)lParam;
    cb->refcount = 1;
    cb->IRichEditOleCallback_iface.lpVtbl = &ole_callback_vtbl;

    WINE_TRACE("AddRefing guest inteface.\n");
    ref = qemu_ops->qemu_execute(QEMU_G2H(guest_ole_callback_AddRef), QEMU_H2G(cb));
    WINE_TRACE("Guest returned refcount %lu.\n", ref);

    if (unicode)
        ret = CallWindowProcW(orig_proc_w, hWnd, msg, wParam, (LPARAM)&cb->IRichEditOleCallback_iface);
    else
        ret = CallWindowProcA(orig_proc_a, hWnd, msg, wParam, (LPARAM)&cb->IRichEditOleCallback_iface);

    /* This will destroy the callback in case the host procedure didn't do anything with it. */
    cb->IRichEditOleCallback_iface.lpVtbl->Release(&cb->IRichEditOleCallback_iface);
}

static LRESULT WINAPI wrap_proc_w(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case EM_SETOLECALLBACK:
            return handle_set_ole_callback(hWnd, msg, wParam, lParam, TRUE);

        default:
            return CallWindowProcW(orig_proc_w, hWnd, msg, wParam, lParam);
    }
}

LRESULT WINAPI wrap_proc_a(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case EM_SETOLECALLBACK:
            return handle_set_ole_callback(hWnd, msg, wParam, lParam, FALSE);

        default:
            return CallWindowProcA(orig_proc_a, hWnd, msg, wParam, lParam);
    }
}

static inline BOOL is_version_nt(void)
{
    return !(GetVersion() & 0x80000000);
}

const WINAPI syscall_handler *qemu_dll_register(const struct qemu_ops *ops, uint32_t *dll_num)
{
    HWND win;
    HMODULE riched20 = GetModuleHandleA("riched20");
    WNDPROC wndproc2;

    WINE_TRACE("Loading host-side riched20 wrapper.\n");

    /* Apparently the only way to change the class wndproc is through a window of that class... */

    win = CreateWindowExA(0, RICHEDIT_CLASS20A, NULL,
            WS_POPUP, 0, 0, 200, 60, NULL, NULL, riched20, NULL);
    if (!win)
        WINE_ERR("Failed to instantiate a RICHEDIT_CLASS20A window.\n");

    orig_proc_a = (WNDPROC)SetClassLongPtrA(win, GCLP_WNDPROC, (ULONG_PTR)wrap_proc_a);
    if (!orig_proc_a)
        WINE_ERR("Failed to set WNDPROC of RICHEDIT_CLASS20A.\n");

    DestroyWindow(win);

    win = CreateWindowExA(0, "RichEdit50A", NULL,
            WS_POPUP, 0, 0, 200, 60, NULL, NULL, riched20, NULL);
    if (!win)
        WINE_ERR("Failed to instantiate a RichEdit50A window.\n");

    wndproc2 = (WNDPROC)SetClassLongPtrA(win, GCLP_WNDPROC, (ULONG_PTR)wrap_proc_a);
    if (wndproc2 != orig_proc_a)
        WINE_ERR("Failed to set WNDPROC of RichEdit50A.\n");

    DestroyWindow(win);

    if (is_version_nt())
    {
        win = CreateWindowExW(0, RICHEDIT_CLASS20W, NULL,
                WS_POPUP, 0, 0, 200, 60, NULL, NULL, riched20, NULL);
        if (!win)
            WINE_ERR("Failed to instantiate a RICHEDIT_CLASS20W window.\n");

        orig_proc_w = (WNDPROC)SetClassLongPtrW(win, GCLP_WNDPROC, (ULONG_PTR)wrap_proc_w);
        if (!orig_proc_w)
            WINE_ERR("Failed to set WNDPROC of RICHEDIT_CLASS20W.\n");

        DestroyWindow(win);

        win = CreateWindowExW(0, MSFTEDIT_CLASS, NULL,
                WS_POPUP, 0, 0, 200, 60, NULL, NULL, riched20, NULL);
        if (!win)
            WINE_ERR("Failed to instantiate a MSFTEDIT_CLASS window.\n");

        wndproc2 = (WNDPROC)SetClassLongPtrW(win, GCLP_WNDPROC, (ULONG_PTR)wrap_proc_w);
        if (wndproc2 != orig_proc_w)
            WINE_ERR("Failed to set WNDPROC of MSFTEDIT_CLASS.\n");

        DestroyWindow(win);
    }
    else
    {
        win = CreateWindowExA(0, "RichEdit20W", NULL,
                WS_POPUP, 0, 0, 200, 60, NULL, NULL, riched20, NULL);
        if (!win)
            WINE_ERR("Failed to instantiate a RICHEDIT_CLASS20W window.\n");

        orig_proc_w = (WNDPROC)SetClassLongPtrA(win, GCLP_WNDPROC, (ULONG_PTR)wrap_proc_w);
        if (!orig_proc_w)
            WINE_ERR("Failed to set WNDPROC of RICHEDIT_CLASS20W.\n");

        DestroyWindow(win);

        win = CreateWindowExA(0, "RichEdit50W", NULL,
                WS_POPUP, 0, 0, 200, 60, NULL, NULL, riched20, NULL);
        if (!win)
            WINE_ERR("Failed to instantiate a RichEdit50W window.\n");

        wndproc2 = (WNDPROC)SetClassLongPtrA(win, GCLP_WNDPROC, (ULONG_PTR)wrap_proc_w);
        if (wndproc2 != orig_proc_w)
            WINE_ERR("Failed to set WNDPROC of RichEdit50W.\n");

        DestroyWindow(win);
    }

    /* Overwrite W: Confused. */

    qemu_ops = ops;
    *dll_num = QEMU_CURRENT_DLL;

    return dll_functions;
}

#endif
