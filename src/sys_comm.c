/* Lua System: File I/O: Serial communication */

#ifdef _WIN32

#define TCIFLUSH	(PURGE_RXABORT | PURGE_RXCLEAR)
#define TCOFLUSH	(PURGE_TXABORT | PURGE_TXCLEAR)
#define TCIOFLUSH	(TCIFLUSH | TCOFLUSH)

#define TIOCM_CAR	EV_RLSD
#define TIOCM_CTS	EV_CTS
#define TIOCM_DSR	EV_DSR
#define TIOCM_RNG	EV_RING

#else

#include <termios.h>

#endif


/*
 * Arguments: fd_udata, [options ...:
 *	reset (string: "reset"),
 *	baud_rate (number),
 *	character_size (string: "cs5".."cs8"),
 *	parity (string: "parno", "parodd", "pareven"),
 *	stop_bits (string: "sb1", "sb2"),
 *	flow_controls (string: "foff", "frtscts", "fxio")]
 * Returns: [fd_udata]
 */
static int
comm_init (lua_State *L)
{
    const fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
    const int nargs = lua_gettop(L);
    int i;

#ifndef _WIN32
    struct termios tio;

    if (tcgetattr(fd, &tio) == -1) goto err;
#else
    DCB dcb;

    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(fd, &dcb)) goto err;
#endif

    for (i = 2; i <= nargs; ++i) {
#ifndef _WIN32
	tcflag_t mask = 0, flag = 0;
#endif

	if (lua_isnumber(L, i)) {
	    const int baud_rate = lua_tointeger(L, i);
#ifndef _WIN32
	    switch (baud_rate) {
	    case 9600: flag = B9600; break;
	    case 19200: flag = B19200; break;
	    case 38400: flag = B38400; break;
	    case 57600: flag = B57600; break;
	    case 115200: flag = B115200; break;
	    }
	    if (cfsetispeed(&tio, flag) == -1
	     || cfsetospeed(&tio, flag) == -1)
		goto err;
#else
	    dcb.BaudRate = baud_rate;
#endif
	} else {
	    const char *opt = lua_tostring(L, i);
	    const char *endp = opt + lua_rawlen(L, i) - 1;

	    if (!opt) continue;
	    switch (*opt) {
	    case 'r':  /* reset */
#ifndef _WIN32
		memset(&tio, 0, sizeof(struct termios));
#else
		memset(&dcb, 0, sizeof(DCB));
#endif
		continue;
	    case 'c':  /* character size */
#ifndef _WIN32
		switch (*endp) {
		case '5': flag = CS5; break;
		case '6': flag = CS6; break;
		case '7': flag = CS7; break;
		default: flag = CS8;
		}
		mask = CSIZE;
#else
		dcb.ByteSize = (char) (*endp - '0');
#endif
		break;
	    case 'p':  /* parity */
#ifndef _WIN32
		switch (*endp) {
		case 'd': flag = PARODD;
		case 'n': flag |= PARENB; break;
		default: flag = 0;  /* no parity */
		}
		mask = PARENB | PARODD;
#else
		{
		    int parity;
		    switch (*endp) {
		    case 'd': parity = ODDPARITY; break;
		    case 'n': parity = EVENPARITY; break;
		    default: parity = 0;  /* no parity */
		    }
		    dcb.Parity = (char) parity;
		    dcb.fParity = (parity != 0);
		}
#endif
		break;
	    case 's':  /* stop bits */
#ifndef _WIN32
		if (*endp == '2') flag = CSTOPB;  /* else: one stop bit */
		mask = CSTOPB;
#else
		dcb.StopBits = (char) (*endp == '2' ? TWOSTOPBITS : ONESTOPBIT);
#endif
		break;
	    case 'f':  /* flow controls */
		/* off */
#ifndef _WIN32
		mask = CRTSCTS;
		tio.c_iflag &= ~(IXON | IXOFF | IXANY);
#else
		dcb.fOutX = dcb.fInX = 0;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;
		dcb.fOutxCtsFlow = 0;
#endif
		switch (opt[1]) {
		case 'x':  /* XON/XOFF */
#ifndef _WIN32
		    tio.c_iflag |= (IXON | IXOFF | IXANY);
#else
		    dcb.fOutX = dcb.fInX = 1;
#endif
		    break;
		case 'r':  /* RTS/CTS */
#ifndef _WIN32
		    flag = CRTSCTS;
#else
		    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
		    dcb.fOutxCtsFlow = 1;
#endif
		    break;
		}
		break;
	    }
	}
#ifndef _WIN32
	tio.c_cflag &= ~mask;
	tio.c_cflag |= flag;
#endif
    }
#ifndef _WIN32
    tio.c_cflag |= CLOCAL | CREAD;

    if (!tcsetattr(fd, TCSANOW, &tio)) {
#else
    if (SetCommState(fd, &dcb)) {
#endif
	lua_settop(L, 1);
	return 1;
    }
 err:
    return sys_seterror(L, 0);
}

/*
 * Arguments: fd_udata, [controls (string: "dtr", "dsr", "rts", "cts") ...]
 * Returns: [fd_udata]
 */
static int
comm_control (lua_State *L)
{
    const fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
    const int nargs = lua_gettop(L);
    int i;

#ifndef _WIN32
    int flags;

    if (ioctl(fd, TIOCMGET, &flags) == -1) goto err;
    flags &= ~(TIOCM_DTR | TIOCM_DSR | TIOCM_RTS | TIOCM_CTS
     | TIOCM_ST | TIOCM_SR | TIOCM_CAR | TIOCM_RNG);
#else
    DCB dcb;

    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(fd, &dcb)) goto err;
    dcb.fDtrControl = dcb.fOutxDsrFlow = dcb.fRtsControl = dcb.fOutxCtsFlow = 0;
