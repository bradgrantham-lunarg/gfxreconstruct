#gen_feature_db.py
#Author: Nick Driscoll
#Intended to be executed from the root of the GFXR repo

import sys
import os
import subprocess
import platform
import json
import json_stream
import xml.etree.ElementTree as ET
from io import StringIO

script_name = os.path.basename(__file__)

#Returns true if running on Windows, false otherwise
def is_windows():
    '''
    Check if the system is Windows
    '''
    return 'windows' == platform.system().lower()

# Print usage instructions
def usage():
    print("Usage for %s will go here." % script_name)
    #print("gfxrecon-optimize-renamed.py - Helper script to perform automatic renaming of gfxrecon-optimize.exe prior to optimization.")
    #print()
    #print("Usage:")
    #print("  gfxrecon-optimize-renamed.py <input-file> <output-file> [--dxr] [--d3d12-pso-removal]")
    #print()
    #print("Required arguments:")
    #print("  <input-file>          The path to input GFXReconstruct capture file to be processed.")
    #print("  <output-file>         The path to output GFXReconstruct capture file to be created.")    
    #print()
    #print("Optional arguments:")    
    #print("  --d3d12-pso-removal   D3D12-only: Remove creation of unreferenced PSOs.")
    #print("  --dxr                 D3D12-only: Optimize for DXR replay.")
    #print("  --gpu                 D3D12-only: Use the specified device for the optimizer replay, where index is the zero-based index")
    #print("                        to the array of physical devices returned by vkEnumeratePhysicalDevices or The optimizer replay")
    #print("                        may fail if the specified device is not compatible with theIDXGIFactory1::EnumAdapters1.")
    #print("                        The optimizer replay may fail if the specified device is not compatible with the original")
    #print("                        capture devices.")
    #print()    
    #print("Note: running without optional arguments will instruct the optimizer to detect API and run all available optimizations.")
    #print()
    #print("Example manual usage (D3D12):")
    #print("  gfxrecon-optimize-renamed.py my_capture.gfxr my_capture_dxr_optimized.gfxr --dxr")
    #print("  gfxrecon-optimize-renamed.py my_capture.gfxr my_capture_pso_optimized.gfxr --d3d12-pso-removal")
    #print()
    #print("Example automatic usage (D3D12 + Vulkan):")
    #print("  gfxrecon-optimize-renamed.py my_capture.gfxr my_capture_optimized.gfxr") 
    #print()

def load_json(path):
    f = open(path)
    return json_stream.load(f)

#Return a list of all sTypes inside a block
def gather_stypes(block):
    #for entry in block:

    print("ugh")

