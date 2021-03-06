/*
 * WLDAP32 - LDAP support for Wine
 *
 * Copyright 2005 Hans Leidekker
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
#ifdef HAVE_LDAP_H
#include <ldap.h>
#endif

#include "windef.h"
#include "winbase.h"
#include "winldap_private.h"
#include "wldap32.h"
#include "wine/debug.h"

#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
WINE_DEFAULT_DEBUG_CHANNEL(wldap32);
#endif

#define WLDAP32_LBER_ERROR (~0U)

/***********************************************************************
 *      ber_alloc_t     (WLDAP32.@)
 *
 * Allocate a berelement structure.
 *
 * PARAMS
 *  options [I] Must be LBER_USE_DER.
 *
 * RETURNS
 *  Success: Pointer to an allocated berelement structure.
 *  Failure: NULL
 *
 * NOTES
 *  Free the berelement structure with ber_free.
 */
WLDAP32_BerElement * CDECL WLDAP32_ber_alloc_t( INT options )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    return pber_alloc_t( options );
#else
    return NULL;
#endif
}


/***********************************************************************
 *      ber_bvdup     (WLDAP32.@)
 *
 * Copy a berval structure.
 *
 * PARAMS
 *  berval [I] Pointer to the berval structure to be copied.
 *
 * RETURNS
 *  Success: Pointer to a copy of the berval structure.
 *  Failure: NULL
 *
 * NOTES
 *  Free the copy with ber_bvfree.
 */
BERVAL * CDECL WLDAP32_ber_bvdup( BERVAL *berval )
{
    return bervalWtoW( berval );
}


/***********************************************************************
 *      ber_bvecfree     (WLDAP32.@)
 *
 * Free an array of berval structures.
 *
 * PARAMS
 *  berval [I] Pointer to an array of berval structures.
 *
 * RETURNS
 *  Nothing.
 *
 * NOTES
 *  Use this function only to free an array of berval structures
 *  returned by a call to ber_scanf with a 'V' in the format string.
 */
void CDECL WLDAP32_ber_bvecfree( PBERVAL *berval )
{
    bvarrayfreeW( berval );
}


/***********************************************************************
 *      ber_bvfree     (WLDAP32.@)
 *
 * Free a berval structure.
 *
 * PARAMS
 *  berval [I] Pointer to a berval structure.
 *
 * RETURNS
 *  Nothing.
 *
 * NOTES
 *  Use this function only to free berval structures allocated by
 *  an LDAP API.
 */
void CDECL WLDAP32_ber_bvfree( BERVAL *berval )
{
    heap_free( berval );
}


/***********************************************************************
 *      ber_first_element     (WLDAP32.@)
 *
 * Return the tag of the first element in a set or sequence.
 *
 * PARAMS
 *  berelement [I] Pointer to a berelement structure.
 *  len        [O] Receives the length of the first element.
 *  opaque     [O] Receives a pointer to a cookie.
 *
 * RETURNS
 *  Success: Tag of the first element.
 *  Failure: LBER_DEFAULT (no more data).
 *
 * NOTES
 *  len and cookie should be passed to ber_next_element.
 */
ULONG CDECL WLDAP32_ber_first_element( WLDAP32_BerElement *berelement, ULONG *ret_len, CHAR **opaque )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    ber_len_t len;
    ber_tag_t ret;

    if ((ret = pber_first_element( berelement, &len, opaque )) != LBER_ERROR)
    {
        if (len > ~0u)
        {
            ERR( "len too large\n" );
            return WLDAP32_LBER_ERROR;
        }
        *ret_len = len;
    }
    return ret;

#else
    return WLDAP32_LBER_ERROR;
#endif
}


/***********************************************************************
 *      ber_flatten     (WLDAP32.@)
 *
 * Flatten a berelement structure into a berval structure.
 *
 * PARAMS
 *  berelement [I] Pointer to a berelement structure.
 *  berval    [O] Pointer to a berval structure.
 *
 * RETURNS
 *  Success: 0
 *  Failure: LBER_ERROR
 *
 * NOTES
 *  Free the berval structure with ber_bvfree.
 */
INT CDECL WLDAP32_ber_flatten( WLDAP32_BerElement *berelement, PBERVAL *berval )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    struct berval *bervalU;
    struct WLDAP32_berval *bervalW;

    if (pber_flatten( berelement, &bervalU )) return WLDAP32_LBER_ERROR;

    bervalW = bervalUtoW( bervalU );
    pber_bvfree( bervalU );
    if (!bervalW) return WLDAP32_LBER_ERROR;
    *berval = bervalW;
    return 0;

#else
    return WLDAP32_LBER_ERROR;
#endif
}


