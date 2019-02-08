import socket
import struct

# https://bugs.python.org/issue29515
IPPROTO_IPV6 = getattr(socket, 'IPPROTO_IPV6', 41)

#                          U K  H A  S n  e t
MULTICAST_ADDRESS = "ff18::554b:4841:536e:6574:1" # b'\xff\x08\x00\x00\x00\x00UKHASnet\x00\x01'
MULTICAST_PORT = 20750


message = "Hello world!"

sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)

sock_addr = socket.getaddrinfo(MULTICAST_ADDRESS, MULTICAST_PORT, socket.AF_INET6, socket.SOCK_DGRAM)[0][4]

print(socket.getaddrinfo(MULTICAST_ADDRESS, MULTICAST_PORT, socket.AF_INET6, socket.SOCK_DGRAM))
print(sock_addr)

# Set multicast TTL (Hopcount)
sock.setsockopt(IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 5)

# Set interface to transmit multicast packet on.
# Either specify IPV6_MULTICAST_IF, or bind to a address on the interface.
sock.setsockopt(IPPROTO_IPV6, socket.IPV6_MULTICAST_IF, 8) # interface 8 (Ethernet) # TODO: Find a way to get the ifindex
#sock.bind(("2a02::", 0))

sock.sendto(message.encode(), sock_addr)