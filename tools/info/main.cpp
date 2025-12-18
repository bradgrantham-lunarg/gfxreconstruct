/*
** Copyright (c) 2020-2024 LunarG, Inc.
** Copyright (c) 2022-2024 Advanced Micro Devices, Inc. All rights reserved.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and associated documentation files (the "Software"),
** to deal in the Software without restriction, including without limitation
** the rights to use, copy, modify, merge, publish, distribute, sublicense,
** and/or sell copies of the Software, and to permit persons to whom the
** Software is furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
*/

#include PROJECT_VERSION_HEADER_FILE

#include "tool_settings.h"

#include "decode/decode_api_detection.h"
#include "decode/stat_consumer.h"
#include "decode/stat_consumer_base.h"
#include "decode/stat_decoder_base.h"
#include "decode/file_processor.h"
#include "format/format.h"
#include "format/format_util.h"

#if ENABLE_OPENXR_SUPPORT
#include "decode/openxr_detection_consumer.h"
#include "decode/openxr_stats_consumer.h"
#include "generated/generated_openxr_decoder.h"
#endif

#include "decode/info_decoder.h"
#include "decode/info_consumer.h"
#include "decode/vulkan_detection_consumer.h"
#include "decode/vulkan_stats_consumer.h"
#include "generated/generated_vulkan_decoder.h"

#if defined(D3D12_SUPPORT)
#include "decode/dx12_stats_consumer.h"
#include "generated/generated_dx12_decoder.h"
#include "decode/dx12_detection_consumer.h"
#include "graphics/dx12_util.h"
#endif

#include "util/argument_parser.h"
#include "util/strings.h"
#include "util/logging.h"
#include "util/to_string.h"
#include "util/platform.h"

#if ENABLE_OPENXR_SUPPORT
#include "openxr/openxr.h"
#endif
#include "vulkan/vulkan.h"

#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <iostream>

#include <nlohmann/json.hpp>

const char kExeInfoOnlyOption[]    = "--exe-info-only";
const char kEnvVarsOnlyOption[]    = "--env-vars-only";
const char kFileFormatOnlyOption[] = "--file-format-only";
const char kEnumGpuIndices[]       = "--enum-gpu-indices";
const char kVerboseOption[]        = "--verbose";
const char kOutputFileArgument[]   = "--output";

const char kOptions[]   = "-h|--help,--version,--no-debug-popup,--exe-info-only,--env-vars-only,--file-format-only,--"
                          "enum-gpu-indices,--verbose";
const char kArguments[] = "--log-level,--output";

const char kUnrecognizedFormatString[] = "<unrecognized-format>";

const int kDefaultIndent = 12;

// Global variables defining where we should output results to
struct ApiAgnosticStats
{
    gfxrecon::format::CompressionType compression_type;
    uint32_t                          trim_start_frame;
    uint32_t                          frame_count;
    gfxrecon::decode::BlockIOError    error_state;
    uint32_t                          blank_frame_count;
    bool                              uses_frame_markers;
};

struct FileFormatInfo
{
    uint32_t major_version               = 0;
    uint32_t minor_version               = 0;
    bool     uses_frame_markers          = false;
    bool     file_supports_frame_markers = false;

    FileFormatInfo(const gfxrecon::decode::FileProcessor& file_processor)
    {
        const gfxrecon::format::FileHeader& file_header = file_processor.GetFileHeader();
        major_version                                   = file_header.major_version;
        minor_version                                   = file_header.minor_version;
        uses_frame_markers                              = file_processor.UsesFrameMarkers();
        file_supports_frame_markers                     = file_processor.FileSupportsFrameMarkers();
    }

    bool NeedsUpdate() const
    {
        return major_version == 0 && minor_version == 0 && uses_frame_markers && !file_supports_frame_markers;
    }
};

class AnnotationRecorder : public gfxrecon::decode::AnnotationHandler
{
  public:
    std::vector<std::string> operation_annotations_;

    virtual void ProcessAnnotation(uint64_t                         block_index,
                                   gfxrecon::format::AnnotationType type,
                                   const std::string&               label,
                                   const std::string&               data) override
    {
        if (type == gfxrecon::format::AnnotationType::kJson &&
            label.compare(gfxrecon::format::kAnnotationLabelOperation) == 0)
        {
            if (data.size() > 0)
            {
                // Inspect annotations spotted in the capture file
                nlohmann::json json_obj = nlohmann::json::parse(data);
                if (json_obj.is_discarded())
                {
                    GFXRECON_LOG_WARNING("Invalid JSON in annotation: \"%s\"", data.c_str());
                }
                else
                {
                    operation_annotations_.push_back(data);
                }
            }
        }
    }
};

std::string AdapterTypeToString(gfxrecon::format::AdapterType type)
{
    switch (type)
    {
        case gfxrecon::format::AdapterType::kUnknownAdapter:
            return "Unknown type (DXGI 1.0)";
        case gfxrecon::format::AdapterType::kSoftwareAdapter:
            return "Software";
        case gfxrecon::format::AdapterType::kHardwareAdapter:
            return "Hardware";
        default:
            return "Unknown";
    }
}

static void PrintUsage(const char* exe_name)
{
    std::string app_name     = exe_name;
    size_t      dir_location = app_name.find_last_of("/\\");
    if (dir_location >= 0)
    {
        app_name.replace(0, dir_location + 1, "");
    }
    GFXRECON_WRITE_CONSOLE("\n%s - Print statistics for a GFXReconstruct capture file.\n", app_name.c_str());
    GFXRECON_WRITE_CONSOLE("Usage:");
    GFXRECON_WRITE_CONSOLE("  %s [-h | --help] [--version] [--exe-info-only] [--verbose] [--output <file>] <capture-file>\n",
                app_name.c_str());
    GFXRECON_WRITE_CONSOLE("Required arguments:");
    GFXRECON_WRITE_CONSOLE("  <capture-file>\tThe GFXReconstruct capture file to be processed.");
    GFXRECON_WRITE_CONSOLE("\nOptional arguments:");
    GFXRECON_WRITE_CONSOLE("  -h\t\t\tPrint usage information and exit (same as --help).");
    GFXRECON_WRITE_CONSOLE("  --version\t\tPrint version information and exit.");
    GFXRECON_WRITE_CONSOLE("  --exe-info-only\tQuickly exit after extracting captured application's executable name");
    GFXRECON_WRITE_CONSOLE("  --file-format-only\tQuickly exit after extracting file format information");
    GFXRECON_WRITE_CONSOLE("  --env-vars-only\tQuickly exit after extracting captured application's environment variables");
#if defined(WIN32) && defined(_DEBUG)
    GFXRECON_WRITE_CONSOLE("  --no-debug-popup\tDisable the 'Abort, Retry, Ignore' message box");
    GFXRECON_WRITE_CONSOLE("        \t\tdisplayed when abort() is called (Windows debug only).");
#endif
#if defined(WIN32)
    GFXRECON_WRITE_CONSOLE("  --enum-gpu-indices\tPrint GPU indices and exit");
#endif
    GFXRECON_WRITE_CONSOLE("  --verbose\t\tOutput more information in JSON format");
    GFXRECON_WRITE_CONSOLE(
        "  --output\t\tOutput generated information to the provided file. If not defined output goes to std::out");
}

