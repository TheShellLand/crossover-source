/*
 * Copyright (C) 2005 Francois Gouget
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

#ifndef __WINE_ADSHLP_H
#define __WINE_ADSHLP_H

#include "wine/winheader_enter.h"

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI ADsBuildEnumerator(IADsContainer*,IEnumVARIANT**);
HRESULT WINAPI ADsBuildVarArrayStr(LPWSTR*,DWORD,VARIANT*);
HRESULT WINAPI ADsEnumerateNext(IEnumVARIANT*,ULONG,VARIANT*,ULONG*);
HRESULT WINAPI ADsGetObject(LPCWSTR,REFIID,VOID**);
HRESULT WINAPI ADsOpenObject(LPCWSTR,LPCWSTR,LPCWSTR,DWORD,REFIID,VOID**);
LPWSTR  WINAPI AllocADsStr(LPWSTR);
BOOL    WINAPI FreeADsMem(LPVOID);
BOOL    WINAPI FreeADsStr(LPWSTR);

#ifdef __cplusplus
}
#endif

#include "wine/winheader_exit.h"

#endif
