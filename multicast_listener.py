import socket
import struct

# https://bugs.python.org/issue29515
IPPROTO_IPV6 = getattr(socket, 'IPPROTO_IPV6', 41)

MULTICAST_ADDRESS = "ff18::554b:4841:536e:6574:1"
MULTICAST_PORT = 20750
MULTICAST_INTERFACE = 8
# http://cubicspot.blogspot.com/2016/04/need-random-tcp-port-number-for-your.html
# https://www.random.org/integers/?num=1&min=5001&max=49151&col=5&base=10&format=html&rnd=new
# http://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.txt


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
for ifnum in [1, 8, 13]: # On interfaces 1 (loopback), 8 (Ethernet) and 13 (vbox-hostonly) # TODO: Find a way to generate this list.
    mreq = socket.inet_pton(socket.AF_INET6, MULTICAST_ADDRESS) + struct.pack('@L', ifnum) # Multicast address + interface
    sock.setsockopt(IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)

# 5s timeout
sock.settimeout(5)

while True:
    try:
        data, addr = sock.recvfrom(1500)
        print(data, addr)
    except socket.timeout:
        pass