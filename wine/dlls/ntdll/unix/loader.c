/*
 * Unix interface for loader functions
 *
 * Copyright (C) 2020 Alexandre Julliard
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

#if 0
#pragma makedep unix
#endif

#include "config.h"
#include "wine/port.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#ifdef HAVE_LINK_H
# include <link.h>
#endif
#ifdef HAVE_PWD_H
# include <pwd.h>
#endif
#ifdef HAVE_ELF_H
# include <elf.h>
#endif
#ifdef HAVE_LINK_H
# include <link.h>
#endif
#ifdef HAVE_SYS_AUXV_H
# include <sys/auxv.h>
#endif
#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif
#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef __APPLE__
# include <CoreFoundation/CoreFoundation.h>
# define LoadResource MacLoadResource
# define GetCurrentThread MacGetCurrentThread
# include <CoreServices/CoreServices.h>
# undef LoadResource
# undef GetCurrentThread
# include <pthread.h>
# include <mach/mach.h>
# include <mach/mach_error.h>
# include <mach-o/getsect.h>
# include <crt_externs.h>
# include <spawn.h>
# ifndef _POSIX_SPAWN_DISABLE_ASLR
#  define _POSIX_SPAWN_DISABLE_ASLR 0x0100
# endif
#endif
#ifdef __ANDROID__
# include <jni.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "windef.h"
#include "winnt.h"
#include "winbase.h"
#include "winnls.h"
#include "winioctl.h"
#include "winternl.h"
#include "unix_private.h"
#include "esync.h"
#define WINE_LIST_HOSTADDRSPACE
#include "wine/list.h"
#include "wine/debug.h"

/* 32on64 FIXME: A lot of complicated changes discarded. AJ, fix it. */

WINE_DEFAULT_DEBUG_CHANNEL(module);

void     (WINAPI *pDbgUiRemoteBreakin)( void *arg ) = NULL;
NTSTATUS (WINAPI *pKiRaiseUserExceptionDispatcher)(void) = NULL;
void     (WINAPI *pKiUserApcDispatcher)(CONTEXT*,ULONG_PTR,ULONG_PTR,ULONG_PTR,PNTAPCFUNC) = NULL;
NTSTATUS (WINAPI *pKiUserExceptionDispatcher)(EXCEPTION_RECORD*,CONTEXT*) = NULL;
void     (WINAPI *pLdrInitializeThunk)(CONTEXT*,void**,ULONG_PTR,ULONG_PTR) = NULL;
void     (WINAPI *pRtlUserThreadStart)( PRTL_THREAD_START_ROUTINE entry, void *arg ) = NULL;
ULONG    (WINAPI *pRtlFindClearBitsAndSet)(PRTL_BITMAP lpBits, ULONG ulCount, ULONG ulHint) = NULL;
#ifdef __i386_on_x86_64__
void *   (WINAPI *pRtlAllocateHeap)( HANDLE heap, ULONG flags, SIZE_T size ) = NULL;
BOOLEAN  (WINAPI *pRtlFreeHeap)( HANDLE heap, ULONG flags, void *ptr ) = NULL;
#endif

void     (WINAPI *pLdrInitializeThunk_stdcall32)(CONTEXT*,void**,ULONG_PTR,ULONG_PTR) = NULL;
static void LdrInitializeThunk_stdcall64(CONTEXT *context, void** unknown2, ULONG_PTR unknown3, ULONG_PTR unknown4)
{
    pLdrInitializeThunk_stdcall32(context, unknown2, unknown3, unknown4);
}

static NTSTATUS (CDECL *p__wine_set_unix_funcs)( int version, const struct unix_funcs *funcs );
static void *syscall_dispatcher;

#ifdef __GNUC__
static void fatal_error( const char *err, ... ) __attribute__((noreturn, format(printf,1,2)));
#endif

#if defined(linux) || defined(__APPLE__)
static const BOOL use_preloader = TRUE;
#else
static const BOOL use_preloader = FALSE;
#endif

static const BOOL is_win64 = (sizeof(void *) > sizeof(int));

static char * HOSTPTR argv0;
static const char * HOSTPTR bin_dir;
static const char * HOSTPTR dll_dir;
static SIZE_T dll_path_maxlen;

const char * HOSTPTR home_dir = NULL;
const char * HOSTPTR data_dir = NULL;
const char * HOSTPTR build_dir = NULL;
const char * HOSTPTR config_dir = NULL;
const char * HOSTPTR * HOSTPTR dll_paths = NULL;
const char * HOSTPTR user_name = NULL;
static HMODULE ntdll_module;
static const IMAGE_EXPORT_DIRECTORY *ntdll_exports;

struct file_id
{
    dev_t dev;
    ino_t ino;
};

#include "wine/hostptraddrspace_enter.h"
struct builtin_module
{
    struct list    entry;
    struct file_id id;
    void          *handle;
    void          * WIN32PTR module;
    void          *unix_handle;
};

static struct list builtin_modules = LIST_INIT( builtin_modules );

static NTSTATUS add_builtin_module( void * WIN32PTR module, void * HOSTPTR handle, const struct stat *st )
{
    struct builtin_module *builtin;
    if (!(builtin = malloc( sizeof(*builtin) ))) return STATUS_NO_MEMORY;
    builtin->handle = handle;
    builtin->module = module;
    builtin->unix_handle = NULL;
    if (st)
    {
        builtin->id.dev = st->st_dev;
        builtin->id.ino = st->st_ino;
    }
    else memset( &builtin->id, 0, sizeof(builtin->id) );
    list_add_tail( &builtin_modules, &builtin->entry );
    return STATUS_SUCCESS;
}
#include "wine/hostptraddrspace_exit.h"

/* adjust an array of pointers to make them into RVAs */
static inline void fixup_rva_ptrs( void *array, BYTE *base, unsigned int count )
{
    BYTE **src = array;
    DWORD *dst = array;

    for ( ; count; count--, src++, dst++) *dst = *src ? *src - base : 0;
}

/* fixup an array of RVAs by adding the specified delta */
static inline void fixup_rva_dwords( DWORD *ptr, int delta, unsigned int count )
{
    for ( ; count; count--, ptr++) if (*ptr) *ptr += delta;
}


/* fixup an array of name/ordinal RVAs by adding the specified delta */
static inline void fixup_rva_names( UINT_PTR *ptr, int delta )
{
    for ( ; *ptr; ptr++) if (!(*ptr & IMAGE_ORDINAL_FLAG)) *ptr += delta;
#ifdef __i386_on_x86_64__
    ptr++;
    for ( ; *ptr; ptr++) if (!(*ptr & IMAGE_ORDINAL_FLAG)) *ptr += delta;
#endif
}


/* fixup RVAs in the resource directory */
static void fixup_so_resources( IMAGE_RESOURCE_DIRECTORY *dir, BYTE *root, int delta )
{
    IMAGE_RESOURCE_DIRECTORY_ENTRY *entry = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    unsigned int i;

    for (i = 0; i < dir->NumberOfNamedEntries + dir->NumberOfIdEntries; i++, entry++)
    {
        void *ptr = root + entry->u2.s2.OffsetToDirectory;
        if (entry->u2.s2.DataIsDirectory) fixup_so_resources( ptr, root, delta );
        else fixup_rva_dwords( &((IMAGE_RESOURCE_DATA_ENTRY *)ptr)->OffsetToData, delta, 1 );
    }
}

/* These selectors are defined in libport, but since it's statically linked, each
 * .so has its own copy of the selectors. Set them here.
 */
static void fixup_so_32on64_sels( void * HOSTPTR handle, const char * HOSTPTR name )
{
#ifdef __i386_on_x86_64__
    unsigned short * HOSTPTR cs32, * HOSTPTR cs64, * HOSTPTR ds32;
    cs32 = dlsym( handle, "wine_32on64_cs32" );
    cs64 = dlsym( handle, "wine_32on64_cs64" );
    ds32 = dlsym( handle, "wine_32on64_ds32" );

    if (!cs32 || !cs64 || !ds32)
        WARN( "at least one of cs32/cs64/ds32 is missing from %s\n", debugstr_a(name) );
    if (cs32) *cs32 = wine_32on64_cs32;
    if (cs64) *cs64 = wine_32on64_cs64;
    if (ds32) *ds32 = wine_32on64_ds32;
#endif
}

/* die on a fatal error; use only during initialization */
static void fatal_error( const char *err, ... )
{
    va_list args;

    va_start( args, err );
    fprintf( stderr, "wine: " );
    vfprintf( stderr, err, args );
    va_end( args );
    exit(1);
}

static void set_max_limit( int limit )
{
    struct rlimit rlimit;

    if (!getrlimit( limit, &rlimit ))
    {
        rlimit.rlim_cur = rlimit.rlim_max;
        if (setrlimit( limit, &rlimit ) != 0)
        {
#if defined(__APPLE__) && defined(RLIMIT_NOFILE) && defined(OPEN_MAX)
            /* On Leopard, setrlimit(RLIMIT_NOFILE, ...) fails on attempts to set
             * rlim_cur above OPEN_MAX (even if rlim_max > OPEN_MAX). */
            if (limit == RLIMIT_NOFILE && rlimit.rlim_cur > OPEN_MAX)
            {
                rlimit.rlim_cur = OPEN_MAX;
                setrlimit( limit, &rlimit );
            }
#endif
        }
    }
}

/* canonicalize path and return its directory name */
static char * HOSTPTR realpath_dirname( const char * HOSTPTR name )
{
    char * HOSTPTR p, * HOSTPTR fullpath = realpath( name, NULL );

    if (fullpath)
    {
        p = strrchr( fullpath, '/' );
        if (p == fullpath) p++;
        if (p) *p = 0;
    }
    return fullpath;
}

