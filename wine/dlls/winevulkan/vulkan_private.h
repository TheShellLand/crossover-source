/* Wine Vulkan ICD private data structures
 *
 * Copyright 2017 Roderick Colenbrander
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

#ifndef __WINE_VULKAN_PRIVATE_H
#define __WINE_VULKAN_PRIVATE_H

/* Perform vulkan struct conversion on 32-bit x86 platforms. */
#if defined(__i386__) || defined(__i386_on_x86_64__)
#define USE_STRUCT_CONVERSION
#endif

#include "wine/debug.h"
#include "wine/heap.h"
#define WINE_LIST_HOSTADDRSPACE
#include "wine/list.h"
#define VK_NO_PROTOTYPES
/* 32on64 FIXME: made WINE_VK_HOST 32on64-only to fix 32-bit crashes on Linux. */
#ifdef __i386_on_x86_64__
#define WINE_VK_HOST
#endif
#include "wine/vulkan.h"
#include "wine/vulkan_driver.h"

#include "vulkan_thunks.h"

#include "wine/hostptraddrspace_enter.h"

/* Magic value defined by Vulkan ICD / Loader spec */
#define VULKAN_ICD_MAGIC_VALUE 0x01CDC0DE

#define WINEVULKAN_QUIRK_GET_DEVICE_PROC_ADDR 0x00000001
#define WINEVULKAN_QUIRK_ADJUST_MAX_IMAGE_COUNT 0x00000002
#define WINEVULKAN_QUIRK_IGNORE_EXPLICIT_LAYERS 0x00000004

struct vulkan_func
{
    const char *name;
    void * WIN32PTR func;
};

/* Base 'class' for our Vulkan dispatchable objects such as VkDevice and VkInstance.
 * This structure MUST be the first element of a dispatchable object as the ICD
 * loader depends on it. For now only contains loader_magic, but over time more common
 * functionality is expected.
 */
struct wine_vk_base
{
    /* Special section in each dispatchable object for use by the ICD loader for
     * storing dispatch tables. The start contains a magical value '0x01CDC0DE'.
     */
    UINT_PTR loader_magic;
};

/* Some extensions have callbacks for those we need to be able to
 * get the wine wrapper for a native handle
 */
struct wine_vk_mapping
{
    struct list link;
    uint64_t native_handle;
    uint64_t wine_wrapped_handle;
};

struct VkCommandBuffer_T
{
    struct wine_vk_base base;
    struct VkDevice_T *device; /* parent */
    VkCommandBuffer command_buffer; /* native command buffer */

    struct list pool_link;
    struct wine_vk_mapping mapping;
};

struct VkDevice_T
{
    struct wine_vk_base base;
    struct vulkan_device_funcs funcs;
    struct VkPhysicalDevice_T *phys_dev; /* parent */
    VkDevice device; /* native device */

    struct VkQueue_T * WIN32PTR * WIN32PTR queues;
    uint32_t max_queue_families;

    unsigned int quirks;

    struct wine_vk_mapping mapping;
};

struct wine_debug_utils_messenger;

struct wine_debug_report_callback
{
    struct VkInstance_T *instance; /* parent */
    VkDebugReportCallbackEXT debug_callback; /* native callback object */

    /* application callback + data */
    PFN_vkDebugReportCallbackEXT user_callback;
    void *user_data;

    struct wine_vk_mapping mapping;
};

struct VkInstance_T
{
    struct wine_vk_base base;
    struct vulkan_instance_funcs funcs;
    VkInstance instance; /* native instance */

    /* We cache devices as we need to wrap them as they are
     * dispatchable objects.
     */
    struct VkPhysicalDevice_T * WIN32PTR * WIN32PTR phys_devs;
    uint32_t phys_dev_count;

    VkBool32 enable_wrapper_list;
    struct list wrappers;
    SRWLOCK wrapper_lock;

    struct wine_debug_utils_messenger *utils_messengers;
    uint32_t utils_messenger_count;

    struct wine_debug_report_callback default_callback;

    unsigned int quirks;

    struct wine_vk_mapping mapping;
};

struct VkPhysicalDevice_T
{
    struct wine_vk_base base;
    struct VkInstance_T *instance; /* parent */
    VkPhysicalDevice phys_dev; /* native physical device */

    VkExtensionProperties *extensions;
    uint32_t extension_count;

    struct wine_vk_mapping mapping;
};

struct VkQueue_T
{
    struct wine_vk_base base;
    struct VkDevice_T *device; /* parent */
    VkQueue queue; /* native queue */

    VkDeviceQueueCreateFlags flags;

    struct wine_vk_mapping mapping;
};

struct wine_cmd_pool
{
    VkCommandPool command_pool;

    struct list command_buffers;

    struct wine_vk_mapping mapping;
};

static inline struct wine_cmd_pool *wine_cmd_pool_from_handle(VkCommandPool handle)
{
    return (struct wine_cmd_pool *)(uintptr_t)handle;
}

static inline VkCommandPool wine_cmd_pool_to_handle(struct wine_cmd_pool *cmd_pool)
{
    return (VkCommandPool)(uintptr_t)cmd_pool;
}

struct wine_debug_utils_messenger
{
    struct VkInstance_T *instance; /* parent */
    VkDebugUtilsMessengerEXT debug_messenger; /* native messenger */

    /* application callback + data */
    PFN_vkDebugUtilsMessengerCallbackEXT user_callback;
    void *user_data;

    struct wine_vk_mapping mapping;
};

static inline struct wine_debug_utils_messenger *wine_debug_utils_messenger_from_handle(
        VkDebugUtilsMessengerEXT handle)
{
    return (struct wine_debug_utils_messenger *)(uintptr_t)handle;
}

static inline VkDebugUtilsMessengerEXT wine_debug_utils_messenger_to_handle(
        struct wine_debug_utils_messenger *debug_messenger)
{
    return (VkDebugUtilsMessengerEXT)(uintptr_t)debug_messenger;
}

static inline struct wine_debug_report_callback *wine_debug_report_callback_from_handle(
        VkDebugReportCallbackEXT handle)
{
    return (struct wine_debug_report_callback *)(uintptr_t)handle;
}

static inline VkDebugReportCallbackEXT wine_debug_report_callback_to_handle(
        struct wine_debug_report_callback *debug_messenger)
{
    return (VkDebugReportCallbackEXT)(uintptr_t)debug_messenger;
}

void * WIN32PTR wine_vk_get_device_proc_addr(const char *name) DECLSPEC_HIDDEN;
void * WIN32PTR wine_vk_get_instance_proc_addr(const char *name) DECLSPEC_HIDDEN;

BOOL wine_vk_device_extension_supported(const char *name) DECLSPEC_HIDDEN;
BOOL wine_vk_instance_extension_supported(const char *name) DECLSPEC_HIDDEN;

BOOL wine_vk_is_type_wrapped(VkObjectType type) DECLSPEC_HIDDEN;
uint64_t wine_vk_unwrap_handle(VkObjectType type, uint64_t handle) DECLSPEC_HIDDEN;

#include "wine/hostptraddrspace_exit.h"

#endif /* __WINE_VULKAN_PRIVATE_H */