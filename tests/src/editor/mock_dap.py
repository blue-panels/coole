#!/usr/bin/env python3
# A debug adapter that only pretends: it answers the requests of the Debug Adapter Protocol
# the debugger plugin sends, with a program of one function stopped on its breakpoints, for
# the tests of the client.
#
#   mock_dap.py            on stdin and stdout
#   mock_dap.py --tcp N    on a socket of port N; 0: a port of its own, said on stdout
#   mock_dap.py --split    each message written a byte at a time

import json
import socket
import sys
import time


class Adapter:
    def __init__(self, rfile, wfile, split):
        self.rfile = rfile
        self.wfile = wfile
        self.split = split
        self.seq = 0
        self.breakpoints = {}
        self.functions = []

    def write(self, message):
        self.seq += 1
        message["seq"] = self.seq
        body = json.dumps(message).encode()
        data = b"Content-Length: %d\r\n\r\n" % len(body) + body
        if self.split:
            for i in range(len(data)):
                self.wfile.write(data[i:i + 1])
                self.wfile.flush()
        else:
            self.wfile.write(data)
            self.wfile.flush()

    def read(self):
        length = None
        while True:
            line = self.rfile.readline()
            if not line:
                return None
            line = line.strip()
            if not line:
                break
            name, _, value = line.partition(b":")
            if name.lower() == b"content-length":
                length = int(value)
        return json.loads(self.rfile.read(length))

    def respond(self, request, body=None, success=True, message=None):
        response = {"type": "response", "request_seq": request["seq"],
                    "command": request["command"], "success": success}
        if body is not None:
            response["body"] = body
        if message is not None:
            response["message"] = message
        self.write(response)

    def event(self, name, body=None):
        event = {"type": "event", "event": name}
        if body is not None:
            event["body"] = body
        self.write(event)

    def stopped(self, reason):
        self.event("stopped", {"reason": reason, "threadId": 1, "allThreadsStopped": True})

    def line(self):
        for lines in self.breakpoints.values():
            if lines:
                return lines[0]
        return 1

    def source(self):
        for path in self.breakpoints:
            return path
        return "/nonexistent/mock.c"

    def handle(self, request):
        command = request["command"]
        args = request.get("arguments", {})
        if command == "initialize":
            self.respond(request, {"supportsConfigurationDoneRequest": True,
                                   "supportsFunctionBreakpoints": True,
                                   "supportsDisassembleRequest": True,
                                   "supportsSteppingGranularity": True,
                                   "supportsInstructionBreakpoints": True})
            self.event("initialized")
        elif command == "launch":
            self.respond(request)
            self.event("output", {"category": "console", "output": "mock: launched %s\n"
                                  % args.get("program", "?")})
            if args.get("console") == "integratedTerminal":
                self.write({"type": "request", "command": "runInTerminal",
                            "arguments": {"kind": "integrated", "cwd": args.get("cwd", "/"),
                                          "args": ["/bin/echo", "from the terminal"]}})
        elif command == "setBreakpoints":
            path = args["source"].get("path")
            lines = [b["line"] for b in args.get("breakpoints", [])]
            self.breakpoints[path] = lines
            self.respond(request, {"breakpoints": [
                {"id": 100 + i, "verified": True, "line": line}
                for i, line in enumerate(lines)]})
        elif command == "setFunctionBreakpoints":
            self.functions = [b["name"] for b in args.get("breakpoints", [])]
            self.respond(request, {"breakpoints": [
                {"id": 200 + i, "verified": True} for i in range(len(self.functions))]})
        elif command == "setInstructionBreakpoints":
            self.respond(request, {"breakpoints": [
                {"id": 300 + i, "verified": True}
                for i in range(len(args.get("breakpoints", [])))]})
        elif command == "setExceptionBreakpoints":
            self.respond(request, {"breakpoints": []})
        elif command == "configurationDone":
            self.respond(request)
            self.event("output", {"category": "stdout", "output": "program says hello\n"})
            self.stopped("breakpoint")
        elif command == "threads":
            self.respond(request, {"threads": [{"id": 1, "name": "main"}]})
        elif command == "stackTrace":
            self.respond(request, {"stackFrames": [
                {"id": 1000, "name": "main", "line": self.line(), "column": 1,
                 "source": {"path": self.source()}, "instructionPointerReference": "0x1000"},
                {"id": 1001, "name": "_start", "line": 0, "column": 0,
                 "instructionPointerReference": "0x900"}], "totalFrames": 2})
        elif command == "scopes":
            self.respond(request, {"scopes": [
                {"name": "Locals", "presentationHint": "locals", "variablesReference": 1,
                 "expensive": False},
                {"name": "Registers", "presentationHint": "registers", "variablesReference": 2,
                 "expensive": False}]})
        elif command == "variables":
            ref = args.get("variablesReference")
            if ref == 1:
                variables = [{"name": "x", "value": "42", "variablesReference": 0},
                             {"name": "s", "value": "{...}", "variablesReference": 3}]
            elif ref == 2:
                variables = [{"name": "rip", "value": "0x1000", "variablesReference": 0}]
            else:
                variables = [{"name": "a", "value": "1", "variablesReference": 0,
                              "evaluateName": "obj.a"}]
            self.respond(request, {"variables": variables})
        elif command == "evaluate":
            expression = args.get("expression", "")
            if expression == "bad":
                self.respond(request, success=False, message="no symbol bad")
            elif expression == "obj":
                self.respond(request, {"result": "<object>", "variablesReference": 3,
                                       "type": "Thing"})
            else:
                self.respond(request, {"result": "<%s>" % expression, "variablesReference": 0})
        elif command == "disassemble":
            self.respond(request, {"instructions": [
                {"address": "0x1000", "instruction": "push %rbp", "symbol": "main",
                 "location": {"path": self.source()}, "line": self.line()},
                {"address": "0x1001", "instruction": "mov %rsp,%rbp", "symbol": "main+1"},
                {"address": "0x1004", "instruction": "ret", "symbol": "main+4"}]})
        elif command in ("next", "stepIn", "stepOut"):
            self.respond(request)
            self.event("continued", {"threadId": 1})
            self.stopped("step")
        elif command == "continue":
            self.respond(request, {"allThreadsContinued": True})
            self.event("exited", {"exitCode": 3})
            self.event("terminated")
        elif command == "pause":
            self.respond(request)
            self.stopped("pause")
        elif command in ("disconnect", "terminate"):
            self.respond(request)
            return False
        else:
            self.respond(request, success=False, message="unknown request %s" % command)
        return True

    def run(self):
        while True:
            message = self.read()
            if message is None:
                return
            if message.get("type") == "response":
                if message.get("command") == "runInTerminal":
                    pid = message.get("body", {}).get("processId")
                    self.event("output", {"category": "console",
                                          "output": "mock: runInTerminal %s %s\n"
                                          % (message.get("success"), pid)})
                continue
            if not self.handle(message):
                return


def main():
    split = "--split" in sys.argv
    if "--tcp" in sys.argv:
        port = int(sys.argv[sys.argv.index("--tcp") + 1])
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", port))
        server.listen(1)
        print("mock DAP server listening at 127.0.0.1:%d" % server.getsockname()[1], flush=True)
        connection, _ = server.accept()
        Adapter(connection.makefile("rb"), connection.makefile("wb"), split).run()
        connection.close()
    else:
        Adapter(sys.stdin.buffer, sys.stdout.buffer, split).run()
    time.sleep(0.05)


if __name__ == "__main__":
    main()