#endif

    for (i = 2; i <= nargs; ++i) {
	const char *s = lua_tostring(L, i);

	if (!s) continue;
	switch (*s) {
	case 'd':  /* DTR/DSR */
#ifndef _WIN32
	    flags |= (s[1] == 't') ? TIOCM_DTR : TIOCM_DSR;
#else
	    if (s[1] == 't') dcb.fDtrControl = 1;
	    else dcb.fOutxDsrFlow = 1;
#endif
	    break;
	case 'r':  /* RTS */
#ifndef _WIN32
	    flags |= TIOCM_RTS;
#else
	    dcb.fRtsControl = 1;
#endif
	    break;
	case 'c':  /* CTS */
#ifndef _WIN32
	    flags |= TIOCM_CTS;
#else
	    dcb.fOutxCtsFlow = 1;
#endif
	    break;
	}
    }
#ifndef _WIN32
    if (!ioctl(fd, TIOCMSET, &flags)) {
#else
    if (SetCommState(fd, &dcb)) {
#endif
	lua_settop(L, 1);
	return 1;
    }
 err:
    return sys_seterror(L, 0);
}

/*
 * Arguments: fd_udata, [read_timeout (milliseconds)]
 * Returns: [fd_udata]
 */
static int
comm_timeout (lua_State *L)
{
    const fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
    const int rtime = lua_tointeger(L, 2);

#ifndef _WIN32
    struct termios tio;

    if (tcgetattr(fd, &tio) == -1) goto err;
    tio.c_cc[VTIME] = rtime / 100;
    tio.c_cc[VMIN] = 0;

    if (!tcsetattr(fd, TCSANOW, &tio)) {
#else
    COMMTIMEOUTS timeouts;

    memset(&timeouts, 0, sizeof(COMMTIMEOUTS));
    timeouts.ReadIntervalTimeout = rtime ? rtime : MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier =
     timeouts.ReadTotalTimeoutConstant = rtime;

    if (SetCommTimeouts(fd, &timeouts)) {
#endif
	lua_settop(L, 1);
	return 1;
    }
#ifndef _WIN32
 err:
#endif
    return sys_seterror(L, 0);
}

/*
 * Arguments: fd_udata, in_buffer_size (number), out_buffer_size (number)
 * Returns: [fd_udata]
 */
static int
comm_queues (lua_State *L)
{
#ifndef _WIN32
    if (1) {
#else
    const fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
    const int rqueue = lua_tointeger(L, 2);
    const int wqueue = lua_tointeger(L, 3);

    if (SetupComm(fd, rqueue, wqueue)) {
#endif
	lua_settop(L, 1);
	return 1;
    }
#ifdef _WIN32
    return sys_seterror(L, 0);
#endif
}

/*
 * Arguments: fd_udata, [mode (string: "rw", "r", "w")]
 * Returns: [fd_udata]
 */
static int
comm_purge (lua_State *L)
{
    const fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
    const char *mode = lua_tostring(L, 2);
    int flags;

    flags = TCIOFLUSH;
    if (mode)
	switch (mode[0]) {
	case 'w': flags = TCOFLUSH; break;
	case 'r': if (!mode[1]) flags = TCIFLUSH;
	}
#ifndef _WIN32
    if (!tcflush(fd, flags)) {
#else
    if (PurgeComm(fd, flags)) {
#endif
	lua_settop(L, 1);
	return 1;
    }
    return sys_seterror(L, 0);
}

/*
 * Arguments: fd_udata, options (string: "car", "cts", "dsr", "ring") ...
 * Returns: [boolean ...]
 */
static int
comm_wait (lua_State *L)
{
    const fd_t fd = (fd_t) lua_unboxinteger(L, 1, FD_TYPENAME);
    const int nargs = lua_gettop(L);
    unsigned long status;
    int flags = 0;
    int i, res;

    for (i = 2; i <= nargs; ++i) {
	const char *s = lua_tostring(L, i);

	if (!s) continue;
	switch (*s) {
	case 'c':  /* CAR/CTS */
	    flags |= (s[1] == 'a') ? TIOCM_CAR : TIOCM_CTS;
	    break;
	case 'd':  /* DSR */
	    flags |= TIOCM_DSR;
	    break;
	case 'r':  /* RING */
	    flags |= TIOCM_RNG;
	    break;
	}
    }

    sys_vm_leave();
#ifndef _WIN32
    do {
#ifdef TIOCMIWAIT
	res = !(ioctl(fd, TIOCMIWAIT, flags) || ioctl(fd, TIOCMGET, &status));
#else
	while ((res = !ioctl(fd, TIOCMGET, &status)) && !(status & flags))
	    usleep(10000);  /* 10 msec polling */
#endif
    } while (!res && SYS_ERRNO == EINTR);
#else
    res = SetCommMask(fd, flags) && WaitCommEvent(fd, &status, NULL);
#endif
    sys_vm_enter();

    if (res) {
	if (flags & TIOCM_CAR)
	    lua_pushboolean(L, status & TIOCM_CAR);
	if (flags & TIOCM_CTS)
	    lua_pushboolean(L, status & TIOCM_CTS);
	if (flags & TIOCM_DSR)
	    lua_pushboolean(L, status & TIOCM_DSR);
	if (flags & TIOCM_RNG)
	    lua_pushboolean(L, status & TIOCM_RNG);
	return nargs - 1;
    }
    return sys_seterror(L, 0);
}


#define COMM_METHODS \
    {"comm_init",	comm_init}, \
    {"comm_control",	comm_control}, \
    {"comm_timeout",	comm_timeout}, \
    {"comm_queues",	comm_queues}, \
    {"comm_purge",	comm_purge}, \
    {"comm_wait",	comm_wait}
