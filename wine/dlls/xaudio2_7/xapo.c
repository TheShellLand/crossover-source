/*
 * Copyright (c) 2015 Mark Harmstone
 * Copyright (c) 2015 Andrew Eikum for CodeWeavers
 * Copyright (c) 2018 Ethan Lee for CodeWeavers
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

#include "config.h"

#include <stdarg.h>

#define NONAMELESSUNION
#define COBJMACROS

#include "xaudio_private.h"
#include "xaudio2fx.h"
#include "xapofx.h"

#include "wine/debug.h"
#include "wine/heap.h"

#include <FAPO.h>
#include <FAPOFX.h>
#include <FAudioFX.h>

WINE_DEFAULT_DEBUG_CHANNEL(xaudio2);

static XA2XAPOFXImpl *impl_from_IXAPO(IXAPO *iface)
{
    return CONTAINING_RECORD(iface, XA2XAPOFXImpl, IXAPO_iface);
}

static XA2XAPOFXImpl *impl_from_IXAPOParameters(IXAPOParameters *iface)
{
    return CONTAINING_RECORD(iface, XA2XAPOFXImpl, IXAPOParameters_iface);
}

static HRESULT WINAPI XAPOFX_QueryInterface(IXAPO *iface, REFIID riid, void **ppvObject)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);

    TRACE("%p, %s, %p\n", This, wine_dbgstr_guid(riid), ppvObject);

    if(IsEqualGUID(riid, &IID_IUnknown) ||
            IsEqualGUID(riid, &IID_IXAPO) ||
            IsEqualGUID(riid, &IID_IXAPO27))
        *ppvObject = &This->IXAPO_iface;
    else if(IsEqualGUID(riid, &IID_IXAPOParameters) ||
            IsEqualGUID(riid, &IID_IXAPO27Parameters))
        *ppvObject = &This->IXAPOParameters_iface;
    else
        *ppvObject = NULL;

    if(*ppvObject){
        IUnknown_AddRef((IUnknown*)*ppvObject);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI XAPOFX_AddRef(IXAPO *iface)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    ULONG ref = This->fapo->AddRef(This->fapo);
    TRACE("(%p)->(): Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI XAPOFX_Release(IXAPO *iface)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    ULONG ref = This->fapo->Release(This->fapo);

    TRACE("(%p)->(): Refcount now %u\n", This, ref);

    if(!ref)
        HeapFree(GetProcessHeap(), 0, This);

    return ref;
}

static HRESULT WINAPI XAPOFX_GetRegistrationProperties(IXAPO *iface,
    XAPO_REGISTRATION_PROPERTIES **props)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    HRESULT hr;
    FAPORegistrationProperties *fprops;

    TRACE("%p, %p\n", This, props);

    hr = This->fapo->GetRegistrationProperties(This->fapo, &fprops);
    if(FAILED(hr))
        return hr;

    /* TODO: check for version == 20 and use XAPO20_REGISTRATION_PROPERTIES */
    *props = ADDRSPACECAST(XAPO_REGISTRATION_PROPERTIES *, fprops);
    return hr;
}

static HRESULT WINAPI XAPOFX_IsInputFormatSupported(IXAPO *iface,
        const WAVEFORMATEX *output_fmt, const WAVEFORMATEX *input_fmt,
        WAVEFORMATEX **supported_fmt)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    FAudioWaveFormatEx *supported_fa_fmt;
    HRESULT hr;

    TRACE("%p, %p, %p, %p\n", This, output_fmt, input_fmt, supported_fmt);
    hr = This->fapo->IsInputFormatSupported(This->fapo,
            (const FAudioWaveFormatEx *)output_fmt,
            (const FAudioWaveFormatEx *)input_fmt,
            supported_fmt ? &supported_fa_fmt : NULL);
    if (supported_fmt)
        *supported_fmt = ADDRSPACECAST(WAVEFORMATEX *, supported_fa_fmt);
    return hr;
}