/***********************************************************************
 *      ber_free     (WLDAP32.@)
 *
 * Free a berelement structure.
 *
 * PARAMS
 *  berelement [I] Pointer to the berelement structure to be freed.
 *  buf       [I] Flag.
 *
 * RETURNS
 *  Nothing.
 *
 * NOTES
 *  Set buf to 0 if the berelement was allocated with ldap_first_attribute
 *  or ldap_next_attribute, otherwise set it to 1.
 */
void CDECL WLDAP32_ber_free( WLDAP32_BerElement *berelement, INT buf )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    pber_free( berelement, buf );
#endif
}


/***********************************************************************
 *      ber_init     (WLDAP32.@)
 *
 * Initialise a berelement structure from a berval structure.
 *
 * PARAMS
 *  berval [I] Pointer to a berval structure.
 *
 * RETURNS
 *  Success: Pointer to a berelement structure.
 *  Failure: NULL
 *
 * NOTES
 *  Call ber_free to free the returned berelement structure.
 */
WLDAP32_BerElement * CDECL WLDAP32_ber_init( BERVAL *berval )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    struct berval *bervalU;
    WLDAP32_BerElement *ret;

    if (!(bervalU = bervalWtoU( berval ))) return NULL;
    ret = pber_init( bervalU );
    heap_free( bervalU );
    return ret;
#else
    return NULL;
#endif
}


/***********************************************************************
 *      ber_next_element     (WLDAP32.@)
 *
 * Return the tag of the next element in a set or sequence.
 *
 * PARAMS
 *  berelement [I]   Pointer to a berelement structure.
 *  len        [I/O] Receives the length of the next element.
 *  opaque     [I/O] Pointer to a cookie.
 *
 * RETURNS
 *  Success: Tag of the next element.
 *  Failure: LBER_DEFAULT (no more data).
 *
 * NOTES
 *  len and cookie are initialized by ber_first_element and should
 *  be passed on in subsequent calls to ber_next_element.
 */
ULONG CDECL WLDAP32_ber_next_element( WLDAP32_BerElement *berelement, ULONG *ret_len, CHAR *opaque )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    ber_len_t len;
    ber_tag_t ret;

    if ((ret = pber_next_element( berelement, &len, opaque )) != LBER_ERROR)
    {
        if (len > ~0u)
        {
            ERR( "len too large\n" );
            return WLDAP32_LBER_ERROR;
        }
        *ret_len = len;
    }
    return ret;

#else
    return WLDAP32_LBER_ERROR;
#endif
}


/***********************************************************************
 *      ber_peek_tag     (WLDAP32.@)
 *
 * Return the tag of the next element.
 *
 * PARAMS
 *  berelement [I] Pointer to a berelement structure.
 *  len        [O] Receives the length of the next element.
 *
 * RETURNS
 *  Success: Tag of the next element.
 *  Failure: LBER_DEFAULT (no more data).
 */
ULONG CDECL WLDAP32_ber_peek_tag( WLDAP32_BerElement *berelement, ULONG *ret_len )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    ber_len_t len;
    ber_tag_t ret;

    if ((ret = pber_peek_tag( berelement, &len )) != LBER_ERROR)
    {
        if (len > ~0u)
        {
            ERR( "len too large\n" );
            return WLDAP32_LBER_ERROR;
        }
        *ret_len = len;
    }
    return ret;

#else
    return WLDAP32_LBER_ERROR;
#endif
}


/***********************************************************************
 *      ber_skip_tag     (WLDAP32.@)
 *
 * Skip the current tag and return the tag of the next element.
 *
 * PARAMS
 *  berelement [I] Pointer to a berelement structure.
 *  len        [O] Receives the length of the skipped element.
 *
 * RETURNS
 *  Success: Tag of the next element.
 *  Failure: LBER_DEFAULT (no more data).
 */
ULONG CDECL WLDAP32_ber_skip_tag( WLDAP32_BerElement *berelement, ULONG *ret_len )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    ber_len_t len;
    ber_tag_t ret;

    if ((ret = pber_skip_tag( berelement, &len )) != LBER_ERROR)
    {
        if (len > ~0u)
        {
            ERR( "len too large\n" );
            return WLDAP32_LBER_ERROR;
        }
        *ret_len = len;
    }
    return ret;

#else
    return WLDAP32_LBER_ERROR;
#endif
}


/***********************************************************************
 *      ber_printf     (WLDAP32.@)
 *
 * Encode a berelement structure.
 *
 * PARAMS
 *  berelement [I/O] Pointer to a berelement structure.
 *  fmt        [I]   Format string.
 *  ...        [I]   Values to encode.
 *
 * RETURNS
 *  Success: Non-negative number. 
 *  Failure: LBER_ERROR
 *
 * NOTES
 *  berelement must have been allocated with ber_alloc_t. This function
 *  can be called multiple times to append data.
 */