/* if string ends with tail, remove it */
static char * HOSTPTR remove_tail( const char * HOSTPTR str, const char *tail )
{
    size_t len = strlen( str );
    size_t tail_len = strlen( tail );
    char * HOSTPTR ret;

    if (len < tail_len) return NULL;
    if (strcmp( str + len - tail_len, tail )) return NULL;
    ret = malloc( len - tail_len + 1 );
    memcpy( ret, str, len - tail_len );
    ret[len - tail_len] = 0;
    return ret;
}

/* build a path from the specified dir and name */
static char * HOSTPTR build_path( const char * HOSTPTR dir, const char * HOSTPTR name )
{
    size_t len = strlen( dir );
    char * HOSTPTR ret = malloc( len + strlen( name ) + 2 );

    memcpy( ret, dir, len );
    if (len && ret[len - 1] != '/') ret[len++] = '/';
    strcpy( ret + len, name );
    return ret;
}

static void set_dll_path(void)
{
    char * HOSTPTR p, * HOSTPTR path = getenv( "WINEDLLPATH" );
    int i, count = 0;

    if (path) for (p = path, count = 1; *p; p++) if (*p == ':') count++;

    dll_paths = malloc( (count + 2) * sizeof(*dll_paths) );
    count = 0;

    if (!build_dir) dll_paths[count++] = dll_dir;

    if (path)
    {
        path = strdup(path);
        for (p = strtok( path, ":" ); p; p = strtok( NULL, ":" )) dll_paths[count++] = strdup( p );
        free( path );
    }

    for (i = 0; i < count; i++) dll_path_maxlen = max( dll_path_maxlen, strlen(dll_paths[i]) );
    dll_paths[count] = NULL;
}


static void set_home_dir(void)
{
    const char * HOSTPTR home = getenv( "HOME" );
    const char * HOSTPTR name = getenv( "USER" );
    const char * HOSTPTR p;

    if (!home || !name)
    {
        struct passwd *pwd = getpwuid( getuid() );
        if (pwd)
        {
            if (!home) home = pwd->pw_dir;
            if (!name) name = pwd->pw_name;
        }
        if (!name) name = "wine";
    }
    if ((p = strrchr( name, '/' ))) name = p + 1;
    if ((p = strrchr( name, '\\' ))) name = p + 1;
    home_dir = strdup( home );
    user_name = strdup( name );
}


static void set_config_dir(void)
{
    char * HOSTPTR p, * HOSTPTR dir;
    const char * HOSTPTR prefix = getenv( "WINEPREFIX" );

    if (prefix)
    {
        if (prefix[0] != '/')
            fatal_error( "invalid directory %s in WINEPREFIX: not an absolute path\n", prefix );
        config_dir = dir = strdup( prefix );
        for (p = dir + strlen(dir) - 1; p > dir && *p == '/'; p--) *p = 0;
    }
    else
    {
        if (!home_dir) fatal_error( "could not determine your home directory\n" );
        if (home_dir[0] != '/') fatal_error( "the home directory %s is not an absolute path\n", home_dir );
        config_dir = build_path( home_dir, ".wine" );
    }
}

static void init_paths( char * HOSTPTR * HOSTPTR argv )
{
    Dl_info info;

    argv0 = strdup( argv[0] );

    if (!dladdr( init_paths, &info ) || !(dll_dir = realpath_dirname( info.dli_fname )))
        fatal_error( "cannot get path to ntdll.so\n" );

    if (!(build_dir = remove_tail( dll_dir, "/dlls/ntdll" )))
    {
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__FreeBSD_kernel__) || defined(__NetBSD__)
        bin_dir = realpath_dirname( "/proc/self/exe" );
#elif defined (__FreeBSD__) || defined(__DragonFly__)
        bin_dir = realpath_dirname( "/proc/curproc/file" );
#else
        bin_dir = realpath_dirname( argv0 );
#endif
        if (!bin_dir) bin_dir = build_path( dll_dir, DLL_TO_BINDIR );
        data_dir = build_path( bin_dir, BIN_TO_DATADIR );
    }

    set_dll_path();
    set_home_dir();
    set_config_dir();
}

#ifdef __i386_on_x86_64__
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static char wine_version[128];
static char wine_build_id[128];

/* Initialize string buffers for 32on64 mode */
static void init_strings(void)
{
    extern const char wine_build[];
    strcpy(wine_version, PACKAGE_VERSION);
    strcpy(wine_build_id, wine_build);
}

/*********************************************************************
 *                  wine_get_version   (NTDLL.@)
 */
const char * CDECL wine_get_version(void)
{
    pthread_once(&init_once, init_strings);
    return wine_version;
}

/*********************************************************************
 *                  wine_get_build_id   (NTDLL.@)
 */
const char * CDECL wine_get_build_id(void)
{
    pthread_once(&init_once, init_strings);
    return wine_build_id;
}

#else

/*********************************************************************
 *                  wine_get_version
 */
const char * CDECL wine_get_version(void)
{
    return PACKAGE_VERSION;
}

/*********************************************************************
 *                  wine_get_build_id
 */
const char * CDECL wine_get_build_id(void)
{
    extern const char wine_build[];
    return wine_build;
}

#endif

/*********************************************************************
 *                  wine_get_host_version
 */
void CDECL wine_get_host_version( const char **sysname, const char **release )
{
#ifdef HAVE_SYS_UTSNAME_H
    static struct utsname buf;
    static BOOL init_done;

    if (!init_done)
    {
        uname( &buf );
        init_done = TRUE;
    }
    if (sysname) *sysname = buf.sysname;
    if (release) *release = buf.release;
#else
    if (sysname) *sysname = "";
    if (release) *release = "";
#endif
}

/* return whether the OS needs to use a 64-bit process to emulate 32-bit Wine */
int wine_needs_32on64(void)
{
#ifdef __APPLE__
    static int result = -1;
    struct utsname name;
    unsigned major, minor;

    if (result == -1)
    {
        if (getenv("WINE_USE_32ON64"))
            result = 1;
        else
        {
            result = (uname(&name) == 0 &&
                      sscanf(name.release, "%u.%u", &major, &minor) == 2 &&
                      major >= 19);
        }
    }

    return result;
#else
    return 0;
#endif
}


static void preloader_exec( char * HOSTPTR * HOSTPTR argv )
{
    if (use_preloader)
    {
        static const char *preloader = "wine-preloader";
        char * HOSTPTR p;

        if (!(p = strrchr( argv[1], '/' ))) p = argv[1];
        else p++;

        if (strlen(p) > 6 && !strcmp( p + strlen(p) - 6, "32on64" )) preloader = "wine32on64-preloader";
        else if (strlen(p) > 2 && !strcmp( p + strlen(p) - 2, "64" )) preloader = "wine64-preloader";
        argv[0] = malloc( p - argv[1] + strlen(preloader) + 1 );
        memcpy( argv[0], argv[1], p - argv[1] );
        strcpy( argv[0] + (p - argv[1]), preloader );

#ifdef __APPLE__
        {
            posix_spawnattr_t attr;
            posix_spawnattr_init( &attr );
            posix_spawnattr_setflags( &attr, POSIX_SPAWN_SETEXEC | _POSIX_SPAWN_DISABLE_ASLR );
            posix_spawn( NULL, argv[0], NULL, &attr, argv, *_NSGetEnviron() );
            posix_spawnattr_destroy( &attr );
        }
#endif
        execv( argv[0], argv );
        free( argv[0] );
    }
    execv( argv[1], argv + 1 );
}

