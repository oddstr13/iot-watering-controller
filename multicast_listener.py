import socket
import struct
import subprocess

import arrow

# https://bugs.python.org/issue29515
IPPROTO_IPV6 = getattr(socket, 'IPPROTO_IPV6', 41)

MULTICAST_ADDRESS = "ff18::554b:4841:536e:6574:1"
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

# Binds to all interfaces on the given port
sock.bind(('::', MULTICAST_PORT))

# Allow messages from this socket to loop back for development
# This
sock.setsockopt(IPPROTO_IPV6, socket.IPV6_MULTICAST_LOOP, True)

# Construct message for joining multicast group
for ifnum in getInterfaces():
    mreq = socket.inet_pton(socket.AF_INET6, MULTICAST_ADDRESS) + struct.pack('@L', ifnum) # Multicast address + interface
    sock.setsockopt(IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)

# 5s timeout
sock.settimeout(5)

while True:
    try:
        data, addr = sock.recvfrom(1500)
        print(arrow.now(), data, addr)
    except socket.timeout:
        pass