# Generated by h2py from /usr/include/sys/socket.h

# Included from sys/netconfig.h
NETCONFIG = "/etc/netconfig"
NETPATH = "NETPATH"
NC_TPI_CLTS = 1
NC_TPI_COTS = 2
NC_TPI_COTS_ORD = 3
NC_TPI_RAW = 4
NC_NOFLAG = 00
NC_VISIBLE = 01
NC_BROADCAST = 02
NC_NOPROTOFMLY = "-"
NC_LOOPBACK = "loopback"
NC_INET = "inet"
NC_IMPLINK = "implink"
NC_PUP = "pup"
NC_CHAOS = "chaos"
NC_NS = "ns"
NC_NBS = "nbs"
NC_ECMA = "ecma"
NC_DATAKIT = "datakit"
NC_CCITT = "ccitt"
NC_SNA = "sna"
NC_DECNET = "decnet"
NC_DLI = "dli"
NC_LAT = "lat"
NC_HYLINK = "hylink"
NC_APPLETALK = "appletalk"
NC_NIT = "nit"
NC_IEEE802 = "ieee802"
NC_OSI = "osi"
NC_X25 = "x25"
NC_OSINET = "osinet"
NC_GOSIP = "gosip"
NC_NOPROTO = "-"
NC_TCP = "tcp"
NC_UDP = "udp"
NC_ICMP = "icmp"
NC_TPI_CLTS = 1
NC_TPI_COTS = 2
NC_TPI_COTS_ORD = 3
NC_TPI_RAW = 4
SOCK_STREAM = NC_TPI_COTS
SOCK_DGRAM = NC_TPI_CLTS
SOCK_RAW = NC_TPI_RAW
SOCK_RDM = 5
SOCK_SEQPACKET = 6
SO_DEBUG = 0x0001
SO_ACCEPTCONN = 0x0002
SO_REUSEADDR = 0x0004
SO_KEEPALIVE = 0x0008
SO_DONTROUTE = 0x0010
SO_BROADCAST = 0x0020
SO_USELOOPBACK = 0x0040
SO_LINGER = 0x0080
SO_OOBINLINE = 0x0100
SO_DONTLINGER = (~SO_LINGER)
SO_SNDBUF = 0x1001
SO_RCVBUF = 0x1002
SO_SNDLOWAT = 0x1003
SO_RCVLOWAT = 0x1004
SO_SNDTIMEO = 0x1005
SO_RCVTIMEO = 0x1006
SO_ERROR = 0x1007
SO_TYPE = 0x1008
SO_PROTOTYPE = 0x1009
SOL_SOCKET = 0xffff
AF_UNSPEC = 0
AF_UNIX = 1
AF_INET = 2
AF_IMPLINK = 3
AF_PUP = 4
AF_CHAOS = 5
AF_NS = 6
AF_NBS = 7
AF_ECMA = 8
AF_DATAKIT = 9
AF_CCITT = 10
AF_SNA = 11
AF_DECnet = 12
AF_DLI = 13
AF_LAT = 14
AF_HYLINK = 15
AF_APPLETALK = 16
AF_NIT = 17
AF_802 = 18
AF_OSI = 19
AF_X25 = 20
AF_OSINET = 21
AF_GOSIP = 22
AF_IPX = 23
AF_MAX = 23
PF_UNSPEC = AF_UNSPEC
PF_UNIX = AF_UNIX
PF_INET = AF_INET
PF_IMPLINK = AF_IMPLINK
PF_PUP = AF_PUP
PF_CHAOS = AF_CHAOS
PF_NS = AF_NS
PF_NBS = AF_NBS
PF_ECMA = AF_ECMA
PF_DATAKIT = AF_DATAKIT
PF_CCITT = AF_CCITT
PF_SNA = AF_SNA
PF_DECnet = AF_DECnet
PF_DLI = AF_DLI
PF_LAT = AF_LAT
PF_HYLINK = AF_HYLINK
PF_APPLETALK = AF_APPLETALK
PF_NIT = AF_NIT
PF_802 = AF_802
PF_OSI = AF_OSI
PF_X25 = AF_X25
PF_OSINET = AF_OSINET
PF_GOSIP = AF_GOSIP
PF_IPX = AF_IPX
PF_MAX = AF_MAX
SOMAXCONN = 5
MSG_OOB = 0x1
MSG_PEEK = 0x2
MSG_DONTROUTE = 0x4
MSG_MAXIOVLEN = 16
def OPTLEN(x): return ((((x) + sizeof (long) - 1) / sizeof (long)) * sizeof (long))

SOCKETSYS = 88
SOCKETSYS = 83
SO_ACCEPT = 1
SO_BIND = 2
SO_CONNECT = 3
SO_GETPEERNAME = 4
SO_GETSOCKNAME = 5
SO_GETSOCKOPT = 6
SO_LISTEN = 7
SO_RECV = 8
SO_RECVFROM = 9
SO_SEND = 10
SO_SENDTO = 11
SO_SETSOCKOPT = 12
SO_SHUTDOWN = 13
SO_SOCKET = 14
SO_SOCKPOLL = 15
SO_GETIPDOMAIN = 16
SO_SETIPDOMAIN = 17
SO_ADJTIME = 18
