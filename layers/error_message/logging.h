/* Copyright (c) 2015-2024 The Khronos Group Inc.
 * Copyright (c) 2015-2024 Valve Corporation
 * Copyright (c) 2015-2024 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <array>
#include <cstdarg>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/utility/vk_struct_helper.hpp>

#include "vk_layer_config.h"
#include "containers/custom_containers.h"
#include "generated/vk_layer_dispatch_table.h"
#include "generated/vk_object_types.h"

#if defined __ANDROID__
#include <android/log.h>
#define LOGCONSOLE(...) ((void)__android_log_print(ANDROID_LOG_INFO, "VALIDATION", __VA_ARGS__))
[[maybe_unused]] static const char *kForceDefaultCallbackKey = "debug.vvl.forcelayerlog";
#endif

extern const char *kVUIDUndefined;

typedef enum DebugCallbackStatusBits {
    DEBUG_CALLBACK_UTILS = 0x00000001,     // This struct describes a VK_EXT_debug_utils callback
    DEBUG_CALLBACK_DEFAULT = 0x00000002,   // An internally created callback, used if no user-defined callbacks are registered
    DEBUG_CALLBACK_INSTANCE = 0x00000004,  // An internally created temporary instance callback
} DebugCallbackStatusBits;
typedef VkFlags DebugCallbackStatusFlags;

struct LogObjectList {
    small_vector<VulkanTypedHandle, 4, uint32_t> object_list;

    template <typename HANDLE_T>
    void add(HANDLE_T object) {
        object_list.emplace_back(object, ConvertCoreObjectToVulkanObject(VkHandleInfo<HANDLE_T>::kVkObjectType));
    }

    template <typename... HANDLE_T>
    void add(HANDLE_T... objects) {
        (..., add(objects));
    }

    void add(VulkanTypedHandle typed_handle) { object_list.emplace_back(typed_handle); }

    template <typename HANDLE_T>
    LogObjectList(HANDLE_T object) {
        add(object);
    }

    template <typename... HANDLE_T>
    LogObjectList(HANDLE_T... objects) {
        (..., add(objects));
    }

    [[nodiscard]] auto size() const { return object_list.size(); }
    [[nodiscard]] auto empty() const { return object_list.empty(); }
    [[nodiscard]] auto begin() const { return object_list.begin(); }
    [[nodiscard]] auto end() const { return object_list.end(); }

    LogObjectList(){};
};

typedef struct VkLayerDbgFunctionState {
    DebugCallbackStatusFlags callback_status;

    // Debug report related information
    VkDebugReportCallbackEXT debug_report_callback_object;
    PFN_vkDebugReportCallbackEXT debug_report_callback_function_ptr;
    VkFlags debug_report_msg_flags;

    // Debug utils related information
    VkDebugUtilsMessengerEXT debug_utils_callback_object;
    VkDebugUtilsMessageSeverityFlagsEXT debug_utils_msg_flags;
    VkDebugUtilsMessageTypeFlagsEXT debug_utils_msg_type;
    PFN_vkDebugUtilsMessengerCallbackEXT debug_utils_callback_function_ptr;

    void *pUserData;

    bool IsUtils() const { return ((callback_status & DEBUG_CALLBACK_UTILS) != 0); }
    bool IsDefault() const { return ((callback_status & DEBUG_CALLBACK_DEFAULT) != 0); }
    bool IsInstance() const { return ((callback_status & DEBUG_CALLBACK_INSTANCE) != 0); }
} VkLayerDbgFunctionState;

// TODO: Could be autogenerated for the specific handles for extra type safety...
template <typename HANDLE_T>
static inline uint64_t HandleToUint64(HANDLE_T h) {
    return CastToUint64<HANDLE_T>(h);
}

static inline uint64_t HandleToUint64(uint64_t h) { return h; }

// Data we store per label for logging
struct LoggingLabel {
    std::string name;
    std::array<float, 4> color;

    void Reset() { *this = LoggingLabel(); }
    bool Empty() const { return name.empty(); }

    VkDebugUtilsLabelEXT Export() const {
        VkDebugUtilsLabelEXT out = vku::InitStructHelper();
        out.pLabelName = name.c_str();
        std::copy(color.cbegin(), color.cend(), out.color);
        return out;
    };

    LoggingLabel() : name(), color({{0.f, 0.f, 0.f, 0.f}}) {}
    LoggingLabel(const VkDebugUtilsLabelEXT *label_info) {
        if (label_info && label_info->pLabelName) {
            name = label_info->pLabelName;
            std::copy_n(std::begin(label_info->color), 4, color.begin());
        } else {
            Reset();
        }
    }

    LoggingLabel(const LoggingLabel &) = default;
    LoggingLabel &operator=(const LoggingLabel &) = default;
    LoggingLabel &operator=(LoggingLabel &&) = default;
    LoggingLabel(LoggingLabel &&) = default;

    template <typename Name, typename Vec>
    LoggingLabel(Name &&name_, Vec &&vec_) : name(std::forward<Name>(name_)), color(std::forward<Vec>(vec_)) {}
};

struct LoggingLabelState {
    std::vector<LoggingLabel> labels;
    LoggingLabel insert_label;

    // Export the labels, but in reverse order since we want the most recent at the top.
    std::vector<VkDebugUtilsLabelEXT> Export() const {
        size_t count = labels.size() + (insert_label.Empty() ? 0 : 1);
        std::vector<VkDebugUtilsLabelEXT> out(count);

        if (!count) return out;

        size_t index = count - 1;
        if (!insert_label.Empty()) {
            out[index--] = insert_label.Export();
        }
        for (const auto &label : labels) {
            out[index--] = label.Export();
        }
        return out;
    }
};

class TypedHandleWrapper {
  public:
    template <typename Handle>
    TypedHandleWrapper(Handle h, VulkanObjectType t) : handle_(h, t) {}

    const VulkanTypedHandle &Handle() const { return handle_; }
    VulkanObjectType Type() const { return handle_.type; }

  protected:
    VulkanTypedHandle handle_;
};

struct Location;

struct MessageFormatSettings {
    bool display_application_name = false;
    std::string application_name;
};

class DebugReport {
  public:
    std::vector<VkLayerDbgFunctionState> debug_callback_list;
    // We use unordered_set to use trivial hashing for filter_message_ids as we already store hashed values
    vvl::unordered_set<uint32_t> filter_message_ids{};
    // This mutex is defined as mutable since the normal usage for a debug report object is as 'const'. The mutable keyword allows
    // the layers to continue this pattern, but also allows them to use/change this specific member for synchronization purposes.
    mutable std::mutex debug_output_mutex;
    uint32_t duplicate_message_limit = 0;
    const void *instance_pnext_chain{};
    bool force_default_log_callback{false};
    uint32_t device_created = 0;
    MessageFormatSettings message_format_settings;

    void SetUtilsObjectName(const VkDebugUtilsObjectNameInfoEXT *pNameInfo);
    void SetMarkerObjectName(const VkDebugMarkerObjectNameInfoEXT *pNameInfo);
    std::string GetUtilsObjectNameNoLock(const uint64_t object) const;
    std::string GetMarkerObjectNameNoLock(const uint64_t object) const;

    void SetDebugUtilsSeverityFlags(std::vector<VkLayerDbgFunctionState> &callbacks);
    void RemoveDebugUtilsCallback(uint64_t callback);

    std::string FormatHandle(const char *handle_type_name, uint64_t handle) const;

    std::string FormatHandle(const VulkanTypedHandle &handle) const {
        return FormatHandle(string_VulkanObjectType(handle.type), handle.handle);
    }

    std::string FormatHandle(const TypedHandleWrapper &wrapper) const { return FormatHandle(wrapper.Handle()); }

    template <typename T, typename std::enable_if_t<!std::is_base_of<TypedHandleWrapper, T>::value, void *> = nullptr>
    std::string FormatHandle(T handle) const {
        return FormatHandle(VkHandleInfo<T>::Typename(), HandleToUint64(handle));
    }

    // Formats messages to be in the proper format, handles VUID logic, and any legacy issues
    bool LogMsg(VkFlags msg_flags, const LogObjectList &objects, const Location &loc, std::string_view vuid_text,
                const char *format, va_list argptr);
    // Core logging that interacts with the DebugCallbacks
    bool DebugLogMsg(VkFlags msg_flags, const LogObjectList &objects, const char *message, const char *text_vuid) const;

    void BeginQueueDebugUtilsLabel(VkQueue queue, const VkDebugUtilsLabelEXT *label_info);
    void EndQueueDebugUtilsLabel(VkQueue queue);
    void InsertQueueDebugUtilsLabel(VkQueue queue, const VkDebugUtilsLabelEXT *label_info);

    void BeginCmdDebugUtilsLabel(VkCommandBuffer command_buffer, const VkDebugUtilsLabelEXT *label_info);
    void EndCmdDebugUtilsLabel(VkCommandBuffer command_buffer);
    void InsertCmdDebugUtilsLabel(VkCommandBuffer command_buffer, const VkDebugUtilsLabelEXT *label_info);
    void ResetCmdDebugUtilsLabel(VkCommandBuffer command_buffer);
    void EraseCmdDebugUtilsLabel(VkCommandBuffer command_buffer);

  private:
    bool UpdateLogMsgCounts(int32_t vuid_hash) const;
    bool LogMsgEnabled(std::string_view vuid_text, VkDebugUtilsMessageSeverityFlagsEXT severity,
                       VkDebugUtilsMessageTypeFlagsEXT type);

    VkDebugUtilsMessageSeverityFlagsEXT active_severities{0};
    VkDebugUtilsMessageTypeFlagsEXT active_types{0};
    mutable vvl::unordered_map<uint32_t, uint32_t> duplicate_message_count_map{};

    vvl::unordered_map<VkQueue, std::unique_ptr<LoggingLabelState>> debug_utils_queue_labels;
    vvl::unordered_map<VkCommandBuffer, std::unique_ptr<LoggingLabelState>> debug_utils_cmd_buffer_labels;
    vvl::unordered_map<uint64_t, std::string> debug_object_name_map;
    vvl::unordered_map<uint64_t, std::string> debug_utils_object_name_map;
};

template DebugReport *GetLayerDataPtr<DebugReport>(void *data_key, std::unordered_map<void *, DebugReport *> &data_map);

VKAPI_ATTR VkResult LayerCreateMessengerCallback(DebugReport *debug_report, bool default_callback,
                                                 const VkDebugUtilsMessengerCreateInfoEXT *create_info,
                                                 VkDebugUtilsMessengerEXT *messenger);

VKAPI_ATTR VkResult LayerCreateReportCallback(DebugReport *debug_report, bool default_callback,
                                              const VkDebugReportCallbackCreateInfoEXT *create_info,
                                              VkDebugReportCallbackEXT *callback);

template <typename T>
static inline void LayerDestroyCallback(DebugReport *debug_report, T callback) {
    std::unique_lock<std::mutex> lock(debug_report->debug_output_mutex);
    debug_report->RemoveDebugUtilsCallback(CastToUint64(callback));
}

VKAPI_ATTR void ActivateInstanceDebugCallbacks(DebugReport *debug_report);

VKAPI_ATTR void DeactivateInstanceDebugCallbacks(DebugReport *debug_report);

VKAPI_ATTR void LayerDebugUtilsDestroyInstance(DebugReport *debug_report);

VKAPI_ATTR VkBool32 VKAPI_CALL MessengerBreakCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data);

VKAPI_ATTR VkBool32 VKAPI_CALL MessengerLogCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                    VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data);

VKAPI_ATTR VkBool32 VKAPI_CALL MessengerWin32DebugOutputMsg(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                            VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                            const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                                                            void *user_data);
