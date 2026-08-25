# Trin 3: InitImpl/spor med RETURVÆRDIER.
#   - G/A2!/V!:  eglCreateContext (wrapper / Android-intern / driver) + retur
#   - R:         eglGetError + returværdi
#   - H:         eglMakeCurrent
#   - P:         eglGetProcAddress-navne + retur (find manglende gl*-symboler)
#   - MOZ_LOG=GLContext:5 for InitImpl-detaljer
# Output: /root/ff_init_trace.log
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
set environment MOZ_GL_SPEW=1
set environment MOZ_LOG=GLContext:5
python
import gdb

_pending = set()

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
    try:
        with open('/proc/%d/maps' % pid) as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 6 and (parts[5] == name or parts[5].endswith(name)):
                    return int(parts[0].split('-')[0], 16)
    except Exception:
        pass
    return None

def words_at(addr, maxn):
    raw = read_mem(addr, maxn * 4)
    out = []
    for i in range(0, len(raw), 4):
        out.append('0x%08x' % int.from_bytes(raw[i:i+4], 'little'))
    return ' '.join(out)

def add_ret_bp(label, addr):
    global _pending
    addr &= 0xFFFFFFFE
    if not addr or addr in _pending:
        return
    _pending.add(addr)
    class RetBP(gdb.Breakpoint):
        def __init__(self, addr, label):
            super().__init__('*0x%x' % addr)
            self._label = label
        def stop(self):
            try:
                _pending.discard(addr)
                print('%s-ret => 0x%x (pc=0x%x)'
                      % (self._label, reg('r0'), reg('pc')))
            except Exception as e:
                print('## ret-bp error: %r' % e)
            finally:
                try:
                    self.delete()
                except Exception:
                    pass
            return False
    try:
        RetBP(addr, label)
    except Exception:
        _pending.discard(addr)

def mk_sym_bp(location, fmt, capture=False, bt=False):
    class BP(gdb.Breakpoint):
        def __init__(self, location, fmt, capture, bt):
            super().__init__(location)
            self._fmt = fmt
            self._capture = capture
            self._bt = bt
            self._label = location
        def stop(self):
            try:
                print(self._fmt())
                if self._capture:
                    lr = reg('lr') & 0xFFFFFFFF
                    add_ret_bp(self._label, lr)
                if self._bt:
                    gdb.execute('bt 3')
            except Exception as e:
                print('## bp error at %s: %r' % (self._location, e))
            return False
    BP(location, fmt, capture, bt)

mk_sym_bp('eglGetDisplay',
          lambda: 'A t%d eglGetDisplay native=0x%x' % (tid(), reg('r0')))
mk_sym_bp('eglInitialize',
          lambda: 'B t%d eglInitialize dpy=0x%x' % (tid(), reg('r0')))
mk_sym_bp('eglBindAPI',
          lambda: 'F t%d eglBindAPI api=0x%x pc=0x%x' % (tid(), reg('r0'), reg('pc')),
          bt=True)

class ChooseBP(gdb.Breakpoint):
    def __init__(self):
        super().__init__('eglChooseConfig')
    def stop(self):
        try:
            print('C t%d eglChooseConfig dpy=0x%x attr=0x%x %s'
                  % (tid(), reg('r0'), reg('r1'), words_at(reg('r1'), 12)))
        except Exception as e:
            print('## C error: %r' % e)
        return False
ChooseBP()

class CreateBP(gdb.Breakpoint):
    def __init__(self):
        super().__init__('eglCreateContext')
    def stop(self):
        try:
            print('G t%d eglCreateContext dpy=0x%x cfg=0x%x share=0x%x attr=0x%x %s'
                  % (tid(), reg('r0'), reg('r1'), reg('r2'), reg('r3'),
                     words_at(reg('r3'), 10)))
            lr = reg('lr') & 0xFFFFFFFF
            add_ret_bp('G', lr)
        except Exception as e:
            print('## G error: %r' % e)
        return False
CreateBP()

class MakeBP(gdb.Breakpoint):
    def __init__(self):
        super().__init__('eglMakeCurrent')
    def stop(self):
        try:
            print('H t%d eglMakeCurrent dpy=0x%x draw=0x%x read=0x%x ctx=0x%x'
                  % (tid(), reg('r0'), reg('r1'), reg('r2'), reg('r3')))
            lr = reg('lr') & 0xFFFFFFFF
            add_ret_bp('H', lr)
        except Exception as e:
            print('## H error: %r' % e)
        return False
MakeBP()

class ErrorBP(gdb.Breakpoint):
    def __init__(self):
        super().__init__('eglGetError')
    def stop(self):
        try:
            print('R t%d eglGetError pc=0x%x' % (tid(), reg('pc')))
            lr = reg('lr') & 0xFFFFFFFF
            add_ret_bp('R', lr)
        except Exception as e:
            print('## R error: %r' % e)
        return False
ErrorBP()

class ProcBP(gdb.Breakpoint):
    def __init__(self):
        super().__init__('eglGetProcAddress')
    def stop(self):
        try:
            try:
                name = gdb.selected_inferior().read_memory(reg('r0'), 60).tobytes().split(b'\0')[0].decode()
            except Exception:
                name = '?'
            print('P t%d eglGetProcAddress(%s)' % (tid(), name))
            lr = reg('lr') & 0xFFFFFFFF
            add_ret_bp('P', lr)
        except Exception as e:
            print('## P error: %r' % e)
        return False
ProcBP()

class InstallAndroidCreate(gdb.Breakpoint):
    def __init__(self):
        super().__init__('eglBindAPI')
        self.installed = False
    def stop(self):
        try:
            if not self.installed:
                egl = lib_base('/system/lib/libEGL.so')
                drv = lib_base('libEGL_POWERVR_ROGUE.so')
                if egl:
                    for off in (0x6534, 0x12534):
                        mk_sym_bp('*0x%x' % (egl + off),
                                  lambda off=off: 'A2! t%d dpy=0x%x cfg=0x%x share=0x%x attr=0x%x %s (off=0x%x)'
                                  % (tid(), reg('r0'), reg('r1'), reg('r2'), reg('r3'),
                                     words_at(reg('r3'), 10), off),
                                  capture=True)
                if drv:
                    mk_sym_bp('*0x%x' % (drv + 0x11cc),
                              lambda: 'V! t%d dpy=0x%x cfg=0x%x share=0x%x attr=0x%x %s'
                              % (tid(), reg('r0'), reg('r1'), reg('r2'), reg('r3'),
                                 words_at(reg('r3'), 10)),
                              capture=True)
                self.installed = True
        except Exception as e:
            print('## InstallAndroidCreate error: %r' % e)
        return False
InstallAndroidCreate()
end
run