if __name__ == "__main__":

    #Early exit if we're missing our arguments
    if len(sys.argv) < 2:
        usage()
        print("Error: missing path to capture directory")
        exit(-1)
    capture_dir = sys.argv[1]

    #Parse vk.xml
    vk_xml_path = "external/Vulkan-Headers/registry/vk.xml"
    vk_xml_root = ET.parse(vk_xml_path).getroot()
    commands_root = vk_xml_root.find("commands")

    #Collect the names of all vk functions
    print("Collecting vk function names...")
    all_vk_funcs = []
    alias_map = {}
    for c in commands_root:
        if "alias" not in c.attrib:
            fn_name = c.find("proto").find("name").text
            all_vk_funcs.append(fn_name)
        else:
            alias_map[c.attrib["name"]] = c.attrib["alias"]

    #Get name of gfxrecon-convert binary
    GFXR_CONVERT_NAME = "gfxrecon-convert"
    if is_windows():
        GFXR_CONVERT_NAME += ".exe"
    convert_tool_path = os.path.join(os.path.dirname(__file__), GFXR_CONVERT_NAME)

    #Variables to collect all vk functions across all traces
    capture_funcs = set()

    #We expect to find commit-suite.json and extended-suite.json
    #suite_jsons = ["commit-suite.json", "extended-suite.json"]
    suite_jsons = ["commit-suite.json"]
    convert_processes = []
    trace_paths = []
    json_paths = []
    for suite_json in suite_jsons:
        full_suite_json_path = capture_dir + "/" + suite_json
        suite = load_json(full_suite_json_path)

        for trace in suite["traces"].persistent():
            #Skip non-vulkan traces
            if "api" in trace and trace["api"] != "vulkan":
                continue

            trace_dir = capture_dir + "/" + trace["directory"]
            trace_paths.append(trace_dir)
            for file in os.listdir(trace_dir):
                filename = os.fsdecode(file)
                if not filename.endswith(".gfxr"):
                    continue

                print("Launching gfxr-convert on %s..." % filename)
                full_trace_path = trace_dir + "/" + filename
                #out_json_path = "/tmp/" + os.path.splitext(os.path.basename(full_trace_path))[0] + ".json"
                out_json_path = os.path.splitext(full_trace_path)[0] + ".json"
                json_paths.append(out_json_path)
                cmd = [convert_tool_path, "--output", out_json_path, full_trace_path]
                p = subprocess.Popen(cmd)
                convert_processes.append(p)

    #Wait for the conversions to complete
    print("Waiting for convert jobs to finish...")
    for p in convert_processes:
        p.wait()

    print("Processing json captures...")
    for (json_path, trace_path) in zip(json_paths, trace_paths):
        features = {}
        print("Analyzing %s..." % json_path)
        capture_json = load_json(json_path)

        #Iterate over all blocks and extract the function names
        for block in capture_json.persistent():
            if "function" in block:
                if "functions" not in features:
                    features["functions"] = set()

                #Grab function's name
                fn_name = block["function"]["name"]
                if fn_name in alias_map:
                    features["functions"].add(alias_map[fn_name])
                    capture_funcs.add(alias_map[fn_name])
                else:
                    features["functions"].add(fn_name)
                    capture_funcs.add(fn_name)

                #Grab the sType
                if "sTypes" not in features:
                    features["sTypes"] = set()
                #features["sTypes"].add()

                #Special casing vkCreateInstance and vkCreateDevice as we want to extract
                #pEnabledFeatures from vkCreateDevice ppEnabledExtensionNames from both
                #I'm also assuming that there will be exactly one call to each function
                if fn_name == "vkCreateInstance":
                    features["instance_ppEnabledExtensionNames"] = block["function"]["args"]["pCreateInfo"]["ppEnabledExtensionNames"]
                elif fn_name == "vkCreateDevice":
                    features["device_ppEnabledExtensionNames"] = block["function"]["args"]["pCreateInfo"]["ppEnabledExtensionNames"]
                    features["pEnabledFeatures"] = block["function"]["args"]["pCreateInfo"]["pEnabledFeatures"]

            elif "meta" in block:
                if "metas" not in features:
                    features["metas"] = set()
                features["metas"].add(block["meta"]["name"])

        #Convert sets into lists
        features["functions"] = sorted(features["functions"])
        features["metas"] = sorted(features["metas"])
        features["sTypes"] = sorted(features["sTypes"])

        #No guarantee that the following fields have anything in them
        if features["instance_ppEnabledExtensionNames"] is not None:
            features["instance_ppEnabledExtensionNames"] = sorted(features["instance_ppEnabledExtensionNames"])
        if features["device_ppEnabledExtensionNames"] is not None:
            features["device_ppEnabledExtensionNames"] = sorted(features["device_ppEnabledExtensionNames"])
        if features["pEnabledFeatures"] is not None:
            features["pEnabledFeatures"] = sorted(features["pEnabledFeatures"])

        raw_out = json.dumps(features, indent=4)
        db_path = trace_path + "/" + os.path.splitext(os.path.basename(json_path))[0] + ".db.json"
        print("Writing db to %s" % db_path)
        f = open(db_path, "w")
        f.write(raw_out)
        f.close()


    #Report the results
    missingno = 0
    for fn_name in all_vk_funcs:
        if fn_name not in capture_funcs and fn_name not in alias_map:
            #print("Missing coverage for %s" % fn_name)
            missingno += 1

    print("Coverage rate: %f%%" % (100.0 * (1.0 - missingno / len(all_vk_funcs))))

    print("Done!")