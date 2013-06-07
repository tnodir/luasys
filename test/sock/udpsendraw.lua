#!/usr/bin/env lua

local sys = require"sys"
local sock = require"sys.sock"

-- http://www.inf.puc-rio.br/~roberto/struct/
local struct = require"struct"


local port = 1313
local host = "127.0.0.1"

local fd = sock.handle()
assert(fd:socket("raw"))
assert(fd:sockopt("hdrincl", 1))

local saddr = assert(sock.addr())
assert(saddr:inet(port, sock.inet_pton(host)))

-- ip header fields
local ip_header
do
	local ip_ihl = 5
	local ip_ver = 4
	local ip_tos = 16 -- low delay
	local ip_tot_len = 0  -- kernel will fill the correct total length
	local ip_id = 54321  -- Id of this packet
	local ip_frag_off = 0
	local ip_ttl = 64  -- hops
	local ip_proto = 17  -- IPPROTO_UDP
	local ip_check = 0  -- kernel will fill the correct checksum
	local ip_saddr = sock.inet_pton(host)  -- Spoof the source ip address if you want to
	local ip_daddr = sock.inet_pton(host)

	local ip_ihl_ver = bit32.lshift(ip_ver, 4) + ip_ihl

	ip_header = struct.pack('>BBHHHBBHc4c4', ip_ihl_ver, ip_tos, ip_tot_len, ip_id,
		ip_frag_off, ip_ttl, ip_proto, ip_check, ip_saddr, ip_daddr)
end

-- udp header fields
local udp_header
do
	local udp_srcport = 0
	local udp_destport = port
	local udp_len = 8
	local udp_chksum = 0

	udp_header = struct.pack('>HHHH', udp_srcport, udp_destport, udp_len, udp_chksum)
end

assert(fd:send(ip_header .. udp_header, saddr))