INT WINAPIV WLDAP32_ber_printf( WLDAP32_BerElement *berelement, PCHAR fmt, ... )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    __ms_va_list list;
    int ret = 0;
    char new_fmt[2];

    new_fmt[1] = 0;
    __ms_va_start( list, fmt );
    while (*fmt)
    {
        new_fmt[0] = *fmt++;
        switch(new_fmt[0])
        {
        case 'b':
        case 'e':
        case 'i':
            {
                int i = va_arg( list, int );
                ret = pber_printf( berelement, new_fmt, i );
                break;
            }
        case 'o':
        case 's':
            {
                char *str = va_arg( list, char * );
                ret = pber_printf( berelement, new_fmt, str );
                break;
            }
        case 't':
            {
                unsigned int tag = va_arg( list, unsigned int );
                ret = pber_printf( berelement, new_fmt, tag );
                break;
            }
        case 'v':
            {
                char **array = va_arg( list, char ** );
                ret = pber_printf( berelement, new_fmt, array );
                break;
            }
        case 'V':
            {
                struct WLDAP32_berval **array = va_arg( list, struct WLDAP32_berval ** );
                struct berval **arrayU;
                if (!(arrayU = bvarrayWtoU( array )))
                {
                    ret = -1;
                    break;
                }
                ret = pber_printf( berelement, new_fmt, arrayU );
                bvarrayfreeU( arrayU );
                break;
            }
        case 'X':
            {
                char *str = va_arg( list, char * );
                int len = va_arg( list, int );
                new_fmt[0] = 'B';  /* 'X' is deprecated */
                ret = pber_printf( berelement, new_fmt, str, len );
                break;
            }
        case 'n':
        case '{':
        case '}':
        case '[':
        case ']':
            ret = pber_printf( berelement, new_fmt );
            break;
        default:
            FIXME( "Unknown format '%c'\n", new_fmt[0] );
            ret = -1;
            break;
        }
        if (ret == -1) break;
    }
    __ms_va_end( list );
    return ret;
#else
    return WLDAP32_LBER_ERROR;
#endif
}


/***********************************************************************
 *      ber_scanf     (WLDAP32.@)
 *
 * Decode a berelement structure.
 *
 * PARAMS
 *  berelement [I/O] Pointer to a berelement structure.
 *  fmt        [I]   Format string.
 *  ...        [I]   Pointers to values to be decoded.
 *
 * RETURNS
 *  Success: Non-negative number. 
 *  Failure: LBER_ERROR
 *
 * NOTES
 *  berelement must have been allocated with ber_init. This function
 *  can be called multiple times to decode data.
 */
INT WINAPIV WLDAP32_ber_scanf( WLDAP32_BerElement *berelement, PCHAR fmt, ... )
{
#if defined(HAVE_LDAP) && !defined(__i386_on_x86_64__)
    __ms_va_list list;
    int ret = 0;
    char new_fmt[2];

    new_fmt[1] = 0;
    __ms_va_start( list, fmt );
    while (*fmt)
    {
        new_fmt[0] = *fmt++;
        switch(new_fmt[0])
        {
        case 'a':
            {
                char **ptr = va_arg( list, char ** );
                ret = pber_scanf( berelement, new_fmt, ptr );
                break;
            }
        case 'b':
        case 'e':
        case 'i':
            {
                int *i = va_arg( list, int * );
                ret = pber_scanf( berelement, new_fmt, i );
                break;
            }
        case 't':
            {
                unsigned int *tag = va_arg( list, unsigned int * );
                ret = pber_scanf( berelement, new_fmt, tag );
                break;
            }
        case 'v':
            {
                char ***array = va_arg( list, char *** );
                ret = pber_scanf( berelement, new_fmt, array );
                break;
            }
        case 'B':
            {
                char **str = va_arg( list, char ** );
                int *len = va_arg( list, int * );
                ret = pber_scanf( berelement, new_fmt, str, len );
                break;
            }
        case 'O':
            {
                struct berval **ptr = va_arg( list, struct berval ** );
                ret = pber_scanf( berelement, new_fmt, ptr );
                break;
            }
        case 'V':
            {
                struct WLDAP32_berval **arrayW, ***array = va_arg( list, struct WLDAP32_berval *** );
                struct berval **arrayU;
                if ((ret = pber_scanf( berelement, new_fmt, &arrayU )) == -1) break;
                if ((arrayW = bvarrayUtoW( arrayU ))) *array = arrayW;
                else ret = -1;
                bvarrayfreeU( arrayU );
                break;
            }
        case 'n':
        case 'x':
        case '{':
        case '}':
        case '[':
        case ']':
            ret = pber_scanf( berelement, new_fmt );
            break;
        default:
            FIXME( "Unknown format '%c'\n", new_fmt[0] );
            ret = -1;
            break;
        }
        if (ret == -1) break;
    }
    __ms_va_end( list );
    return ret;
#else
    return WLDAP32_LBER_ERROR;
#endif
}
