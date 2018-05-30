/*
 * Copyright 2017 André Hentschel
 * Copyright 2018 Stefan Dösinger for CodeWeavers
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

#define COBJMACROS

#include <windows.h>
#include <stdio.h>
#include <d3d11_1.h>

#include "windows-user-services.h"
#include "dll_list.h"

#ifdef QEMU_DLL_GUEST
#include <dxgi1_2.h>
#include <debug.h>
#else
#include <dxgi1_5.h>
#include <wine/debug.h>
#endif

#include "qemu_dxgi.h"

WINE_DEFAULT_DEBUG_CHANNEL(qemu_dxgi);

struct qemu_dxgi_device_QueryInterface
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t riid;
    uint64_t object;
};

#ifdef QEMU_DLL_GUEST

static inline struct qemu_dxgi_device *impl_from_IDXGIDevice2(IDXGIDevice2 *iface)
{
    return CONTAINING_RECORD(iface, struct qemu_dxgi_device, IDXGIDevice2_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_device_QueryInterface(IDXGIDevice2 *iface, REFIID riid, void **object)
{
    struct qemu_dxgi_device_QueryInterface call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    WINE_TRACE("iface %p, riid %s, object %p\n", iface, wine_dbgstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_IDXGIObject)
            || IsEqualGUID(riid, &IID_IDXGIDevice)
            || IsEqualGUID(riid, &IID_IDXGIDevice1)
            || IsEqualGUID(riid, &IID_IDXGIDevice2))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (device->child_layer)
    {
        WINE_TRACE("forwarding to child layer %p.\n", device->child_layer);
        return IUnknown_QueryInterface(device->child_layer, riid, object);
    }

    /* Test if we're supposed to know about it. */
    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_QUERYINTERFACE);
    call.iface = (ULONG_PTR)device;
    call.riid = (ULONG_PTR)riid;

    qemu_syscall(&call.super);
    *object = NULL;

    return E_NOINTERFACE;
}

#else

void qemu_dxgi_device_QueryInterface(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_QueryInterface *c = (struct qemu_dxgi_device_QueryInterface *)call;
    struct qemu_dxgi_device *device;
    IUnknown *obj;

    device = QEMU_G2H(c->iface);
    c->super.iret = IDXGIDevice2_QueryInterface(device->host, QEMU_G2H(c->riid), (void **)&obj);
    if (SUCCEEDED(c->super.iret))
    {
        WINE_FIXME("Host device returned IID %s which this wrapper does not know about.\n",
                wine_dbgstr_guid(QEMU_G2H(c->riid)));
        IUnknown_Release(obj);
    }
}

#endif

struct qemu_dxgi_device_AddRef
{
    struct qemu_syscall super;
    uint64_t iface;
};

#ifdef QEMU_DLL_GUEST