static HRESULT WINAPI XAPOFX_IsOutputFormatSupported(IXAPO *iface,
        const WAVEFORMATEX *input_fmt, const WAVEFORMATEX *output_fmt,
        WAVEFORMATEX **supported_fmt)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    FAudioWaveFormatEx *supported_fa_fmt;
    HRESULT hr;

    TRACE("%p, %p, %p, %p\n", This, input_fmt, output_fmt, supported_fmt);
    hr = This->fapo->IsOutputFormatSupported(This->fapo,
            (const FAudioWaveFormatEx *)input_fmt,
            (const FAudioWaveFormatEx *)output_fmt,
            supported_fmt ? &supported_fa_fmt : NULL);
    if (supported_fmt)
        *supported_fmt = ADDRSPACECAST(WAVEFORMATEX *, supported_fa_fmt);
    return hr;
}

static HRESULT WINAPI XAPOFX_Initialize(IXAPO *iface, const void *data,
        UINT32 data_len)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    TRACE("%p, %p, %u\n", This, data, data_len);
    return This->fapo->Initialize(This->fapo, data, data_len);
}

static void WINAPI XAPOFX_Reset(IXAPO *iface)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    TRACE("%p\n", This);
    This->fapo->Reset(This->fapo);
}

static HRESULT WINAPI XAPOFX_LockForProcess(IXAPO *iface, UINT32 in_params_count,
        const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *in_params,
        UINT32 out_params_count,
        const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *out_params)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    TRACE("%p, %u, %p, %u, %p\n", This, in_params_count, in_params,
            out_params_count, out_params);
#ifndef __i386_on_x86_64__
    return This->fapo->LockForProcess(This->fapo,
            in_params_count,
            (const FAPOLockForProcessBufferParameters *)in_params,
            out_params_count,
            (const FAPOLockForProcessBufferParameters *)out_params);
#else
    {
        HRESULT hr;
        uint32_t i;
        FAPOLockForProcessBufferParameters * WIN32PTR in, * WIN32PTR out;

        in = CoTaskMemAlloc(in_params_count * sizeof(*in));
        out = CoTaskMemAlloc(out_params_count * sizeof(*out));
        if (!in || !out)
            return E_OUTOFMEMORY;

        for (i = 0; i < in_params_count; i++)
        {
            in[i].pFormat = (const FAudioWaveFormatEx *)in_params[i].pFormat;
            in[i].MaxFrameCount = in_params[i].MaxFrameCount;
        }
        for (i = 0; i < out_params_count; i++)
        {
            out[i].pFormat = (const FAudioWaveFormatEx *)out_params[i].pFormat;
            out[i].MaxFrameCount = out_params[i].MaxFrameCount;
        }
        hr = This->fapo->LockForProcess(This->fapo,
                in_params_count,
                in,
                out_params_count,
                out);
        CoTaskMemFree(in);
        CoTaskMemFree(out);
        return hr;
    }
#endif
}

static void WINAPI XAPOFX_UnlockForProcess(IXAPO *iface)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    TRACE("%p\n", This);
    This->fapo->UnlockForProcess(This->fapo);
}

static void WINAPI XAPOFX_Process(IXAPO *iface, UINT32 in_params_count,
        const XAPO_PROCESS_BUFFER_PARAMETERS *in_params,
        UINT32 out_params_count,
        XAPO_PROCESS_BUFFER_PARAMETERS *out_params, BOOL enabled)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    TRACE("%p, %u, %p, %u, %p, %u\n", This, in_params_count, in_params,
            out_params_count, out_params, enabled);
#ifndef __i386_on_x86_64__
    This->fapo->Process(This->fapo, in_params_count,
            (const FAPOProcessBufferParameters *)in_params, out_params_count,
            (FAPOProcessBufferParameters *)out_params, enabled);
#else
    {
        /* They are arrays, but docs say XAudio2 only supports one input/output stream */
        FAPOProcessBufferParameters in, out;
        if (in_params && in_params_count)
        {
            in.pBuffer = in_params[0].pBuffer;
            in.BufferFlags = (FAPOBufferFlags)in_params[0].BufferFlags;
            in.ValidFrameCount = in_params[0].ValidFrameCount;
        }
        if (out_params && out_params_count)
        {
            out.pBuffer = out_params[0].pBuffer;
            out.BufferFlags = (FAPOBufferFlags)out_params[0].BufferFlags;
            out.ValidFrameCount = out_params[0].ValidFrameCount;
        }
        This->fapo->Process(This->fapo, in_params_count,
                in_params ? &in : NULL, out_params_count,
                out_params ? &out : NULL, enabled);
        if (out_params && out_params_count)
        {
            out_params[0].pBuffer = ADDRSPACECAST(void *, out.pBuffer);
            out_params[0].BufferFlags = (FAPOBufferFlags)out.BufferFlags;
            out_params[0].ValidFrameCount = out.ValidFrameCount;
        }
    }
