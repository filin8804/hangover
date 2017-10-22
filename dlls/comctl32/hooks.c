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
#include <commctrl.h>

#include "thunk/qemu_windows.h"
#include "thunk/qemu_commctrl.h"

#include "user32_wrapper.h"
#include "windows-user-services.h"
#include "dll_list.h"
#include "qemu_comctl32.h"

#ifndef QEMU_DLL_GUEST

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(qemu_comctl32);

#if HOST_BIT == GUEST_BIT

void hook_wndprocs()
{
    /* Do nothing */
}

#else

static WNDPROC orig_rebar_wndproc;

static LRESULT WINAPI rebar_wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT ret;
    REBARINFO rbi_host;
    REBARBANDINFOW band_host;

    struct qemu_REBARINFO *rbi_guest;
    struct qemu_REBARBANDINFO *band_guest;

    WINE_TRACE("Message %x\n", msg);

    switch (msg)
    {
        case RB_SETBARINFO:
            rbi_guest = (struct qemu_REBARINFO *)lParam;
            if (rbi_guest->cbSize == sizeof(REBARINFO))
                WINE_ERR("Got a guest message with host size. Called from a Wine DLL?.\n");

            REBARINFO_g2h(&rbi_host, rbi_guest);
            lParam = (LPARAM)&rbi_host;
            break;

        case RB_INSERTBANDA:
        case RB_INSERTBANDW:
            band_guest = (struct qemu_REBARBANDINFO *)lParam;
            if (band_guest)
            {
                if (band_guest->cbSize == sizeof(REBARBANDINFOW))
                    WINE_ERR("Got a guest message with host size. Called from a Wine DLL?.\n");

                REBARBANDINFO_g2h(&band_host, band_guest);
                lParam = (LPARAM)&band_host;
            }
            break;
    }

    ret = CallWindowProcW(orig_rebar_wndproc, hWnd, msg, wParam, lParam);

    return ret;
}

static void rebar_notify(MSG *guest, MSG *host, BOOL ret)
{
    NMHDR *hdr = (NMHDR *)host->lParam;
    struct qemu_NMREBARCHILDSIZE *childsize;

    WINE_TRACE("Handling a rebar notify message\n");
    if (ret)
    {
        if (guest->lParam != host->lParam)
            HeapFree(GetProcessHeap(), 0, (void *)guest->lParam);
        return;
    }

    switch (hdr->code)
    {
        case NM_CUSTOMDRAW:
            WINE_FIXME("Unhandled notify message NM_CUSTOMDRAW.\n");
            break;

        case NM_NCHITTEST:
            WINE_FIXME("Unhandled notify message NM_NCHITTEST.\n");
            break;

        case RBN_CHILDSIZE:
            WINE_TRACE("Handling notify message RBN_CHILDSIZE.\n");
            childsize = HeapAlloc(GetProcessHeap(), 0, sizeof(*childsize));
            NMREBARCHILDSIZE_h2g(childsize, (NMREBARCHILDSIZE *)hdr);
            guest->lParam = (LPARAM)childsize;
            break;

        case RBN_HEIGHTCHANGE:
            WINE_FIXME("Unhandled notify message RBN_HEIGHTCHANGE.\n");
            break;

        case RBN_AUTOSIZE:
            WINE_FIXME("Unhandled notify message RBN_AUTOSIZE.\n");
            break;

        case RBN_DELETINGBAND:
            WINE_FIXME("Unhandled notify message RBN_DELETINGBAND.\n");
            break;

        case RBN_CHEVRONPUSHED:
            WINE_FIXME("Unhandled notify message RBN_CHEVRONPUSHED.\n");
            break;

        case RBN_LAYOUTCHANGED:
            WINE_FIXME("Unhandled notify message RBN_LAYOUTCHANGED.\n");
            break;

        case RBN_BEGINDRAG:
            WINE_FIXME("Unhandled notify message RBN_BEGINDRAG.\n");
            break;

        case RBN_ENDDRAG:
            WINE_FIXME("Unhandled notify message RBN_ENDDRAG.\n");
            break;

        default:
            WINE_ERR("Unexpected notify message %x.\n", hdr->code);
    }
}

static WNDPROC hook_class(const WCHAR *name, WNDPROC replace)
{
    WNDPROC ret;
    HMODULE comctl32 = GetModuleHandleA("comctl32");
    HWND win;

    win = CreateWindowExW(0, name, NULL,
            WS_POPUP, 0, 0, 200, 60, NULL, NULL, comctl32, NULL);
    if (!win)
        WINE_ERR("Failed to instantiate a %s window.\n", wine_dbgstr_w(name));

    ret = (WNDPROC)SetClassLongPtrW(win, GCLP_WNDPROC, (ULONG_PTR)replace);
    if (!ret)
        WINE_ERR("Failed to set WNDPROC of %s.\n", wine_dbgstr_w(name));

    DestroyWindow(win);

    return ret;
}

void hook_wndprocs(void)
{
    /* Intercepting messages before they enter the control has the advantage that we
     * don't have to bother the user32 mapper with comctl32 internals and that we don't
     * have to inspect the window class on every WM_USER + x message. The disadvantage
     * is that the hook wndproc will also run when the message is received from another
     * Wine DLL, in which case translation will break things. */

    orig_rebar_wndproc = hook_class(REBARCLASSNAMEW, rebar_wndproc);
    /* Toolbars: Used by Wine's comdlg32, and internal messages from comctl32. */
}

void register_notify_callbacks(void)
{
    QEMU_USER32_NOTIFY_FUNC register_notify;
    HMODULE qemu_user32 = GetModuleHandleA("qemu_user32");

    if (!qemu_user32)
        WINE_ERR("Cannot get qemu_user32.dll\n");
    register_notify = (void *)GetProcAddress(qemu_user32, "qemu_user32_notify");
    if (!register_notify)
        WINE_ERR("Cannot get qemu_user32_notify\n");

    register_notify(REBARCLASSNAMEW, rebar_notify);
}

#endif

#endif
