# GFXReconstruct Coding Guide

How GFXReconstruct (GFXR) code is written, reviewed and tested in practice, for AI contributors and new engineers. Every rule cites the merged pull requests it came from; snippets are copied from those PRs. As of `origin/dev` commit `280a0ac62` (2026-10-14), covering PRs #1167 through #3343 merged between 2024-10-21 and 2026-10-14.

## 0. How to use this guide

CONTRIBUTING.md is the mechanics, `docs/GFXReconstruct Architectural Principles.md` is the why, AGENTS.md is build, regeneration, tests and the written coding rules. This guide is the delta: what engineers do beyond or against those documents, with the PRs that show it.

**Pre-submit checklist.** Each line is backed by a section below.

- The change is in the right layer: capture hook or override, consumer override, feature module, `framework/graphics` or `framework/util` helper; never a generator special case or a hand-edited generated file (§1, #1922, #3063).
- Every alias of the command (core, KHR, EXT, vendor, "2" variant) reaches the same handler, and every `switch` on the type you touched has the new case (§5, #2042, #3217, #3203).
- `GFXRECON_ASSERT` only for conditions GFXR's own code controls; application input, capture-file content and user input get a runtime branch that logs (§3, #2135, #3138).
- Replay-injected Vulkan calls run inside an injected-commands scope, check their `VkResult`, pick a queue family the replay device created, and re-query device properties instead of reusing capture values (§5, #3186, #2936, #2923).
- New output, files, log lines or GPU work are behind a default-off option; existing option syntax and exit codes are unchanged (§3, #2134, #3247, #3103).
- A new capture-file block is a new `MetaDataType` with the old one renamed `_deprecated`; a decode of a pre-change capture still works (§2.4, #2934, #2605).
- A new option is wired through every file in its set, including both USAGE docs and `android/scripts/gfxrecon.py` (§2.1, #2238, #3181).
- Generated output is regenerated for every API the generator serves and committed in the same PR (§2.9, #2628, #2160).
- The test is named: known-good capture, call-count test, Catch2 unit test, external suite case plus `test_suite.ref` bump, or a written procedure (§6, #3299, #2861, #3259).
- No `std::format`; fixed-width integers print with `PRIu64`; OS checks are `#if defined(_WIN32)`; API code is gated on its build macro, not the OS; new source files are in both the desktop and the Android CMakeLists (§3, #2723, #3084, #3028, #2173, #2586).
- The description states the problem before the change, the title matches the final diff, no fix-up or formatting commits remain, and dead code the change created is deleted (§7, #3213, #3155, #1844, #3151).

## 1. Where a change belongs

**Capture (`framework/encode/`, `layer/`).** Per-handle state lives on the wrapper struct in `framework/encode/vulkan_handle_wrappers.h`, is filled by a `PostProcess_vkX` on `VulkanCaptureManager` gated on `result == VK_SUCCESS` and `IsCaptureModeTrack()`, and is forwarded to `VulkanStateTracker::TrackX` (#2524, #2725, #2896, #2903). Hooks are `CustomEncoderPreCall`/`CustomEncoderPostCall<ApiCall_vkX>` specializations in `framework/encode/custom_vulkan_encoder_commands.h`; the generated encoders already call them, so no regeneration is needed (#2231, #2366, #2896). A call whose arguments or encoded value must change gets an `Override*` registered in `capture_overrides.json` (#1922, #3267). D3D12 mirrors this with `CustomWrapperPreCall/PostCall` in `framework/encode/custom_dx12_wrapper_commands.h`, `Dx12StateTracker::Track*` and `Dx12StateWriter` (#1826, #2026, #2293). API-agnostic settings and file writing are in `CommonCaptureManager`, with one-line forwarders on `ApiCaptureManager` (#3086, #2011, #2599). Rejected: a generator special case for one command (#1922); a new base class for `CommonCaptureManager` ("have" one, not "be" one, #1913); a second settings struct beside `OptionsMap` (#2281); a per-call check that belongs in `Initialize` (#2206).

**Decode and format (`framework/decode/`, `framework/format/`).** Block reading and error classification go in `BlockParser`; frame and state bookkeeping in `BlockProcessor` or the `FileProcessor` visitors (#2514, #2547, #3138, #3257). Subclass behavior is a narrow virtual hook on `FileProcessor`, not a copy of the loop (#2415). New call IDs append before `ApiCall_VulkanLast` in `framework/format/api_call_id.h`; new metadata IDs append to `MetaDataType` in `framework/format/format.h` (#2168, #2540, #2143). Decoders return captured bits verbatim; replay policy belongs to the consumer (#2199, #3257).

**Consumers.** A replay behavior change is an `Override*` registered in `replay_overrides.json` (#2302, #2785, #3188). Per-object replay state is a field on the `Vulkan*Info` struct in `framework/decode/vulkan_object_info.h`, filled in the create override and cleared on destroy or in `ClearCommandBufferInfo` (#1795, #2188, #3217). Behavior that depends on a replay mode is a virtual on `VulkanSwapchain` (#2623, #2786) or on `VulkanResourceAllocator` with a pass-through in the default allocator (#1632, #2078), not a branch in the consumer. Feature-specific replay (frame looping, dump resources) is a subclass or a delegate class with its own files (#2698, #2935, #1941). Rejected: a device member plumbed through every swapchain subclass instead of `QueueInfo::parent` (#1795); a function-local `static` for pNext storage (#2032); a singleton address tracker (#2398); centralized teardown that destroyed in the wrong order (#3299); dump-only work outside `Dx12DumpResources` and its option gate (#1862).

**Tools (`tools/`).** `main.cpp` is API-agnostic. Per-API code lives in `tools/<tool>/<tool>_<api>_feature.cpp`, ends with `GFXR_UTIL_REGISTER_FEATURE_CREATOR`, and is wrapped in the API's build macro (#2751, #2858, #2997, #3054). Nothing API-specific goes on the shared feature interface; a D3D12-only option stays a one-off `#if defined(D3D12_SUPPORT)` in `main.cpp` (#2751), and the Android app handle lives in `application::Application` (#2927).

**Helpers.** Put a helper in the lowest directory whose types it uses: `framework/util` when API-agnostic (#1955, #2494, #2389), `framework/graphics` when Vulkan- or D3D12-typed and needed by both encode and decode (#2129, #3036, #3044), `framework/decode` when it holds decode object-info types (#3066, #1739, #3038). A reviewer moved a decode-only utility out of util to keep "statics and the `extern "C"` symbol out of other libraries" (#1739). Do a file move in its own no-behavior-change commit: #1902 bundled a move with a fix, merged stale, and was reverted by #2318 within the hour.

**Generated versus hand-written.** Three mechanisms, in this order:

1. *Template change* when the fix applies to every command or struct of a kind: pNext handle mapping (#1994, #2737), registry `len` handling (#3265), union decoders (#2794). Shared behavior goes in `khronos_generators/khronos_*`; per-API facts in `ApiData` or the API base generator (#1898, #2102, #3033). DX12 generators are standalone and must not import the Khronos ones (#1827, single PR).
2. *Override JSON entry* when one command needs non-default behavior: `capture_overrides.json`, `replay_overrides.json`, `dump_resources_overrides.json`, `replay_frame_loop_overrides.json`, `replay_async_overrides.json`, and the DX12 equivalents under `framework/generated/dx12_generators/` (#1922, #2187, #2785, #3117); the most common move in the corpus (about 40 PRs). Growing a named list in the JSON beats a generator branch (#2967, #3304, #2561).
3. *`custom_*` files* when vk.xml cannot describe a struct (unions, pointers with no `len`, discriminated payloads): add it to `blacklists.json` and hand-write every piece the generator would have produced (#2103, #2581, #2794). Partial coverage breaks convert or tocpp.

Patterned hand-written code is expected to be generated eventually, or to carry a TODO with a filed issue (#1928, #2075); a reviewer accepted hand-written code when "the generator would be longer than the list" (#3108).

## 2. Recipes

### 2.1 Add a replay option

Files, in order (#2774, #2785, #3016, #3247; layout from #3216):

1. `framework/decode/vulkan_replay_options.h`: a field with an in-class default, `bool x{ false }` (a reviewer asked for `false`, not `0`, #2774). API-neutral options go in `framework/decode/replay_options.h`; a base-struct field must be parsed in `GetReplayOptions`, not an API parser (#1882, #3096).
2. Vulkan-only parsing and the `k...` constant go in `tools/replay/replay_vulkan_feature.cpp` (`GetVulkanReplayOptions`), plus a `util::FeatureOptionDesc` in `ReplayVulkanFeature::GetOptionDescs()`; `tools/tool_feature_options.h` merges those into the parser and usage text, so no `kOptions`/`kArguments`/`PrintUsage` edit is needed (#3216; D3D12 in `tools/replay/replay_d3d12_feature.cpp`, #3227). Options shared by all APIs still go in `tools/tool_settings.h` and `tools/replay/replay_settings.h`, as every pre-#3216 PR did (#2642, #2760).
3. Closed value sets go in `accepted_values`, matched case-insensitively so wrapper scripts keep working (#3216). Numeric values parse inside try/catch, warn with the invalid value and keep the default (#2642, #2713).
4. `USAGE_desktop_Vulkan.md` and `USAGE_android.md`, synopsis and option list, same words as the help text (#2785, #3172; #2128 and #2642 skipped this).
5. `android/scripts/gfxrecon.py`: an argparse entry ending "(forwarded to replay tool)" plus forwarding in `MakeExtrasString` (#2759, #3058).
6. Consume the option at one override with one guard, from both `OverrideQueueSubmit` and `OverrideQueueSubmit2` when it affects submits (#2642, #2774, #2827). One author names the helper `Maybe*()` and returns early when the option is off (#2785, #2760; single-author convention).
7. Test with the call-count helpers in `test/verify-gfxr.cpp` (§6; #3299, #3315).

The generators do nothing here. Trap: #2238 spelled the constant `...bindigs` while usage said `...bindings`, so the documented flag never parsed.

### 2.2 Add a capture setting

Files, in order (#2294, #3106, #2206, #2011):

1. `framework/encode/capture_settings.h`: `X_LOWER`/`X_UPPER` macros and the `TraceSettings` field with its default as initializer.
2. `framework/encode/capture_settings.cpp`: `kXEnvVar[] = GFXRECON_OPTION_STR(X)` (one macro yields the desktop and Android names since #2063), a `kOptionX` key, a `LoadSingleOptionEnvVar` line, and `field = ParseXString(FindOption(options, kOptionX), field)` in `ProcessOptions`. Pass the field's own value as the default; #1904 copied another field's. Enum strings use `StringCompareNoCase(...) == 0` and warn on unknown values with a `Settings Loader:` prefix; resolve conflicts with existing options here and name the winner (#1980, #3106).
3. Copy into a `CommonCaptureManager` member in `Initialize`; add a getter and an `ApiCaptureManager` forwarder if encoders need it (#2011, #2206).
4. `layer/json/VkLayer_gfxreconstruct.json.in` entry with env, type (`LIST`, `ENUM`), platforms and default (#2294, #3106); `layer/vk_layer_settings.txt` (#3106; most settings skip it, Appendix B).
5. Capture tables in `USAGE_desktop_Vulkan.md`, `USAGE_desktop_D3D12.md` and `USAGE_android.md` (#2294, #3106).
6. Test: a test app that sets the env var in `configure_instance_builder`, a case and a known-good `.gfxr` (#2294; most PRs skipped this).

### 2.3 Support a new Vulkan extension or struct

Header bump (#2168, #2284, #2410, #2885, #3240): update `external/Vulkan-Headers`, regenerate, commit header, output and hand fixes separately (#2540, #2624). For an extension GFXR cannot handle yet, add it to `_remove_extensions` in `framework/generated/khronos_generators/vulkan_generators/vulkan_base_generator.py` and its `*_EXTENSION_NAME` to `kVulkanUnsupportedDeviceExtensions` in `framework/encode/vulkan_entry_base.cpp`; skipping codegen alone still advertises the extension to applications (#2168, #2284, #3115). Append each new `ApiCall_*` before `ApiCall_VulkanLast`, never renumbering, continuing after any `ApiCall_Reserved_0x*` slot, with `ApiFamily_Vulkan` (#2517, #2540). New two-call queries get a `*ArrayIndices` entry in `framework/decode/vulkan_object_info.h`; commands naming a new handle type get a `CommandHandleType` and a `CheckCommandHandle` case (#2885, #2553, #2168). Fix hand-written switches that gain enum values (#2284, #2624, #2410). Refresh `test/known_good/set-environment.gfxr`, which embeds the header version (#2433, #2540); run extended CI and list the hand edits in the description (#2780, #3240).

A struct the registry cannot size goes in `blacklists.json` with hand-written `EncodeStruct` (`framework/encode/custom_vulkan_struct_encoders.cpp`), `Decoded_*` and `DecodeStruct` (`framework/decode/custom_vulkan_struct_decoders.h`), `FieldToJson` (`framework/decode/custom_vulkan_struct_to_json.cpp`) and `framework/decode/vulkan_cpp_structs.cpp`, built on the existing typed array encoders ("use the existing `EncodeStringArray`", #2581). Plan for captures written with the old encoding: #2581 did not, and #2605 had to detect the old form after old captures crashed convert and replay.

Tracking a new command for trimming takes one `CustomEncoderPostCall` per alias ID, all forwarding to one `PostProcess_` named after the core command (#2725, #2896, #2903). `framework/encode/custom_vulkan_encoder_commands.h` (#2896):
```cpp
template <>
struct CustomEncoderPostCall<format::ApiCallId::ApiCall_vkTransitionImageLayoutEXT>
{
    template <typename... Args>
    static void Dispatch(VulkanCaptureManager* manager, VkResult result, Args... args)
    {
        manager->PostProcess_vkTransitionImageLayout(result, args...);
    }
};
```
Check which `#if` block the specialization lands in: the `vkBeginCommandBuffer` hooks from #2231 and #2366 sit inside `#if ENABLE_OPENXR_SUPPORT` and compile out when OpenXR is off (#2524 placed its own outside the guard).

### 2.4 Add or change a metacommand

AGENTS.md gives the five steps. As practiced (#2934, #2042, #2572, #2143, #3139):

1. Reserve the ID first in its own small PR, `kReservedNN` in `framework/format/format.h` or `ApiCall_Reserved_0x...`, with a comment saying what it is for (#3139; #2143 reserved slot 36 for pending #2142). Rebase before merge: #2509's first ID collided with a header update. Out-of-tree experiments use `kBeginExperimentalReservedRange = 1U << 15U` (#2348).
2. Append the new `MetaDataType` and header struct. When replacing a block, rename the old enumerator and struct `_deprecated` and give the new one a `2` suffix, both reviewer requests in #2934 (`kInitDx12AccelerationStructureCommand_deprecated = 19`, `...Command2 = 38`; #2042 used `_deprecated2`).
3. Copy an existing writer: value-initialize the struct, size it with `GetMetaDataBlockBaseSize(cmd) + payload`, `MakeMetaDataId(ApiFamily_X, kY)`, one combined write (#2572 mirrors `WriteSetOpaqueAddressCommand`; #2142 in `dx12_state_writer.cpp`).
4. Decode through a defaulted no-op virtual on `ApiDecoder` (`DispatchExtendedMetaDataBlock` in `framework/decode/api_decoder.h`) and `MetadataConsumerBase`, so existing decoders compile unchanged (#1815, #3139, #3255; #2572 added a pure virtual and forced empty overrides into every decoder base).
5. Keep a parser branch for the old block that warns once and recommends recapture. `framework/decode/block_parser.cpp` (#2934):
```cpp
else if (meta_data_type == format::MetaDataType::kInitDx12AccelerationStructureCommand_deprecated)
{
    GFXRECON_LOG_WARNING_ONCE(
        "This capture contains a deprecated metacommand to create a Dx12AccelerationStructureCommand.  While still "
        "supported, this "
        "metacommand may not include new structure of the captured Dx12AccelerationStructureCommand.");
```
6. Decode a capture made before your change. If the recorded call stream changed (#2740 added recorded `AddRef`/`Release` calls), update the external references in the same PR (#2775).

A `format.h` edit that leaves on-disk bytes identical, such as `wchar_t` to `char16_t` with a `static_assert` on size (#3236) or a field rename (#2491), skips this recipe.

### 2.5 Add a Consumer override

Vulkan replay (#2302, #2785, #3188, #3217):

1. Add `"vkCmdX": "OverrideCmdX"` to `framework/generated/khronos_generators/vulkan_generators/replay_overrides.json`, mapping every KHR/EXT alias to the same core name ("Map `vkCmdWaitEvents2KHR` directly to `OverrideCmdWaitEvents2`", #3217; #2785, #2628).
2. Regenerate and commit the generated consumer in the same PR ("They shouldn't be committed separately or our internal build systems will have issues", #2628).
3. Implement `OverrideCmdX` in `framework/decode/vulkan_replay_consumer_base.h`/`.cpp`: assert the decoder and info pointers; read captured values through `GetPointer()`/`GetMetaStructPointer()`, write driver output to `GetOutputPointer()` (#2628, #2977, #2862); call the passed `func` (#1739, #2078).
4. Call the driver unconditionally and fill object info only on `VK_SUCCESS` (or `VK_INCOMPLETE` for enumerations); gating the call on `original_result` "behaves differently from capture and from other OverrideCreate calls" (#2260, #2649).
5. If capture and replay results legitimately differ, add the pair to `kResultValuesAllowedByReplayEnvironment` instead of warning on every call (#3291, #3235).

Since #3007 the generated signature is `Process_vkX(const ApiCallInfo&, args::X&)`; a feature consumer subclass overrides `Process_vkX`, checks its mode and always forwards to the generated base, which owns the bookkeeping (#2698, #3052, #3171). A hand-written override that replaces generated code must reproduce everything the generated body did, including array unwrapping with the count, fan-out to every clone command buffer, and the JSON registration: #3267's `OverrideQueueBindSparse` was unreachable and dropped every `VkBindSparseInfo` after the first; #2945 lost the fan-out.

OpenXR has no JSON mapping in use. Add the command to `extra_manual_commands` in `framework/generated/khronos_generators/openxr_generators/gencode.py` and to the matching list in `openxr_replay_consumer_body_generator.py`, write `OpenXrReplayConsumerBase::Process_xrX`, regenerate (#2147, #2204).

### 2.6 Add dump-resources support for a command

Vulkan dump resources (VDR) (#2138, #2216, #2255, #2945, #3038, #3289):

1. Add the command and all aliases to `framework/generated/khronos_generators/vulkan_generators/dump_resources_overrides.json`, mapped to one `Override*` (#3289, #3038). When the KHR PFN is a typedef of the core PFN one mapping is enough (#2255); otherwise one-line `Override*KHR` forwarders call a shared `Handle*` typed on the core PFN (#2138, #2216).
2. Implement `VulkanReplayDumpResourcesBase::OverrideCmdX` in `framework/decode/vulkan_replay_dump_resources.cpp`, guarded by `IsRecording()`, forwarding `func` through the `ForEach*CommandBuffer` helpers (#2945). Since #3289 the generator classifies state commands (fan out to all clones through `ForEachStateCommandBuffer`) versus work commands (`ForEachWorkCommandBuffer`, `GetWorkCommandBuffer()` only).
3. Context state goes in `vulkan_replay_dump_resources_draw_calls.*` or `_compute_ray_tracing.*`; update `DrawCallTypeToStr` for a new enum value (#2216).
4. Replay-side object info the dumper needs is a `Vulkan*Info` field filled by a `VulkanReplayConsumerBase::Override*`, only when dumping is enabled, including the async pipeline path (#2083, #2227).
5. Regenerate, then bump `test_suite.ref` (#2766, #3038, #3289). VDR has no in-repo test.
6. New dump output is opt-in or self-describing, because users otherwise "suddenly get more data than they expected" (#2398); options go in the input JSON (`tools/replay/parse_dump_resources_cli.cpp`, `kVDROption*` keys in `framework/decode/vulkan_replay_dump_resources_options.h`), not new CLI flags (#2463), documented with type and default in `vulkan_dump_resources.md` (#2315).

Pick core or KHR entry points the dumper calls itself by comparing the device-table entry against its `graphics::noop::vkX` stub, core first; a `nullptr` test always passes because the tables hold stubs (#2904, #3030, #3188).

### 2.7 Add a tool feature module

AGENTS.md names the pattern; the series that built it is #2751, #2858, #2939, #2997, #3054, #2927, #3103, #3216, almost all by one author. It holds because reviewers enforce it and AGENTS.md states it.

1. `tools/<tool>/<tool>_feature.h`: a pure interface deriving from `util::FeatureBase` (`framework/util/feature_base.h`), collaborators by reference, implementations `override`/`final` without `virtual` (#2858).
2. When every API feature is consumer plus decoder, one template over the base, and per-API files hold a `using` alias and the registration line, as `tools/convert/convert_vulkan_feature.cpp` does (#2858, #2939).
3. One `<tool>_<api>_feature.cpp` per API, the whole file inside `#if defined(D3D12_SUPPORT)` or `#if ENABLE_OPENXR_SUPPORT`, always listed in CMake (#2858, #2997).
4. Each feature owns its detection consumer and exposes `WasDetected() const`; `main.cpp` ORs the results and exits if nothing was detected (#2939, #2997).
5. `main.cpp` iterates `FeatureModuleRegistry<Base>::GetSingleton()`; delete the API code it no longer uses (#2751, #2997).
6. Preserve the old behavior: the bounded detection scan (#3054), the `FileProcessor` file-open log (#2751), the `-1` exit code (#3103). For replay, update the three Android `CMakeLists.txt` files too (#2927, #3069).

### 2.8 Add a test app, test case and reference

`TESTING_test_apps.md` covers the app. Register it in all five places (#2180, #3058, #3299): `test/test_apps/CMakeLists.txt`; `kAppNames` and `CreateTestApp` in `test/test_apps/launcher/test_launcher.cpp`; both `GFXRECON_TEST_LAUNCHER_LINK_LIBRARIES` lists (`test/test_apps/launcher/CMakeLists.txt`, `android/test/test_apps/launcher/CMakeLists.txt`); `add_test_app(...)` in `android/test/test_apps/common/CMakeLists.txt`. Missing the Android lists fails only Android CI (#2309, #2245). Then:

1. `test/test_cases/<name>.cpp`, appended to `GFXRECON_TESTCASES` in `test/CMakeLists.txt` (#1930, #2115).
2. Record `test/known_good/<name>.gfxr` on the Mock ICD (#1930, #2157). `verify_gfxr` in `test/verify-gfxr.cpp` converts both captures to JSON, parses them with `clean_gfxr_json`, and asserts an empty `nlohmann::json::diff`; binary comparison was rejected (#1886).
3. Steer the driver through `vkmock::TestConfig` only inside `if (test_config)`, because apps also run on real drivers (#1930, #2115). Hide new volatile fields by extending `clean_gfxr_json` (#2220, #2245).
4. Gate platform-only apps in both CMake (`CMAKE_SYSTEM_NAME MATCHES "Linux|BSD|GNU"`) and the launcher (#1930, #2115).
5. Test apps fail loudly with `VERIFY_VK_RESULT` or `throw`; a process that exits 0 "will count as a success" and `GFXRECON_LOG_ERROR` does not abort (#2119, #2135).
6. Re-record affected known-goods in their own commit when a header or shared app change alters output, and say why (#3287, #2682, #2220).

### 2.9 Change a generator

1. AGENTS.md: never hand-edit generated files. CI regenerates Vulkan and OpenXR and fails on `git diff --exit-code` (#2172, moved ahead of the build by #2591). #2160 edited 31 generated OpenXR files by hand and #2172 caught the drift.
2. Regenerate every API the generator serves: shared `khronos_generators/` code means `generate_vulkan.py` and `generate_openxr.py` (#3308, #2172), `generate_dx12.py` for DX12 (#2765, #2836). Commit the output in the same PR, optionally as its own codegen-only commit (#2553, #2628, #2630).
3. Take facts from the registry, not hand lists: handles from `VK_DEFINE_HANDLE` (#1898), sType from `values` (#2082), lengths from `len` (#3265), chainability from `structextends` rather than a scraped JSON (#2455). Treat every struct with an sType and pNext as chainable, because applications chain where the spec forbids (#2654, #2557).
4. Emit a call to a hand-written helper rather than boilerplate in every struct (#2455, #2541, #3265); pass `->GetPointer()` to `void*` parameters (#1906).
5. A refactor leaves the output unchanged and the PR says so (#2469, #2137, #3175). A clean diff is not sufficient: the Khronos refactor shipped pNext mapping that stopped at the chain head (#1994, "our tests aren't good enough to catch this"), predicates that never matched (#1950, #2036) and a 2D loop with the wrong index (#2009, #2195). Name one generated function whose output should change, confirm it did, and add a round-trip test in `framework/test/` (#3111, #3265).
6. When generated code mutates decoded arrays in place, keep raw and `Decoded_` arrays in sync, call `SetLength`, restore afterward, and add a mutable accessor instead of a `const_cast` (#3207, #3304).
7. Wrap `executor.map` in `list(...)` so worker exceptions surface (#2994).

### 2.10 D3D12 and OpenXR differences

**D3D12.** Trim-state restore (#1826, #2026, #2293, #1947): a `CustomWrapperPostCall<ApiCall_X>` in `framework/encode/custom_dx12_wrapper_commands.h` dispatching to `D3D12CaptureManager::PostProcess_<Interface>_<Method>`, the tracker called only under `IsCaptureModeTrack()`, a field on the narrowest `*Info` in `framework/encode/dx12_object_wrapper_info.h`, and a synthetic call in `Dx12StateWriter` placed right after the object's create write: parameters, then `S_OK`, then `WriteMethodCall`. `framework/encode/dx12_state_writer.cpp` (#2026):
```cpp
void Dx12StateWriter::WriteSetName(format::HandleId handle_id, const DxWrapperInfo& wrapper_info)
{
    encoder_.EncodeWString(wrapper_info.object_name.c_str());
    encoder_.EncodeInt32Value(S_OK);
    WriteMethodCall(format::ApiCallId::ApiCall_ID3D12Object_SetName, handle_id, &parameter_stream_);
    parameter_stream_.Clear();
}
```
A capture override is needed when the value must change before it is encoded; a post-call cannot change a desc passed by value (#2187). A new enum value, union member or PSO subobject reaches every hand-written switch: encoder, decoder, unwrapper, object mapper, state tracker, JSON, dump resources and `framework/format/platform_types_d3d_overrides.h` ("Search for all switches on D3D12_SRV_DIMENSION/D3D12_UAV_DIMENSION", #3203; #2633, #2826). Capture the revised-create-views `TryCreate*View` calls even when they fail; track only on `SUCCEEDED` (#3203, #3312). Pointer lengths the generator gets wrong get a table entry in `framework/generated/dx12_generators/dx12_base_generator.py` (#3203, #2836). Agility SDK bumps (single author: #2073, #2406; also #2677, #3203) append `ApiCall_*` IDs under an `// Agility SDK x.y.z` comment, bump the `D3D12SDKVersion` export in replay and optimize, and blacklist union-bearing structs. Hold COM interfaces in the `graphics::dx12::*ComPtr` typedefs (#1866, #3227); GFXR's own `AddRef`/`Release` on wrapped objects is recorded, so a capture-side ownership fix shifts reference block indices and needs a `test_suite.ref` bump (#2740, #2771, #2775).

**OpenXR.** Gate every OpenXR file with `#if ENABLE_OPENXR_SUPPORT`, never `#ifdef`, because the macro carries a value (#2160, #2172). Replay assumes the runtime differs from capture: re-query enumeration counts, `FATAL` only when replay returns fewer items than the captured capacity, `ERROR` per missing item (#2147, #2204); drop events that may not recur from the fatal-timeout set (#2242). When OpenXR lacks a hook Vulkan has, copy the Vulkan implementation into the OpenXR decoder or consumer base and wire it in both `desktop_main.cpp` and `android_main.cpp` (#2991, #2218). The OpenXR evidence is 12 PRs from three authors.

## 3. Idioms reviewers expect

**Asserts.** AGENTS.md and CONTRIBUTING.md say `GFXRECON_ASSERT`, never `assert`. This is the most-broken written rule: about 1,750 `assert(` calls remain in hand-written `framework/` code and new ones passed review in #2075, #3091 and #3308. Do not take a nearby `assert` as permission; convert the `assert` calls on lines you touch (#1846 "For new code please prefer `GFXRECON_ASSERT`", #2511, #3012, #3267). An assert is not a guard. If the application, the capture file or the user can break the condition, add a runtime branch that logs and skips, because Release compiles the assert away (#2135 "`GFXRECON_ASSERT` alone crashes in release; add an explicit null check"; #2083; #3138 "this isn't an invariant to assert on"). `framework/encode/vulkan_capture_manager.cpp` (#2135):
```cpp
auto* wrapper = vulkan_wrappers::GetWrapper<vulkan_wrappers::SwapchainKHRWrapper>(swapchain);
GFXRECON_ASSERT(wrapper != nullptr);
if (wrapper != nullptr)
{
    if (image_index >= wrapper->image_acquired_info.size())
    {
        wrapper->image_acquired_info.resize(image_index + 1);
    }
    wrapper->image_acquired_info[image_index].is_acquired = true;
}
```
The converse holds: when a condition is true by construction, assert it instead of writing a guard that never fires (#3245 "state our assumption and assert"; #3153; #3260). Remove asserts the spec does not support and comment why the value is valid (#2339, #2795, #3259).

**FATAL.** Since #2986 `GFXRECON_LOG_FATAL` calls the fatal callback and then `std::abort()`. Use it only where continuing is impossible, never in capture ("Should GFXR be allowed to force-quit the application it is capturing?"); `framework/encode` has no FATAL calls. `framework/util/logging.cpp` (#2986):
```cpp
if (severity == LoggingSeverity::kFatal)
{
    if (fatal_callback_)
    {
        fatal_callback_(non_indented_version.c_str());
    }
    // guarantee LOG_FATAL is fatal
    std::abort();
}
```
Older "FATAL, then `return`" code (#2357, #2286) never returns now; use ERROR when the code is meant to continue. Tool mains return `EXIT_FAILURE` instead of calling `exit()`, so the RAII log guard runs (#3054, #3074).

**Severity and `_ONCE`.** Replay announces every change it makes to captured behavior and stays silent on the normal path (#2736 "skip the info log on every regular replay"; #2272; #1902). A degraded path says what will not happen ("its contents will not be restored across loop repetitions", #3234; #3295). WARNING is a deviation, INFO an expected adjustment, DEBUG per-object detail; setup messages that warn about nothing are demoted (#2713, #3176). Anything that can repeat per call, draw or frame uses `_ONCE` (#2219 after "flooding logs"; #2405; #3177); `GFXRECON_LOG_ONCE` is a function-local static, so it fires once per call site, not per object. Use the plain macro when frequency matters (#3220). An option value that cannot be parsed warns, names the value and falls back (#2133, #2713, #3216); a well-formed option the device or build cannot honor aborts with an explanation (#2604, #2736; Appendix B).

**Error propagation.** Log each failure once, at the lowest layer that knows the reason, with `errno` captured into a local immediately (#2840, #2701, #3310). `framework/util/file_output_stream.cpp` (#3292):
```cpp
if (platform::FileFlush(file_) != 0)
{
    const int error = errno;
    GFXRECON_LOG_ERROR(
        "Failed to flush buffered data to file \"%s\" (errno %d: %s)", name, error, strerror(error));
}
```
Check and forward `VkResult`; populate object info only on success (#2936 "Need to check `VkResult`", #2260, #3152). Return a tri-state status enum, not a lossy bool, where callers must tell a benign stop from an error (#2514, #2625). Every `switch` over an enum gets a `default:` that logs the unhandled value, and handled-but-empty cases get an explicit case (#1870, #2083, #1975, #2826).

**Pointers, ownership and const.** CONTRIBUTING.md covers `== nullptr`; reviewers also ask for `== 0` on integers and braces on every `if` (#1937, #1942, #2081, #2128). A class that owns Vulkan handles is move-only with real move operations ("when you got a dtor, you will ... also need the rest of the rule-of-5", #2827; #3271; #3162 leaked a `VkCommandPool` for lack of a destructor). One reviewer asked for full construction in the constructor ("I am not a fan of semi-initialized objects", #3256; single PR, and sibling types still use `Create()`, Appendix B). `framework/decode/vulkan_temporary_objects.h` (#3256):
```cpp
// Ownership of the buffer has to transfer rather than be duplicated, so we won't call `vkDestroyBuffer` on an
// already destroyed buffer.
TemporaryBuffer(const TemporaryBuffer&)            = delete;
TemporaryBuffer& operator=(const TemporaryBuffer&) = delete;
```
Take required collaborators by reference; store values or references, not raw pointers into maps ("never a question of use-after-free", #2922; #2648; #2561). Narrowest ownership: plain member, then `unique_ptr`, then `shared_ptr`; no bare `new` (#2751, #2451, #2397). Never `const_cast` ("We tend to copy and modify to a local store", #3207; #2359; #2081). `const` on range-for variables and locals (#2044, #2081); callables stored by value, not `const std::function&` (#2574, single PR); read-only arrays as `std::span<const T>` by value (#2937, #2724). Per-device replay helpers live in a map keyed by `const VulkanDeviceInfo*`, created lazily through a `GetDeviceX(device_info)` accessor, erased in `OverrideDestroyDevice` before the driver call and cleared in the destructor, because trimmed captures never destroy the device (#2827, #2271, #2999, #3234, #3299).

**pNext.** Use `graphics::vulkan_struct_get_pnext<T>`, `vulkan_struct_remove_pnext<T>` and `vulkan_struct_add_pnext<T>` from `framework/graphics/vulkan_struct_get_pnext.h`, never a hand-walked loop (#1820, #2088, #3195, #2434), bound in an if-initializer with `auto*` and `!= nullptr` (#1942, #2549). Loop when the spec lets the struct repeat (#3177); use the single lookup when it cannot (#3195). `framework/decode/vulkan_replay_consumer_base.cpp` (#3177):
```cpp
while (graphics::vulkan_struct_remove_pnext<VkDeviceDeviceMemoryReportCreateInfoEXT>(&modified_create_info))
{
    GFXRECON_LOG_WARNING("VkDeviceDeviceMemoryReportCreateInfoEXT callback was removed at device creation");
}
```
On the decode side read chained structs through `GetPNextMetaStruct<Decoded_T>`, never by casting `GetPointer()` (#1832, #2068). Copy a struct you replace into function-scope storage, often `std::optional<T>`, and attach the copy to the modified create info (#2438, #1955). On capture, replace a pNext with NULL only when the pointer is shown invalid, never because the spec says it should be NULL ("we can't just assume NULL for pNext even if stated in the spec", #2455; #3308).

**Format strings and platform `#if`.** Print `VkResult` with `util::ToString(result).c_str()`, not `%d` (#1919, #2381); 64-bit IDs with `PRIu64`, handles and addresses with `0x%" PRIx64`, `size_t` with `%zu` (#1966, #3084, #2723); quote paths; no trailing `\n` (#2095). No `std::format`: it compiled on CI and failed at runtime against the older system libstdc++ and the Android NDK, and took five PRs to remove (#2647, #2718, #2723, #2728, #2761; #2793 fixed `{}` left in strings). Gate API code on the feature macro, not the OS (`#if defined(D3D12_SUPPORT)`, `#if ENABLE_OPENXR_SUPPORT`; #2173, #3103), naming the macro in the `#endif` comment; one author also guards Vulkan-typed code in API-shared files with `#if defined(GFXRECON_ENABLE_VULKAN)` (#3104). OS checks are `#if defined(_WIN32)` (#3028, #3308). Android-only behavior sits under `#if defined(VK_USE_PLATFORM_ANDROID_KHR)` for API questions and `__ANDROID__` for OS plumbing, with a comment on why other platforms differ (#3162, #2245, #2281). OS differences hide behind `util::platform` inline wrappers with one signature in both halves of `framework/util/platform.h` (#1929, #2606, #2645); `#if` chains usually end in `#else #error` (#2109, #2294). Keep 32-bit working: `VK_HANDLE_TO_UINT64`, `VK_NULL_HANDLE` not `nullptr`, widen before shifting (#2040, #2867, #2811).

**Allocation and locking on the capture path.** Capture entry points run under a shared API-call lock, so a container mutated from entry points gets its own named mutex, declared beside it and locked after the cheap early returns (#2340, #3197, #2201). `framework/encode/vulkan_state_tracker.cpp` (#3197):
```cpp
{
    // resource_utils_ needs to be updated when a device is destroyed, otherwise the entries left behind will
    // attempt to access the device's call table during destruction
    std::lock_guard<std::mutex> resource_utils_lock(resource_utils_mutex_);
    resource_utils_.erase(wrapper->handle);
}
```
Hold a lock only around the down-chain call, and do not re-lock what trim start already serializes (#3267). Per-thread state is `thread_local`; a flag several threads touch is atomic or mutex-guarded, never a plain `static` (#2390, #2524, #2599). `thread_local` does not follow work to another thread: compute on the calling thread and capture by value (#3172, #2898). Do process-constant checks once in `Initialize` (#2206); no `std::string` or vector allocations on per-call paths, `const auto&` over `shared_ptr` containers (#3191, #3220, #2766). Back a perf claim with numbers ("I'm not going to approve until I see actual numbers", #2549; #2920). Write the threading contract down, `// Guarded by queue_mutex_.` per member (#3310, #2783, #3139). The Principles say avoid locks in capture; in practice a needed lock is accepted when it is narrow and commented (#3267, #2340, #3197, #2599, #2646).[^locks]

**Naming and file layout.** Members end in a trailing underscore; members and locals are snake_case, including in test apps (#1955 "That is the naming style we should follow", #1839, #3016, #3033, #2553). Types are PascalCase without `_t` (#1955, #2261; one reviewer). Constants take a `k` prefix and replace magic numbers (#2110, #3196, #3180). Name a thing for what it is, not its caller, representation or old meaning: `is_png` became `expand_to_rgb` (#1883), `CanImageBeDumped` "is the name for a calculation, not a status" (#2419), "tuple" in a name is "like calling an array index index_int" (#2395, #3271). API-specific names carry the API (`VulkanDeviceInfo`, `GetVkDeviceInfo`, `vulkan_`/`dx12_` files; #1817, #1846, #3033). Fold identical bodies into one template with a `static_assert`, "to avoid cut-and-paste errors later" (#2227, #3140). A delegate or distinct concern gets its own `.h/.cpp` pair named for what it holds (#1941, #3066, #2508). Do not widen a shared type for one caller (#3247, #2751, #1795). Register every new source file in both `framework/<lib>/CMakeLists.txt` and `android/framework/<lib>/CMakeLists.txt` (#2586, #3026, #2459). Project macros take a `GFXRECON_` prefix (#2154).

[^locks]: The Principles doc (PERFORMANCE) says "avoid locks and allocations" in capture. Five PRs by four LunarG authors added narrowly scoped locks to capture entry points; reviewers trimmed their scope and never rejected one.

## 4. API knowledge that shows up in review

**Vulkan.** Queues: `queueFamilyIndex` is unique only per `(family, VkDeviceQueueCreateFlags)` (VUID-VkDeviceCreateInfo-queueFamilyIndex-02802), so protected and unprotected queues collide when keyed by index (#2795, #3186, #3267); `vkGetDeviceQueue` retrieves only `flags == 0` queues (#3199); a command pool's family must be one the device was created with (VUID-vkCreateCommandPool-queueFamilyIndex-01937, #2923); `vkCmdBlitImage` needs graphics support, `vkCmdCopyImage` does not (#2910, #2916). Synchronization: a binary semaphore signal satisfies exactly one wait (#2283, #2458, #2337); `vkQueueSubmit` with `submitCount 0` and a fence is valid and signals the fence (#3023, #2936); a fence passed to submit must be unsignaled (VUID-vkQueueSubmit-fence-00063, #2504); `BOTTOM_OF_PIPE` in a barrier gives no memory dependency, use `ALL_COMMANDS` (#2369, #2827); shader-stage and pipeline-stage enums are distinct (#2194); sync1 calls take sync1 constants (#2895). Memory and images: memory requirements are driver-dependent, so re-query on the replay device (#2078, #2413); AHB import needs a dedicated allocation with `allocationSize` equal to the queried value (VUID-VkMemoryAllocateInfo-allocationSize-02383, #1820, #2371) and `allocationSize == 0` is a valid property result (#3259); a copy's aspect mask has exactly one bit (VUID-VkBufferImageCopy-aspectMask-09103, #2192); a barrier's `oldLayout` must match the actual layout (#2979); `VkBufferUsageFlags2CreateInfo` overrides legacy usage (#3234, #3260). Descriptors: inline uniform block `descriptorCount` is bytes and template `stride` is ignored (#2076, #2339, #2359); only SAMPLER and COMBINED_IMAGE_SAMPLER descriptors carry samplers (#3012); `vkCmdBindDescriptorSets2` selects bind points through `stageFlags`, and task/mesh count as graphics (#2255, #3036). Entry points: `vkQueueSubmit2KHR`, `vkCreateRenderPass2KHR` and `vkCmdBindVertexBuffers2EXT` decode separately from their core aliases even when sType values are equal (#3140, #2264, #2138); `VkPhysicalDeviceBufferDeviceAddressFeaturesEXT` has its own sType (#2846); a core entry point is legal only when both instance and device versions reach the promotion version (#3191). Lifetimes: children before `vkDestroyDevice` (VUID-vkDestroyDevice-device-05137) and before `vkDestroyInstance` (VUID-vkDestroyInstance-instance-00629) (#2999, #3298, #2196); a presentable image is usable only between acquire and present (#3066); `VkDeviceDeviceMemoryReportCreateInfoEXT` may repeat in a chain (#3177), `VkImageFormatListCreateInfo` may not (#3195); `VkPhysicalDeviceVulkan11Features` and the standalone feature struct must not both be chained (#2042, #3083). Results: `vkGetSwapchainImagesKHR` writes handles on `VK_INCOMPLETE` (#2649); `VK_TIMEOUT`/`VK_NOT_READY` from acquire mean no image (#2806); `VK_SUBOPTIMAL_KHR` is success (#2799); `WAIT_BIT` on a query never written hangs (#2681, #3235). Capture: `FAIL_ON_PIPELINE_COMPILE_REQUIRED` returns `VK_PIPELINE_COMPILE_REQUIRED` with a null pipeline (#1938, #2620); `oldSwapchain` may free non-acquired images even when creation fails (#1966).

**D3D12.** Resource state is decided at `ExecuteCommandLists`, not when the barrier is recorded, and `D3D12_RESOURCE_STATE_COMMON` is 0 (#2001). LUIDs, function pointers, GPU VAs, descriptor handles and window ids are system-specific and are translated at replay (#1885, #2161, #2066, #2225). `GetCachedBlob` output exists only after the PSO (#2383); `CreateCommandList1` creates a closed list with no allocator (#1835); `CREATE_NOT_RESIDENT` heaps need `MakeResident`, which is reference counted with `Evict` (#2293, #3157). `CopyDescriptors` size arrays are optional and mean size 1 (#2555). `ALLOW_WRITE_WATCH` is a GFXR-injected flag hidden from the app and the file (#2187). Committed resources are zeroed, reused heap memory is not (#3227). `ExecuteIndirect` executes `min(count buffer, MaxCommandCount)` (#2069).

**OpenXR.** Two-call enumerate: capacity 0 returns the count, `XR_ERROR_SIZE_INSUFFICIENT` means too small (#2147, #2204). `xrPollEvent` returns `XR_EVENT_UNAVAILABLE` on an empty queue and event occurrence depends on the runtime (#2242). `xrEndFrame` is the frame boundary (#2218). With `XR_KHR_vulkan_enable2` the runtime creates the Vulkan instance and device and may issue Vulkan calls itself (#2162, #2075). On 32-bit, non-dispatchable handles are all `uint64_t`, so per-type overloads are guarded by pointer size (#2075; ref unknown).

**How engineers cite the spec.** VUIDs go in the comment at the line that depends on them (#3186, #2795, #3099, #2504). `framework/graphics/vulkan_util.h` (#3186):
```cpp
//! number of queues created per (queue-family, VkDeviceQueueCreateFlags). the combination is what
//! VUID-VkDeviceCreateInfo-queueFamilyIndex-02802 requires to be unique, a family-index alone is not.
std::unordered_map<uint32_t, std::map<VkDeviceQueueCreateFlags, uint32_t>> queue_family_queue_counts;
```
Spec text is quoted above conservative handling (#1966, #2042, #1995). Some source VUIDs are marked approximate (#2434, #2429, #2385); check a VUID before quoting it.

## 5. Corner cases GFXR engineers always check

**Every sibling of the thing you touched.** Core/KHR/EXT/vendor aliases and "2" variants decode as separate calls, so a fix in one misses the others: `vkAcquireNextImage2KHR` (#2806), `OverrideCmdEndRenderPass2` (#3016), sync2 KHR submit in dump resources (#3030), `vkCmdCopyQueryPoolResultsToMemoryKHR` (#2945), `vkQueueSubmit2KHR` in optimize (#3140), mesh-shader EXT versus NV counts (#2832, #2901), `GetHeapProperties` beside `GetDesc` (#2187). `OverrideQueueSubmit` and `OverrideQueueSubmit2` stay structurally identical (#2503, #2283, #2611), and the async `--pipeline-creation-jobs` path is a sibling too (#2227, #2620, #1888). Share one body through a template. `framework/decode/vulkan_referenced_resource_consumer_base.h` (#3140):
```cpp
template <typename T>
void Process_vkQueueSubmit2(StructPointerDecoder<T>* pSubmits)
{
    static_assert(std::is_same_v<T, Decoded_VkSubmitInfo2>);
    if (!pSubmits->IsNull() && pSubmits->HasData())
```

**Null, zero and ignored inputs.** Count 0 with a null array (#2962, #2739), zero `allocationSize` (#3259), zero vertex stride (#2236), `pApplicationInfo == nullptr` (#2117, #2815), null `pStrides`/`pSizes` (#2138, #2184), null handles passed to destroy (#2952), garbage `pInheritanceInfo` on a primary (#2366), `memory == VK_NULL_HANDLE` with a swapchain bind (#2092), `SetName(nullptr)` (#2026), a null fullscreen desc (#2017). Spec-nullable handles get a check, not an assert (#2092, #2287).

**Arrays with more than one element.** Copy-paste slips are the most common bug in parallel code: `pColorAttachments->` for `[i]` (#2264, fixed by #2282), `pViewFormats[0]` (#2438), `pStages[i]` for `[j]` (#2997, #3006), `meta_bind_infos->` (#3185), `submit_info_data->pNext` (#3226), optimal tiling checked against `linearTilingFeatures` (#2270, #2275), `a.empty() || a.empty()` (#2392), `cmd_buf_info = cmd_buf_info` (#2353). Diff each copy against its source; at three copies, factor a helper (#2635).

**Capture-time data against the replay device.** Memory type index (#1820), memory requirements (#2078, #2413), queue family indices (#2786, #2923, #3199), device addresses reused after free (#3109), surface capabilities (#3127, #2025), data-graph bind points (#3174). Re-query at the point of use; key capture data by `format::HandleId` and null-check object-info lookups (#2869, #3052, #2935). Record the captured and the replay-modified value as separate fields (#3091, #3099, #2078).

**Injected GPU work.** Every replay-synthesized Vulkan call runs inside an injected scope (`GetInjectedDeviceCalls(device).Open()` since #3172, `util::MarkInjectedCommandsHelper` before), opened above locals whose destructors make Vulkan calls and closed before async work, because the marker is thread-local (#1879, #3186, #3191, #3172). Check its `VkResult` (#2936, #2766). Pick the family from the replay `VkDeviceCreateInfo`, accept GRAPHICS or COMPUTE as implying transfer, check `VK_QUEUE_GRAPHICS_BIT` before a blit, and return `VK_QUEUE_FAMILY_IGNORED` when nothing fits (#2328, #2786, #2910). Forward the app's waits to the first split submission and its signals to the last, strip waits an injected submit consumed, and signal the app's fence with an empty submit when needed (#2283, #3030, #3289, #3066). Reset a replay-owned fence after every wait, wait on the fence rather than `vkQueueWaitIdle`, bound the wait and say why (#2766, #3110, #2305, #2791). `framework/decode/vulkan_resource_initializer.cpp` (#2305):
```cpp
result = device_table_->QueueSubmit(queue, 1, &submit_info, fence);
if (result != VK_SUCCESS)
{
    device_table_->DestroyFence(device_, fence, nullptr);
    return result;
}
// Wait a sensible amount of time (10 seconds) to avoid hanging in case a prior
// operation caused the GPU to hang or crash.
result = device_table_->WaitForFences(device_, 1, &fence, VK_TRUE, 10000000000);
```
Write timeouts as `UINT64_MAX` or a named constant; `~0UL` is 32 bits on Windows, about four seconds (#3084 is the same `long`-width bug in output). Access masks match the layout and operation (#2786, #2070), aspects match the format (#3220), and dispatches never land inside a render pass (#2040, #2222).

**Lifetime across boundaries.** Trimmed captures never call `vkDestroyDevice` (#2267, #2271); more than one instance or device (#2188, #2386, #2423); command buffers reset and re-recorded, so every per-command-buffer field is cleared in `ClearCommandBufferInfo` and merged from secondaries in `OverrideCmdExecuteCommands` (#1879, #3217, #3235); objects created before a frame loop and destroyed inside it (#3052, #3107, #3171); tracking left behind after destroy (#3052, #2948, #3015, #3197); memory freed before the buffer bound to it (#3099, #2288, #3271).

**Files and decoding.** Clean EOF versus trailing bytes versus I/O error (#2514, #2622, #2454); unknown, reserved and experimental blocks skipped with a one-time warning and counted as success (#2348, #2442, #2547); old encodings detected and consumed (#2605, #2877, #2466); frame-end markers out of order from multithreaded capture (#3138); 64-to-`size_t` narrowing checked with `GFXRECON_CHECK_CONVERSION_DATA_LOSS` (#2731, #2459).

**Build configurations.** `-DD3D12_SUPPORT=OFF` on Windows (#2173); `-DGFXRECON_ENABLE_OPENXR=OFF` (#2636); `-DGFXRECON_ENABLE_VULKAN=OFF` (#3104, #3233); `BUILD_SHARED_LIBS=ON` (#2560); GFXR as a subproject, so `CMAKE_CURRENT_LIST_DIR` or `GFXRECON_SOURCE_DIR`, never `CMAKE_SOURCE_DIR` (#2099, #3133, #2989); the Gradle build, which uses `android/**/CMakeLists.txt` and never the top-level file (#2616, #2626, #2961); CMake 3.16.3 (#1988, #1997, #2000); 32-bit (#2867, #2811, #3039); Debug, where asserts are live (#2122, #2164, #3102); release-assert builds guarded with `!defined(NDEBUG) || defined(GFXRECON_ENABLE_RELEASE_ASSERTS)` (#3018, single author).

## 6. Testing a change the GFXR way

AGENTS.md states the requirement; practice falls short (591 of 921 PRs record no test). Reviewers do ask ("We need tests", #3247; #2261), so pick the strongest of these that fits:

1. **Known-good capture comparison** for anything that changes what capture writes: a Mock ICD test app plus `test/test_cases/<name>.cpp` and `test/known_good/<name>.gfxr` (§2.8; #1886, #2294, #2157). Regenerate affected known-goods in the PR that changes encoding (#2265, #2517, #3287).
2. **Call-count test** for a replay option that injects or reshapes work (one author so far: #3299, #3315; the sub-rules were stated by reviewers): capture once, replay with and without the flag via `replay_and_count_recapture`, assert the counted commands are non-zero, then assert exact deltas as `baseline + n`, never a subtraction or a range ("to avoid any possible underflow", #3299; "BeforeEverySubmit" means equality, #3315). `test/test_cases/serialize-compute-and-transfer.cpp` (#3299):
```cpp
ASSERT_GT(with_option["vkCmdDispatch"], 0u);
ASSERT_GT(with_option["vkCmdDispatchBase"], 0u);
ASSERT_GT(with_option["vkCmdDispatchIndirect"], 0u);

// Must not add or drop a dispatch.
EXPECT_EQ(with_option["vkCmdDispatch"], baseline["vkCmdDispatch"]);
```
   A smoke test that only checks replay exit status proves nothing (#3058, rewritten by #3299). Capture filenames include the gtest suite and test name (#3299); env vars are scoped with the RAII `EnvironmentVariables` helper (#2109).
3. **Catch2 unit test** for GPU-free logic in `framework/<facility>/test/` or `framework/test/` (round-trip encode/decode, #3111, #3265, #3044; pNext filtering, #3193; pointer probes, #3308; rotation helpers, #3091), at log level `kError` (#2646). Unit tests cover the helper, not the integration, so pair them with a capture run (#1844, #3108).
4. **External suite case** when the Mock ICD cannot reach the path: add a capture or `.gfxr` plus input JSON to the suite repo that fails without the fix, and bump `test_suite.ref` in this PR as its own commit (#2733 "Add new test for VDR_2733", #2617, #3289, #2572). Any change to dump output names or convert JSON needs the references updated in the same PR (#2861, #2094, #1959 was reverted by #1964 for lacking this). External authors cannot see suite failures; share the failing case (#2771).
5. **Written procedure** when none of the above applies: which capture, flags, platforms and drivers; before/after output for `info` and `convert` (#3259, #2271, #2848). Run replay changes with `--validate` (#1884, #2181, #2194).

Run extended CI before merging changes to shared replay or decode machinery, swapchains or platforms that broke before, and link the run (#3070, #2885, #2799). Test in Debug as well as Release: #2164 passed the suite in Release and hit an assert in a reviewer's Debug build, and the Dbg CI entries ran Release until #3102. Most test cases capture and compare; replay cases are recent (#3016, #3299, #3247; Appendix B).

## 7. Writing the commits and the PR as practiced

CONTRIBUTING.md and AGENTS.md give the format: problem and rationale first, small single-purpose PRs, no unrelated edits, squash noise commits, license header on new files, `Fixes #N`. The as-practiced delta:

- **Problem first.** Reviewers ask "What problem does this PR solve? ... what is the symptom that this fixes?" (#2213) and want it "in case someone comes back in the future to understand the PR" (#3213); copy the description into the commit message so the why survives squash (#2957). #3259 is the model: triggering API result, why the old asserts were wrong, per-file changes, replay impact, superseded PR, manual test procedure. 26% of LunarG PR descriptions are empty; they draw objections only when the motivation is unclear.
- **Answers go in code comments.** When a reviewer asks why, "include your answer as a comment in that function" (#3271; #1879; #2681 "so we don't try this change again"). No hand-written TODO on dev cites an issue, yet reviewers ask for a tracking issue on every deferred path or bypass (#2661, #3009, #3295); write `// TODO(#NNNN): ...`.
- **One purpose.** Splits requested so each part can "land as a unique commit in dev" (#2563 to #2576; #1911 to #1949; #2561 to #2585; #3155 to #3157). Mixed PRs are tolerated with "separate them next time" (#1739, #1846); bundled PRs get reverted whole (#2088, reverted by #2151, re-landed as #2155 and #2157). Reviewers accept "fix what you move" inside a refactor (#2997, #3216).
- **No incidental edits.** Reviewers revert whitespace, clang-format reflow of untouched lines, copyright-only changes and stray submodule bumps (#1891, #2083, #1971, #2439). Run `git clang-format-14` on staged changes, not `clang-format -i` on files.
- **Bisectable history.** Every landed commit compiles ("or else the tree won't be bi-sectable", #1844; #2410, #3033). Squash merge is the default (63%) and absorbs branch noise, including the `Merge branch 'dev'` commits on 106 branches.[^merge]
- **Title and description match the final diff.** When review moves code out, update both and add "Split out to #N" (#3155, #3110); 52 PRs carry a description that no longer matches their diff (#2604, #3196).
- **Reverts and relands.** Revert with `git revert`, state the reason (#1964 and #2151 do not), and reland with the default path unchanged (#2030, #2157, #2775, #2914). Name the PR you supersede in the description (#3259; #2563 and #2576).
- **Deletions.** Delete dead code, options, files and docs in the same PR; this is the most common scope request that asks for more work (#2751, #3151, #2989, #3153).
- **Rare but wanted.** `Fixes #N` appears in 5% of descriptions and before/after output for `info` and `convert` in 7 of 81 tool PRs; include both.

[^merge]: CONTRIBUTING.md says to rebase on `dev`; 106 branches merged `dev` instead with no objection, because squash merging hides the merge commits. Rebase when the PR will be rebase-merged.

## 8. Known traps

Each family: what happened, what caught it, the durable fix, the pre-submit check.

1. **A large change lands untested and its regressions arrive one at a time.** Asset file #1644 needed #1858, #1861, #1869, #1887, #1946, #1979; portable ray tracing #1955 needed #1982, #1993, #2053; VDR transfers #2561 needed #2617, #2733, #2917; the FileProcessor series #2379/#2424/#2484 needed #2394, #2442, #2454, #2514. Reviewers ask for tests (#2395, #3247) and splits (#1911, #2563). Check: above about 500 non-generated lines, name the test that fails if each new branch is wrong.
2. **Build or CI change checked in one configuration.** CMake minimum #1988 to #1997 to #2000; ARM64 detection #2217 to #2253 to #2262 (unquoted empty `${CMAKE_GENERATOR_PLATFORM}`); #2506 to #2560 (`BUILD_SHARED_LIBS=ON`); 16 KB pages #2616 to #2626 (Gradle ignores the top-level file); format check #2908 reverted by #2913, relanded as #2914 (`github.base_ref` unset in the merge queue); extended gate #2938 needed #2946, #2947, #2953; Dbg entries ran Release until #3102. Check: list the configurations the change affects and how each was exercised; run Jenkins changes from the branch with the manual pipeline (#2955).
3. **A new case handled at one site, not its siblings.** #1920, #2042, #2806, #3016, #2227, #3030, #2945, #3044, #2696 to #2746, #2944, #2948, #3203. Fix: templated helpers (#2227, #3140) and alias mapping in the override JSON. Check: grep every alias, "2"/KHR/EXT/core variant and every `switch` on the type you touched.
4. **Copy-paste and index slips.** #2264 to #2282, #2270 to #2275, #2359 to #2392, #2561 to #2617, #2570 to #2635, #2232, #2283, #2115, #2728 to #2793. Usually not caught by review. Check: §5, arrays.
5. **Assert used as the runtime guard.** #2089 reverted by #2122 ("invisible in Release-only CI because the guarding assert is compiled out"); #2184 to #2200 (assert moved outside the depth branch); #2197 to #2236 (asserted `stride != 0`, which is valid); #3010 to #3012; asserts valid apps trip removed in #2339, #2795, #2727, #2269, #2287. Check: can a valid application or a capture file make this fire?
6. **Default behavior, option meaning or output changed unasked.** #1998 reverted (commit be51de061) and relanded as #2030 keeping both `--dump-resources` forms; #1959 reverted by #1964; `--wsi auto` #2736 made `--remove-unsupported` effectively required (#2837); #2726 to #2808 (default filenames); #2154 manually reverted by #2246 (Android backtraces became anonymous); #3285 (a new default window size disabled the zero sentinel). Check: run the tool with no new flags; every output byte, log line and exit code is unchanged.
7. **Injected GPU work assumes queue capability or synchronization.** #2910 reverted by #2916 (blit on a present queue without graphics); #2923; #3199; #2110 to #2194 (shader stage as pipeline stage); #2283 to #2369 (`BOTTOM_OF_PIPE` stalled one driver); #2766 (fence reset skipped); query waits #2533 to #2681 (hang) to #2723 to #3235. Caught by extended CI on other vendors and validation layers. Check: for every command GFXR records itself, which family runs it, is the capability guaranteed, does the replay device have that family?
8. **Lifetime, teardown and state left behind.** #2267 to #2271 (trimmed captures never destroy the device); #2128 to #2188 to #2196 (messenger leak); frame-loop fences #2936 to #3052 to #3107 to #3171; #2610 to #2666 (`erase_if` on a map taken by value erased a copy); #2412 (iterator invalidation); #2646 to #2888 (non-virtual base destructor, unflushed streams); #1934 to #1940 (uninitialized local). Check: does it still work trimmed, with two devices, and looping?
9. **Capture-time data used against the replay device.** #3186, #3109, #2666, #1820, #2025, #3174, #3195, #3199. Fix: re-query the replay device; key by capture IDs. Check: does any index, address, mask or limit reaching a replay call come from the file?
10. **Codegen refactor verified only by diffing output.** #1994, #1950 to #2036, #2009 to #2195, #2654 to #2823 (duplicate mapping), #1906 (decoder object passed as `void*`), #2160 to #2172. Check: name one generated function whose output should change and confirm it did; regenerate every API.
11. **Capture-stream content changed for existing captures or references.** #2581 to #2605 (new struct encoding crashed old captures); #2934 (in-place layout change failed extended CI); #2740 reverted by #2771, relanded as #2775 (recorded `AddRef`/`Release` shifted reference block indices); #2466 (field meaning repurposed in an existing block). Check: decode a capture made before your change; if the recorded stream changes, update CI references in the same PR.
12. **Symptom patched, not the cause.** #2799 to #2831 (an Android "driver bug" barrier skip was GFXR using member `device_table_` instead of the `device_table` argument); #1961 to #1967 (stride from the wrong format); #2012 to #2039 (sRGB flip-flop); #1922 to #2366 (override nulled a pointer the generated encoder had already read); #2623 to #2661 (disabled rather than fixed). Check: can you explain why it crashed, not only why your change stops it?
13. **Dropping or altering the application's call.** #3162 to #3205 (`vkFrameBoundaryANDROID`, not in a registered extension, silently dropped; the helper now returns `bool` and the call is forwarded); #2572 ("silent no-ops oppose the RECONSTRUCT principle"); #3203 (failed `TryCreate*` must still be captured); #2455 (forcing a NULL pNext rejected); #2366 (a `const_cast` write into the app's struct, which reviewers now reject, #3207). Check: on every early return, is the application's call still forwarded and recorded?
14. **Code newer than the oldest toolchain.** `std::format` (#2451 to #2647, #2718, #2728, #2761, #2793); `hardware_destructive_interference_size` missing in the NDK (#2783); float `to_chars` on older macOS (#2389); MSVC x86 C4293 (#2811); designated initializers before C++20 (#2051). Check: does it build with the oldest compiler and standard library in CI?
15. **Hook or override placed where it does not run.** `OverrideQueueBindSparse` missing from `capture_overrides.json` (#3267); `vkBeginCommandBuffer` hooks inside `#if ENABLE_OPENXR_SUPPORT` (#2231, #2366; #2524 placed its specializations outside the guard); a generator target-list typo that stopped a file regenerating (#2084); a venv change in scripts CI no longer called (#2722 to #2753); a root `add_link_options` that never reached Android (#2961). Check: prove the new code runs in the configuration you care about, with a log line or a failing test.

## 9. What reviewers push back on, ranked

Generalizable reviewer corrections from `stats.json` (LunarG-authored PRs / external-authored PRs), with one gist each. External PRs draw behavior-preservation and error-handling objections at about twice the LunarG rate; LunarG PRs draw structure objections.

| # | Category | LunarG | External | Gist |
|---|---|---|---|---|
| 1 | Style idiom | 105 | 6 | "prefer `GFXRECON_ASSERT` over `assert` within the framework" (#2083); "One line per variable definition." (#2520) |
| 2 | Correctness | 79 | 22 | "`prim_it` is invalidated by `erase`/`insert`" (#2412) |
| 3 | Placement / reuse | 87 | 8 | "Use `vulkan_struct_get_pnext` instead of hand-walking the pNext chain" (#1820); "do not dirty the feature interface" (#2751) |
| 4 | Behavior preservation | 51 | 20 | "Any addition to output has to be behind an option" (#2134); "It's important that existing captures continue to work" (#2934) |
| 5 | Naming | 53 | 6 | "Don't put the representation ('tuple') in type or variable names" (#2395) |
| 6 | Lifetime / ownership | 46 | 5 | Hold a value "so that there's never a question of use-after-free" (#2922) |
| 7 | Explain for maintainers | 46 | 4 | "some comments would be greatly helpful" (#2330); "include your answer as a comment in that function" (#3271) |
| 8 | Scope / split | 41 | 7 | Split so it can "land as a unique commit in dev" (#2563) |
| 9 | API validity | 41 | 7 | STRING `pValues` "is an array of pointers to C strings, not a byte array" (#2581); "The mapping is not 1-to-1, but 1-to-many" (#3193) |
| 10 | Error handling | 34 | 11 | "Need to check `VkResult`" (#2936) |

Next: missing test 32 / 8 (#2261), docs-usage 29 / 3 (#2315), logging 26 / 10 (#2713), const and raw pointers 21 / 3 (#2081), hot-path performance 20 / 7 (#2549), platform ifdef 16 / 4 (#2206), thread safety 12 / 0 (#3172).

## Appendix A. Exemplar PRs by area

**build-ci-deps**
- #1823: validate a cached `find_library` result, then `unset(CACHE)` before re-finding.
- #2506 (with #2560): measured rebuild-time problem statement; a frequently changing value isolated in one translation unit; declare the library type.
- #2878: CMake workaround scoped by compiler ID, other compilers untouched, quirk explained in a comment.
- #3133: subproject-safe CMake paths (`CMAKE_CURRENT_LIST_DIR` captured at include time, `$<TARGET_FILE:...>`).
- #3340: pin a toolchain dependency in every workflow ahead of an announced upstream change.

**capture-core**
- #3308: null-safe span probe in `util::platform`, defaulted parameter on a frozen function, both Khronos APIs regenerated, OS-level unit test.
- #2294: full capture-setting plumbing including the manifest `LIST`, all USAGE files, test app, test case and known-good.
- #3106: enum-string option parser, precedence rules, manifest `ENUM`.
- #2830: description with problem, rejected alternatives and rationale; symmetric add/remove of an index map.
- #2599: exported C control API, no-op callback substitution, mutex for paired state.

**capture-vulkan**
- #3259: how to remove an invalid assert: problem, superseded attempt, replay impact, test procedure, spec-backed comment.
- #2896: minimal tracking recipe: alias specializations, success and track-mode gate, `Track*`.
- #2524: covers every signal and clear source of a tracked state; atomic flag.
- #3197: scoped per-container mutex, cache cleanup on device destroy, `try_emplace`.
- #2265: `std::variant` tracking so trim re-emits the exact entry point the app used; known-good updated.

**codegen**
- #2102: shared predicate in the Khronos base replaces per-API name lists; headless test app.
- #3111: special-case a type in `make_decode_invocation` with an encode/decode round-trip test in `framework/test`.
- #3265: registry-driven generator change with backward-compatible decode and tests for old captures.
- #2455: registry derivation, a helper call and a pointer probe instead of enforcing spec behavior.
- #2885: header bump with the hand edits listed and a commented compatibility kludge (removed in #2919).

**d3d12**
- #2383: trim ordering fixed by splitting `StandardCreateWrite` into dependency-ordered passes.
- #1885: generator plumbing first, then a templated `PostCall_*` with mapping only on mutual success.
- #2187: capture override chosen over a post-call hook; hides capture-injected flags; sibling API added on request.
- #2496: deep-copy-then-repoint unwrapping; type-dispatched mapping with an explicit empty branch.
- #3157: Problem/Solution/Performance/Testing description for a four-line fix.

**dump-resources**
- #2328: sentinel, fallback and log-and-fail shape in a small fix; three reviewer improvements accepted.
- #2216: alias families collapsed into one `Handle*` plus a `DrawCallType` tag.
- #2419: a bool replaced by a status enum carried through records into the JSON.
- #2791: RAII `TemporaryFence`/`TemporaryCommandBuffer` with bounded waits and logged results.
- #3044: adapter moved to `framework/graphics`, conditional deep copy, round-trip unit test.

**format-decode**
- #3257: cross-tool behavior change: shared helper, sentinel instead of assert, every caller updated.
- #2574: callable owned by value; minimal Catch2 regression test.
- #2415: a narrow virtual hook replacing a duplicated loop.
- #2348: backward-compatible format extension: reserved range, reader skips with warn-once.
- #2540: header update split into submodule, regenerate and known-good commits, with a description.

**openxr**
- #2147: manual-override recipe end to end, plus dispatch-table stubs.
- #2172: CI regeneration check and fixing the generator, not the output.
- #2218: wiring a Vulkan-side facility into OpenXR on both desktop and Android mains.
- #2991: minimal decoder `Dispatch*` fan-out mirroring Vulkan.
- #2162: a comment explaining why a guard exists; FATAL plus assert on an unsupported configuration.

**replay-vulkan**
- #3235: bounded replay waits; problem-and-rationale description; alias entries; allow-listed result pairs.
- #2827: rule-of-five ownership, per-device executor split from per-call execution.
- #2785: the smallest complete opt-in option: `Maybe*` helper, alias mapping, injected bracketing, docs.
- #2196: one teardown helper for replay-internal objects called from both the destroy override and the destructor.
- #3036: crash fix on an external PR: bind points from `stageFlags`, per-bind-point guard, move kept in its own commit.

**tests**
- #3300: generator fix plus a unit test reproducing UB at an odd offset, as separate commits; sanitizer output in the description.
- #2109: `verify_gfxr` extended with a defaulted parameter, RAII env scoping, `#error` fallback.
- #2157: capture fix shipped with app, test case and known-good, untrimmed behavior unchanged.
- #3058 (assertions from #3299): option plumbing plus a test app registered in all desktop and Android lists.
- #2245: a platform-only path made testable on desktop with a minimal mock and `clean_gfxr_json` handling.

**tools-cli-json**
- #2858: template feature-module pattern and the interface-hygiene points.
- #3140: external PR adding a missing alias through a shared template helper, excluding `IsStateLoading()` submits.
- #3236: format-struct field fixed without changing on-disk layout; `static_assert` platform guard.
- #3218: additive plugin ABI change with version negotiation, doc update and unit test.
- #2407: full replay-option recipe with an enum in `options.h`, `gfxrecon.py` `choices` and a capability check before overriding.

**util-platform**
- #3292: separate flush and close error reports with `errno`; commit messages that explain the problem first.
- #2645: platform wrapper keeps the frozen signature while switching to wide APIs; OS limit documented at the call.
- #3039: CMake option to compile definition to `#if defined` gating of platform capabilities.
- #2888: small virtual-hook fix with the C++ lifetime reason in a comment.
- #2601: util algorithm fix with an extended Catch2 test asserting exact offsets.

## Appendix B. Practiced vs documented: open items

Items where practice and a document disagree and the evidence does not meet the bar to state either side (fewer than three PRs or two LunarG authors, or a reviewer objected and was overruled). Each needs a maintainer decision.

1. **Frozen Consumer and `framework/util` signatures (Principles INTERFACE).** In-tree virtuals were renamed or changed in #1955, #2585, #2934, #3005, #2466, #2415, #2449, #2858, #3007; util signatures in #2134, #2848, #2646 (`Log::kXSeverity` to `LoggingSeverity`), #2986 (`GFXRECON_LOG_FATAL_ONCE` deleted), #2729, #3155 (`gpu_va_map.h` deleted but still listed). No reviewer objected; only the plugin ABI is defended (#3218). Decide which interfaces are frozen; until then, call out signature changes in the description.
2. **Default dump-resources output changes.** Filenames, JSON layout and which buffers are dumped changed by default in #1905, #1952, #2226, #2238, #2260, #2489, #2726, #2951, almost all by one author and unchallenged, while reviewers enforced opt-in for added output (#2398, #3247). Declare VDR output unstable in `vulkan_dump_resources.md` or apply the opt-in rule.
3. **Deleting options (INVOCATION).** #2463 removed about 15 `--dump-resources-*` flags after polling users in the thread; #3151 removed the `LZ4_OPTIONAL` CMake option. Decide when deprecation is allowed and whether removed flags need a non-fatal message.
4. **Abort on an option replay cannot honor.** INVOCATION says disable "with a non-fatal message"; #2604 and #2736 abort at a reviewer's request because running with different options "can generate wrong data". The doc should distinguish "cannot honor" from "deprecated".
5. **Defaults changed as bug fixes.** 0-based `--frame-range` (#2587, a reviewer objected and was overruled), fatal `--measurement-range` (#2800), messenger severity (#2188), info's instance choice (#2451), capture env-var default (#2294, privacy), compile-required passthrough (#2620, PRESERVE). State the default change and the overriding principle in the description.
6. **Generator hierarchy in AGENTS.md.** It describes `base_generators/ -> khronos_generators/`; `framework/generated/base_generators/` has no tracked files on dev (moved in #1898, DX12 split in #1827). Fix the doc.
7. **Replay option plumbing in older PRs.** Contributors copying pre-#3216 PRs will edit `kOptions`/`PrintUsage` for a Vulkan-only option that now belongs in `GetOptionDescs()`. Add the §2.1 file set to AGENTS.md.
8. **Test documentation.** `TESTING_test_apps.md` omits the Android registration, `GFXRECON_TESTCASES` and the known-good step (#2180 asked for it); AGENTS.md says to run `./gfxrecon-testapp-runner` from `install/test`, while the loader and Mock ICD environment comes from `test/run-tests.sh` (#1886, #2155); "each test captures and replays" describes a minority of cases (#3016, #3299).
9. **External suite as the accepted test.** Reviewers accepted `test_suite.ref` bumps as satisfying the test requirement (#2398, #2412, #2553, #3188); the `test_suite.ref`/`test.ref` workflow is documented nowhere in the repo.
10. **`test/icd/generated` hand edits.** #2739 edited `test/icd/generated/function_definitions.h` directly; no in-repo generator exists for it. Say whether the generated-file rule applies.
11. **PRESERVE and extra extensions.** Capture enables extra device extensions on every `vkCreateDevice` (#3020) and strips the frame-boundary struct (#1902, #2320); replay force-enables `timelineSemaphore` (#3083) and fakes benign extensions by default (#2272). No reviewer cited the principle except in #1902.
12. **Replay-added synchronization in user-requested modes.** Frame looping, dump resources and the virtual swapchain add waits, submits and copies (#2936, #3188, #3260, #3066, #2869), accepted because the mode is user-requested or needed for same-environment replay. Document the exception.
13. **`CHECK_VK_RESULT`.** Exists only in `framework/decode/vulkan_replay_frame_loop_consumer.cpp` and calls `std::exit(-1)`; other code returns the `VkResult` or asserts and logs (#2305, #3186). Promote one shared helper or keep it file-local.
14. **Construct-in-constructor.** One reviewer enforced full construction (#3256); sibling `Temporary*` types still use `Create()` returning `VkResult`. Decide how constructors report Vulkan failure.
15. **Allocation at map/unmap time in capture.** #2830 adds per-page vector entries for write-watch allocations; PERFORMANCE does not say whether allocation outside the per-call path is tolerated.
16. **Compile-time versus runtime platform checks.** `VK_USE_PLATFORM_ANDROID_KHR` (#3162) versus `GetWsiExtension()` at runtime (#2799). Suggested: runtime for WSI behavior, compile-time for OS facilities.
17. **32-bit support.** Jenkins 32-bit removed but kept as a manual switch (#2955); 32-bit Android off by default (#2307); 32-bit Linux still in extended CI (#3039). No doc states the support level.
18. **Commit granularity.** CONTRIBUTING.md says "more is better" and also "squash unnecessary commits"; reviewers ask to squash (#1844, #2410, #3033) and 63% of PRs are squash-merged.
19. **JSON key case outside convert.** Dump-resources JSON is lowerCamelCase (#2419, #1883); metacommand JSON args in convert are snake_case to match `format.h` (#1932). No doc covers VDR JSON stability or case.
20. **`layer/vk_layer_settings.txt`.** About 24 settings appear there; most new settings skip it (#2294) while #3106 added one. Full list or sample?
21. **snake_case free helpers.** `vulkan_struct_add_pnext` and friends are snake_case against the style guide CONTRIBUTING.md points to (#2434, #2835); reviewers objected to `_t` types (#1955, #2261) but never to the helpers. New helpers in an existing snake_case family match that family.
22. **Reviewer-less CI merges.** Several CI PRs merged with no reviewer (#2639, #2641, #2656, #2742, #2776, #2790) before CODEOWNERS covered `/ci/` (#2860). Do `ci/` changes need review?

## Appendix C. How this guide was produced and how to regenerate it

The pipeline lives at `D:/gfxr-style-distill` (stdlib Python, run with `python -I`; `gh` authenticated; `D:/gfxreconstruct` with `origin/dev` fetched). As-of commit `280a0ac62841fa6020d9f56cb8e1d96705c71848` (2026-10-14).

1. `fetch_prs.py` fetched every PR merged since 2024-10-19 (#1167 to #3343) and, with `--unmerged`, 67 reviewed-but-unmerged PRs used only as a failure signal. Each PR has `raw/prs/<N>/` with the GraphQL record, full diff, a non-generated diff, generated-file stats and a review bundle.
2. `triage.py` built `prs.jsonl` (1,017 rows; 921 merged PRs summarized: 743 by LunarG authors, 178 external), area and batch assignments, and the holdout. Holdout: 15 PRs merged after 2026-06-01 (`holdout.json`) plus 10 reserves, excluded from every chapter and from this guide; they are kept for validation, where an agent following the guide re-implements each held-out change and a judge compares it with the merged PR.
3. Map agents summarized each batch into `summaries/<N>.json` against `summary.schema.json` (idioms, design moves, reviewer corrections with category and outcome, corner cases, API knowledge, testing kind, practiced-vs-documented notes). `lint_summaries.py` requeued invalid summaries; `aggregate.py` produced `agg/themes/*.jsonl`, `agg/area/*.json`, `agg/failures.jsonl` and `stats.json`.
4. Area agents (12 areas, some in dated slices), theme agents (14 themes) and a failures agent wrote `chapters/*.md`, each rule with its PRs and reviewer quotes, snippets verified against the patches, and notes on single-author or thin evidence.
5. The first draft merged the chapters under `editor.md`: link, do not restate the docs; every rule cites PRs; snippets unchanged and at most ten lines; no names; practice stated over the doc only with three or more PRs by two or more LunarG authors and no reviewer objection, the doc stated when a reviewer objected, and everything else in Appendix B. A critic pass (`critic.md`) then cut generic and doc-duplicating text, downgraded single-author claims, and re-checked names against the chapters and `origin/dev`.
6. `python -I refcheck.py` on this file verified that every cited PR exists in the index and is not held out, every backticked path exists on `origin/dev`, and every snippet appears in the patch of a PR cited within the twelve lines before it. The holdout validation follows.

To refresh: re-run `fetch_prs.py` (new PRs only) and `triage.py` (the holdout is kept unless `--reselect-holdout`), map only the new batches, then re-run the reduce, synthesis, critic and refcheck stages.
