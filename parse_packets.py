#!/usr/bin/env python
# -*- coding: utf-8 -*-
import urllib.parse
import struct
import re

packet_re = re.compile(br"(?:^|(?P<size>[^\0]))(?P<ttl>[0-9])(?P<sequence>[a-z])(?P<values>[^\[]*)\[(?P<hops>[^\]]+)\](?:(?P<crc>..)|$)", re.DOTALL)

packet_content_re = re.compile(br"(?P<ttl>[0-9])(?P<sequence>[a-z])(?P<values>[^\[]*)\[(?P<hops>[^\]]+)\]", re.DOTALL)

with open('putty.log', 'r', encoding="latin-1") as fh:
    packets = fh.read()

packets = [line.rstrip('\r\n') for line in packets.split('\n') if line.startswith("origin=")]

print(packets)

def crc16_ccitt(crc, data):
    msb = crc >> 8
    lsb = crc & 255
    for c in data:
        x = c ^ msb
        x ^= (x >> 4)
        msb = (lsb ^ (x >> 3) ^ (x << 4)) & 255
        lsb = (x ^ (x << 5)) & 255
    return (msb << 8) + lsb

def rfm69_crc(data):
    return crc16_ccitt(0x1D0F, data) ^ 0xFFFF

for p in packets:
    #print(p)
    x = dict([(i[0], urllib.parse.unquote_to_bytes(i[1])) for i in [i.split('=') for i in p.split('&')]])
    #print(x)
    data = x.get('data', None)
    if data:
        res = list(packet_re.finditer(data))

        print(data[:70], '...')
        print("Size:", len(data[1:]))

        plain_packet = packet_content_re.match(data)
        if plain_packet:
            print("This is a plain UKHASnet packet.")
            continue

        plen = data[0]
        print("Packet length:", plen)

        packet = data[1:plen+1]
        print(packet)
        #//crc = struct.unpack_from(">H", data, plen+1)[0]
        crc = (data[plen+1] << 8) + data[plen+2]
        crc_calculated = rfm69_crc(data[:plen+1])
        print("CRC:", hex(crc))
        print("Calculated CRC:", hex(crc_calculated)) # ! CRC includes packet length byte
        crc_ok = crc == crc_calculated
        print("CRC OK:", crc_ok)

        print(res)
        print(len(res))
        for m in res:
            print(m.groupdict())

        #if crc_ok and not len(res):
        #    print("Something went wrong..")
        #    exit(1)


    print()
