import socket
import struct
import subprocess
import time

import arrow
try:
    import win32con
    import win32gui
    import win32api
    _windows_magic_window = None
except:
    pass

# https://bugs.python.org/issue29515
IPPROTO_IPV6 = getattr(socket, 'IPPROTO_IPV6', 41)

MULTICAST_ADDRESSES = [
    "ff18::554b:4841:536e:6574:1",
    "ff18::554b:4841:536e:6574:2",
]
MULTICAST_PORT = 20750
MULTICAST_INTERFACE = 8
# http://cubicspot.blogspot.com/2016/04/need-random-tcp-port-number-for-your.html
# https://www.random.org/integers/?num=1&min=5001&max=49151&col=5&base=10&format=html&rnd=new
# http://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.txt


def cmd(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, encoding=None):
    #print(args)
    p = subprocess.Popen(args, stdout=stdout, stderr=stderr)
    if p.stdout is not None:
        if encoding is not None:
            return (p.stdout.read()).decode(encoding)
        return p.stdout.read()
    else:
        p.wait()
        return p.returncode

def windowsPowerMagic():
    # https://stackoverflow.com/questions/1411186/python-windows-shutdown-events
    global _windows_magic_window

    hinst = win32api.GetModuleHandle(None)
    wndclass = win32gui.WNDCLASS()
    wndclass.hInstance = hinst
    wndclass.lpszClassName = "testWindowClass"
    messageMap = {
        win32con.WM_QUERYENDSESSION : windowsPowerMagicCallback,
        win32con.WM_ENDSESSION : windowsPowerMagicCallback,
        win32con.WM_QUIT : windowsPowerMagicCallback,
        win32con.WM_DESTROY : windowsPowerMagicCallback,
        win32con.WM_CLOSE : windowsPowerMagicCallback,
        win32con.WM_POWERBROADCAST: windowsPowerMagicCallback,
    }

    wndclass.lpfnWndProc = messageMap

    try:
        myWindowClass = win32gui.RegisterClass(wndclass)
        _windows_magic_window = win32gui.CreateWindowEx(win32con.WS_EX_LEFT,
                                     myWindowClass, 
                                     "testMsgWindow", 
                                     0, 
                                     0, 
                                     0, 
                                     win32con.CW_USEDEFAULT, 
                                     win32con.CW_USEDEFAULT, 
                                     0, 
                                     0, 
                                     hinst, 
                                     None)
        #win32gui.ShowWindow(_windows_magic_window, win32con.SW_SHOWMAXIMIZED) # Oh, look, there's the window! Frozen tho..
    except Exception as e:
        print("Exception: %s" % str(e))


    #if _windows_magic_window is None:
    #    print("hwnd is none!")
    #else:
    #    print("hwnd: %s" % _windows_magic_window)

def windowsPowerMagicCallback(hwnd, msg, wparam, lparam):
    print(arrow.now(), "win32 window event {}".format((msg, wparam, lparam)))
    if msg == win32con.WM_POWERBROADCAST and wparam == win32con.PBT_APMRESUMEAUTOMATIC:
        print(arrow.now(), "resuming from sleep")
        multicast_listen()

def windowsPowerMagicTick():
    win32gui.PumpWaitingMessages()


def windowsParseRoute():
    data = cmd(["route", "print"], encoding="UTF8").replace('\r\n', '\n')
    
    res = {}
    for block in data.split('\n\n'):
        block = block.strip("=").strip()
        if block.startswith("Interface List"):
            res['interfaces'] = {}
            _header, blockdata = block.split('\n', 1)
            for line in blockdata.split('\n'):
                line = line.strip()
                ifid, rest = line.split('...', 1)
                mac, rest = rest.split('.', 1)
                name = rest.lstrip('.')
                res['interfaces'][int(ifid)] = {
                    'name': name.strip()
                }
                if mac:
                    res['interfaces'][int(ifid)]['mac'] = ':'.join(mac.split())
        # TODO: IPv4 & IPv6 route info parsing not implemented
        #elif block.startswith("IPv4 Route Table"):
        #    for _block in block.split("===="):
        #        _block = _block.strip('=').strip()
        #        if not _block: continue
                #print("%%", _block)
        #else:
        #    pass
            #print("###", block)

    return res

def getInterfaces():
    # TODO: Make multi-platform
    return list(windowsParseRoute().get('interfaces').keys())

# Initialise socket for IPv6 datagrams
sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

# Allows address to be reused
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

try: sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
except: pass

try: sock.setsockopt(socket.SOL_IP, socket.IP_PKTINFO)
except: pass

# Binds to all interfaces on the given port
sock.bind(('::', MULTICAST_PORT))

# Allow messages from this socket to loop back for development
# This
sock.setsockopt(IPPROTO_IPV6, socket.IPV6_MULTICAST_LOOP, True)

def multicast_listen():
    print("Subscribing to multicast addresses")
    # Construct message for joining multicast group
    for ifnum in getInterfaces():
        for mcast in MULTICAST_ADDRESSES:
            print(ifnum, mcast)
            mreq = socket.inet_pton(socket.AF_INET6, mcast) + struct.pack('@L', ifnum) # Multicast address + interface
            try:
                sock.setsockopt(IPPROTO_IPV6, socket.IPV6_LEAVE_GROUP, mreq)
                time.sleep(0.01)
            except: pass
            sock.setsockopt(IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)

multicast_listen()
# 5s timeout
sock.settimeout(0.1)

windowsPowerMagic()

has_recvmsg = bool(hasattr(socket, "recvmsg"))
while True:
    try:
        windowsPowerMagicTick()
        try:
            if has_recvmsg:
                # Not available on Windows.
                # Need to set up one socket per multicast address (and interface if desired)
                # in order to get the target address of the packet.
                data, anc, flags, addr = socket.recvmsg(1500, 500)
                print(arrow.now(), data, addr, anc, flags)
            else:
                data, addr = sock.recvfrom(1500)
                print(arrow.now(), data, addr)
        except socket.timeout:
            pass
    except KeyboardInterrupt:
        print("KeyboardInterrupt, exiting.")
        break