static std::string GetVkVersionString(uint32_t api_version)
{
    uint32_t major = VK_API_VERSION_MAJOR(api_version);
    uint32_t minor = VK_API_VERSION_MINOR(api_version);
    uint32_t patch = VK_API_VERSION_PATCH(api_version);

    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

static std::string GetXrVersionString(uint32_t api_version)
{
    uint32_t major = XR_VERSION_MAJOR(api_version);
    uint32_t minor = XR_VERSION_MINOR(api_version);
    uint32_t patch = XR_VERSION_PATCH(api_version);

    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

void GatherApiAgnosticStats(ApiAgnosticStats&                api_agnostic_stats,
                            gfxrecon::decode::FileProcessor& file_processor,
                            gfxrecon::decode::StatConsumer&  stat_consumer,
                            uint32_t                         blank_frame_count)
{
    api_agnostic_stats.error_state = file_processor.GetErrorState();

    // File options.
    gfxrecon::format::CompressionType compression_type = gfxrecon::format::CompressionType::kNone;

    auto file_options = file_processor.GetFileOptions();
    for (const auto& option : file_options)
    {
        if (option.key == gfxrecon::format::FileOption::kCompressionType)
        {
            compression_type = static_cast<gfxrecon::format::CompressionType>(option.value);
        }
    }
    api_agnostic_stats.compression_type   = compression_type;
    api_agnostic_stats.trim_start_frame   = stat_consumer.GetTrimmedStartFrame();
    api_agnostic_stats.frame_count        = file_processor.GetCurrentFrameNumber();
    api_agnostic_stats.uses_frame_markers = file_processor.UsesFrameMarkers();
    api_agnostic_stats.blank_frame_count  = blank_frame_count;
}

nlohmann::json GetOperationsJson(const AnnotationRecorder& annotation_recorder)
{
    nlohmann::json annotations_json = nlohmann::json::array();
    for (const auto& annotation_info : annotation_recorder.operation_annotations_)
    {
        annotations_json.push_back(nlohmann::json::parse(annotation_info));
    }
    return annotations_json;
}

void PrintAnnotationsText(std::ostream& output_stream, const AnnotationRecorder& annotation_recorder)
{
    // If the capture file had target annotations, display them in an info block
    if (!annotation_recorder.operation_annotations_.empty())
    {
        static const std::map<std::string, std::string> kTargetAnnotations = {
            { gfxrecon::format::kOperationAnnotationGfxreconstructVersion, "GFXR version" },
            { gfxrecon::format::kOperationAnnotationTimestamp, "Capture timestamp" },
            { gfxrecon::format::kOperationAnnotationVulkanVersion, "Vulkan version" },
            { gfxrecon::format::kOperationAnnotationCaptureParameters, "Non-default capture options" },
        };

        output_stream << "\n";
        output_stream << "Annotations:\n";
        for (const auto& annotation_info : annotation_recorder.operation_annotations_)
        {
            nlohmann::json json_obj = nlohmann::json::parse(annotation_info);
            for (const auto& item : json_obj.items())
            {
                const auto target = kTargetAnnotations.find(item.key());
                if (target != kTargetAnnotations.end())
                {
                    if (item.value().is_object())
                    {
                        // Convert the JSON annotation object into multiline output.
                        // Unfortunately, this puts the last brace on a new line at column 0.
                        // So insert a tab right in  front of it.
                        std::string out = "\n\t" + item.value().dump(kDefaultIndent);
                        out.insert(out.size() - 1, 1, '\t');
                        output_stream << std::format("\t{}: {}", target->second, out) << '\n';
                    }
                    else
                    {
                        output_stream << std::format("\t{}: {}", target->second, item.value().get<std::string>());
                    }
                }
            }
        }
    }
}

inline std::string GetFrameMarkerString(bool uses_frame_markers, bool needs_update)
{
    return uses_frame_markers ? (needs_update ? "explicit (unsupported)" : "explicit") : "implicit";
}

nlohmann::json GetFileFormatInfoJson(const gfxrecon::decode::FileProcessor& file_processor)
{
    FileFormatInfo file_format_info(file_processor);

    auto file_format_version = std::to_string(file_format_info.major_version) + "." + std::to_string(file_format_info.minor_version);
    auto frame_marker_string = GetFrameMarkerString(file_format_info.uses_frame_markers, file_format_info.NeedsUpdate());

    return {
        {"file-version", file_format_version},
        {"frame-delimiters", frame_marker_string},
    };
}

void PrintFileFormatInfoText(std::ostream& output_stream, const gfxrecon::decode::FileProcessor& file_processor)
{
    FileFormatInfo file_format_info(file_processor);
    output_stream << std::format("\tFile format version: {}.{}", file_format_info.major_version, file_format_info.minor_version) << '\n';
    output_stream << "\tFrame delimiters: " << GetFrameMarkerString(file_format_info.uses_frame_markers, file_format_info.NeedsUpdate()) << '\n';
}

std::string GetDriverInfoString(const gfxrecon::decode::InfoConsumer& driver_info_consumer)
{
    if (gfxrecon::util::platform::StringLength(driver_info_consumer.GetDriverDesc()) > 0)
    {
        return driver_info_consumer.GetDriverDesc();
    }
    else
    {
        return "Not available";
    }
}

void PrintDriverInfoText(std::ostream& output_stream, const gfxrecon::decode::InfoConsumer& driver_info_consumer)
{
    output_stream << "\n";
    output_stream << "Driver info:\n";
    if (gfxrecon::util::platform::StringLength(driver_info_consumer.GetDriverDesc()) > 0)
    {
        output_stream << "\t" << driver_info_consumer.GetDriverDesc() << '\n';
    }
    else
    {
        output_stream << "\tDriver info not available.\n";
        output_stream << "\n";
    }
}

nlohmann::json GetExeInfoJson(const gfxrecon::decode::InfoConsumer& info_consumer)
{
    auto        exe_version  = info_consumer.GetAppVersion();
    std::string file_desc    = info_consumer.GetFileDescription();
    std::string product_name = info_consumer.GetProductName();
    std::string version = std::to_string(exe_version[0]) + "." + std::to_string(exe_version[1]) + "." +
                                    std::to_string(exe_version[2]) + "." + std::to_string(exe_version[3]);

    return {
        {"name", info_consumer.GetAppExeName()},
        {"version", version},
        {"company", info_consumer.GetCompanyName()},
        {"product", product_name},
        {"file-description", file_desc},
    };
}

void PrintExeInfoText(std::ostream& output_stream, const gfxrecon::decode::InfoConsumer& info_consumer)
{
    auto        exe_version  = info_consumer.GetAppVersion();
    std::string file_desc    = info_consumer.GetFileDescription();
    std::string product_name = info_consumer.GetProductName();

    output_stream << "Exe info:\n";
    output_stream << "\tApplication exe name: " << info_consumer.GetAppExeName() << '\n';

    auto exe_version_string = std::format("{}.{}.{}.{}", exe_version[0], exe_version[1], exe_version[2], exe_version[3]);
    output_stream << "\tApplication version: " << exe_version_string << '\n';
    output_stream << "\tApplication Company name: " << info_consumer.GetCompanyName() << '\n';

    // we are combining file description and product name and presenting both only if they are not same
    std::string app_data = file_desc;
    if (strcmp(product_name.c_str(), "N/A") != 0)
    {
        if (strcmp(product_name.c_str(), file_desc.c_str()) != 0)
        {
            app_data += " // ";
            app_data += info_consumer.GetProductName();
        }
    }
    output_stream << "\tProduct name: " << app_data << '\n';
}

nlohmann::json GetEnvironmentVariableInfoJson(const std::vector<std::string>& environment_variables)
{
    nlohmann::json environment;

    static const std::string delimiter = "=";
    for (const auto& var : environment_variables)
    {
        const auto& delimiter_pos       = var.find(delimiter);
        std::string key                 = var.substr(0, delimiter_pos);
        std::string value               = var.substr(delimiter_pos + 1);
        environment[key] = value;
    }

    return environment;
}

void PrintEnvironmentVariableInfoText(std::ostream& output_stream, gfxrecon::decode::InfoConsumer& info_consumer)
{
    output_stream << "Environment variables:\n";
    for (const auto& var : info_consumer.GetEnvironmentVariables())
    {
        output_stream << "\t" << var << '\n';
    }
}

nlohmann::json GetDetectedApiInfoJson(bool vulkan_present, bool dx12_present, bool openxr_present)
{
    std::vector<std::string> apis;
    if (vulkan_present)
    {
        apis.push_back("Vulkan");
    }
    if (dx12_present)
    {
        apis.push_back("D3D12");
    }
    if (openxr_present)
    {
        apis.push_back("OpenXR");
    }
    return apis;
}

void PrintDetectedApiInfoText(std::ostream& output_stream, bool vulkan_present, bool dx12_present, bool openxr_present)
{
    if (!vulkan_present && !dx12_present && !openxr_present)
    {
        output_stream << "Unable to detect capture file API(s). Writing all stats.\n";
    }
}

nlohmann::json GetApiAgnosticStatsJson(const ApiAgnosticStats& api_agnostic_stats,
                               bool                    vulkan_present,
                               bool                    dx12_present)
{
    // Compression type.
    std::string compression_type_name = gfxrecon::format::GetCompressionTypeName(api_agnostic_stats.compression_type);
    if (compression_type_name.empty())
    {
        compression_type_name = kUnrecognizedFormatString;
    }

    return {
        {"compression", {
            {"format", compression_type_name},
        }},
        {"frames", {
            {"blank-count", api_agnostic_stats.blank_frame_count},
            {"actual-count", api_agnostic_stats.frame_count},
            {"total-count", api_agnostic_stats.blank_frame_count + api_agnostic_stats.frame_count},
            {"start-frame", api_agnostic_stats.trim_start_frame},
            {"end-frame", api_agnostic_stats.trim_start_frame + api_agnostic_stats.frame_count - 1},
        }}
    };
}

void PrintApiAgnosticStatsText(std::ostream& output_stream, const ApiAgnosticStats& api_agnostic_stats, bool vulkan_present, bool dx12_present)
{
    // Compression type.
    std::string compression_type_name = gfxrecon::format::GetCompressionTypeName(api_agnostic_stats.compression_type);
    if (compression_type_name.empty())
    {
        compression_type_name = kUnrecognizedFormatString;
    }

    output_stream << "\n";
    output_stream << "File info:\n";
    output_stream << "\tCompression format: " << compression_type_name << '\n';

    if (api_agnostic_stats.trim_start_frame == 0)
    {
        // Not a trimmed file.
        output_stream << "\tTotal frames: " << api_agnostic_stats.frame_count << '\n';
    }
    else
    {
        if (api_agnostic_stats.blank_frame_count)
        {
            output_stream << "\tBlank frames: " << api_agnostic_stats.blank_frame_count << '\n';
            output_stream << "\tCaptured frames: " << api_agnostic_stats.frame_count << '\n';
        }

        // Print out the total frames and range based on the API (since we have 2 different ways of showing it)
        if (vulkan_present)
        {
            auto frames_string = std::format("{} (trimmed frame range {}-{})",
                        api_agnostic_stats.frame_count,
                        api_agnostic_stats.trim_start_frame,
                        api_agnostic_stats.trim_start_frame + api_agnostic_stats.frame_count - 1);
            output_stream << "\tTotal frames: " << frames_string << '\n';
        }
        else
        {
            output_stream << "\tTotal frames: " << api_agnostic_stats.blank_frame_count + api_agnostic_stats.frame_count << '\n';

            auto frames_string = std::format("{}-{}",
                        api_agnostic_stats.trim_start_frame,
                        api_agnostic_stats.trim_start_frame + api_agnostic_stats.frame_count - 1);
            output_stream << "\tApplication frame range: " << frames_string << '\n';
        }
    }
}

nlohmann::json GetVulkanDeviceMemoryStatsJson(uint64_t        alloc_count,
                                      uint64_t        min_alloc,
                                      uint64_t        max_alloc,
                                      uint32_t        gfx_pipelines,
                                      uint32_t        comp_pipelines,
                                      uint32_t        rt_pipelines)
{
    return {
        {"memory-alloc", {
            {"count", alloc_count},
            {"min-size", min_alloc},
            {"max-size", max_alloc},
        }},
        {"pipeline-info", {
            {"graphics-count", gfx_pipelines},
            {"compute-count", comp_pipelines},
            {"raytracing-count", rt_pipelines},
        }},
    };
}

void PrintVulkanDeviceMemoryStatsText(std::ostream& output_stream,
                                      uint64_t alloc_count,
                                      uint64_t min_alloc,
                                      uint64_t max_alloc,
                                      uint32_t gfx_pipelines,
                                      uint32_t comp_pipelines,
                                      uint32_t rt_pipelines)
{
    output_stream << "\nVulkan device memory allocation info:\n";
    output_stream << "\tTotal allocations:   " << alloc_count << '\n';

    if (alloc_count > 0)
    {
        output_stream << "\tMin allocation size: " << min_alloc << '\n';
        output_stream << "\tMax allocation size: " << max_alloc << '\n';
    }

    output_stream << "\nVulkan pipeline info:\n";
    output_stream << "\tTotal graphics pipelines:   " << gfx_pipelines << '\n';
    output_stream << "\tTotal compute pipelines:    " << comp_pipelines << '\n';
    output_stream << "\tTotal raytracing pipelines: " << rt_pipelines << '\n';
}

std::string GetVulkanDeviceTypeString(VkPhysicalDeviceType device_type)
{
    switch (device_type)
    {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            return "VK_PHYSICAL_DEVICE_TYPE_OTHER";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "VK_PHYSICAL_DEVICE_TYPE_CPU";
            break;
        default:
            return std::format("Unknown ({})", static_cast<int>(device_type));
            break;
    }
}

nlohmann::json GetVulkanStatsJson(const gfxrecon::decode::FileProcessor&       file_processor,
                          const gfxrecon::decode::VulkanStatsConsumer& vulkan_stats_consumer)
{
    nlohmann::json vulkan_stats;

    uint32_t inst_count    = vulkan_stats_consumer.GetInstanceCount();
    auto     instance_info = vulkan_stats_consumer.GetInstanceInfo();
    auto     pd_info       = vulkan_stats_consumer.GetPhysicalDeviceInfo();
    auto     dev_info      = vulkan_stats_consumer.GetDeviceInfo();

    vulkan_stats["header-version"] = std::to_string(VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE)) + "." +
                                              std::to_string(VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE)) + "." +
                                              std::to_string(VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));
    auto& instances_json = vulkan_stats["instances"] = nlohmann::json::array();

    uint32_t       inst_index = 0;
    nlohmann::json instance_array;
    for (auto& it : instance_info)
    {
        nlohmann::json instance_json;

        auto& application_info = instance_json["application-info"];
        application_info["application"]["name"]    = it.second.app_info.app_name;
        application_info["application"]["version"] = it.second.app_info.app_version;
        application_info["engine"]["name"]         = it.second.app_info.engine_name;
        application_info["engine"]["version"]      = it.second.app_info.engine_version;
        application_info["api-version"] = GetVkVersionString(it.second.app_info.api_version);

        instance_json["extensions"] = it.second.enabled_extensions;

        auto& resolutions = application_info["resolutions"] = nlohmann::json::array();
        for (const auto& resolution : it.second.resolutions)
        {
            resolutions.push_back({{"width", resolution.width}, {"height", resolution.height}});
        }

        auto& physical_devices = instance_json["physical-devices"] = nlohmann::json::array();
        for (auto pd : it.second.used_physical_devices)
        {
            nlohmann::json pd_json;
            const auto&           properties =
                vulkan_stats_consumer.GetDeviceProperties(reinterpret_cast<gfxrecon::format::HandleId>(pd));
            if (properties != nullptr)
            {
                pd_json["name"] = properties->deviceName;
                pd_json["type"]        = GetVulkanDeviceTypeString(properties->deviceType);
                pd_json["api-version"] = GetVkVersionString(properties->apiVersion);
                pd_json["id"]             = properties->deviceID;
                pd_json["vendor"]         = properties->vendorID;
                pd_json["driver-version"] = properties->driverVersion;
                std::string uuid_string   = gfxrecon::util::uuid_to_string(VK_UUID_SIZE, properties->pipelineCacheUUID);
                pd_json["uuid"]           = uuid_string;

                auto& vulkan_devices = pd_json["vulkan-devices"] = nlohmann::json::array();

                for (auto dev : pd_info[pd].devices)
                {
                    nlohmann::json dev_json;

                    dev_json["extensions"] = dev_info[dev].enabled_extensions;

                    // For Verbose, we write out each devices alloc info.
                    auto memory_stats_json = GetVulkanDeviceMemoryStatsJson(dev_info[dev].allocation_count,
                                                     dev_info[dev].min_allocation_size,
                                                     dev_info[dev].max_allocation_size,
                                                     dev_info[dev].graphics_pipelines,
                                                     dev_info[dev].compute_pipelines,
                                                     dev_info[dev].raytracing_pipelines);
                    dev_json.update(memory_stats_json);

                    vulkan_devices.push_back(dev_json);
                }

                physical_devices.push_back(pd_json);
            }
        }

        instances_json.push_back(instance_json);
    }
    return vulkan_stats;
}