static NTSTATUS loader_exec( const char * HOSTPTR loader, char * HOSTPTR * HOSTPTR argv, client_cpu_t cpu )
{
    char * HOSTPTR p, * HOSTPTR path;

    if (build_dir)
    {
        argv[1] = build_path( build_dir, (cpu == CPU_x86_64) ? "loader/wine64" : wine_needs_32on64() ? "loader/wine32on64" : "loader/wine" );
        preloader_exec( argv );
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    if ((p = strrchr( loader, '/' ))) loader = p + 1;

    argv[1] = build_path( bin_dir, loader );
    preloader_exec( argv );

    argv[1] = getenv( "WINELOADER" );
    if (argv[1]) preloader_exec( argv );

    if ((path = getenv( "PATH" )))
    {
        for (p = strtok( strdup( path ), ":" ); p; p = strtok( NULL, ":" ))
        {
            argv[1] = build_path( p, loader );
            preloader_exec( argv );
        }
    }

    argv[1] = build_path( BINDIR, loader );
    preloader_exec( argv );
    return STATUS_INVALID_IMAGE_FORMAT;
}


/***********************************************************************
 *           exec_wineloader
 *
 * argv[0] and argv[1] must be reserved for the preloader and loader respectively.
 */
NTSTATUS exec_wineloader( char * HOSTPTR * HOSTPTR argv, int socketfd, const pe_image_info_t *pe_info )
{
    int is_child_64bit = (pe_info->cpu == CPU_x86_64 || pe_info->cpu == CPU_ARM64);
    ULONGLONG res_start = pe_info->base;
    ULONGLONG res_end = pe_info->base + pe_info->map_size;
    const char * HOSTPTR loader = argv0;
    const char * HOSTPTR loader_env = getenv( "WINELOADER" );
    char preloader_reserve[64], socket_env[64];

    if (!wine_is_64bit() ^ !is_child_64bit)
    {
        /* remap WINELOADER to the alternate 32/64-bit version if necessary */
        if (loader_env)
        {
            int len = strlen( loader_env );
            char * HOSTPTR env = malloc( sizeof("WINELOADER=") + len + 6 );

            if (!env) return STATUS_NO_MEMORY;
            strcpy( env, "WINELOADER=" );
            strcat( env, loader_env );
            len += sizeof("WINELOADER=") - 1;
            if (is_child_64bit)
            {
                if (!strcmp( env + len - 6, "32on64" )) env[len - 6] = 0;
                strcat( env, "64" );
            }
            else
            {
                if (!strcmp( env + len - 2, "64" )) env[len - 2] = 0;
                if (wine_needs_32on64())
                    strcat( env, "32on64" );

            }
            loader = env;
            putenv( env );
        }
        else loader = is_child_64bit ? "wine64" : wine_needs_32on64() ? "wine32on64" : "wine";
    }

    signal( SIGPIPE, SIG_DFL );

    sprintf( socket_env, "WINESERVERSOCKET=%u", socketfd );
    sprintf( preloader_reserve, "WINEPRELOADRESERVE=%x%08x-%x%08x",
             (ULONG)(res_start >> 32), (ULONG)res_start, (ULONG)(res_end >> 32), (ULONG)res_end );

    putenv( preloader_reserve );
    putenv( socket_env );

    return loader_exec( loader, argv, pe_info->cpu );
}


/***********************************************************************
 *           exec_wineserver
 *
 * Exec a new wine server.
 */
static void exec_wineserver( char * HOSTPTR * HOSTPTR argv )
{
    char * HOSTPTR path;

    if (build_dir)
    {
        if (!is_win64)  /* look for 64-bit server */
        {
            char * HOSTPTR loader = realpath_dirname( build_path( build_dir, "loader/wine64" ));
            if (loader)
            {
                argv[0] = build_path( loader, "../server/wineserver" );
                execv( argv[0], argv );
            }
        }
        argv[0] = build_path( build_dir, "server/wineserver" );
        execv( argv[0], argv );
        return;
    }

    argv[0] = build_path( bin_dir, "wineserver" );
    execv( argv[0], argv );

    argv[0] = getenv( "WINESERVER" );
    if (argv[0]) execv( argv[0], argv );

    if ((path = getenv( "PATH" )))
    {
        for (path = strtok( strdup( path ), ":" ); path; path = strtok( NULL, ":" ))
        {
            argv[0] = build_path( path, "wineserver" );
            execvp( argv[0], argv );
        }
    }

    argv[0] = build_path( BINDIR, "wineserver" );
    execv( argv[0], argv );
}


/***********************************************************************
 *           start_server
 *
 * Start a new wine server.
 */
void start_server( BOOL debug )
{
    static BOOL started;  /* we only try once */
    char * HOSTPTR argv[3];
    static char debug_flag[] = "-d";

    if (!started)
    {
        int status;
        int pid = fork();
        if (pid == -1) fatal_error( "fork: %s", strerror(errno) );
        if (!pid)
        {
            argv[1] = debug ? debug_flag : NULL;
            argv[2] = NULL;
            exec_wineserver( argv );
            fatal_error( "could not exec wineserver\n" );
        }
        waitpid( pid, &status, 0 );
        status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        if (status == 2) return;  /* server lock held by someone else, will retry later */
        if (status) exit(status);  /* server failed */
        started = TRUE;
    }
}


/*************************************************************************
 *		map_so_dll
 *
 * Map a builtin dll in memory and fixup RVAs.
 */
static NTSTATUS map_so_dll( const IMAGE_NT_HEADERS *nt_descr, HMODULE module )
{
    static const char builtin_signature[32] = "Wine builtin DLL";
    IMAGE_DATA_DIRECTORY *dir;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *sec;
    BYTE *addr = (BYTE *)module;
    DWORD code_start, code_end, data_start, data_end, align_mask;
    int delta, nb_sections = 2;  /* code + data */
    unsigned int i;
    DWORD size = (sizeof(IMAGE_DOS_HEADER)
                  + sizeof(builtin_signature)
                  + sizeof(IMAGE_NT_HEADERS)
                  + nb_sections * sizeof(IMAGE_SECTION_HEADER));

    if (anon_mmap_fixed( addr, size, PROT_READ | PROT_WRITE, 0 ) != addr) return STATUS_NO_MEMORY;

    dos = (IMAGE_DOS_HEADER *)addr;
    nt  = (IMAGE_NT_HEADERS *)((BYTE *)(dos + 1) + sizeof(builtin_signature));
    sec = (IMAGE_SECTION_HEADER *)(nt + 1);

    /* build the DOS and NT headers */

    dos->e_magic    = IMAGE_DOS_SIGNATURE;
    dos->e_cblp     = 0x90;
    dos->e_cp       = 3;
    dos->e_cparhdr  = (sizeof(*dos) + 0xf) / 0x10;
    dos->e_minalloc = 0;
    dos->e_maxalloc = 0xffff;
    dos->e_ss       = 0x0000;
    dos->e_sp       = 0x00b8;
    dos->e_lfanew   = sizeof(*dos) + sizeof(builtin_signature);

    *nt = *nt_descr;

    delta      = (const BYTE *)nt_descr - addr;
    align_mask = nt->OptionalHeader.SectionAlignment - 1;
    code_start = (size + align_mask) & ~align_mask;
    data_start = delta & ~align_mask;
#ifdef __APPLE__
    {
        Dl_info dli;
        unsigned long data_size;
        /* need the mach_header, not the PE header, to give to getsegmentdata(3) */
        dladdr(addr, &dli);
        code_end   = getsegmentdata(dli.dli_fbase, "__DATA", &data_size) - addr;
        data_end   = (code_end + data_size + align_mask) & ~align_mask;
    }
#else
    code_end   = data_start;
    data_end   = (nt->OptionalHeader.SizeOfImage + delta + align_mask) & ~align_mask;
#endif

    fixup_rva_ptrs( &nt->OptionalHeader.AddressOfEntryPoint, addr, 1 );

    nt->FileHeader.NumberOfSections                = nb_sections;
    nt->OptionalHeader.BaseOfCode                  = code_start;
#ifndef _WIN64
    nt->OptionalHeader.BaseOfData                  = data_start;
#endif
    nt->OptionalHeader.SizeOfCode                  = code_end - code_start;
    nt->OptionalHeader.SizeOfInitializedData       = data_end - data_start;
    nt->OptionalHeader.SizeOfUninitializedData     = 0;
    nt->OptionalHeader.SizeOfImage                 = data_end;
    nt->OptionalHeader.ImageBase                   = (ULONG_PTR)addr;

    /* build the code section */

    memcpy( sec->Name, ".text", sizeof(".text") );
    sec->SizeOfRawData = code_end - code_start;
    sec->Misc.VirtualSize = sec->SizeOfRawData;
    sec->VirtualAddress   = code_start;
    sec->PointerToRawData = code_start;
    sec->Characteristics  = (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
    sec++;

    /* build the data section */

    memcpy( sec->Name, ".data", sizeof(".data") );
    sec->SizeOfRawData = data_end - data_start;
    sec->Misc.VirtualSize = sec->SizeOfRawData;
    sec->VirtualAddress   = data_start;
    sec->PointerToRawData = data_start;
    sec->Characteristics  = (IMAGE_SCN_CNT_INITIALIZED_DATA |
                             IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ);
    sec++;

    for (i = 0; i < nt->OptionalHeader.NumberOfRvaAndSizes; i++)
        fixup_rva_dwords( &nt->OptionalHeader.DataDirectory[i].VirtualAddress, delta, 1 );

    /* build the import directory */

    dir = &nt->OptionalHeader.DataDirectory[IMAGE_FILE_IMPORT_DIRECTORY];
    if (dir->Size)
    {
        IMAGE_IMPORT_DESCRIPTOR *imports = (IMAGE_IMPORT_DESCRIPTOR *)(addr + dir->VirtualAddress);

        while (imports->Name)
        {
            fixup_rva_dwords( &imports->u.OriginalFirstThunk, delta, 1 );
            fixup_rva_dwords( &imports->Name, delta, 1 );
            fixup_rva_dwords( &imports->FirstThunk, delta, 1 );
            if (imports->u.OriginalFirstThunk)
                fixup_rva_names( (UINT_PTR *)(addr + imports->u.OriginalFirstThunk), delta );
            if (imports->FirstThunk)
                fixup_rva_names( (UINT_PTR *)(addr + imports->FirstThunk), delta );
            imports++;
        }
    }

    /* build the resource directory */

    dir = &nt->OptionalHeader.DataDirectory[IMAGE_FILE_RESOURCE_DIRECTORY];
    if (dir->Size)
    {
        void *ptr = addr + dir->VirtualAddress;
        fixup_so_resources( ptr, ptr, delta );
    }

    /* build the export directory */

    dir = &nt->OptionalHeader.DataDirectory[IMAGE_FILE_EXPORT_DIRECTORY];
    if (dir->Size)
    {
        IMAGE_EXPORT_DIRECTORY *exports = (IMAGE_EXPORT_DIRECTORY *)(addr + dir->VirtualAddress);

        fixup_rva_dwords( &exports->Name, delta, 1 );
        fixup_rva_dwords( &exports->AddressOfFunctions, delta, 1 );
        fixup_rva_dwords( &exports->AddressOfNames, delta, 1 );
        fixup_rva_dwords( &exports->AddressOfNameOrdinals, delta, 1 );
        fixup_rva_dwords( (DWORD *)(addr + exports->AddressOfNames), delta, exports->NumberOfNames );
#ifdef __i386_on_x86_64__
        fixup_rva_ptrs( addr + exports->AddressOfFunctions, addr, exports->NumberOfFunctions * 2 );
#else
        fixup_rva_ptrs( addr + exports->AddressOfFunctions, addr, exports->NumberOfFunctions );
#endif
    }
    return STATUS_SUCCESS;
}

static const IMAGE_EXPORT_DIRECTORY *get_export_dir( HMODULE module )
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)module;
    const IMAGE_NT_HEADERS *nt;
    DWORD addr;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)((const BYTE *)dos + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    addr = nt->OptionalHeader.DataDirectory[IMAGE_FILE_EXPORT_DIRECTORY].VirtualAddress;
    if (!addr) return NULL;
    return (IMAGE_EXPORT_DIRECTORY *)((BYTE *)module + addr);
}

static ULONG_PTR find_ordinal_export( HMODULE module, const IMAGE_EXPORT_DIRECTORY *exports, DWORD ordinal )
{
    const DWORD *functions = (const DWORD *)((BYTE *)module + exports->AddressOfFunctions);

    if (ordinal >= exports->NumberOfFunctions) return 0;
    if (!functions[ordinal]) return 0;
    return (ULONG_PTR)module + functions[ordinal];
}

