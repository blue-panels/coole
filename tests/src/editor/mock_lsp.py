#!/usr/bin/env python3
# A language server that only pretends, for the tests of the client: it keeps the texts it is
# sent, says a problem for each, and answers a hover with the text it has of the document, which
# shows whether the text came before the request.  A definition is the start of the second
# line of the document, a completion a few words, enough to try the editor with.
#
#   mock_lsp.py LOG [MODE]
#
# Every message it gets is written to the file LOG, a JSON line each.  MODE:
#   normal     (the default)
#   utf8       positions counted in UTF-8
#   crash      ends after initialized
#   garbage    writes a broken header after initialized
#   hang       neither answers shutdown nor ends on exit
#   split      each message written a byte at a time
#   deep       answers a hover with JSON nested too deep

import json
import sys
import time

log = open(sys.argv[1], "w")
mode = sys.argv[2] if len(sys.argv) > 2 else "normal"
rfile = sys.stdin.buffer
wfile = sys.stdout.buffer
texts = {}


def write_raw(data):
    if mode == "split":
        for i in range(len(data)):
            wfile.write(data[i:i + 1])
            wfile.flush()
    else:
        wfile.write(data)
        wfile.flush()


def write(message):
    message["jsonrpc"] = "2.0"
    body = json.dumps(message).encode()
    write_raw(b"Content-Length: %d\r\n\r\n" % len(body) + body)


def read():
    length = None
    while True:
        line = rfile.readline()
        if not line:
            return None
        line = line.strip()
        if not line:
            break
        name, _, value = line.partition(b":")
        if name.lower() == b"content-length":
            length = int(value)
    return json.loads(rfile.read(length))


def diagnose(uri, version):
    text = texts.get(uri, "")
    write({"method": "textDocument/publishDiagnostics",
           "params": {"uri": uri, "version": version,
                      "diagnostics": [{"range": {"start": {"line": 0, "character": 0},
                                                 "end": {"line": 0, "character": 1}},
                                       "severity": 1, "message": "length %d" % len(text)}]}})


while True:
    message = read()
    if message is None:
        # one that hangs has to be killed
        while mode == "hang":
            time.sleep(1)
        break
    log.write(json.dumps(message) + "\n")
    log.flush()
    method = message.get("method")
    params = message.get("params") or {}
    if method == "initialize":
        capabilities = {"textDocumentSync": {"openClose": True, "change": 1,
                                             "save": {"includeText": True}},
                        "hoverProvider": True, "definitionProvider": True,
                        "completionProvider": {"triggerCharacters": [".", "->"]}}
        if mode == "utf8":
            capabilities["positionEncoding"] = "utf-8"
        write({"id": message["id"], "result": {"capabilities": capabilities}})
    elif method == "initialized":
        if mode == "crash":
            sys.exit(3)
        if mode == "garbage":
            write_raw(b"Content-Length: x\r\n\r\n{}")
        write({"id": "cfg", "method": "workspace/configuration",
               "params": {"items": [{"section": "a.b"}, {"section": "missing"}, {}]}})
        write({"id": 7, "method": "no/such/method", "params": {}})
        write({"method": "window/showMessage", "params": {"type": 1, "message": "hello\x1b[31m"}})
    elif method == "textDocument/didOpen":
        document = params["textDocument"]
        texts[document["uri"]] = document["text"]
        diagnose(document["uri"], document["version"])
    elif method == "textDocument/didChange":
        document = params["textDocument"]
        texts[document["uri"]] = params["contentChanges"][-1]["text"]
        diagnose(document["uri"], document["version"])
        # problems of a text before this one: the client drops them
        write({"method": "textDocument/publishDiagnostics",
               "params": {"uri": document["uri"], "version": document["version"] - 1,
                          "diagnostics": []}})
    elif method == "textDocument/didClose":
        texts.pop(params["textDocument"]["uri"], None)
    elif method == "textDocument/hover":
        if mode == "deep":
            body = b'{"jsonrpc":"2.0","id":%d,"result":' % message["id"] + b"[" * 300 + b"]" * 300 + b"}"
            write_raw(b"Content-Length: %d\r\n\r\n" % len(body) + body)
            continue
        uri = params["textDocument"]["uri"]
        write({"id": message["id"],
               "result": {"contents": {"kind": "plaintext", "value": texts.get(uri)}}})
    elif method == "textDocument/definition":
        uri = params["textDocument"]["uri"]
        write({"id": message["id"],
               "result": [{"uri": uri, "range": {"start": {"line": 1, "character": 4},
                                                 "end": {"line": 1, "character": 5}}}]})
    elif method == "textDocument/completion":
        write({"id": message["id"], "result": {"isIncomplete": False, "items": [
            {"label": "alpha", "detail": "int alpha", "sortText": "2"},
            {"label": "alphabet", "detail": "char *alphabet", "sortText": "1",
             "additionalTextEdits": [{"range": {"start": {"line": 0, "character": 0},
                                                "end": {"line": 0, "character": 0}},
                                      "newText": "/* added */\n"}]},
            {"label": "beta", "insertText": "beta()", "sortText": "3"}]}})
    elif method == "test/error":
        write({"id": message["id"], "error": {"code": -32000, "message": "it went wrong"}})
    elif method == "test/slow":
        pass
    elif method == "shutdown":
        if mode != "hang":
            write({"id": message["id"], "result": None})
    elif method == "exit":
        if mode != "hang":
            sys.exit(0)
