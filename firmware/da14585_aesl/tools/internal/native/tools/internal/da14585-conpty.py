"""Hidden Windows pseudoconsole, without a shell or third-party runtime.

API sequence: https://learn.microsoft.com/windows/console/creating-a-pseudoconsole-session
Caller must pin the native executable and bind its complete argument vector.
This module never supplies commands, chooses a target, or accesses a probe.
"""
import ctypes as C
from ctypes import wintypes as W
import subprocess


class Coord(C.Structure):
    _fields_ = [('X', C.c_short), ('Y', C.c_short)]


class Startup(C.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR),
        ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD), ('dwXSize', W.DWORD),
        ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD),
        ('dwFillAttribute', W.DWORD), ('dwFlags', W.DWORD), ('wShowWindow', W.WORD),
        ('cbReserved2', W.WORD), ('lpReserved2', C.POINTER(W.BYTE)),
        ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]


class StartupEx(C.Structure):
    _fields_ = [('StartupInfo', Startup), ('lpAttributeList', C.c_void_p)]


class ProcessInfo(C.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]


class Pipe:
    def __init__(self, kernel, handle):
        self.kernel, self.handle = kernel, handle

    def write(self, data):
        actual = W.DWORD()
        if not self.kernel.WriteFile(self.handle, data, len(data), C.byref(actual), None):
            raise C.WinError(C.get_last_error())
        if actual.value != len(data):
            raise OSError('Short pseudoconsole input; no replay')
        return actual.value

    def flush(self):
        pass

    def read1(self, count):
        data, actual = C.create_string_buffer(count), W.DWORD()
        if not self.kernel.ReadFile(self.handle, data, count, C.byref(actual), None):
            if C.get_last_error() in (6, 109, 232, 995):
                return b''
            raise C.WinError(C.get_last_error())
        return data.raw[:actual.value]

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


class Process:
    def __init__(self, argv):
        self.kernel = k = C.WinDLL('kernel32', use_last_error=True)
        signatures = {
            'CreatePipe': (W.BOOL, [C.POINTER(W.HANDLE), C.POINTER(W.HANDLE), C.c_void_p, W.DWORD]),
            'CreatePseudoConsole': (C.c_long, [Coord, W.HANDLE, W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)]),
            'ClosePseudoConsole': (None, [W.HANDLE]),
            'InitializeProcThreadAttributeList': (W.BOOL, [C.c_void_p, W.DWORD, W.DWORD, C.POINTER(C.c_size_t)]),
            'UpdateProcThreadAttribute': (W.BOOL, [C.c_void_p, W.DWORD, C.c_size_t, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p]),
            'DeleteProcThreadAttributeList': (None, [C.c_void_p]),
            'CreateProcessW': (W.BOOL, [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL, W.DWORD,
                C.c_void_p, W.LPCWSTR, C.POINTER(StartupEx), C.POINTER(ProcessInfo)]),
            'ReadFile': (W.BOOL, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]),
            'WriteFile': (W.BOOL, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]),
            'CloseHandle': (W.BOOL, [W.HANDLE]), 'TerminateProcess': (W.BOOL, [W.HANDLE, W.UINT]),
            'WaitForSingleObject': (W.DWORD, [W.HANDLE, W.DWORD]),
            'GetExitCodeProcess': (W.BOOL, [W.HANDLE, C.POINTER(W.DWORD)]),
            'GetStdHandle': (W.HANDLE, [W.DWORD]), 'SetStdHandle': (W.BOOL, [W.DWORD, W.HANDLE]),
        }
        for name, (result, args) in signatures.items():
            function = getattr(k, name)
            function.restype, function.argtypes = result, args
        child_input, host_input, host_output, child_output = (W.HANDLE() for _ in range(4))
        self.console, self.info = W.HANDLE(), ProcessInfo()
        self.stdin = self.stdout = None
        initialized = False
        try:
            if not k.CreatePipe(C.byref(child_input), C.byref(host_input), None, 0) or not k.CreatePipe(C.byref(host_output), C.byref(child_output), None, 0):
                raise C.WinError(C.get_last_error())
            result = k.CreatePseudoConsole(Coord(240, 80), child_input, child_output, 0, C.byref(self.console))
            if result:
                raise OSError(f'CreatePseudoConsole rejected: HRESULT 0x{result&0xFFFFFFFF:08X}')
            size = C.c_size_t()
            k.InitializeProcThreadAttributeList(None, 1, 0, C.byref(size))
            attributes = C.create_string_buffer(size.value)
            if not k.InitializeProcThreadAttributeList(attributes, 1, 0, C.byref(size)):
                raise C.WinError(C.get_last_error())
            initialized = True
            if not k.UpdateProcThreadAttribute(attributes, 0, 0x20016, self.console, C.sizeof(self.console), None, None):
                raise C.WinError(C.get_last_error())
            startup = StartupEx()
            startup.StartupInfo.cb, startup.lpAttributeList = C.sizeof(startup), C.addressof(attributes)
            command = C.create_unicode_buffer(subprocess.list2cmdline(argv))
            original = [(number, k.GetStdHandle(number&0xFFFFFFFF)) for number in (-10, -11, -12)]
            try:
                for number, handle in original:
                    k.SetStdHandle(number&0xFFFFFFFF, None)
                if not k.CreateProcessW(argv[0], command, None, None, False, 0x80000, None, None, C.byref(startup), C.byref(self.info)):
                    raise C.WinError(C.get_last_error())
            finally:
                for number, handle in original:
                    k.SetStdHandle(number&0xFFFFFFFF, handle)
            self.stdin, self.stdout = Pipe(k, host_input), Pipe(k, host_output)
            host_input = host_output = None
        finally:
            if initialized:
                k.DeleteProcThreadAttributeList(attributes)
            for handle in (child_input, child_output, host_input, host_output):
                if handle:
                    k.CloseHandle(handle)
            if self.stdin is None and self.console:
                k.ClosePseudoConsole(self.console)
                self.console = None

    def poll(self):
        result = self.kernel.WaitForSingleObject(self.info.hProcess, 0)
        if result == 0x102:
            return None
        if result != 0:
            raise C.WinError(C.get_last_error())
        code = W.DWORD()
        if not self.kernel.GetExitCodeProcess(self.info.hProcess, C.byref(code)):
            raise C.WinError(C.get_last_error())
        return code.value

    def wait(self, timeout):
        result = self.kernel.WaitForSingleObject(self.info.hProcess, max(0, int(timeout*1000)))
        if result == 0x102:
            raise TimeoutError('Commander did not exit within its deadline')
        return self.poll()

    def kill(self):
        if self.poll() is None and not self.kernel.TerminateProcess(self.info.hProcess, 1):
            raise C.WinError(C.get_last_error())

    def dispose(self):
        # The caller keeps its output-draining thread alive during this call.
        if self.console:
            self.kernel.ClosePseudoConsole(self.console)
            self.console = None
        for name in ('hThread', 'hProcess'):
            handle = getattr(self.info, name)
            if handle:
                self.kernel.CloseHandle(handle)
                setattr(self.info, name, None)