static ULONG_PTR find_named_export( HMODULE module, const IMAGE_EXPORT_DIRECTORY *exports,
                                    const char *name )
{
    const WORD *ordinals = (const WORD *)((BYTE *)module + exports->AddressOfNameOrdinals);
    const DWORD *names = (const DWORD *)((BYTE *)module + exports->AddressOfNames);
    int min = 0, max = exports->NumberOfNames - 1;

    while (min <= max)
    {
        int res, pos = (min + max) / 2;
        char *ename = (char *)module + names[pos];
        if (!(res = strcmp( ename, name ))) return find_ordinal_export( module, exports, ordinals[pos] );
        if (res > 0) max = pos - 1;
        else min = pos + 1;
    }
    return 0;
}

static ULONG_PTR find_pe_export( HMODULE module, const IMAGE_EXPORT_DIRECTORY *exports,
                                 const IMAGE_IMPORT_BY_NAME *name )
{
    const WORD *ordinals = (const WORD *)((BYTE *)module + exports->AddressOfNameOrdinals);
    const DWORD *names = (const DWORD *)((BYTE *)module + exports->AddressOfNames);

    if (name->Hint < exports->NumberOfNames)
    {
        char *ename = (char *)module + names[name->Hint];
        if (!strcmp( ename, (char *)name->Name ))
            return find_ordinal_export( module, exports, ordinals[name->Hint] );
    }
    return find_named_export( module, exports, (char *)name->Name );
}

static inline void *get_rva( void *module, ULONG_PTR addr )
{
    return (BYTE *)module + addr;
}

static NTSTATUS fixup_ntdll_imports( const char * HOSTPTR name, HMODULE module )
{
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_IMPORT_DESCRIPTOR *descr;
    const IMAGE_DATA_DIRECTORY *dir;
    const IMAGE_THUNK_DATA *import_list;
    IMAGE_THUNK_DATA *thunk_list;

    nt = get_rva( module, ((IMAGE_DOS_HEADER *)module)->e_lfanew );
    dir = &nt->OptionalHeader.DataDirectory[IMAGE_FILE_IMPORT_DIRECTORY];
    if (!dir->VirtualAddress || !dir->Size) return STATUS_SUCCESS;

    descr = get_rva( module, dir->VirtualAddress );
    for (; descr->Name && descr->FirstThunk; descr++)
    {
        thunk_list = get_rva( module, descr->FirstThunk );

        /* ntdll must be the only import */
        if (strcmp( get_rva( module, descr->Name ), "ntdll.dll" ))
        {
            ERR( "module %s is importing %s\n", debugstr_a(name), (char *)get_rva( module, descr->Name ));
            return STATUS_PROCEDURE_NOT_FOUND;
        }
        if (descr->u.OriginalFirstThunk)
            import_list = get_rva( module, descr->u.OriginalFirstThunk );
        else
            import_list = thunk_list;

        while (import_list->u1.Ordinal)
        {
            if (IMAGE_SNAP_BY_ORDINAL( import_list->u1.Ordinal ))
            {
                int ordinal = IMAGE_ORDINAL( import_list->u1.Ordinal ) - ntdll_exports->Base;
                thunk_list->u1.Function = find_ordinal_export( ntdll_module, ntdll_exports, ordinal );
                if (!thunk_list->u1.Function) ERR( "%s: ntdll.%u not found\n", debugstr_a(name), ordinal );
            }
            else  /* import by name */
            {
                IMAGE_IMPORT_BY_NAME *pe_name = get_rva( module, import_list->u1.AddressOfData );
                thunk_list->u1.Function = find_pe_export( ntdll_module, ntdll_exports, pe_name );
#ifdef __i386_on_x86_64__
                if ((!thunk_list->u1.Function) && !strcmp( (const char*)pe_name->Name, "__wine_dbg_header_HOSTPTR" ))
                    thunk_list->u1.Function = TRUNCCAST(ULONG_PTR, dlsym( RTLD_SELF_HOSTPTR, "wine_thunk___wine_dbg_header_HOSTPTR" ));
#endif
                if (!thunk_list->u1.Function) ERR( "%s: ntdll.%s not found\n", debugstr_a(name), pe_name->Name );
            }
            import_list++;
            thunk_list++;
        }

#ifdef __i386_on_x86_64__
        import_list++;
        thunk_list++;
        while (import_list->u1.Ordinal)
        {
            if (IMAGE_SNAP_BY_ORDINAL( import_list->u1.Ordinal ))
            {
                int ordinal = IMAGE_ORDINAL( import_list->u1.Ordinal ) - ntdll_exports->Base;
                /*
                thunk_list->u1.Function = find_ordinal_export( ntdll_module, ntdll_exports, ordinal, TRUE );
                if (!thunk_list->u1.Function) ERR( "%s: ntdll.%u not found\n", debugstr_a(name), ordinal );
                */
                ERR( "%s: \"Extra\"/64-bit import by ordinal not implemented (ntdll.%u)\n", debugstr_a(name), ordinal );
            }
            else  /* import by name */
            {
                /* Resolve these imports directly from the Unix side of ntdll.
                 * Now that ntdll is PE(+Unix) and not a hybrid DLL, the PE export table isn't useful.
                 * 32on64 FIXME: this works, but is pretty messy.
                 */
                IMAGE_IMPORT_BY_NAME *pe_name = get_rva( module, import_list->u1.AddressOfData );
                /*thunk_list->u1.Function = find_pe_export( ntdll_module, ntdll_exports, pe_name, TRUE );*/
                thunk_list->u1.Function = TRUNCCAST(ULONG_PTR, dlsym( RTLD_SELF_HOSTPTR, (char *)pe_name->Name ));
                if (!thunk_list->u1.Function) TRACE( "%s: ntdll.%s not found from Unix side of ntdll\n", debugstr_a(name), pe_name->Name );
            }
            import_list++;
            thunk_list++;
        }
#endif
    }
    return STATUS_SUCCESS;
}