void PrintVulkanStatsText(std::ostream& output_stream,
                          const gfxrecon::decode::FileProcessor&       file_processor,
                          const gfxrecon::decode::VulkanStatsConsumer& vulkan_stats_consumer)
{

    uint32_t inst_count    = vulkan_stats_consumer.GetInstanceCount();
    auto     instance_info = vulkan_stats_consumer.GetInstanceInfo();
    auto     pd_info       = vulkan_stats_consumer.GetPhysicalDeviceInfo();
    auto     dev_info      = vulkan_stats_consumer.GetDeviceInfo();

    // Find the best instance (use the last one if nothing else looks good)
    VkInstance best_instance = vulkan_stats_consumer.GetLastCreatedInstance();
    uint32_t   max_allocs    = 0;
    uint32_t   max_pipelines = 0;
    for (auto& it : instance_info)
    {
        uint32_t used_allocs    = 0;
        uint32_t used_pipelines = 0;
        for (auto pd : it.second.used_physical_devices)
        {
            for (auto dev : pd_info[pd].devices)
            {
                used_allocs += dev_info[dev].allocation_count;
                used_pipelines += dev_info[dev].graphics_pipelines + dev_info[dev].compute_pipelines +
                                  dev_info[dev].raytracing_pipelines;
            }
        }
        if (used_allocs > max_allocs && used_pipelines > max_pipelines)
        {
            best_instance = it.second.instance_id;
            max_allocs    = used_allocs;
            max_pipelines = used_pipelines;
        }
    }
    auto& best_instance_info = instance_info[best_instance];

    output_stream << "\nVulkan application info:\n";
    output_stream << "\tApplication name:    " << best_instance_info.app_info.app_name << '\n';
    output_stream << "\tApplication version: " << best_instance_info.app_info.app_version << '\n';
    output_stream << "\tEngine name:         " << best_instance_info.app_info.engine_name << '\n';
    output_stream << "\tEngine version:      " << best_instance_info.app_info.engine_version << '\n';
    output_stream << "\tTarget API version:  " << std::format("{} ({})",
                best_instance_info.app_info.api_version,
                GetVkVersionString(best_instance_info.app_info.api_version)) << '\n';
    std::string resolutions = "\tUsed resolutions:    ";
    for (const auto& resolution : best_instance_info.resolutions)
    {
        resolutions += std::to_string(resolution.width) + "x" + std::to_string(resolution.height) + " ";
    }
    output_stream << resolutions << '\n';

    output_stream << "\nVulkan physical device info:\n";
    for (auto pd : best_instance_info.used_physical_devices)
    {
        auto properties = vulkan_stats_consumer.GetDeviceProperties(reinterpret_cast<gfxrecon::format::HandleId>(pd));
        if (properties != nullptr)
        {
            output_stream << "\tDevice name:         " << properties->deviceName << '\n';
            output_stream << "\tDevice ID:           " << std::format("0x{:x}", properties->deviceID) << '\n';
            output_stream << "\tVendor ID:           " << std::format("0x{:x}", properties->vendorID) << '\n';
            auto driver_version_string = std::format("{} (0x%{:x})", properties->driverVersion, properties->driverVersion); 
            output_stream << "\tDriver version:      " << driver_version_string << '\n';
            auto api_version_string = std::format("{} ({})", properties->apiVersion, GetVkVersionString(properties->apiVersion).c_str());
            output_stream << "\tAPI version:         " << api_version_string << '\n';
        }
    }

    // For Verbose, we right out each devices alloc info.
    PrintVulkanDeviceMemoryStatsText(output_stream,
                                     vulkan_stats_consumer.GetTotalAllocationCount(),
                                     vulkan_stats_consumer.GetTotalMinAllocationSize(),
                                     vulkan_stats_consumer.GetTotalMaxAllocationSize(),
                                     vulkan_stats_consumer.GetTotalGraphicsPipelineCount(),
                                     vulkan_stats_consumer.GetTotalComputePipelineCount(),
                                     vulkan_stats_consumer.GetTotalRayTracingPipelineCount());

    // TODO: This is the number of recorded draw calls, which will not reflect the number of draw calls
    // executed when recorded once to a command buffer that is submitted/replayed more than once.
    // output_stream << "\nDraw/dispatch call info:\n";
    // output_stream << "\tTotal draw calls: " << stats_consumer.GetTotalDrawCount() << '\n';
    // output_stream << "\tTotal dispatch calls: " << stats_consumer.GetTotalDispatchCount() << '\n';

    // Print Physical device info
    const gfxrecon::decode::VulkanStatsConsumer::PhysicalDeviceProperties& physical_device_properties =
        vulkan_stats_consumer.GetPhysicalDeviceProperties();

    output_stream << "\nPhysical device properties:\n";
    for (const auto& props : physical_device_properties)
    {
        output_stream << "  Device: " << props.first << '\n';
        auto api_version = std::format("0x%{:x} ({})", props.second.apiVersion, GetVkVersionString(props.second.apiVersion).c_str());
        output_stream << "\tAPI version:         " << api_version << '\n';
        output_stream << "\tDriver version:      " << std::format("0x{:x}", props.second.driverVersion) << '\n';
        output_stream << "\tVendor ID:           " << std::format("0x{:x}", props.second.vendorID) << '\n';
        output_stream << "\tDevice ID:           " << std::format("0x{:x}", props.second.deviceID) << '\n';
        output_stream << "\tDevice name:         " << props.second.deviceName << '\n';
    }

    if (file_processor.GetCurrentFrameNumber() == 0)
    {
        output_stream << "\nFile did not contain any frames\n";
    }
}