static ULONG STDMETHODCALLTYPE dxgi_device_AddRef(IDXGIDevice2 *iface)
{
    struct qemu_dxgi_device_AddRef call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_ADDREF);
    call.iface = (ULONG_PTR)device;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_AddRef(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_AddRef *c = (struct qemu_dxgi_device_AddRef *)call;
    struct qemu_dxgi_device *device;

    WINE_TRACE("\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_AddRef(device->host);
}

#endif

struct qemu_dxgi_device_Release
{
    struct qemu_syscall super;
    uint64_t iface;
};

#ifdef QEMU_DLL_GUEST

void __fastcall dxgi_device_guest_destroy(struct qemu_dxgi_device *device)
{
    wined3d_private_store_cleanup(&device->private_store);
}

static ULONG STDMETHODCALLTYPE dxgi_device_Release(IDXGIDevice2 *iface)
{
    struct qemu_dxgi_device_Release call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_RELEASE);
    call.iface = (ULONG_PTR)device;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

ULONG qemu_dxgi_device_Release_internal(struct qemu_dxgi_device *device)
{
    ULONG ref;
    struct qemu_dxgi_adapter *adapter = device->adapter;

    IDXGIAdapter2_AddRef(adapter->host);
    ref = IDXGIDevice2_Release(device->host);
    qemu_dxgi_adapter_Release_internal(adapter);

    if (!ref)
    {
        WINE_TRACE("Destroying dxgi device wrapper %p (host device %p.)\n", device, device->host);
        qemu_ops->qemu_execute(QEMU_G2H(dxgi_device_guest_destroy), QEMU_H2G(device));
        HeapFree(GetProcessHeap(), 0, device);
    }

    return ref;
}

void qemu_dxgi_device_Release(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_Release *c = (struct qemu_dxgi_device_Release *)call;
    struct qemu_dxgi_device *device;

    WINE_TRACE("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = qemu_dxgi_device_Release_internal(device);
}

#endif

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_SetPrivateData(IDXGIDevice2 *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    WINE_TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, wine_dbgstr_guid(guid), data_size, data);

    return dxgi_set_private_data(&device->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_device_SetPrivateDataInterface(IDXGIDevice2 *iface,
        REFGUID guid, const IUnknown *object)
{
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    WINE_TRACE("iface %p, guid %s, object %p.\n", iface, wine_dbgstr_guid(guid), object);

    return dxgi_set_private_data_interface(&device->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_device_GetPrivateData(IDXGIDevice2 *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    WINE_TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, wine_dbgstr_guid(guid), data_size, data);

    return dxgi_get_private_data(&device->private_store, guid, data_size, data);
}

#endif

struct qemu_dxgi_device_GetParent
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t parent;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_GetParent(IDXGIDevice2 *iface, REFIID riid, void **parent)
{
    struct qemu_dxgi_device_GetParent call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);
    struct qemu_dxgi_adapter *adapter;

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_GETPARENT);
    call.iface = (ULONG_PTR)device;

    qemu_syscall(&call.super);
    adapter = (struct qemu_dxgi_adapter *)(ULONG_PTR)call.parent;

    return IDXGIAdapter_QueryInterface(&adapter->IDXGIAdapter3_iface, riid, parent);
}

#else

void qemu_dxgi_device_GetParent(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_GetParent *c = (struct qemu_dxgi_device_GetParent *)call;
    struct qemu_dxgi_device *device;

    WINE_TRACE("\n");
    device = QEMU_G2H(c->iface);
    c->parent = QEMU_H2G(device->adapter);
}

#endif

struct qemu_dxgi_device_GetAdapter
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t adapter;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_GetAdapter(IDXGIDevice2 *iface, IDXGIAdapter **adapter)
{
    struct qemu_dxgi_device_GetAdapter call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);
    struct qemu_dxgi_adapter *adapter_impl;

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_GETADAPTER);
    call.iface = (ULONG_PTR)device;

    qemu_syscall(&call.super);
    adapter_impl = (struct qemu_dxgi_adapter *)(ULONG_PTR)call.adapter;
    if (SUCCEEDED(call.super.iret))
        *adapter = (IDXGIAdapter *)&adapter_impl->IDXGIAdapter3_iface;

    return call.super.iret;
}

#else

void qemu_dxgi_device_GetAdapter(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_GetAdapter *c = (struct qemu_dxgi_device_GetAdapter *)call;
    struct qemu_dxgi_device *device;
    IDXGIAdapter *adapter;

    WINE_TRACE("\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_GetAdapter(device->host, &adapter);
    if (FAILED(c->super.iret))
        return;
    if (adapter != (IDXGIAdapter *)device->adapter->host)
        WINE_FIXME("Did not get the adapter we expected.\n");

    c->adapter = QEMU_H2G(device->adapter);
}

#endif

struct qemu_dxgi_device_CreateSurface
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t desc;
    uint64_t surface_count;
    uint64_t usage;
    uint64_t shared_resource;
    uint64_t surface;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_CreateSurface(IDXGIDevice2 *iface, const DXGI_SURFACE_DESC *desc,
        UINT surface_count, DXGI_USAGE usage, const DXGI_SHARED_RESOURCE *shared_resource, IDXGISurface **surface)
{
    /* FIXME: This should be implemented on the client side by calling into d3d11. */
    struct qemu_dxgi_device_CreateSurface call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);
    struct qemu_dxgi_surface **obj;
    UINT i;

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_CREATESURFACE);
    call.iface = (ULONG_PTR)device;
    call.desc = (ULONG_PTR)desc;
    call.surface_count = surface_count;
    call.usage = usage;
    call.shared_resource = (ULONG_PTR)shared_resource;

    qemu_syscall(&call.super);
    if (FAILED(call.super.iret))
        return call.super.iret;

    obj = (struct qemu_dxgi_surface **)(ULONG_PTR)call.surface;
    for (i = 0; i < surface_count; ++i)
    {
        qemu_dxgi_surface_guest_init(obj[i], NULL);
        surface[i] = &obj[i]->IDXGISurface1_iface;
    }
    HeapFree(GetProcessHeap(), 0, obj);

    return call.super.iret;
}

