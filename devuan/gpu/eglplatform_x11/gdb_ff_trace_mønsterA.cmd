# Trin 2 (v5): KOMPLET per-attempt-sporing — alle EGL-kald med argumenter.
# Mål: se hvilken config hvert forsøg vælger (A/B/C/D), hvilken API der bindes
# (F), og hvilken config create bruger (G/A2!/V!) — for at forklare det
# krydsede mønster (ES-attempt på GL-config, GL-attempt på ES-config).
set pagination off
set breakpoint pending on
set follow-fork-mode parent
set detach-on-fork on
set environment LD_PRELOAD=/root/system_shim.so:/root/egl_platform_shim.so
set environment LD_LIBRARY_PATH=/opt/hybris
set environment EGL_PLATFORM=x11
set environment DISPLAY=:0
set environment MOZ_X11_EGL=1
set environment MOZ_DISABLE_CONTENT_SANDBOX=1
set environment MOZ_DISABLE_GPU_SANDBOX=1
python
import gdb

def reg(name):
    return int(gdb.parse_and_eval('$' + name))

def tid():
    try:
        t = gdb.selected_thread()
        return t.num if t else 0
    except Exception:
        return 0

def read_mem(addr, n):
    try:
        return gdb.selected_inferior().read_memory(addr, n).tobytes()
    except Exception:
        return b''

def lib_base(name):
    pid = gdb.selected_inferior().pid
    with open('/proc/%d/maps' % pid) as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 6 and (parts[5] == name or parts[5].endswith(name)):
                return int(parts[0].split('-')[0], 16)
    return None

def mk_bp(label, addr, capture_ret=False):
    if not addr:
        return
    for bp in (gdb.breakpoints() or []):
        if bp.location == ('*0x%x' % addr):
            return
    class RetBP(gdb.Breakpoint):
        def __init__(self, addr):
            super().__init__('*0x%x' % (addr & 0xFFFFFFFF))
        def stop(self):
            print('%s-ret => 0x%x' % (label, reg('r0')))
            self.delete()
            return False
    class BP(gdb.Breakpoint):
        def stop(self):
            print('%s t%d dpy=0x%x cfg=0x%x share=0x%x attr=0x%x pc=0x%x'
                  % (label, tid(), reg('r0'), reg('r1'), reg('r2'), reg('r3'), reg('pc')))
            if capture_ret:
                lr = reg('lr') & 0xFFFFFFFF
                if lr:
                    RetBP(lr)
            return False
    BP('*0x%x' % addr)

def mk_simple(label, fn):
    class BP(gdb.Breakpoint):
        def stop(self):
            print(fn())
            return False
    BP(label)

mk_simple('eglGetDisplay', lambda: 'A t%d eglGetDisplay native=0x%x pc=0x%x' % (tid(), reg('r0'), reg('pc')))
mk_simple('eglInitialize', lambda: 'B t%d eglInitialize dpy=0x%x' % (tid(), reg('r0')))

class BindAPIBP(gdb.Breakpoint):
    def stop(self):
        print('F t%d eglBindAPI api=0x%x pc=0x%x' % (tid(), reg('r0'), reg('pc')))
        gdb.execute('bt 4')
        return False
BindAPIBP('eglBindAPI')

class ChooseConfigBP(gdb.Breakpoint):
    def stop(self):
        attr = reg('r1')
        raw = read_mem(attr, 48)
        words = []
        for i in range(0, min(len(raw), 48), 4):
            words.append('0x%08x' % int.from_bytes(raw[i:i+4], 'little'))
        print('C t%d eglChooseConfig dpy=0x%x attr=0x%x %s' % (tid(), reg('r0'), attr, ' '.join(words)))
        gdb.execute('bt 3')
        return False
ChooseConfigBP('eglChooseConfig')

class GetConfigAttribBP(gdb.Breakpoint):
    def stop(self):
        print('D t%d eglGetConfigAttrib dpy=0x%x cfg=0x%x name=0x%x' % (tid(), reg('r0'), reg('r1'), reg('r2')))
        return False
GetConfigAttribBP('eglGetConfigAttrib')

class CreateContextBP(gdb.Breakpoint):
    def stop(self):
        attr = reg('r3')
        raw = read_mem(attr, 40)
        words = []
        for i in range(0, min(len(raw), 40), 4):
            words.append('0x%08x' % int.from_bytes(raw[i:i+4], 'little'))
        print('G t%d eglCreateContext dpy=0x%x cfg=0x%x share=0x%x attr=0x%x %s'
              % (tid(), reg('r0'), reg('r1'), reg('r2'), attr, ' '.join(words)))
        return False
CreateContextBP('eglCreateContext')

class MakeCurrentBP(gdb.Breakpoint):
    def stop(self):
        print('H t%d eglMakeCurrent dpy=0x%x draw=0x%x read=0x%x ctx=0x%x'
              % (tid(), reg('r0'), reg('r1'), reg('r2'), reg('r3')))
        return False
MakeCurrentBP('eglMakeCurrent')

class ErrorBP(gdb.Breakpoint):
    def stop(self):
        print('R t%d eglGetError pc=0x%x' % (tid(), reg('pc')))
        gdb.execute('bt 4')
        return False
ErrorBP('eglGetError')

class GetProcBP(gdb.Breakpoint):
    def stop(self):
        try:
            name = gdb.selected_inferior().read_memory(reg('r0'), 40).tobytes().split(b'\0')[0].decode()
        except Exception:
            name = '?'
        print('P t%d eglGetProcAddress(%s)' % (tid(), name))
        return False
GetProcBP('eglGetProcAddress')

class InstallAndroidCreate(gdb.Breakpoint):
    def __init__(self):
        super().__init__('eglBindAPI')
        self.installed = False
    def stop(self):
        if not self.installed:
            egl = lib_base('/system/lib/libEGL.so')
            drv = lib_base('libEGL_POWERVR_ROGUE.so')
            if egl:
                mk_bp('A1!', egl + 0x6534)
                mk_bp('A2!', egl + 0x12534)
            if drv:
                mk_bp('V!', drv + 0x11cc)
            self.installed = True
        return False
InstallAndroidCreate()
end
run