#if defined(D3D12_SUPPORT)
nlohmann::json GetDx12RuntimeInfoJson(gfxrecon::decode::Dx12StatsConsumer& dx12_consumer)
{
    gfxrecon::format::Dx12RuntimeInfo runtime_info = dx12_consumer.GetDx12RuntimeInfo();

    std::string runtime_src = "N/A";
    std::string runtime_ver = "N/A";

    if (runtime_src.empty() == false)
    {
        runtime_src = runtime_info.src;
        runtime_ver = std::to_string(runtime_info.version[0]) + "." + std::to_string(runtime_info.version[1]) + "." +
                      std::to_string(runtime_info.version[2]) + "." + std::to_string(runtime_info.version[3]);
    }

    return {
        {"version", runtime_ver},
        {"source", runtime_src},
    };
}

void PrintDx12RuntimeInfoText(std::ostream& output_stream,
                              gfxrecon::decode::Dx12StatsConsumer& dx12_consumer)
{
    gfxrecon::format::Dx12RuntimeInfo runtime_info = dx12_consumer.GetDx12RuntimeInfo();

    std::string runtime_src = "N/A";
    std::string runtime_ver = "N/A";

    if (runtime_src.empty() == false)
    {
        runtime_src = runtime_info.src;
        runtime_ver = std::to_string(runtime_info.version[0]) + "." + std::to_string(runtime_info.version[1]) + "." +
                      std::to_string(runtime_info.version[2]) + "." + std::to_string(runtime_info.version[3]);
    }

    output_stream << "D3D12 runtime info:\n";
    output_stream << "\tVersion: " << runtime_ver << '\n';
    output_stream << "\tSource: " << runtime_src << '\n';
    output_stream << "\n";
}