#else

void qemu_dxgi_device_CreateSurface(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_CreateSurface *c = (struct qemu_dxgi_device_CreateSurface *)call;
    struct qemu_dxgi_device *device;
    IDXGISurface1 **host;
    struct qemu_dxgi_surface **obj;
    qemu_ptr *out32;
    UINT count, i, j;

    WINE_TRACE("\n");
    device = QEMU_G2H(c->iface);
    count = c->surface_count;

    host = HeapAlloc(GetProcessHeap(), 0, sizeof(*host) * count);
    if (!host)
    {
        WINE_WARN("Out of memory\n");
        c->super.iret = E_OUTOFMEMORY;
        return;
    }
    obj = HeapAlloc(GetProcessHeap(), 0, sizeof(*obj) * count);
    if (!host)
    {
        WINE_WARN("Out of memory\n");
        c->super.iret = E_OUTOFMEMORY;
        HeapFree(GetProcessHeap(), 0, host);
        return;
    }

    c->super.iret = IDXGIDevice2_CreateSurface(device->host, QEMU_G2H(c->desc), c->surface_count,
            c->usage, QEMU_G2H(c->shared_resource), (IDXGISurface **)host);
    if (FAILED(c->super.iret))
    {
        HeapFree(GetProcessHeap(), 0, obj);
        HeapFree(GetProcessHeap(), 0, host);
        return;
    }

    for (i = 0; i < count; ++i)
    {
        c->super.iret = qemu_dxgi_surface_create(host[i], device, &obj[i]);
        if (FAILED(c->super.iret))
            goto release;
    }

#if GUEST_BIT != HOST_BIT
    out32 = (qemu_ptr *)obj;
    for (i = 0; i < count; ++i)
        out32[i] = QEMU_H2G(obj[i]);
#endif
    c->surface = QEMU_H2G(obj);
    return;

release:
    for (j = 0; j < i; ++j)
        HeapFree(GetProcessHeap(), 0, obj[i]);
    for (i = 0; i < count; ++i)
        IDXGISurface1_Release(host[i]);
    HeapFree(GetProcessHeap(), 0, obj);
    HeapFree(GetProcessHeap(), 0, host);
}

#endif

