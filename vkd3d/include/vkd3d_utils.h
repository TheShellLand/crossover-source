/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

#ifndef __VKD3D_UTILS_H
#define __VKD3D_UTILS_H

#include <vkd3d.h>

#ifndef VKD3D_UTILS_API_VERSION
#define VKD3D_UTILS_API_VERSION VKD3D_API_VERSION_1_0
#endif

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define VKD3D_WAIT_OBJECT_0 (0)
#define VKD3D_WAIT_TIMEOUT (1)
#define VKD3D_WAIT_FAILED (~0u)
#define VKD3D_INFINITE (~0u)

/* 1.0 */
HANDLE vkd3d_create_event(void);
HRESULT vkd3d_signal_event(HANDLE event);
unsigned int vkd3d_wait_event(HANDLE event, unsigned int milliseconds);
void vkd3d_destroy_event(HANDLE event);

#define D3D12CreateDevice(a, b, c, d) D3D12CreateDeviceVKD3D(a, b, c, d, VKD3D_UTILS_API_VERSION)
HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T data_size, REFIID iid, void **deserializer);
HRESULT WINAPI D3D12GetDebugInterface(REFIID iid, void **debug);
HRESULT WINAPI D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob);

/* 1.2 */
HRESULT WINAPI D3D12CreateDeviceVKD3D(IUnknown *adapter, D3D_FEATURE_LEVEL feature_level,
        REFIID iid, void **device, enum vkd3d_api_version api_version);
HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void *data,
        SIZE_T data_size, REFIID iid, void **deserializer);
HRESULT WINAPI D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob);

/* 1.3 */
HRESULT WINAPI D3DCompile(const void *data, SIZE_T data_size, const char *filename,
        const D3D_SHADER_MACRO *defines, ID3DInclude *include, const char *entrypoint,
        const char *target, UINT flags, UINT effect_flags, ID3DBlob **shader, ID3DBlob **error_messages);
HRESULT WINAPI D3DCompile2(const void *data, SIZE_T data_size, const char *filename,
        const D3D_SHADER_MACRO *defines, ID3DInclude *include, const char *entrypoint,
        const char *target, UINT flags, UINT effect_flags, UINT secondary_flags,
        const void *secondary_data, SIZE_T secondary_data_size, ID3DBlob **shader,
        ID3DBlob **error_messages);
HRESULT WINAPI D3DCreateBlob(SIZE_T data_size, ID3D10Blob **blob);
HRESULT WINAPI D3DPreprocess(const void *data, SIZE_T size, const char *filename,
        const D3D_SHADER_MACRO *defines, ID3DInclude *include,
        ID3DBlob **shader, ID3DBlob **error_messages);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_UTILS_H */