nlohmann::json GetDx12AdapterInfoJson(gfxrecon::decode::Dx12StatsConsumer& dx12_consumer)
{
    const auto & adapters = dx12_consumer.GetAdapters();
    nlohmann::json                                       adapters_json = nlohmann::json::array();

    if (!adapters.empty())
    {
        std::unordered_map<int64_t, std::string> adapter_workload;
        dx12_consumer.CalcAdapterWorkload(adapter_workload, adapters);

        for (const auto& adapter : adapters)
        {
            const int64_t luid = (adapter.LuidHighPart << 31) | adapter.LuidLowPart;

            std::string adapter_workload_pct = "";

            if (adapter_workload.count(luid) > 0)
            {
                if (adapter_workload[luid] != "")
                {
                    adapter_workload_pct = "(" + adapter_workload[luid] + "% of GPU submissions)";
                }
            }
            else if (adapter_workload.size() > 0)
            {
                adapter_workload_pct = "(0% of GPU submissions)";
            }

            std::string adapter_type =
                AdapterTypeToString(gfxrecon::graphics::dx12::ExtractAdapterType(adapter.extra_info));

            nlohmann::json json_adapter;
            json_adapter["description"]["details"]          = gfxrecon::util::WCharArrayToString(adapter.Description);
            json_adapter["description"]["workload-percent"] = adapter_workload_pct;
            json_adapter["vendor-id"]                       = adapter.VendorId;
            json_adapter["device-id"]                       = adapter.DeviceId;
            json_adapter["subsys-id"]                       = adapter.SubSysId;
            json_adapter["revision"]                        = adapter.Revision;
            auto& memory = json_adapter["memory"];
            memory["dedicated"]["video"]    = adapter.DedicatedVideoMemory;
            memory["dedicated"]["system"]   = adapter.DedicatedSystemMemory;
            memory["shared"]                = adapter.SharedSystemMemory;
            memory["luid"]["low"]           = adapter.LuidLowPart;
            memory["luid"]["high"]          = adapter.LuidHighPart;
            json_adapter["adapter-type"]                    = adapter_type;
            adapters_json.push_back(json_adapter);
        }
    }

    return adapters_json;
}

void PrintDx12AdapterInfoText(std::ostream& output_stream,
                              gfxrecon::decode::Dx12StatsConsumer& dx12_consumer)
{
    output_stream << "D3D12 adapter info:\n";

    const auto& adapters = dx12_consumer.GetAdapters();

    if (adapters.empty() == false)
    {
        std::unordered_map<int64_t, std::string> adapter_workload;
        dx12_consumer.CalcAdapterWorkload(adapter_workload, adapters);

        for (const auto& adapter : adapters)
        {
            const int64_t luid = (adapter.LuidHighPart << 31) | adapter.LuidLowPart;

            std::string adapter_workload_pct = "";

            if (adapter_workload.count(luid) > 0)
            {
                if (adapter_workload[luid] != "")
                {
                    adapter_workload_pct = "(" + adapter_workload[luid] + "% of GPU submissions)";
                }
            }
            else if (adapter_workload.size() > 0)
            {
                adapter_workload_pct = "(0% of GPU submissions)";
            }

            std::string adapter_type =
                AdapterTypeToString(gfxrecon::graphics::dx12::ExtractAdapterType(adapter.extra_info));

            auto description_string = std::format("{} {}", 
                        gfxrecon::util::WCharArrayToString(adapter.Description).c_str(),
                        adapter_workload_pct.c_str());
            output_stream << "\tDescription: " << description_string << '\n';
            output_stream << "\tVendor ID: " << std::format("0x{:x}", adapter.VendorId) << '\n';
            output_stream << "\tDevice ID: " << std::format("0x{:x}", adapter.DeviceId) << '\n';
            output_stream << "\tSubsys ID: " << std::format("0x{:x}", adapter.SubSysId) << '\n';
            output_stream << "\tRevision: " << adapter.Revision << '\n';
            output_stream << "\tDedicated Video Memory: " << adapter.DedicatedVideoMemory << '\n';
            output_stream << "\tDedicated System Memory: " << adapter.DedicatedSystemMemory << '\n';
            output_stream << "\tShared System Memory: " << adapter.SharedSystemMemory << '\n';
            output_stream << "\tLUID LowPart: " << std::format("0x{:x}", adapter.LuidLowPart) << '\n';
            output_stream << "\tLUID HighPart: " << std::format("0x{:x}", adapter.LuidHighPart) << '\n';
            output_stream << "\tAdapter type: " << adapter_type << '\n';
            output_stream << "\n";
        }
    }
    else
    {
        output_stream << "\tAdapter info not available.\n";
        output_stream << "\n";
    }
}