struct qemu_dxgi_device_QueryResourceResidency
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t resources;
    uint64_t residency;
    uint64_t resource_count;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_QueryResourceResidency(IDXGIDevice2 *iface, IUnknown *const *resources, DXGI_RESIDENCY *residency, UINT resource_count)
{
    struct qemu_dxgi_device_QueryResourceResidency call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_QUERYRESOURCERESIDENCY);
    call.iface = (ULONG_PTR)device;
    call.resources = (ULONG_PTR)resources;
    call.residency = (ULONG_PTR)residency;
    call.resource_count = resource_count;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_QueryResourceResidency(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_QueryResourceResidency *c = (struct qemu_dxgi_device_QueryResourceResidency *)call;
    struct qemu_dxgi_device *device;

    WINE_FIXME("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_QueryResourceResidency(device->host, QEMU_G2H(c->resources), QEMU_G2H(c->residency), c->resource_count);
}

#endif

struct qemu_dxgi_device_SetGPUThreadPriority
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t priority;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_SetGPUThreadPriority(IDXGIDevice2 *iface, INT priority)
{
    struct qemu_dxgi_device_SetGPUThreadPriority call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_SETGPUTHREADPRIORITY);
    call.iface = (ULONG_PTR)device;
    call.priority = priority;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_SetGPUThreadPriority(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_SetGPUThreadPriority *c = (struct qemu_dxgi_device_SetGPUThreadPriority *)call;
    struct qemu_dxgi_device *device;

    WINE_FIXME("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_SetGPUThreadPriority(device->host, c->priority);
}

#endif

struct qemu_dxgi_device_GetGPUThreadPriority
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t priority;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_GetGPUThreadPriority(IDXGIDevice2 *iface, INT *priority)
{
    struct qemu_dxgi_device_GetGPUThreadPriority call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_GETGPUTHREADPRIORITY);
    call.iface = (ULONG_PTR)device;
    call.priority = (ULONG_PTR)priority;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_GetGPUThreadPriority(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_GetGPUThreadPriority *c = (struct qemu_dxgi_device_GetGPUThreadPriority *)call;
    struct qemu_dxgi_device *device;

    WINE_FIXME("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_GetGPUThreadPriority(device->host, QEMU_G2H(c->priority));
}

#endif

struct qemu_dxgi_device_SetMaximumFrameLatency
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t max_latency;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_SetMaximumFrameLatency(IDXGIDevice2 *iface, UINT max_latency)
{
    struct qemu_dxgi_device_SetMaximumFrameLatency call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_SETMAXIMUMFRAMELATENCY);
    call.iface = (ULONG_PTR)device;
    call.max_latency = max_latency;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_SetMaximumFrameLatency(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_SetMaximumFrameLatency *c = (struct qemu_dxgi_device_SetMaximumFrameLatency *)call;
    struct qemu_dxgi_device *device;

    WINE_FIXME("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_SetMaximumFrameLatency(device->host, c->max_latency);
}

#endif

struct qemu_dxgi_device_GetMaximumFrameLatency
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t max_latency;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_GetMaximumFrameLatency(IDXGIDevice2 *iface, UINT *max_latency)
{
    struct qemu_dxgi_device_GetMaximumFrameLatency call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_GETMAXIMUMFRAMELATENCY);
    call.iface = (ULONG_PTR)device;
    call.max_latency = (ULONG_PTR)max_latency;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_GetMaximumFrameLatency(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_GetMaximumFrameLatency *c = (struct qemu_dxgi_device_GetMaximumFrameLatency *)call;
    struct qemu_dxgi_device *device;

    WINE_FIXME("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_GetMaximumFrameLatency(device->host, QEMU_G2H(c->max_latency));
}

#endif

struct qemu_dxgi_device_OfferResources
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t resource_count;
    uint64_t resources;
    uint64_t priority;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_OfferResources(IDXGIDevice2 *iface, UINT resource_count, IDXGIResource * const *resources, DXGI_OFFER_RESOURCE_PRIORITY priority)
{
    struct qemu_dxgi_device_OfferResources call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_OFFERRESOURCES);
    call.iface = (ULONG_PTR)device;
    call.resource_count = resource_count;
    call.resources = (ULONG_PTR)resources;
    call.priority = priority;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_OfferResources(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_OfferResources *c = (struct qemu_dxgi_device_OfferResources *)call;
    struct qemu_dxgi_device *device;

    WINE_FIXME("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_OfferResources(device->host, c->resource_count, QEMU_G2H(c->resources), c->priority);
}

#endif

struct qemu_dxgi_device_ReclaimResources
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t resource_count;
    uint64_t resources;
    uint64_t discarded;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_ReclaimResources(IDXGIDevice2 *iface, UINT resource_count, IDXGIResource * const *resources, BOOL *discarded)
{
    struct qemu_dxgi_device_ReclaimResources call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_RECLAIMRESOURCES);
    call.iface = (ULONG_PTR)device;
    call.resource_count = resource_count;
    call.resources = (ULONG_PTR)resources;
    call.discarded = (ULONG_PTR)discarded;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_ReclaimResources(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_ReclaimResources *c = (struct qemu_dxgi_device_ReclaimResources *)call;
    struct qemu_dxgi_device *device;

    WINE_FIXME("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_ReclaimResources(device->host, c->resource_count, QEMU_G2H(c->resources), QEMU_G2H(c->discarded));
}

#endif

struct qemu_dxgi_device_EnqueueSetEvent
{
    struct qemu_syscall super;
    uint64_t iface;
    uint64_t event;
};

#ifdef QEMU_DLL_GUEST

static HRESULT STDMETHODCALLTYPE dxgi_device_EnqueueSetEvent(IDXGIDevice2 *iface, HANDLE event)
{
    struct qemu_dxgi_device_EnqueueSetEvent call;
    struct qemu_dxgi_device *device = impl_from_IDXGIDevice2(iface);

    call.super.id = QEMU_SYSCALL_ID(CALL_DXGI_DEVICE_ENQUEUESETEVENT);
    call.iface = (ULONG_PTR)device;
    call.event = (ULONG_PTR)event;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_dxgi_device_EnqueueSetEvent(struct qemu_syscall *call)
{
    struct qemu_dxgi_device_EnqueueSetEvent *c = (struct qemu_dxgi_device_EnqueueSetEvent *)call;
    struct qemu_dxgi_device *device;

    WINE_FIXME("Unverified!\n");
    device = QEMU_G2H(c->iface);

    c->super.iret = IDXGIDevice2_EnqueueSetEvent(device->host, QEMU_G2H(c->event));
}

#endif

#ifdef QEMU_DLL_GUEST

static const struct IDXGIDevice2Vtbl dxgi_device_vtbl =
{
    /* IUnknown methods */
    dxgi_device_QueryInterface,
    dxgi_device_AddRef,
    dxgi_device_Release,
    /* IDXGIObject methods */
    dxgi_device_SetPrivateData,
    dxgi_device_SetPrivateDataInterface,
    dxgi_device_GetPrivateData,
    dxgi_device_GetParent,
    /* IDXGIDevice methods */
    dxgi_device_GetAdapter,
    dxgi_device_CreateSurface,
    dxgi_device_QueryResourceResidency,
    dxgi_device_SetGPUThreadPriority,
    dxgi_device_GetGPUThreadPriority,
    /* IDXGIDevice1 methods */
    dxgi_device_SetMaximumFrameLatency,
    dxgi_device_GetMaximumFrameLatency,
    /* IDXGIDevice2 methods */
    dxgi_device_OfferResources,
    dxgi_device_ReclaimResources,
    dxgi_device_EnqueueSetEvent,
};

void qemu_dxgi_device_guest_init(struct qemu_dxgi_device *device)
{
    device->IDXGIDevice2_iface.lpVtbl = &dxgi_device_vtbl;
    wined3d_private_store_init(&device->private_store);
}

struct qemu_dxgi_device *unsafe_impl_from_IDXGIDevice(IUnknown *iface)
{
    IDXGIDevice2 *qemu_device;
    struct qemu_dxgi_device *device;
    HRESULT hr;

    if (!iface)
        return NULL;
    if (FAILED(hr = IDXGIAdapter_QueryInterface(iface, &IID_IDXGIDevice2, (void **)&qemu_device)))
    {
        WINE_ERR("Failed to get IDXGIDevice2 interface, hr %#x.\n", hr);
        return NULL;
    }
    if (qemu_device->lpVtbl != (void *)&dxgi_device_vtbl)
    {
        WINE_ERR("Handle other people's device interfaces.\n");
        device =  NULL;
    }
    else
    {
        device = CONTAINING_RECORD(qemu_device, struct qemu_dxgi_device, IDXGIDevice2_iface);
    }
    IDXGIDevice_Release(qemu_device);
    return device;
}

#else

extern HRESULT WINAPI DXGID3D10CreateDevice(HMODULE d3d10core, IDXGIFactory *factory, IDXGIAdapter *adapter,
        unsigned int flags, const D3D_FEATURE_LEVEL *feature_levels, unsigned int level_count, void **device);
HRESULT qemu_dxgi_device_create(HMODULE mod, struct qemu_dxgi_adapter *adapter, struct qemu_dxgi_factory *factory,
        unsigned int flags, const D3D_FEATURE_LEVEL *feature_levels, unsigned int level_count, size_t layer_size,
        struct qemu_dxgi_device **device)
{
    struct qemu_dxgi_device *obj;
    HRESULT hr;
    IUnknown *unk_device;

    obj = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*obj) + layer_size);
    if (!obj)
    {
        WINE_WARN("Out of memory\n");
        return E_OUTOFMEMORY;
    }

    obj->adapter = adapter;

    /* Note that calling DXGID3D10CreateDevice here assumes that D3D11CreateDevice doesn't do any
     * magic on its own, beyond passing the upper layer HMODULE to dxgi. */
    hr = DXGID3D10CreateDevice(mod, factory ? (IDXGIFactory *)factory->host : NULL,
            adapter ? (IDXGIAdapter *)adapter->host : NULL,
            flags, feature_levels, level_count, (void **)&unk_device);
    if (FAILED(hr))
    {
        WINE_FIXME("Host D3D11CoreCreateDevice failed.\n");
        HeapFree(GetProcessHeap(), 0, obj);
        return hr;
    }

    hr = IUnknown_QueryInterface(unk_device, &IID_IDXGIDevice2, (void **)&obj->host);
    if (FAILED(hr))
        WINE_ERR("QI for IDXGIDevice2 failed.\n");
    IUnknown_Release(unk_device);

    *device = obj;
    return hr;
}

#endif
