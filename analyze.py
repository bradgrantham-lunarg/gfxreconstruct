#!/usr/bin/env python3

import math
import json
import sys
import os

# dictionary by HandleID of a (dictionary of "allocation" function and "commands" containing all the decoded JSON vkCmd functions)
commandbuffers = {}

frame = 0 # GFXR's frame number minus 1
# This is a list of lists of vkCmd functions
frame_enqueued = {}

functions_not_handled = set()

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
<head>
<title>Capture Analysis for %s</title>

<style>
    .collapsible {
        background-color: #777;
        color: white;
        cursor: pointer;
        padding: 18px;
        blarg-width: 40%%;
        indent: 1em;
        border: none;
        text-align: left;
        outline: none;
        font-size: 15px;
    }

    .active, .collapsible:hover {
        background-color: #555;
    }

    .content {
        padding: 0 18px;
        display: none;
        overflow: hidden;
        background-color: #f1f1f1;
    }
</style>
<body>
""" % os.path.basename(summary["header"]["source-path"])

html_footer = """
<script>
    var coll = document.getElementsByClassName("collapsible");
    var i;

    for (i = 0; i < coll.length; i++) {
        coll[i].style.display = "block";
        coll[i].nextElementSibling.style.display = "none";
        coll[i].addEventListener("click", function() {
            this.classList.toggle("active");
            var content = this.nextElementSibling;
            if (content.style.display === "block") {
                content.style.display = "none";
            } else {
                content.style.display = "block";
            }
        });
    }

    var expand_button = document.getElementsByClassName("expand-button")[0];
    var collapse_button = document.getElementsByClassName("collapse-button")[0];
    expand_button.addEventListener("click", function() {
        var coll = document.getElementsByClassName("collapsible");
        for (var i = 0; i < coll.length; i++) {
            coll[i].nextElementSibling.style.display = "block";
        }
    });
    collapse_button.addEventListener("click", function() {
        var coll = document.getElementsByClassName("collapsible");
        for (var i = 0; i < coll.length; i++) {
            coll[i].nextElementSibling.style.display = "none";
        }
    });
</script>
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

def json_to_nested_tables(j):
    h = "<table>\n"
    if isinstance(j, dict):
        for (key, value) in j.items():
            h += "<tr><td>%s</td><td>\n" % key
            h += json_to_nested_tables(value)
            h += "</td></tr>\n"
    elif isinstance(j, list):
        for value in j:
            h += "<tr><td>\n"
            h += json_to_nested_tables(value)
            h += "</td></tr>\n"
    else:
        h += "<tr><td>%s</td></tr>\n" % j
    h += "</table>\n"
    return h

for (frame_number, enqueueds) in frame_enqueued.items():
    html += '<button type="button" class="collapsible">frame %d (%d enqueues)</button>\n' % (frame_number, len(enqueueds))
    html += '<table>\n'
    for enqueued in enqueueds:
        html += '<tr><td>\n'
        if enqueued["name"] == "vkQueueSubmit":
            if len(enqueued["args"]["pSubmits"]) == 1 and len(enqueued["args"]["pSubmits"][0]["pCommandBuffers"]) == 1:
                submit = enqueued["args"]["pSubmits"][0]
                commandbufferID = submit["pCommandBuffers"][0]
                html += '<button type="button" class="collapsible">%s (1 submission, command buffer %s)</button>\n' % (enqueued["name"], commandbufferID)
                html += '<table>\n'
                for command in enqueued["command_buffer_contents"][commandbufferID]:
                    html += '<tr><td>'
                    name = command["name"]
                    html += '<button type="button" class="collapsible">%s</button>\n' % name
                    html += json_to_nested_tables(command["args"])
                    html += '</td></tr>\n'
                html += '</table>\n'
            else:
                html += '<button type="button" class="collapsible">%s (%d submissions)</button>\n' % (enqueued["name"], len(enqueued["args"]["pSubmits"]))
                html += '<table>\n'
                for submit in enqueued["args"]["pSubmits"]:
                    html += '<tr><td>\n'
                    for commandbufferID in submit["pCommandBuffers"]:
                        html += '<button type="button" class="collapsible">Command buffer %s</button>\n' % commandbufferID
                        html += '<table>\n'
                        for command in enqueued["command_buffer_contents"][commandbufferID]:
                            html += '<tr><td>'
                            name = command["name"]
                            html += '<button type="button" class="collapsible">%s</button>\n' % name
                            html += json_to_nested_tables(command["args"])
                            html += '</td></tr>\n'
                        html += '</table>\n'
                    html += '</td></tr>\n'
                html += '</table>\n'
        else:
            # script doesn't have special processing for whatever this is
            html += '<button type="button" class="collapsible">%s</button>\n' % enqueued["name"]
            html += json_to_nested_tables(command["args"])
        html += '</td></tr>\n'
    html += '</table>\n'

print(html_header + html + html_footer)