nlohmann::json GetDx12SwapchainInfoJson(gfxrecon::decode::Dx12StatsConsumer& dx12_consumer)
{
    auto [width, height] = dx12_consumer.GetSwapchainDimensions();
    return {"dimensions", {{"width", width}, {"height", height}}};
}

void PrintDx12SwapchainInfoText(std::ostream& output_stream, gfxrecon::decode::Dx12StatsConsumer& dx12_consumer)
{
    output_stream << "D3D12 swapchain info:\n";

    if (dx12_consumer.FoundSwapchainInfo())
    {
        output_stream << "\tDimensions: " << dx12_consumer.GetSwapchainDimensionsString() << '\n';
    }
    else
    {
        output_stream << "\tDimensions not available.\n";
    }

    output_stream << "\n";
}

nlohmann::json GetDxrEiInfoJson(gfxrecon::decode::Dx12StatsConsumer& dx12_consumer)
{
    nlohmann::json dxr_ei_info;
    dxr_ei_info["ei-workload"]  = dx12_consumer.ContainsEiWorkload() ? "yes" : "no";
    dxr_ei_info["dxr-workload"] = dx12_consumer.ContainsDxrWorkload() ? "yes" : "no";
    if (dx12_consumer.ContainsEiWorkload() || dx12_consumer.ContainsDxrWorkload())
    {
        dxr_ei_info["dxr/ei-optimized"] = dx12_consumer.ContainsOptFillMem() ? "yes" : "no";
    }
    return dxr_ei_info;
}

void PrintDxrEiInfoText(std::ostream& output_stream, gfxrecon::decode::Dx12StatsConsumer& dx12_consumer)
{
    if (dx12_consumer.ContainsEiWorkload())
    {
        output_stream << "D3D12 EI workload: yes\n";
    }
    else
    {
        output_stream << "D3D12 EI workload: no\n";
    }

    output_stream << "\n";

    if (dx12_consumer.ContainsDxrWorkload())
    {
        output_stream << "D3D12 DXR workload: yes\n";
    }
    else
    {
        output_stream << "D3D12 DXR workload: no\n";
    }

    if (dx12_consumer.ContainsEiWorkload() || dx12_consumer.ContainsDxrWorkload())
    {
        output_stream << "\n";

        if (dx12_consumer.ContainsOptFillMem())
        {
            output_stream << "D3D12 DXR/EI optimized: yes\n";
        }
        else
        {
            output_stream << "D3D12 DXR/EI optimized: no\n";
        }
    }
}

nlohmann::json GetD3D12StatsJson(gfxrecon::decode::FileProcessor&     file_processor,
                         gfxrecon::decode::Dx12StatsConsumer& dx12_consumer,
                         const ApiAgnosticStats&              api_agnostic_stats,
                         gfxrecon::decode::InfoConsumer&      info_consumer,
                         const AnnotationRecorder&            annotation_recorder)
{
    nlohmann::json d3d12_json;

    if (dx12_consumer.GetDXGITestPresentCount() > 0 && api_agnostic_stats.uses_frame_markers == false)
    {
        d3d12_json["total-present-count"] = dx12_consumer.GetDXGITestPresentCount();
    }

    d3d12_json["driver"] = GetDriverInfoString(info_consumer);
    d3d12_json["runtime"] = GetDx12RuntimeInfoJson(dx12_consumer);
    d3d12_json["adapters"] = GetDx12AdapterInfoJson(dx12_consumer);
    if (dx12_consumer.FoundSwapchainInfo()) {
        d3d12_json["swapchain"] = GetDx12SwapchainInfoJson(dx12_consumer);
    }
    auto dxr_ei_json = GetDxrEiInfoJson(dx12_consumer);
    d3d12_json.update(dxr_ei_json);

    return d3d12_json;
}

void PrintD3D12StatsText(std::ostream& output_stream,
                         gfxrecon::decode::FileProcessor&     file_processor,
                         gfxrecon::decode::Dx12StatsConsumer& dx12_consumer,
                         const ApiAgnosticStats&              api_agnostic_stats,
                         gfxrecon::decode::InfoConsumer&      info_consumer,
                         const AnnotationRecorder&            annotation_recorder)
{
    if (dx12_consumer.GetDXGITestPresentCount() > 0 && api_agnostic_stats.uses_frame_markers == false)
    {
        output_stream << "\tTest present count: " << dx12_consumer.GetDXGITestPresentCount() << '\n';
    }

    PrintDriverInfoText(output_stream, info_consumer);
    PrintDx12RuntimeInfoText(output_stream, dx12_consumer);
    PrintDx12AdapterInfoText(output_stream, dx12_consumer);
    PrintDx12SwapchainInfoText(output_stream, dx12_consumer);
    PrintDxrEiInfoText(output_stream, dx12_consumer);
}

static bool CheckOptionEnumGpuIndices(const char* exe_name, const gfxrecon::util::ArgumentParser& arg_parser)
{
    if (arg_parser.IsOptionSet(kEnumGpuIndices))
    {
        IDXGIFactory1* factory1 = nullptr;

        HRESULT result = CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void**>(&factory1));

        if (SUCCEEDED(result))
        {
            gfxrecon::graphics::dx12::ActiveAdapterMap adapters{};
            gfxrecon::graphics::dx12::TrackAdapters(result, reinterpret_cast<void**>(&factory1), adapters);

            GFXRECON_WRITE_CONSOLE("GPU index\tGPU name\tSubSys ID");
            for (size_t index = 0; index < adapters.size(); ++index)
            {
                for (auto adapter : adapters)
                {
                    if (index == adapter.second.adapter_idx)
                    {
                        std::string replay_adapter_str =
                            gfxrecon::util::WCharArrayToString(adapter.second.internal_desc.Description);

                        GFXRECON_WRITE_CONSOLE("%-9x\t%s\t%u",
                                    adapter.second.adapter_idx,
                                    replay_adapter_str.c_str(),
                                    adapter.second.internal_desc.SubSysId);
                        adapter.second.adapter->Release();
                        break;
                    }
                }
            }
            factory1->Release();
        }
        else
        {
            GFXRECON_LOG_ERROR("Failed to enumerate GPU indices");
        }

        return true;
    }

    return false;
}
#endif