#endif
}

static UINT32 WINAPI XAPOFX_CalcInputFrames(IXAPO *iface, UINT32 output_frames)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    TRACE("%p, %u\n", This, output_frames);
    return 0;
}

static UINT32 WINAPI XAPOFX_CalcOutputFrames(IXAPO *iface, UINT32 input_frames)
{
    XA2XAPOFXImpl *This = impl_from_IXAPO(iface);
    TRACE("%p, %u\n", This, input_frames);
    return 0;
}

static const IXAPOVtbl XAPOFX_Vtbl = {
    XAPOFX_QueryInterface,
    XAPOFX_AddRef,
    XAPOFX_Release,
    XAPOFX_GetRegistrationProperties,
    XAPOFX_IsInputFormatSupported,
    XAPOFX_IsOutputFormatSupported,
    XAPOFX_Initialize,
    XAPOFX_Reset,
    XAPOFX_LockForProcess,
    XAPOFX_UnlockForProcess,
    XAPOFX_Process,
    XAPOFX_CalcInputFrames,
    XAPOFX_CalcOutputFrames
};

static HRESULT WINAPI XAPOFXParams_QueryInterface(IXAPOParameters *iface,
        REFIID riid, void **ppvObject)
{
    XA2XAPOFXImpl *This = impl_from_IXAPOParameters(iface);
    return XAPOFX_QueryInterface(&This->IXAPO_iface, riid, ppvObject);
}

static ULONG WINAPI XAPOFXParams_AddRef(IXAPOParameters *iface)
{
    XA2XAPOFXImpl *This = impl_from_IXAPOParameters(iface);
    return XAPOFX_AddRef(&This->IXAPO_iface);
}

static ULONG WINAPI XAPOFXParams_Release(IXAPOParameters *iface)
{
    XA2XAPOFXImpl *This = impl_from_IXAPOParameters(iface);
    return XAPOFX_Release(&This->IXAPO_iface);
}

static void WINAPI XAPOFXParams_SetParameters(IXAPOParameters *iface,
        const void *params, UINT32 params_len)
{
    XA2XAPOFXImpl *This = impl_from_IXAPOParameters(iface);
    TRACE("%p, %p, %u\n", This, params, params_len);
    This->fapo->SetParameters(This->fapo, params, params_len);
}

static void WINAPI XAPOFXParams_GetParameters(IXAPOParameters *iface, void *params,
        UINT32 params_len)
{
    XA2XAPOFXImpl *This = impl_from_IXAPOParameters(iface);
    TRACE("%p, %p, %u\n", This, params, params_len);
    This->fapo->GetParameters(This->fapo, params, params_len);
}

static const IXAPOParametersVtbl XAPOFXParameters_Vtbl = {
    XAPOFXParams_QueryInterface,
    XAPOFXParams_AddRef,
    XAPOFXParams_Release,
    XAPOFXParams_SetParameters,
    XAPOFXParams_GetParameters
};

struct xapo_cf {
    IClassFactory IClassFactory_iface;
    LONG ref;
    const CLSID *class;
};

static struct xapo_cf *xapo_impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct xapo_cf, IClassFactory_iface);
}

static HRESULT WINAPI xapocf_QueryInterface(IClassFactory *iface, REFIID riid, void **ppobj)
{
    if(IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_IClassFactory))
    {
        IClassFactory_AddRef(iface);
        *ppobj = iface;
        return S_OK;
    }

    *ppobj = NULL;
    WARN("(%p)->(%s, %p): interface not found\n", iface, debugstr_guid(riid), ppobj);
    return E_NOINTERFACE;
}