static void load_ntdll_functions( HMODULE module )
{
    void **ptr;

    ntdll_exports = get_export_dir( module );
    assert( ntdll_exports );

#define GET_FUNC(name) \
    if (!(p##name = (void *)find_named_export( module, ntdll_exports, #name ))) \
        ERR( "%s not found\n", #name )

    GET_FUNC( DbgUiRemoteBreakin );
    GET_FUNC( KiRaiseUserExceptionDispatcher );
    GET_FUNC( KiUserExceptionDispatcher );
    GET_FUNC( KiUserApcDispatcher );
#ifndef __i386_on_x86_64__
    GET_FUNC( LdrInitializeThunk );
#endif
    GET_FUNC( RtlUserThreadStart );
    GET_FUNC( RtlFindClearBitsAndSet );
#ifdef __i386_on_x86_64__
    GET_FUNC( RtlAllocateHeap );
    GET_FUNC( RtlFreeHeap );
#endif
    GET_FUNC( __wine_set_unix_funcs );
#undef GET_FUNC

#ifdef __i386_on_x86_64__
    pLdrInitializeThunk = (void *)LdrInitializeThunk_stdcall64;
    if (!(pLdrInitializeThunk_stdcall32 = (void *)find_named_export( module, ntdll_exports, "LdrInitializeThunk" ))) \
        ERR( "%s not found\n", "LdrInitializeThunk" );
#endif

#define SET_PTR(name,val) \
    if ((ptr = (void *)find_named_export( module, ntdll_exports, #name ))) *ptr = val; \
    else ERR( "%s not found\n", #name )

    SET_PTR( __wine_syscall_dispatcher, syscall_dispatcher );
#if defined(__i386__) || defined(__i386_on_x86_64__)
    SET_PTR( __wine_ldt_copy, &__wine_ldt_copy );
#endif
#undef SET_PTR
}


static void *callback_module;

/***********************************************************************
 *           load_builtin_callback
 *
 * Load a library in memory; callback function for wine_dll_register
 */
static void load_builtin_callback( void *module, const char *filename )
{
    callback_module = module;
}


/***********************************************************************
 *           load_libwine
 */
static void load_libwine(void)
{
#ifdef __APPLE__
#define LIBWINE "libwine.1.dylib"
#elif defined(__ANDROID__)
#define LIBWINE "libwine.so"
#else
#define LIBWINE "libwine.so.1"
#endif
    typedef void (*load_dll_callback_t)( void *, const char * );
    static void (* HOSTPTR p_wine_dll_set_callback)( load_dll_callback_t load );
    /* 32on64 FIXME: Some of those may need to be declared host or win32 ptr */
    static int * HOSTPTR p___wine_main_argc;
    static char * HOSTPTR * HOSTPTR * HOSTPTR p___wine_main_argv;
    static char * HOSTPTR * HOSTPTR * HOSTPTR p___wine_main_environ;
    static WCHAR * HOSTPTR * HOSTPTR * HOSTPTR p___wine_main_wargv;

    char * HOSTPTR path;
    void * HOSTPTR handle;

    if (build_dir) path = build_path( build_dir, "libs/wine/" LIBWINE );
    else path = build_path( dll_dir, "../" LIBWINE );

    handle = dlopen( path, RTLD_NOW );
    free( path );
    if (!handle && !(handle = dlopen( LIBWINE, RTLD_NOW ))) return;

    p_wine_dll_set_callback = dlsym( handle, "wine_dll_set_callback" );
    p___wine_main_argc      = dlsym( handle, "__wine_main_argc" );
    p___wine_main_argv      = dlsym( handle, "__wine_main_argv" );
    p___wine_main_wargv     = dlsym( handle, "__wine_main_wargv" );
    p___wine_main_environ   = dlsym( handle, "__wine_main_environ" );

    if (p_wine_dll_set_callback) p_wine_dll_set_callback( load_builtin_callback );
    if (p___wine_main_argc) *p___wine_main_argc = main_argc;
    if (p___wine_main_argv) *p___wine_main_argv = main_argv;
    if (p___wine_main_wargv) *p___wine_main_wargv = main_wargv;
    if (p___wine_main_environ) *p___wine_main_environ = main_envp;
}


/***********************************************************************
 *           dlopen_dll
 */
static NTSTATUS dlopen_dll( const char * HOSTPTR so_name, void **ret_module )
{
    struct builtin_module *builtin;
    void *module, * HOSTPTR handle;
    const IMAGE_NT_HEADERS *nt;

    callback_module = (void *)1;
    handle = dlopen( so_name, RTLD_NOW );
    if (!handle)
    {
        WARN( "failed to load .so lib %s: %s\n", debugstr_a(so_name), dlerror() );
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    fixup_so_32on64_sels( handle, so_name );
    if (callback_module != (void *)1)  /* callback was called */
    {
        if (!callback_module) return STATUS_NO_MEMORY;
        WARN( "got old-style builtin library %s, constructors won't work\n", debugstr_a(so_name) );
        module = callback_module;
        LIST_FOR_EACH_ENTRY( builtin, &builtin_modules, struct builtin_module, entry )
            if (builtin->module == module) goto already_loaded;
    }
    /* 32on64 FIXME: this ADDRSPACECAST is probably not right */
    else if ((nt = ADDRSPACECAST(const IMAGE_NT_HEADERS *, dlsym( handle, "__wine_spec_nt_header" ))))
    {
        module = (HMODULE)((nt->OptionalHeader.ImageBase + 0xffff) & ~0xffff);
        LIST_FOR_EACH_ENTRY( builtin, &builtin_modules, struct builtin_module, entry )
            if (builtin->module == module) goto already_loaded;
        if (map_so_dll( nt, module ))
        {
            dlclose( handle );
            return STATUS_NO_MEMORY;
        }
    }
    else  /* already loaded .so */
    {
        WARN( "%s already loaded?\n", debugstr_a(so_name));
        LIST_FOR_EACH_ENTRY( builtin, &builtin_modules, struct builtin_module, entry )
            if (builtin->handle == handle) goto already_loaded;
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    if (add_builtin_module( module, handle, NULL ))
    {
        dlclose( handle );
        return STATUS_NO_MEMORY;
    }
    virtual_create_builtin_view( module );
    *ret_module = module;
    return STATUS_SUCCESS;

already_loaded:
    *ret_module = builtin->module;
    dlclose( handle );
    return STATUS_SUCCESS;
}


/***********************************************************************
 *           dlopen_unix_dll
 */
static NTSTATUS dlopen_unix_dll( void *module, const char * HOSTPTR name, void **unix_entry )
{
    struct builtin_module *builtin;
    void *unix_module, * HOSTPTR handle, *entry;
    const IMAGE_NT_HEADERS *nt;
    NTSTATUS status = STATUS_INVALID_IMAGE_FORMAT;

    handle = dlopen( name, RTLD_NOW );
    if (!handle) return STATUS_DLL_NOT_FOUND;
    /* 32on64 FIXME: these ADDRSPACECASTs are probably not right */
    if (!(nt = ADDRSPACECAST(const IMAGE_NT_HEADERS *, dlsym( handle, "__wine_spec_nt_header" )))) goto done;
#ifdef __i386_on_x86_64__
    if (!(entry = ADDRSPACECAST(void *, dlsym( handle, "wine_thunk___wine_init_unix_lib" )))) goto done;
#else
    if (!(entry = ADDRSPACECAST(void *, dlsym( handle, "__wine_init_unix_lib" )))) goto done;
#endif
    fixup_so_32on64_sels( handle, name );

    unix_module = (HMODULE)((nt->OptionalHeader.ImageBase + 0xffff) & ~0xffff);
    LIST_FOR_EACH_ENTRY( builtin, &builtin_modules, struct builtin_module, entry )
    {
        if (builtin->module == module)
        {
            if (builtin->unix_handle == handle)  /* already loaded */
            {
                *unix_entry = entry;
                status = STATUS_SUCCESS;
                goto done;
            }
            if (builtin->unix_handle)
            {
                ERR( "module %p already has a Unix module that's not %s\n", module, debugstr_a(name) );
                goto done;
            }
            if ((status = map_so_dll( nt, unix_module ))) goto done;
            if ((status = fixup_ntdll_imports( name, unix_module ))) goto done;
            builtin->unix_handle = handle;
            *unix_entry = entry;
            return STATUS_SUCCESS;
        }
        else if (builtin->unix_handle == handle)
        {
            ERR( "%s already loaded for module %p\n", debugstr_a(name), module );
            goto done;
        }
    }
    ERR( "builtin module not found for %s\n", debugstr_a(name) );
done:
    dlclose( handle );
    return status;
}


/***********************************************************************
 *           load_so_dll
 */
static NTSTATUS CDECL load_so_dll( UNICODE_STRING *nt_name, void **module, BOOL *is_hybrid )
{
    static const WCHAR soW[] = {'.','s','o',0};
    char * HOSTPTR unix_name;
    NTSTATUS status;
    DWORD len;

#ifdef __i386_on_x86_64__
    if (is_hybrid)
        *is_hybrid = TRUE;
#endif

    if (nt_to_unix_file_name( nt_name, &unix_name, FILE_OPEN )) return STATUS_DLL_NOT_FOUND;

    /* remove .so extension from Windows name */
    len = nt_name->Length / sizeof(WCHAR);
    if (len > 3 && !wcsicmp( nt_name->Buffer + len - 3, soW )) nt_name->Length -= 3 * sizeof(WCHAR);

    status = dlopen_dll( unix_name, module );
    free( unix_name );
    return status;
}


/* check a PE library architecture */
static BOOL is_valid_binary( HMODULE module, const SECTION_IMAGE_INFORMATION *info )
{
#if defined(__i386__) || defined(__i386_on_x86_64__)
    return info->Machine == IMAGE_FILE_MACHINE_I386;
#elif defined(__arm__)
    return info->Machine == IMAGE_FILE_MACHINE_ARM ||
           info->Machine == IMAGE_FILE_MACHINE_THUMB ||
           info->Machine == IMAGE_FILE_MACHINE_ARMNT;
#elif defined(__x86_64__)
    /* we don't support 32-bit IL-only builtins yet */
    return info->Machine == IMAGE_FILE_MACHINE_AMD64;
#elif defined(__aarch64__)
    return info->Machine == IMAGE_FILE_MACHINE_ARM64;
#endif
}

/* check if the library is the correct architecture */
/* only returns false for a valid library of the wrong arch */
static int check_library_arch( int fd )
{
#ifdef __APPLE__
    struct  /* Mach-O header */
    {
        unsigned int magic;
        unsigned int cputype;
    } header;

    if (read( fd, &header, sizeof(header) ) != sizeof(header)) return 1;
    if (header.magic != 0xfeedface) return 1;
    if (sizeof(void *) == sizeof(int)) return !(header.cputype >> 24);
    else return (header.cputype >> 24) == 1; /* CPU_ARCH_ABI64 */
#else
    struct  /* ELF header */
    {
        unsigned char magic[4];
        unsigned char class;
        unsigned char data;
        unsigned char version;
    } header;

    if (read( fd, &header, sizeof(header) ) != sizeof(header)) return 1;
    if (memcmp( header.magic, "\177ELF", 4 )) return 1;
    if (header.version != 1 /* EV_CURRENT */) return 1;
#ifdef WORDS_BIGENDIAN
    if (header.data != 2 /* ELFDATA2MSB */) return 1;
#else
    if (header.data != 1 /* ELFDATA2LSB */) return 1;
#endif
    if (sizeof(void *) == sizeof(int)) return header.class == 1; /* ELFCLASS32 */
    else return header.class == 2; /* ELFCLASS64 */
#endif
}

static inline char * HOSTPTR prepend( char * HOSTPTR buffer, const char * HOSTPTR str, size_t len )
{
    return memcpy( buffer - len, str, len );
}

/***********************************************************************
 *	open_dll_file
 *
 * Open a file for a new dll. Helper for find_dll_file.
 */
static NTSTATUS open_dll_file( const char * HOSTPTR name, void **module, SECTION_IMAGE_INFORMATION *image_info )
{
    struct builtin_module *builtin;
    OBJECT_ATTRIBUTES attr = { sizeof(attr) };
    LARGE_INTEGER size;
    struct stat st;
    SIZE_T len = 0;
    NTSTATUS status;
    HANDLE handle, mapping;

    if ((status = open_unix_file( &handle, name, GENERIC_READ | SYNCHRONIZE, &attr, 0,
                                  FILE_SHARE_READ | FILE_SHARE_DELETE, FILE_OPEN,
                                  FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0 )))
    {
        if (status != STATUS_OBJECT_PATH_NOT_FOUND && status != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            /* if the file exists but failed to open, report the error */
            if (!stat( name, &st )) return status;
        }
        /* otherwise continue searching */
        return STATUS_DLL_NOT_FOUND;
    }

    if (!stat( name, &st ))
    {
        LIST_FOR_EACH_ENTRY( builtin, &builtin_modules, struct builtin_module, entry )
        {
            if (builtin->id.dev == st.st_dev && builtin->id.ino == st.st_ino)
            {
                TRACE( "%s is the same file as existing module %p\n", debugstr_a(name),
                       builtin->module );
                NtClose( handle );
                NtUnmapViewOfSection( NtCurrentProcess(), *module );
                *module = builtin->module;
                return STATUS_SUCCESS;
            }
        }
    }
    else memset( &st, 0, sizeof(st) );

    size.QuadPart = 0;
    status = NtCreateSection( &mapping, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY |
                              SECTION_MAP_READ | SECTION_MAP_EXECUTE,
                              NULL, &size, PAGE_EXECUTE_READ, SEC_IMAGE, handle );
    NtClose( handle );
    if (status) return status;

    if (*module)
    {
        NtUnmapViewOfSection( NtCurrentProcess(), *module );
        *module = NULL;
    }
    NtQuerySection( mapping, SectionImageInformation, image_info, sizeof(*image_info), NULL );
    status = NtMapViewOfSection( mapping, NtCurrentProcess(), module, 0, 0, NULL, &len,
                                 ViewShare, 0, PAGE_EXECUTE_READ );
    if (status == STATUS_IMAGE_NOT_AT_BASE) status = STATUS_SUCCESS;
    NtClose( mapping );
    if (status) return status;

    /* ignore non-builtins */
    if (!(image_info->u.ImageFlags & IMAGE_FLAGS_WineBuiltin))
    {
        WARN( "%s found in WINEDLLPATH but not a builtin, ignoring\n", debugstr_a(name) );
        status = STATUS_DLL_NOT_FOUND;
    }
    else if (!is_valid_binary( *module, image_info ))
    {
        TRACE( "%s is for arch %x, continuing search\n", debugstr_a(name), image_info->Machine );
        status = STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
    }

    if (!status) status = add_builtin_module( *module, NULL, &st );

    if (status)
    {
        NtUnmapViewOfSection( NtCurrentProcess(), *module );
        *module = NULL;
    }
    return status;
}


/***********************************************************************
 *           open_builtin_file
 */
static NTSTATUS open_builtin_file( char * HOSTPTR name, void **module, SECTION_IMAGE_INFORMATION *image_info, BOOL *is_hybrid )
{
    NTSTATUS status;
    int fd;

    if (is_hybrid)
        *is_hybrid = FALSE;
    status = open_dll_file( name, module, image_info );
    if (status != STATUS_DLL_NOT_FOUND) return status;

    /* try .so file */

    strcat( name, ".so" );
    if ((fd = open( name, O_RDONLY )) != -1)
    {
        if (check_library_arch( fd ))
        {
            NtUnmapViewOfSection( NtCurrentProcess(), *module );
            *module = NULL;
            if (!dlopen_dll( name, module ))
            {
                memset( image_info, 0, sizeof(*image_info) );
                image_info->u.ImageFlags = IMAGE_FLAGS_WineBuiltin;
#ifdef __i386_on_x86_64__
                if (is_hybrid)
                    *is_hybrid = TRUE;
#endif
                status = STATUS_SUCCESS;
            }
            else
            {
                ERR( "failed to load .so lib %s\n", debugstr_a(name) );
                status = STATUS_PROCEDURE_NOT_FOUND;
            }
        }
        else status = STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
        close( fd );
    }
    return status;
}


/***********************************************************************
 *           load_builtin_dll
 */
static NTSTATUS CDECL load_builtin_dll( const WCHAR *name, void **module, void **unix_entry,
                                        SECTION_IMAGE_INFORMATION *image_info, BOOL *is_hybrid )
{
    unsigned int i, pos, len, namelen, maxlen = 0;
    char * HOSTPTR ptr = NULL, * HOSTPTR file, * HOSTPTR ext = NULL;
    NTSTATUS status = STATUS_DLL_NOT_FOUND;
    BOOL found_image = FALSE;

    len = wcslen( name );
    if (build_dir) maxlen = strlen(build_dir) + sizeof("/programs/") + len;
    maxlen = max( maxlen, dll_path_maxlen + 1 ) + len + sizeof(".so");

    if (!(file = malloc( maxlen ))) return STATUS_NO_MEMORY;

    pos = maxlen - len - sizeof(".so");
    /* we don't want to depend on the current codepage here */
    for (i = 0; i < len; i++)
    {
        if (name[i] > 127) goto done;
        file[pos + i] = (char)name[i];
        if (file[pos + i] >= 'A' && file[pos + i] <= 'Z') file[pos + i] += 'a' - 'A';
        else if (file[pos + i] == '.') ext = file + pos + i;
    }
    file[--pos] = '/';

    if (build_dir)
    {
        /* try as a dll */
        ptr = file + pos;
        namelen = len + 1;
        file[pos + len + 1] = 0;
        if (ext && !strcmp( ext, ".dll" )) namelen -= 4;
        ptr = prepend( ptr, ptr, namelen );
        ptr = prepend( ptr, "/dlls", sizeof("/dlls") - 1 );
        ptr = prepend( ptr, build_dir, strlen(build_dir) );
        status = open_builtin_file( ptr, module, image_info, is_hybrid );
        if (status != STATUS_DLL_NOT_FOUND) goto done;

        /* now as a program */
        ptr = file + pos;
        namelen = len + 1;
        file[pos + len + 1] = 0;
        if (ext && !strcmp( ext, ".exe" )) namelen -= 4;
        ptr = prepend( ptr, ptr, namelen );
        ptr = prepend( ptr, "/programs", sizeof("/programs") - 1 );
        ptr = prepend( ptr, build_dir, strlen(build_dir) );
        status = open_builtin_file( ptr, module, image_info, is_hybrid );
        if (status != STATUS_DLL_NOT_FOUND) goto done;
    }

    for (i = 0; dll_paths[i]; i++)
    {
        file[pos + len + 1] = 0;
        ptr = prepend( file + pos, dll_paths[i], strlen(dll_paths[i]) );
        status = open_builtin_file( ptr, module, image_info, is_hybrid );
        if (status == STATUS_IMAGE_MACHINE_TYPE_MISMATCH) found_image = TRUE;
        else if (status != STATUS_DLL_NOT_FOUND) goto done;
    }

    if (found_image) status = STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
    WARN( "cannot find builtin library for %s\n", debugstr_w(name) );

done:
    if (!status && ext)
    {
        strcpy( ext, ".so" );
        dlopen_unix_dll( *module, ptr, unix_entry );
    }
    free( file );
    return status;
}


/***********************************************************************
 *           unload_builtin_dll
 */
static NTSTATUS CDECL unload_builtin_dll( void *module )
{
    struct builtin_module *builtin;

    LIST_FOR_EACH_ENTRY( builtin, &builtin_modules, struct builtin_module, entry )
    {
        if (builtin->module != module) continue;
        list_remove( &builtin->entry );
        if (builtin->handle) dlclose( builtin->handle );
        if (builtin->unix_handle) dlclose( builtin->unix_handle );
        free( builtin );
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}


#ifdef __FreeBSD__
/* The PT_LOAD segments are sorted in increasing order, and the first
 * starts at the beginning of the ELF file. By parsing the file, we can
 * find that first PT_LOAD segment, from which we can find the base
 * address it wanted, and knowing mapbase where the binary was actually
 * loaded, use them to work out the relocbase offset. */
static BOOL get_relocbase(caddr_t mapbase, caddr_t *relocbase)
{
    Elf_Half i;
#ifdef _WIN64
    const Elf64_Ehdr *elf_header = (Elf64_Ehdr*) mapbase;
#else
    const Elf32_Ehdr *elf_header = (Elf32_Ehdr*) mapbase;
#endif
    const Elf_Phdr *prog_header = (const Elf_Phdr *)(mapbase + elf_header->e_phoff);

    for (i = 0; i < elf_header->e_phnum; i++)
    {
         if (prog_header->p_type == PT_LOAD)
         {
             caddr_t desired_base = (caddr_t)((prog_header->p_vaddr / prog_header->p_align) * prog_header->p_align);
             *relocbase = (caddr_t) (mapbase - desired_base);
             return TRUE;
         }
         prog_header++;
    }
    return FALSE;
}
#endif

/*************************************************************************
 *              init_builtin_dll
 */
static void CDECL init_builtin_dll( void *module )
{
#ifdef HAVE_DLINFO
    struct builtin_module *builtin;
    struct link_map *map;
    void (*init_func)(int, char **, char **) = NULL;
    void (**init_array)(int, char **, char **) = NULL;
    ULONG_PTR i, init_arraysz = 0;
#ifdef _WIN64
    const Elf64_Dyn *dyn;
#else
    const Elf32_Dyn *dyn;
#endif

    LIST_FOR_EACH_ENTRY( builtin, &builtin_modules, struct builtin_module, entry )
    {
        if (builtin->module != module) continue;
        if (!builtin->handle) break;
        if (!dlinfo( builtin->handle, RTLD_DI_LINKMAP, &map )) goto found;
        break;
    }
    return;

found:
    for (dyn = map->l_ld; dyn->d_tag; dyn++)
    {
        caddr_t relocbase = (caddr_t)map->l_addr;

#ifdef __FreeBSD__
        /* On older FreeBSD versions, l_addr was the absolute load address, now it's the relocation offset. */
        if (offsetof(struct link_map, l_addr) == 0)
            if (!get_relocbase(map->l_addr, &relocbase)) return;
#endif
        switch (dyn->d_tag)
        {
        case 0x60009990: init_array = (void *)(relocbase + dyn->d_un.d_val); break;
        case 0x60009991: init_arraysz = dyn->d_un.d_val; break;
        case 0x60009992: init_func = (void *)(relocbase + dyn->d_un.d_val); break;
        }
    }

    TRACE( "%p: got init_func %p init_array %p %lu\n", module, init_func, init_array, init_arraysz );

    if (init_func) init_func( main_argc, main_argv, main_envp );

    if (init_array)
        for (i = 0; i < init_arraysz / sizeof(*init_array); i++)
            init_array[i]( main_argc, main_argv, main_envp );
#endif
}


/***********************************************************************
 *           load_ntdll
 */
static void load_ntdll(void)
{
    NTSTATUS status;
    SECTION_IMAGE_INFORMATION info;
    void *module = NULL;
    char * HOSTPTR name = build_path( dll_dir, "ntdll.dll.so" );

    name[strlen(name) - 3] = 0;  /* remove .so */
    status = open_builtin_file( name, &module, &info, NULL );
    if (status) fatal_error( "failed to load %s error %x\n", name, status );
    free( name );
    load_ntdll_functions( module );
    ntdll_module = module;
}


/***********************************************************************
 *           get_image_address
 */
ULONG_HOSTPTR get_image_address(void)
{
#ifdef HAVE_GETAUXVAL
    ULONG_PTR size, num, phdr_addr = getauxval( AT_PHDR );
    ElfW(Phdr) *phdr;

    if (!phdr_addr) return 0;
    phdr = (ElfW(Phdr) *)phdr_addr;
    size = getauxval( AT_PHENT );
    num = getauxval( AT_PHNUM );
    while (num--)
    {
        if (phdr->p_type == PT_PHDR) return phdr_addr - phdr->p_offset;
        phdr = (ElfW(Phdr) *)((char *)phdr + size);
    }
#elif defined(__APPLE__) && defined(TASK_DYLD_INFO)
    struct task_dyld_info dyld_info;
    mach_msg_type_number_t size = TASK_DYLD_INFO_COUNT;

    if (task_info(mach_task_self(), TASK_DYLD_INFO, (task_info_t)&dyld_info, &size) == KERN_SUCCESS)
    {
        ULONG_HOSTPTR ret = dyld_info.all_image_info_addr;
#ifdef __i386_on_x86_64__
        if (ret != dyld_info.all_image_info_addr)
        {
            ERR("dyld image info is above 4GB limit for 32-bit-on-64-bit process\n");
            ret = 0;
        }
#endif
        return ret;
    }
#endif
    return 0;
}

static void * CDECL dlsym_unix_ntdll( const char *func )
{
#ifdef __i386_on_x86_64__
    return TRUNCCAST(void *, dlsym( RTLD_SELF_HOSTPTR, func ));
#else
    return NULL;
#endif
}


/* math function wrappers */
static double CDECL ntdll_atan( double d )  { return atan( d ); }
static double CDECL ntdll_ceil( double d )  { return ceil( d ); }
static double CDECL ntdll_cos( double d )   { return cos( d ); }
static double CDECL ntdll_fabs( double d )  { return fabs( d ); }
static double CDECL ntdll_floor( double d ) { return floor( d ); }
static double CDECL ntdll_log( double d )   { return log( d ); }
static double CDECL ntdll_pow( double x, double y ) { return pow( x, y ); }
static double CDECL ntdll_sin( double d )   { return sin( d ); }
static double CDECL ntdll_sqrt( double d )  { return sqrt( d ); }
static double CDECL ntdll_tan( double d )   { return tan( d ); }


/***********************************************************************
 *           unix_funcs
 */
static struct unix_funcs unix_funcs =
{
#ifdef __aarch64__
    NtCurrentTeb,
#endif
    DbgUiIssueRemoteBreakin,
    RtlGetSystemTimePrecise,
    RtlWaitOnAddress,
    RtlWakeAddressAll,
    RtlWakeAddressSingle,
    fast_RtlpWaitForCriticalSection,
    fast_RtlpUnWaitCriticalSection,
    fast_RtlDeleteCriticalSection,
    fast_RtlTryAcquireSRWLockExclusive,
    fast_RtlAcquireSRWLockExclusive,
    fast_RtlTryAcquireSRWLockShared,
    fast_RtlAcquireSRWLockShared,
    fast_RtlReleaseSRWLockExclusive,
    fast_RtlReleaseSRWLockShared,
    fast_RtlWakeConditionVariable,
    fast_wait_cv,
    ntdll_atan,
    ntdll_ceil,
    ntdll_cos,
    ntdll_fabs,
    ntdll_floor,
    ntdll_log,
    ntdll_pow,
    ntdll_sin,
    ntdll_sqrt,
    ntdll_tan,
    get_initial_environment,
    get_startup_info,
    get_dynamic_environment,
    get_initial_console,
    get_initial_directory,
    get_unix_codepage_data,
    get_locales,
    virtual_release_address_space,
    set_show_dot_files,
    load_so_dll,
    load_builtin_dll,
    unload_builtin_dll,
    init_builtin_dll,
    unwind_builtin_dll,
    dlsym_unix_ntdll,
    __wine_dbg_get_channel_flags,
    __wine_dbg_strdup,
    __wine_dbg_output,
    __wine_dbg_header,
};


#if defined(__APPLE__) && defined(__x86_64__) && !defined(__i386_on_x86_64__)
static __thread struct tm localtime_tls;
struct tm *my_localtime(const time_t *timep)
{
    return localtime_r(timep, &localtime_tls);
}

static void hook(void *to_hook, const void *replace)
{
    size_t offset;
    int ret;

    struct hooked_function
    {
        char jmp[8];
        const void *dst;
    } *hooked_function = to_hook;
    ULONG_PTR intval = (UINT_PTR)to_hook;

    intval -= (intval % 4096);
    ret = mprotect((void *)intval, 0x2000, PROT_EXEC | PROT_READ | PROT_WRITE);

    /* The offset is from the end of the jmp instruction (6 bytes) to the start of the destination. */
    offset = offsetof(struct hooked_function, dst) - offsetof(struct hooked_function, jmp) - 0x6;

    /* jmp *(rip + offset) */
    hooked_function->jmp[0] = 0xff;
    hooked_function->jmp[1] = 0x25;
    hooked_function->jmp[2] = offset;
    hooked_function->jmp[3] = 0x00;
    hooked_function->jmp[4] = 0x00;
    hooked_function->jmp[5] = 0x00;
    /* Filler */
    hooked_function->jmp[6] = 0xcc;
    hooked_function->jmp[7] = 0xcc;
    /* Dest address absolute */
    hooked_function->dst = replace;

    //size = sizeof(*hooked_function);
    //NtProtectVirtualMemory(proc, (void **)hooked_function, &size, old_protect, &old_protect);
}
#endif

/***********************************************************************
 *           start_main_thread
 */
static void start_main_thread(void)
{
    BOOL suspend;
    NTSTATUS status;
    TEB *teb = virtual_alloc_first_teb();

    signal_init_threading();
    signal_alloc_thread( teb );
#ifdef __i386_on_x86_64__
    signal_init_32on64_ldt();
#endif
    signal_init_thread( teb );
    dbg_init();
    server_init_process();
    startup_info_size = server_init_thread( teb->Peb, &suspend );
    esync_init();
    virtual_map_user_shared_data();
    init_cpu_info();
    syscall_dispatcher = signal_init_syscalls();
    init_files();
    NtCreateKeyedEvent( &keyed_event, GENERIC_READ | GENERIC_WRITE, NULL, 0 );
    load_ntdll();
    load_libwine();
    status = p__wine_set_unix_funcs( NTDLL_UNIXLIB_VERSION, &unix_funcs );
    if (status) exec_process( status );

#if defined(__APPLE__) && defined(__x86_64__) && !defined(__i386_on_x86_64__)
    /* Preallocate TlsExpansionSlots for first thread.  Otherwise, kernelbase will
       allocate it on demand, but won't be able to do the Mac-specific poking to the
       %gs-relative address. */
    if (!teb->TlsExpansionSlots)
        teb->TlsExpansionSlots = RtlAllocateHeap( GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                  8 * sizeof(teb->Peb->TlsExpansionBitmapBits) * sizeof(void*) );
    __asm__ volatile ("movq %0,%%gs:%c1"
                      :
                      : "r" (teb->TlsExpansionSlots), "n" (FIELD_OFFSET(TEB, TlsExpansionSlots)));

    /* This is necessary because we poke PEB into pthread TLS at offset 0x60. It is normally in use by
     * localtime(), which is called a lot by system libraries. Make localtime() go away. */
    hook(localtime, my_localtime);

    while (pRtlFindClearBitsAndSet(teb->Peb->TlsBitmap, 1, 1) != ~0U);
#endif

    server_init_process_done();
}

#ifdef __ANDROID__

#ifndef WINE_JAVA_CLASS
#define WINE_JAVA_CLASS "org/winehq/wine/WineDriver"
#endif

JavaVM *java_vm = NULL;
jobject java_object = 0;
unsigned short java_gdt_sel = 0;

/* main Wine initialisation */
static jstring wine_init_jni( JNIEnv *env, jobject obj, jobjectArray cmdline, jobjectArray environment )
{
    char **argv;
    char *str;
    char error[1024];
    int i, argc, length;

    /* get the command line array */

    argc = (*env)->GetArrayLength( env, cmdline );
    for (i = length = 0; i < argc; i++)
    {
        jobject str_obj = (*env)->GetObjectArrayElement( env, cmdline, i );
        length += (*env)->GetStringUTFLength( env, str_obj ) + 1;
    }

    argv = malloc( (argc + 1) * sizeof(*argv) + length );
    str = (char *)(argv + argc + 1);
    for (i = 0; i < argc; i++)
    {
        jobject str_obj = (*env)->GetObjectArrayElement( env, cmdline, i );
        length = (*env)->GetStringUTFLength( env, str_obj );
        (*env)->GetStringUTFRegion( env, str_obj, 0,
                                    (*env)->GetStringLength( env, str_obj ), str );
        argv[i] = str;
        str[length] = 0;
        str += length + 1;
    }
    argv[argc] = NULL;

    /* set the environment variables */

    if (environment)
    {
        int count = (*env)->GetArrayLength( env, environment );
        for (i = 0; i < count - 1; i += 2)
        {
            jobject var_obj = (*env)->GetObjectArrayElement( env, environment, i );
            jobject val_obj = (*env)->GetObjectArrayElement( env, environment, i + 1 );
            const char *var = (*env)->GetStringUTFChars( env, var_obj, NULL );

            if (val_obj)
            {
                const char *val = (*env)->GetStringUTFChars( env, val_obj, NULL );
                setenv( var, val, 1 );
                if (!strcmp( var, "LD_LIBRARY_PATH" ))
                {
                    void (*update_func)( const char * ) = dlsym( RTLD_DEFAULT,
                                                                 "android_update_LD_LIBRARY_PATH" );
                    if (update_func) update_func( val );
                }
                else if (!strcmp( var, "WINEDEBUGLOG" ))
                {
                    int fd = open( val, O_WRONLY | O_CREAT | O_APPEND, 0666 );
                    if (fd != -1)
                    {
                        dup2( fd, 2 );
                        close( fd );
                    }
                }
                (*env)->ReleaseStringUTFChars( env, val_obj, val );
            }
            else unsetenv( var );

            (*env)->ReleaseStringUTFChars( env, var_obj, var );
        }
    }

    java_object = (*env)->NewGlobalRef( env, obj );

    init_paths( argv );
    virtual_init();
    init_environment( argc, argv, environ );

#ifdef __i386__
    {
        unsigned short java_fs;
        __asm__( "mov %%fs,%0" : "=r" (java_fs) );
        if (!(java_fs & 4)) java_gdt_sel = java_fs;
        __asm__( "mov %0,%%fs" :: "r" (0) );
        start_main_thread();
        __asm__( "mov %0,%%fs" :: "r" (java_fs) );
    }
#else
    start_main_thread();
#endif
    return (*env)->NewStringUTF( env, error );
}

jint JNI_OnLoad( JavaVM *vm, void *reserved )
{
    static const JNINativeMethod method =
    {
        "wine_init", "([Ljava/lang/String;[Ljava/lang/String;)Ljava/lang/String;", wine_init_jni
    };

    JNIEnv *env;
    jclass class;

    java_vm = vm;
    if ((*vm)->AttachCurrentThread( vm, &env, NULL ) != JNI_OK) return JNI_ERR;
    if (!(class = (*env)->FindClass( env, WINE_JAVA_CLASS ))) return JNI_ERR;
    (*env)->RegisterNatives( env, class, &method, 1 );
    return JNI_VERSION_1_6;
}

#endif  /* __ANDROID__ */

#ifdef __APPLE__
static void * HOSTPTR apple_wine_thread( void * HOSTPTR arg )
{
    start_main_thread();
    return NULL;
}

/***********************************************************************
 *           apple_create_wine_thread
 *
 * Spin off a secondary thread to complete Wine initialization, leaving
 * the original thread for the Mac frameworks.
 *
 * Invoked as a CFRunLoopSource perform callback.
 */
static void apple_create_wine_thread( void * HOSTPTR arg )
{
    pthread_t thread;
    pthread_attr_t attr;

    pthread_attr_init( &attr );
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_JOINABLE );
    if (pthread_create( &thread, &attr, apple_wine_thread, NULL )) exit(1);
    pthread_attr_destroy( &attr );
}


/***********************************************************************
 *           apple_main_thread
 *
 * Park the process's original thread in a Core Foundation run loop for
 * use by the Mac frameworks, especially receiving and handling
 * distributed notifications.  Spin off a new thread for the rest of the
 * Wine initialization.
 */
static void apple_main_thread(void)
{
    CFRunLoopSourceContext source_context = { 0 };
    CFRunLoopSourceRef source;

    if (!pthread_main_np()) return;

    /* Multi-processing Services can get confused about the main thread if the
     * first time it's used is on a secondary thread.  Use it here to make sure
     * that doesn't happen. */
    MPTaskIsPreemptive(MPCurrentTaskID());

    /* Give ourselves the best chance of having the distributed notification
     * center scheduled on this thread's run loop.  In theory, it's scheduled
     * in the first thread to ask for it. */
    CFNotificationCenterGetDistributedCenter();

    /* We use this run loop source for two purposes.  First, a run loop exits
     * if it has no more sources scheduled.  So, we need at least one source
     * to keep the run loop running.  Second, although it's not critical, it's
     * preferable for the Wine initialization to not proceed until we know
     * the run loop is running.  So, we signal our source immediately after
     * adding it and have its callback spin off the Wine thread. */
#ifndef __i386_on_x86_64__
    source_context.perform = apple_create_wine_thread;
    source = CFRunLoopSourceCreate( NULL, 0, &source_context );
    CFRunLoopAddSource( CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes );
    CFRunLoopSourceSignal( source );
    CFRelease( source );
#endif
    CFRunLoopRun(); /* Should never return, except on error. */
}
#endif  /* __APPLE__ */


#ifdef __ANDROID__

static int pre_exec(void)
{
#if defined(__i386__) || defined(__x86_64__) || defined(__i386_on_x86_64__)
    return 1;  /* we have a preloader */
#else
    return 0;  /* no exec needed */
#endif
}

#elif defined(__linux__) && (defined(__i386__) || defined(__arm__))

static void check_vmsplit( void *stack )
{
    if (stack < (void *)0x80000000)
    {
        /* if the stack is below 0x80000000, assume we can safely try a munmap there */
        if (munmap( (void *)0x80000000, 1 ) == -1 && errno == EINVAL)
            ERR( "Warning: memory above 0x80000000 doesn't seem to be accessible.\n"
                 "Wine requires a 3G/1G user/kernel memory split to work properly.\n" );
    }
}

static int pre_exec(void)
{
    int temp;

    check_vmsplit( &temp );
#ifdef __i386__
    return 1;  /* we have a preloader on x86 */
#else
    return 0;
#endif
}

#elif defined(__linux__) && (defined(__x86_64__) || defined(__i386_on_x86_64__) || defined(__aarch64__))

static int pre_exec(void)
{
    return 1;  /* we have a preloader on x86-64/arm64 */
}

#elif defined(__APPLE__) && (defined(__i386__) || defined(__x86_64__) || defined(__i386_on_x86_64__))

static int pre_exec(void)
{
    return 1;  /* we have a preloader */
}

#elif (defined(__FreeBSD__) || defined (__FreeBSD_kernel__) || defined(__DragonFly__))

static int pre_exec(void)
{
    struct rlimit rl;

    rl.rlim_cur = 0x02000000;
    rl.rlim_max = 0x02000000;
    setrlimit( RLIMIT_DATA, &rl );
    return 1;
}

#else

static int pre_exec(void)
{
    return 0;  /* no exec needed */
}

#endif


/***********************************************************************
 *           check_command_line
 *
 * Check if command line is one that needs to be handled specially.
 */
static void check_command_line( int argc, char * HOSTPTR * HOSTPTR argv )
{
    static const char usage[] =
        "Usage: wine PROGRAM [ARGUMENTS...]   Run the specified program\n"
        "       wine --help                   Display this help and exit\n"
        "       wine --version                Output version information and exit";

    if (argc <= 1)
    {
        fprintf( stderr, "%s\n", usage );
        exit(1);
    }
    if (!strcmp( argv[1], "--help" ))
    {
        printf( "%s\n", usage );
        exit(0);
    }
    if (!strcmp( argv[1], "--version" ))
    {
        printf( "%s\n", wine_get_build_id() );
        exit(0);
    }
}


/***********************************************************************
 *           __wine_main
 *
 * Main entry point called by the wine loader.
 */
#ifdef __i386_on_x86_64__
void __wine_main_real( int argc, char * HOSTPTR * HOSTPTR argv, char * HOSTPTR * HOSTPTR envp )
#else
void __wine_main( int argc, char * HOSTPTR * HOSTPTR argv, char * HOSTPTR * HOSTPTR envp )
#endif
{
    init_paths( argv );

    if (!getenv( "WINELOADERNOEXEC" ))  /* first time around */
    {
        check_command_line( argc, argv );
        if (pre_exec())
        {
            static char noexec[] = "WINELOADERNOEXEC=1";
            char * HOSTPTR * HOSTPTR new_argv = malloc( (argc + 2) * sizeof(*argv) );

            memcpy( new_argv + 1, argv, (argc + 1) * sizeof(*argv) );
            putenv( noexec );
            loader_exec( argv0, new_argv, client_cpu );
            fatal_error( "could not exec the wine loader\n" );
        }
    }

#ifdef RLIMIT_NOFILE
    set_max_limit( RLIMIT_NOFILE );
#endif
#ifdef RLIMIT_AS
    set_max_limit( RLIMIT_AS );
#endif

    virtual_init();
    init_environment( argc, argv, envp );

#if defined(__APPLE__) && !defined(__i386_on_x86_64__)
    apple_main_thread();
#endif
    start_main_thread();
}

#ifdef __i386_on_x86_64__
static struct args
{
    int argc;
    char * HOSTPTR * HOSTPTR argv;
    char * HOSTPTR * HOSTPTR envp;
} args;

static void * HOSTPTR __wine_main_call_real( void * HOSTPTR arg )
{
    __wine_main_real( args.argc, args.argv, args.envp );
    return NULL;
}

/* 32on64 FIXME: __wine_main is called from the real main(), which has a 64-bit stack.
 * All of ntdll is built with 32-bit stacks though.
 * As a crazy workaround, create a thread with a 32-bit-safe stack (while avoiding passing
 * the address of any stack-allocated objects to any functions), and call the normal
 * __wine_main() and start_main_thread() from there.
 *
 * Biggest FIXME here is that we should probably not let mmap() pick the address.
 */
void __wine_main( int argc, char * HOSTPTR * HOSTPTR argv, char * HOSTPTR * HOSTPTR envp )
{
    static pthread_t thread;
    static pthread_attr_t attr;
    static size_t stacksize;
    void * HOSTPTR stack;

    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &stacksize);

    stack = mmap(NULL, stacksize, PROT_READ | PROT_WRITE, MAP_ANON | 0x8000 /*MAP_32BIT*/ | MAP_PRIVATE, 0, 0);

    pthread_attr_setstack(&attr, stack, stacksize);

    args.argc = argc;
    args.argv = argv;
    args.envp = envp;
    pthread_create(&thread, &attr, __wine_main_call_real, NULL);

    apple_main_thread();

    /* If we're at this point, __wine_main() was not called from the main thread
     * (which happens when running in a Menu Helper). No need to run a runloop here,
     * just wait for the wine thread to exit.
     */
    pthread_join(thread, NULL);
}
#endif