#if ENABLE_OPENXR_SUPPORT
nlohmann::json GetOpenXrStatsJson(const gfxrecon::decode::FileProcessor&       file_processor,
                          const gfxrecon::decode::OpenXrStatsConsumer& openxr_stats_consumer)
{
    auto instance_info = openxr_stats_consumer.GetInstanceInfo();

    nlohmann::json openxr_stats;

    openxr_stats["header-version"] = std::to_string(XR_VERSION_MAJOR(XR_CURRENT_API_VERSION)) + "." +
                                              std::to_string(XR_VERSION_MINOR(XR_CURRENT_API_VERSION)) + "." +
                                              std::to_string(XR_VERSION_PATCH(XR_CURRENT_API_VERSION));
    auto& instances_json = openxr_stats["instances"] = nlohmann::json::array();

    uint32_t       inst_index = 0;
    nlohmann::json instance_array;
    for (auto& it : instance_info)
    {
        nlohmann::json instance_json;

        auto& application_info = instance_json["application-info"];
        application_info["application"]["name"]    = it.second.app_name;
        application_info["application"]["version"] = it.second.app_version;
        application_info["engine"]["name"]         = it.second.engine_name;
        application_info["engine"]["version"]      = it.second.engine_version;
        application_info["api-version"]            = GetXrVersionString(it.second.api_version);

        instance_json["extensions"] = it.second.enabled_extensions;

        instances_json.push_back(instance_json);
    }

    return openxr_stats;
}

void PrintOpenXrStatsText(std::ostream& output_stream,
                          const gfxrecon::decode::FileProcessor&       file_processor,
                          const gfxrecon::decode::OpenXrStatsConsumer& openxr_stats_consumer)
{
    auto instance_info = openxr_stats_consumer.GetInstanceInfo();

    output_stream << "\n";
    output_stream << "OpenXR info:\n";

    auto xr_version_string = std::format("{}.{}.{}", 
                XR_VERSION_MAJOR(XR_CURRENT_API_VERSION),
                XR_VERSION_MINOR(XR_CURRENT_API_VERSION),
                XR_VERSION_PATCH(XR_CURRENT_API_VERSION));
    output_stream << "\tHeader Version:             " << xr_version_string << '\n';

    output_stream << "\tNumber of OpenXR Instances: " << instance_info.size() << '\n';

    // For non-verbose standard output, just print first application/instance info
    output_stream << "\nOpenXR application info:\n";
    output_stream << "\tApplication name:    " << instance_info[0].app_name << '\n';
    output_stream << "\tApplication version: " << instance_info[0].app_version << '\n';
    output_stream << "\tEngine name:         " << instance_info[0].engine_name << '\n';
    output_stream << "\tEngine version:      " << instance_info[0].engine_version << '\n';
    output_stream << std::format("\tTarget API version:  {} ({})",
                instance_info[0].api_version,
                GetXrVersionString(instance_info[0].api_version).c_str()) << '\n';
}
#endif // ENABLE_OPENXR_SUPPORT

// A short pass to get exe info. Only processes the first blocks of a capture file.
void GatherAndPrintExeInfo(std::ostream& output_stream, const std::string& input_filename)
{
    gfxrecon::decode::InfoConsumer  info_consumer(true);
    gfxrecon::decode::FileProcessor file_processor;
    if (file_processor.Initialize(input_filename))
    {
        gfxrecon::decode::InfoDecoder info_decoder;
        info_decoder.AddConsumer(&info_consumer);
        file_processor.AddDecoder(&info_decoder);
        file_processor.ProcessAllFrames();

        PrintExeInfoText(output_stream, info_consumer);
    }
}

// A short pass to get file format info. Only processes the first two frames of a capture file.
void GatherAndPrintFileFormatInfo(std::ostream& output_stream, const std::string& input_filename)
{
    const gfxrecon::decode::InfoConsumer::NoMaxBlockTag no_max_tag;
    gfxrecon::decode::InfoConsumer                      info_consumer(no_max_tag);
    gfxrecon::decode::FileProcessor                     file_processor;
    if (file_processor.Initialize(input_filename))
    {
        gfxrecon::decode::InfoDecoder info_decoder;
        info_decoder.AddConsumer(&info_consumer);
        file_processor.AddDecoder(&info_decoder);
        bool success = file_processor.ProcessNextFrame();
        if (success && !file_processor.UsesFrameMarkers())
        {
            file_processor.ProcessNextFrame();
        }
        output_stream << "File format info:\n";
        PrintFileFormatInfoText(output_stream, file_processor);
    }
}

void GatherAndPrintEnvVars(std::ostream& output_stream, const std::string& input_filename)
{
    gfxrecon::decode::FileProcessor file_processor;
    if (file_processor.Initialize(input_filename))
    {
        gfxrecon::decode::InfoConsumer info_consumer;
        gfxrecon::decode::InfoDecoder  info_decoder;
        info_decoder.AddConsumer(&info_consumer);
        file_processor.AddDecoder(&info_decoder);
        file_processor.ProcessAllFrames();
        if (file_processor.GetErrorState() == gfxrecon::decode::BlockIOError::kErrorNone)
        {
            PrintEnvironmentVariableInfoText(output_stream, info_consumer);
        }
        else
        {
            GFXRECON_LOG_ERROR("Encountered error while reading capture. Unable to report environment variables.");
        }
    }
}