static ULONG WINAPI xapocf_AddRef(IClassFactory *iface)
{
    struct xapo_cf *This = xapo_impl_from_IClassFactory(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(): Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI xapocf_Release(IClassFactory *iface)
{
    struct xapo_cf *This = xapo_impl_from_IClassFactory(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    TRACE("(%p)->(): Refcount now %u\n", This, ref);
    if (!ref)
        HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static inline HRESULT get_fapo_from_clsid(REFCLSID clsid, FAPO **fapo)
{
#ifndef XAPOFX1_VER
    if(IsEqualGUID(clsid, &CLSID_AudioVolumeMeter27))
        return FAudioCreateVolumeMeterWithCustomAllocatorEXT(
            fapo,
            0,
            XAudio_Internal_Malloc,
            XAudio_Internal_Free,
            XAudio_Internal_Realloc
        );
#if XAUDIO2_VER >= 9 && HAVE_FAUDIOCREATEREVERB9WITHCUSTOMALLOCATOREXT
    if(IsEqualGUID(clsid, &CLSID_AudioReverb27))
        return FAudioCreateReverb9WithCustomAllocatorEXT(
            fapo,
            0,
            XAudio_Internal_Malloc,
            XAudio_Internal_Free,
            XAudio_Internal_Realloc
        );
#else
    if(IsEqualGUID(clsid, &CLSID_AudioReverb27))
        return FAudioCreateReverbWithCustomAllocatorEXT(
            fapo,
            0,
            XAudio_Internal_Malloc,
            XAudio_Internal_Free,
            XAudio_Internal_Realloc
        );
#endif
#endif
#if XAUDIO2_VER >= 8 || defined XAPOFX1_VER
    if(IsEqualGUID(clsid, &CLSID_FXReverb) ||
            IsEqualGUID(clsid, &CLSID_FXEQ) ||
            IsEqualGUID(clsid, &CLSID_FXEcho) ||
            IsEqualGUID(clsid, &CLSID_FXMasteringLimiter))
        return FAPOFX_CreateFXWithCustomAllocatorEXT(
            (const FAudioGUID*) clsid,
            fapo,
            NULL,
            0,
            XAudio_Internal_Malloc,
            XAudio_Internal_Free,
            XAudio_Internal_Realloc
        );
#endif
    ERR("Invalid XAPO CLSID!\n");
    return E_INVALIDARG;
}

static HRESULT WINAPI xapocf_CreateInstance(IClassFactory *iface, IUnknown *pOuter,
        REFIID riid, void **ppobj)
{
    struct xapo_cf *This = xapo_impl_from_IClassFactory(iface);
    HRESULT hr;
    XA2XAPOFXImpl *object;

    TRACE("(%p)->(%p,%s,%p)\n", This, pOuter, debugstr_guid(riid), ppobj);

    *ppobj = NULL;

    if(pOuter)
        return CLASS_E_NOAGGREGATION;

    object = heap_alloc(sizeof(*object));
    object->IXAPO_iface.lpVtbl = &XAPOFX_Vtbl;
    object->IXAPOParameters_iface.lpVtbl = &XAPOFXParameters_Vtbl;

    hr = get_fapo_from_clsid(This->class, &object->fapo);

    if(FAILED(hr)){
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    hr = IXAPO_QueryInterface(&object->IXAPO_iface, riid, ppobj);
    IXAPO_Release(&object->IXAPO_iface);

    return hr;
}

static HRESULT WINAPI xapocf_LockServer(IClassFactory *iface, BOOL dolock)
{
    struct xapo_cf *This = xapo_impl_from_IClassFactory(iface);
    FIXME("(%p)->(%d): stub!\n", This, dolock);
    return S_OK;
}

static const IClassFactoryVtbl xapo_Vtbl =
{
    xapocf_QueryInterface,
    xapocf_AddRef,
    xapocf_Release,
    xapocf_CreateInstance,
    xapocf_LockServer
};

HRESULT make_xapo_factory(REFCLSID clsid, REFIID riid, void **ppv)
{
    HRESULT hr;
    struct xapo_cf *ret = HeapAlloc(GetProcessHeap(), 0, sizeof(struct xapo_cf));
    ret->IClassFactory_iface.lpVtbl = &xapo_Vtbl;
    ret->class = clsid;
    ret->ref = 0;
    hr = IClassFactory_QueryInterface(&ret->IClassFactory_iface, riid, ppv);
    if(FAILED(hr))
        HeapFree(GetProcessHeap(), 0, ret);
    return hr;
}
