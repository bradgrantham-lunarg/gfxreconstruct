#!/usr/bin/env python3

import math
import json
import sys

# dictionary by HandleID of a (dictionary of "allocation" function and "commands" containing all the decoded JSON vkCmd functions)
commandbuffers = {}

frame = 0 # GFXR's frame number minus 1
# This is a list of lists of vkCmd functions
frame_enqueued = {}

functions_not_handled = set()
commands_not_handled = set()

summary = {}

frame_number = 1

for line in sys.stdin:
    try:
        record = json.loads(line)
    except:
        print("line: '%s'" % line)
        raise

    index = record.get("index", -1)

    try:
        if "header" in record:
            summary["header"] = record["header"]

        elif "meta" in record:
            meta = record["meta"]
            if meta["name"] == "ExeFileInfo":
                summary["ExeFileInfo"] = meta["args"]

        elif "annotation" in record:
            annotation = record["annotation"]
            if annotation["type"] == "kJson" and annotation["label"] == "operation":
                operation = json.loads(annotation["data"])
                if "tool" in operation:
                    summary["tool"] = operation

        elif "frame" in record:
            frame = record["frame"]
            if frame["marker_type"] == "EndMarker":
                frame_number = int(frame["frame_number"]) + 1

        # XXX DX12?
        elif "function" in record:
            function = record["function"]
            args = function.get("args", {})
            returned = function.get("return", None)
            name = function["name"]

            if name == "vkAllocateCommandBuffers" and returned == "VK_SUCCESS":
                for commandbuffer in args["pCommandBuffers"]:
                    commandbufferID = commandbuffer
                    commandbuffers[commandbufferID] = {"allocation" : function, "handleid": commandbufferID, "commands": []}

            elif name == "vkBeginCommandBuffer" and returned == "VK_SUCCESS":
                commandbufferID = args["commandBuffer"]
                commandbuffers[commandbufferID]["commands"] = []

            elif "vkCmd" == name[:5]:
                commandbufferID = args["commandBuffer"]
                commandbuffers[commandbufferID]["commands"].append(function)

            elif name == "vkEndCommandBuffer" and returned == "VK_SUCCESS":
                pass

            elif name == "vkQueueSubmit" and returned == "VK_SUCCESS":
                try:
                    for s in args["pSubmits"]:
                        command_buffers = {}
                        for commandbufferID in s["pCommandBuffers"]:
                            commands = []
                            commandbuffer = commandbuffers[commandbufferID]
                            for command in commandbuffer["commands"]:
                                commands.append(command)
                            command_buffers[commandbufferID] = commands
                    function["command_buffer_contents"] = command_buffers
                    frame_enqueued.setdefault(frame_number, []).append(function)
                except:
                    print(s)
                    raise

            # elif name in ("vkQueueSubmit2", "vkQueueSubmit2KHR"):
                # for s in args["pSubmits"]:
                    # for i in s["pCommandBufferInfos"]:
                        # commandbufferID = i["commandBuffer"]
                        # commandbuffer = commandbuffers[commandbufferID]
                        # copy = {"command": name, "allocation" : commandbuffer["allocation"], "handleid" : commandbuffer["handleid"], "commands" : commandbuffer["commands"]}
                    # frame_enqueued.setdefault(frame_number, []).append(copy)

            elif name == "vkQueuePresentKHR":
                copy = {"command": "QueuePresent", "args": args}
                frame_enqueued.setdefault(frame_number, []).append(function)

            else:
                functions_not_handled.add(name)
    except:
        print("index: %d" % index)
        raise

html_header = """
<html>
<body>
"""

html_footer = """
</body>
</html>
"""

html = ""

html += "<em>Summary</em><br>\n"
html += "<table>\n"
html += "<tr><td>File</td><td>%s</td></tr>\n" % summary["header"]["source-path"]
if summary["ExeFileInfo"]:
    html += "<tr><td>Executable filename</td><td>%s</td></tr>\n" % summary["ExeFileInfo"]["app_name"]
if summary["tool"]:
    html += "<tr><td>Captured</td><td>%s</td></tr>\n" % summary["tool"]["timestamp"]
    html += "<tr><td>Using GFXR</td><td>%s</td></tr>\n" % summary["tool"]["gfxrecon-version"]
    html += "<tr><td>On Vulkan</td><td>%s</td></tr>\n" % summary["tool"]["vulkan-version"]
html += "</table>\n"
html += "<hr>\n"

for (frame_number, enqueueds) in frame_enqueued.items():
    html += "frame %d<br>\n" % (frame_number)
    html += '<div style="text-indent:1em;margin:0">\n'
    for enqueued in enqueueds:
        html += "<p>%s</p>\n" % enqueued["name"]
        if enqueued["name"] == "vkQueueSubmit":
            html += '<div style="text-indent:2em;margin:0">\n'
            for submit in enqueued["args"]["pSubmits"]:
                for commandbufferID in submit["pCommandBuffers"]:
                    html += "<p>Command buffer %s</p>\n" % commandbufferID
                    html += '<div style="text-indent:3em;margin:0">\n'
                    for command in enqueued["command_buffer_contents"][commandbufferID]:
                        name = command["name"]
                        html += "<p>%s</p>\n" % name
                html += '</div>\n'
            html += '</div>\n'
    html += '</div>\n'

print(html_header + html + html_footer)