void GatherAndPrintAllInfo(std::ostream& output_stream, const std::string& input_filename, bool output_json)
{
    gfxrecon::decode::FileProcessor file_processor;
    if (file_processor.Initialize(input_filename))
    {
        gfxrecon::decode::StatDecoderBase stat_decoder;
        gfxrecon::decode::StatConsumer    stat_consumer;
        stat_decoder.AddConsumer(&stat_consumer);
        file_processor.AddDecoder(&stat_decoder);

        gfxrecon::decode::InfoConsumer info_consumer;
        gfxrecon::decode::InfoDecoder  info_decoder;
        info_decoder.AddConsumer(&info_consumer);
        file_processor.AddDecoder(&info_decoder);

        AnnotationRecorder annotation_recorder;
        file_processor.SetAnnotationProcessor(&annotation_recorder);

        gfxrecon::decode::VulkanDetectionConsumer vulkan_detection_consumer(
            gfxrecon::decode::VulkanDetectionConsumer::kNoBlockLimit);
        gfxrecon::decode::VulkanStatsConsumer vulkan_stats_consumer;
        gfxrecon::decode::VulkanDecoder       vulkan_decoder;
        vulkan_decoder.AddConsumer(&vulkan_detection_consumer);
        vulkan_decoder.AddConsumer(&vulkan_stats_consumer);
        file_processor.AddDecoder(&vulkan_decoder);

#if defined(D3D12_SUPPORT)
        gfxrecon::decode::Dx12DetectionConsumer dx12_detection_consumer(
            gfxrecon::decode::Dx12DetectionConsumer::kNoBlockLimit);
        gfxrecon::decode::Dx12StatsConsumer dx12_consumer;
        gfxrecon::decode::Dx12Decoder       dx12_decoder;
        dx12_decoder.AddConsumer(&dx12_detection_consumer);
        dx12_decoder.AddConsumer(&dx12_consumer);
        file_processor.AddDecoder(&dx12_decoder);
#endif

#if ENABLE_OPENXR_SUPPORT
        gfxrecon::decode::OpenXrDetectionConsumer openxr_detection_consumer;
        gfxrecon::decode::OpenXrStatsConsumer     openxr_stats_consumer;
        gfxrecon::decode::OpenXrDecoder           openxr_decoder;

        openxr_decoder.AddConsumer(&openxr_detection_consumer);
        openxr_decoder.AddConsumer(&openxr_stats_consumer);
        file_processor.AddDecoder(&openxr_decoder);
#endif

        file_processor.ProcessAllFrames();
        if (file_processor.GetErrorState() == gfxrecon::decode::BlockIOError::kErrorNone)
        {
            uint32_t blank_frame_count = 0;
            bool     vulkan_present    = vulkan_detection_consumer.WasVulkanAPIDetected();
            bool     dx12_present      = false;
            bool     openxr_present    = false;
#if defined(D3D12_SUPPORT)
            blank_frame_count = dx12_consumer.GetDummyFrameCount();
            dx12_present      = dx12_detection_consumer.WasD3D12APIDetected();
#endif
#if ENABLE_OPENXR_SUPPORT
            openxr_present = openxr_detection_consumer.WasOpenXrAPIDetected();
#endif

            ApiAgnosticStats api_agnostic_stats = {};
            GatherApiAgnosticStats(api_agnostic_stats, file_processor, stat_consumer, blank_frame_count);
            if (api_agnostic_stats.trim_start_frame < vulkan_stats_consumer.GetTrimmedStartFrame())
            {
                api_agnostic_stats.trim_start_frame = vulkan_stats_consumer.GetTrimmedStartFrame();
            }

            // If no APIs were detected, print stats for all APIs.
            bool missing_api_info = !vulkan_present;
#if defined(D3D12_SUPPORT)
            missing_api_info = missing_api_info && !dx12_present;
#endif
#if ENABLE_OPENXR_SUPPORT
            missing_api_info = missing_api_info && !openxr_present;
#endif
            // If we're missing API info, force printing everything
            if (missing_api_info)
            {
                vulkan_present = true;
                dx12_present   = true;
                openxr_present = true;
            }

            if (output_json)
            {
                nlohmann::json json_content;

                json_content["exe"] = GetExeInfoJson(info_consumer);
                const auto& environment_variables = info_consumer.GetEnvironmentVariables();
                if(!environment_variables.empty()) {
                    json_content["environment"] = GetEnvironmentVariableInfoJson(environment_variables);
                }
                json_content["capture-file"] = GetFileFormatInfoJson(file_processor);
                if (api_agnostic_stats.error_state == gfxrecon::decode::BlockIOError::kErrorNone)
                {
                    auto api_agnostic = GetApiAgnosticStatsJson(api_agnostic_stats, vulkan_present, dx12_present);
                    json_content["capture-file"].update(api_agnostic);

                    if (vulkan_present)
                    {
                        json_content["vulkan"] = GetVulkanStatsJson(file_processor, vulkan_stats_consumer);
                    }

#if defined(D3D12_SUPPORT)
                    if (dx12_present)
                    {
                        json_content["d3d12"] = GetD3D12StatsJson(file_processor,
                                            dx12_consumer,
                                            api_agnostic_stats,
                                            info_consumer,
                                            annotation_recorder);
                    }
#endif
#if ENABLE_OPENXR_SUPPORT
                    if (openxr_present)
                    {
                        json_content["openxr"] = GetOpenXrStatsJson(file_processor, openxr_stats_consumer);
                    }
#endif
                    if (!annotation_recorder.operation_annotations_.empty()) {
                        json_content["operations"] = GetOperationsJson(annotation_recorder);
                    }
                }
                if (file_processor.GetCurrentFrameNumber() == 0)
                {
                    json_content["capture-file"]["frames"]["total-count"] = 0;
                }

                output_stream << json_content.dump(4, ' ', true);
            }
            else
            {
                PrintDetectedApiInfoText(output_stream, vulkan_present, dx12_present, openxr_present);
                PrintExeInfoText(output_stream, info_consumer);
                if (api_agnostic_stats.error_state == gfxrecon::decode::BlockIOError::kErrorNone)
                {
                    PrintApiAgnosticStatsText(output_stream, api_agnostic_stats, vulkan_present, dx12_present);

                    if (vulkan_present)
                    {
                        PrintVulkanStatsText(output_stream, file_processor, vulkan_stats_consumer);
                    }

#if defined(D3D12_SUPPORT)
                    if (dx12_present)
                    {
                        PrintD3D12StatsText(
                            output_stream, file_processor, dx12_consumer, api_agnostic_stats, info_consumer, annotation_recorder);
                    }
#endif
#if ENABLE_OPENXR_SUPPORT
                    if (openxr_present)
                    {
                        PrintOpenXrStatsText(output_stream, file_processor, openxr_stats_consumer);
                    }
#endif

                    PrintAnnotationsText(output_stream, annotation_recorder);
                }
                else if (api_agnostic_stats.error_state != gfxrecon::decode::BlockIOError::kErrorNone)
                {
                    GFXRECON_LOG_ERROR("A failure has occurred during file processing");
                    gfxrecon::util::Log::Release();
                    exit(-1);
                }
                else
                {
                    output_stream << "File did not contain any frames\n";
                }
            }
        }
        else
        {
            output_stream << "Encountered error while reading capture. Stats unavailable.\n";
        }
    }
}

int main(int argc, const char** argv)
{
    gfxrecon::util::Log::Init();

    gfxrecon::util::ArgumentParser arg_parser(argc, argv, kOptions, kArguments);

    gfxrecon::util::Log::Settings log_settings;
    GetLogSettings(arg_parser, log_settings);
    gfxrecon::util::Log::Release();
    gfxrecon::util::Log::Init(log_settings);

    if (CheckOptionPrintUsage(argv[0], arg_parser) || CheckOptionPrintVersion(argv[0], arg_parser))
    {
        gfxrecon::util::Log::Release();
        exit(0);
    }
#if defined(D3D12_SUPPORT)
    else if (CheckOptionEnumGpuIndices(argv[0], arg_parser))
    {
        gfxrecon::util::Log::Release();
        exit(0);
    }
#endif
    else if (arg_parser.IsInvalid() || (arg_parser.GetPositionalArgumentsCount() != 1))
    {
        PrintUsage(argv[0]);
        gfxrecon::util::Log::Release();
        exit(-1);
    }
    else
    {
#if defined(WIN32) && defined(_DEBUG)
        if (arg_parser.IsOptionSet(kNoDebugPopup))
        {
            _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        }
#endif
    }

    const std::vector<std::string>& positional_arguments = arg_parser.GetPositionalArguments();
    std::string                     input_filename       = positional_arguments[0];

    std::ofstream output_file;
    bool          use_file = false;
    if (arg_parser.IsArgumentSet(kOutputFileArgument))
    {
        printf("thinks output is enabled\n");
        std::string output_filename = arg_parser.GetArgumentValue(kOutputFileArgument);
        printf("thinks output is %s\n", output_filename.c_str());
        output_file.open(output_filename);
        use_file      = true;
    }

    std::ostream& output_stream = use_file ? output_file : std::cout;

    if (arg_parser.IsOptionSet(kExeInfoOnlyOption))
    {
        GatherAndPrintExeInfo(output_stream, input_filename);
    }
    else if (arg_parser.IsOptionSet(kEnvVarsOnlyOption))
    {
        GatherAndPrintEnvVars(output_stream, input_filename);
    }
    else if (arg_parser.IsOptionSet(kFileFormatOnlyOption))
    {
        GatherAndPrintFileFormatInfo(output_stream, input_filename);
    }
    else
    {
        GatherAndPrintAllInfo(output_stream, input_filename, arg_parser.IsOptionSet(kVerboseOption));
    }

    gfxrecon::util::Log::Release();
    return 0;
}
