
#include "libaga.h"
#include <ctype.h>
#include <sys/file.h>
#include <sys/time.h>

#define agaUUIDLockPath "/tmp/agauid.lck"

static int uidLockFd = 0;

char *chomp( char *string )
{
	char *p, *s;
	char *b, *lim;
	
	for( s = string, p = &s[strlen(s) - 1]; p >= s && *p <= ' '; p-- ) *p = 0; // trailing wht
	for( b = string, lim = &b[strlen(b)]; b < lim && *b <= ' '; b++ ); // leading wht
	
	if( strlen( b ) )
	{
		// kill comments
		for( s = b, p = &s[strlen(s)]; s < p ; s++ )
		{
			if( *s == '#' )
			{
				*s = 0;
				break;
			}
		}
	}
	
	return b;
}

int buildSockAddr( char *host, int port, int family, struct sockaddr_storage *dstaddr, socklen_t *len )
{
	int retVal = 0;
	struct sockaddr_in *addr4 = (struct sockaddr_in *)dstaddr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)dstaddr;
	struct addrinfo *caddr, *addr, hints;
	char portString[8];

	memset(&hints, 0, sizeof(hints));
	if( family ) 
		hints.ai_family = family;
	else if (strchr( host, ':') ) {
		hints.ai_family = AF_INET6;
	} else 
		hints.ai_family = AF_INET;

	addr = NULL;
	caddr = NULL;
	sprintf( portString, "%d", port );
	if( getaddrinfo( host, portString, &hints, &addr ) ) {
		errLOG( "ERROR: buildSockAddr(): getting address information for host %s: %s", 
				host, 
				strerror(errno));
	}
	else {
		retVal = -1;
		for( caddr = addr; retVal && caddr; caddr = caddr->ai_next )
		{
			if( caddr->ai_protocol == PF_PACKET && 
					(caddr->ai_family == AF_INET ||
					 caddr->ai_family == AF_INET6) )
			{
				if( caddr->ai_family == AF_INET ) {
					*len = sizeof(struct sockaddr_in);
					memcpy( addr4, caddr->ai_addr, sizeof( *addr4 ) );
					addr4->sin_port = htons(port);
					retVal = 0;
				} else if( caddr->ai_family == AF_INET6 ) {
					*len = sizeof(struct sockaddr_in6);
					memcpy( addr6, caddr->ai_addr, sizeof( *addr6 ) );
					addr6->sin6_port = htons(port);
					retVal = 0;
				}
			}
		}
		if( addr ) 
			freeaddrinfo(addr);
	}

	/*
	if( pfam ) 
		hints.ai_family = pfam;
	else if (strchr( host, ':') ) {
		hints.ai_family = AF_INET6;
	} else 
		hints.ai_family = AF_INET;

	if( family == AF_INET ) {
		addr4->sin_family = family;
		addr4->sin_port = htons(port);
		inet_pton(addr4->sin_family,host,&addr4->sin_addr);
		*len = sizeof(struct sockaddr_in);
	} else if ( family == AF_INET6 ) {
		addr6->sin6_family = family;
		addr6->sin6_port = htons(port);
		inet_pton(addr6->sin6_family,host,&addr6->sin6_addr);
		*len = sizeof(struct sockaddr_in6);
	}
	*/

	return retVal;
}

int bindSockAddr(int sockfd, char *bindip, int bindport, int pfam )
{
	int retVal = 0;
	struct sockaddr_storage bind_addr;
	struct sockaddr_in *addr4 = (struct sockaddr_in *)&bind_addr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&bind_addr;
	int val = 0;
	int addr_len = 0;
	int family = AF_INET;

	if( pfam )
		family = pfam;

	memset(&bind_addr, 0, sizeof(bind_addr));
	if( bindip == NULL ) {
		if( family == AF_INET6 )  {
			memcpy(&addr6->sin6_addr, &in6addr_any, 8);
			addr6->sin6_port = htons(bindport);
		}
		else {
			addr4->sin_addr.s_addr = htonl(INADDR_ANY);
			addr4->sin_port = htons(bindport);
		}
	} else {
		if( family == AF_INET ) {
			addr4->sin_family = family;
			addr4->sin_port = htons(bindport);
			inet_pton(addr4->sin_family,bindip,&addr4->sin_addr);
			addr_len = sizeof(struct sockaddr_in);
		} else if ( family == AF_INET6 ) {
			addr6->sin6_family = family;
			addr6->sin6_port = htons(bindport);
			inet_pton(addr6->sin6_family,bindip,&addr6->sin6_addr);
			addr_len = sizeof(struct sockaddr_in6);
		}
	}
	addr_len = sizeof(bind_addr);
	if( bind( sockfd, (const struct sockaddr *)&bind_addr, addr_len ) < 0 ) {
		errLOG( "ERROR: binding local socket: %s", strerror(errno));
		retVal = myErrno;
	} else {
		addr_len = sizeof(int);
		getsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, &addr_len);
		val = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	}
	return retVal;
}

/***********************************************************************
 * Open a TCP Connection
 ***********************************************************************/
int openTCPConnection46( char *host, short port, int timeout, int nonblock, int pfam, char *bindip, int bindport )
{
	int retVal = 0;
	struct addrinfo *addr, hints, *caddr, *raddr;
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;
	int addr_len = 0;
	char portString[6];
	int sockfd;
	int sockflags = 0;
	int family = 0;
	char addrStr[128];

	addr = NULL;
	raddr = NULL;
	memset( &hints, 0, sizeof(hints) );
	sprintf( portString, "%d", port );
	if( pfam ) 
		hints.ai_family = pfam;
	else if (strchr( host, ':') ) {
		hints.ai_family = AF_INET6;
	} else 
		hints.ai_family = AF_INET;

	if( getaddrinfo( host, portString, &hints, &addr ) ) {
		errLOG( "error getting address information for host %s: %s\n", host, strerror(errno));
	}
	else {
		retVal = -1;
		for( caddr = addr; retVal && caddr; caddr = caddr->ai_next )
		{
			if( caddr->ai_protocol == PF_PACKET && 
					(caddr->ai_family == AF_INET ||
					 caddr->ai_family == AF_INET6) )
			{
				if( caddr->ai_family == AF_INET ) {
					addr_len = sizeof(struct sockaddr_in);
					addr4 = (struct sockaddr_in *)caddr->ai_addr;
					addr4->sin_port = htons(port);
					family = AF_INET;
					retVal = 0;
					raddr = caddr;
				} else if( caddr->ai_family == AF_INET6 ) {
					addr_len = sizeof(struct sockaddr_in6);
					addr6 = (struct sockaddr_in6 *)caddr->ai_addr;
					addr6->sin6_port = htons(port);
					family = AF_INET6;
					retVal = 0;
					raddr = caddr;
				}
			}
		}
	}

	if( (sockfd = socket( family, SOCK_STREAM, 0 )) < 0 )
	{
		errLOG( "ERROR: creating socket for connection: %s\n", strerror(errno) );
		retVal = -1;
	}
	else
	{
		if( nonblock )
		{
			sockflags = fcntl( sockfd, F_GETFL, 0 );
			if( fcntl( sockfd, F_SETFL, (sockflags | O_NONBLOCK) ) )
			{
				errLOG( "ERROR: setting socket to nonblocking: %s", strerror(errno) );
			}
		}

		// if we are supposed to bind the socket to a local ip do that
		if( bindip ) {
			retVal = bindSockAddr( sockfd, bindip, bindport, family );
		}
	}

	if( !retVal ) {
		memset(addrStr, 0, 128);
		dbLOG( "DEBUG: connecting to host %s(%s):%s", 
				host, inet_ntop(raddr->ai_family, &raddr->ai_addr, addrStr, 127),
				portString );

		alarm(timeout);
		if( connect( sockfd, raddr->ai_addr, addr_len ) )
		{
			retVal = sockfd;
			if( errno != EINPROGRESS )
			{
				memset(addrStr, 0, 128);
				errLOG( "ERROR: connecting to host %s(%s):%s : %s\n", 
						host, inet_ntop(raddr->ai_family, &raddr->ai_addr, addrStr, 127),
						portString, 
						strerror(errno));
			}
		}
		else
		{
			dbLOG( "DEBUG: connected to host %s", host);
			retVal = sockfd;
		}
		alarm(0);
	}
	if( addr )
		freeaddrinfo(addr);
	
	return retVal;
}

int openTCPConnection( char *host, short port, int timeout, int nonblock ) 
{
	// use defaults to mimic old non-ipv6 compatible api
	return openTCPConnection46( host, port, timeout, nonblock, 0, NULL, 0 );
}

FILE *openTCPStream46( char *host, short port, char *perm, int *sockfd, int contimeout, int nonblock, int pfam, char *bindip, int bindport )
{
	FILE * retVal = NULL;
	int fd = 0;
	fd = openTCPConnection46( host, port, contimeout, nonblock, pfam, bindip, bindport );
	if( fd > 0 )
	{
		retVal = fdopen( fd, perm );
		if( !retVal )
		{
			errLOG( "error opening connected socket as a stream: %s", strerror(errno) );
		}
		else if( sockfd )
			*sockfd = fd;
	}
	else
		errLOG( "openTCPConnection returned an invalid socket" );
	
	return retVal;
}

FILE *openTCPStream( char *host, short port, char *perm, int *sockfd, int contimeout, int nonblock )
{
	return openTCPStream46( host, port, perm, sockfd, contimeout, nonblock, 0, NULL, 0);
}

int openUDPSocket46( char *ip, int port, int pfam, int nonblock)
{
	int sockfd = 0;
	struct sockaddr_storage addr;
	struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
	socklen_t addrlen;
	int val;

	if( pfam == 0 )
		pfam = AF_INET;

	memset(&addr, 0, sizeof(addr));
	addr.ss_family = pfam;

	sockfd = socket( pfam, SOCK_DGRAM, IPPROTO_UDP );
	if( sockfd < 0 ) {
		errLOG( "ERROR: NET: openUDPSocket46(): could not create socket: %s", strerror(errno));
		sockfd = -1;
	} else {
		if( nonblock ) {
			val = fcntl(sockfd, F_GETFL, 0);
			fcntl(sockfd, F_SETFL, (val | O_NONBLOCK));
		}
		if( bindSockAddr( sockfd, ip, port, pfam ) ) {
			close(sockfd);
			sockfd = -1;
		}	
	}

	return sockfd;
}

/****************************************************************************
 * return the size of the given file
 ***************************************************************************/
size_t fileSize( char *path )
{
	size_t retVal = 0;
	struct stat st;
	
	memset( &st, 0, sizeof(st) );
	if( stat( path, &st ) )
	{
		errLOG( "error statting file %s: %s", path, strerror(errno));
		retVal = ( errno ? -errno : -1 );
	}
	else
	{
		retVal = st.st_size;
	}
	return retVal;
}

/***************************************
 * return true if a socket is connected
 ***************************************/
int socketIsConnected( int fd )
{
	int retVal = 0;
	fd_set writeset;
	struct timeval tv;
	
	memset( &tv, 0, sizeof(tv) );
	memset( &writeset, 0, sizeof(writeset) );
	FD_SET( fd, &writeset );
	
	if( select( fd+1, NULL, &writeset, NULL, &tv ) > 0 )
	{
		if( FD_ISSET( fd, &writeset ) )
			retVal = 1;
	}
	
	return retVal;
}

/***************************************************************************
 * change a character array into a long
 **************************************************************************/
long agactol(unsigned char *a, short b)
{
	unsigned char endian_constant[2] = {0,1};
	union { 
		unsigned char ba[4];
    long lv;} work;
	int     ix;
	
	work.lv=0l;
	if (*(short *)endian_constant == 1)
		for(ix = (4 - b);ix < 4;ix++) work.ba[ix] = *(a++);
	else
		while (b > 0)
		{
			b--;
			work.ba[b] = *(a++);
		}
	return work.lv;
}

char *formatIpFromSS( struct sockaddr_storage *ss )
{
	static char ip[32];
	struct sockaddr_in *a4 = (struct sockaddr_in *)ss;
	struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;

	memset(ip, 0, 32);

	if( ss->ss_family == AF_INET ) {
		inet_ntop(AF_INET, &a4->sin_addr, ip, 31);
	} else if ( ss->ss_family == AF_INET6 ) {
		inet_ntop(AF_INET6, &a6->sin6_addr, ip, 31);
	} else {
		errLOG("ERROR: libaga: formatIpFromSS(): invalid ss_family=%d", ss->ss_family);
	}

	return ip;
}

char *formatIpFromSS_r( struct sockaddr_storage *ss, char *ip, int sz)
{
	struct sockaddr_in *a4 = (struct sockaddr_in *)ss;
	struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;

	memset(ip, 0, sz);

	if( ss->ss_family == AF_INET ) {
		inet_ntop(AF_INET, &a4->sin_addr, ip, sz-1);
	} else if ( ss->ss_family == AF_INET6 ) {
		inet_ntop(AF_INET6, &a6->sin6_addr, ip, sz-1);
	} else {
		errLOG("ERROR: libaga: formatIpFromSS_r(): invalid ss_family=%d", ss->ss_family);
	}


	return ip;
}

int getLocalPort( int sock, unsigned short *port )
{
	struct sockaddr_in name;
	int namelen = sizeof(name);
	unsigned short retVal = 0;
	
	if( getsockname( sock, (struct sockaddr *)&name, &namelen ) )
	{
		errLOG( "error obtaining local socket information: %s", strerror(errno));
		retVal = -1;
	}
	else
	{
		*port = ntohs( name.sin_port );
	}
	
	return retVal;
}

char *getRemoteIP( int sock, char *dest, int len )
{
	struct sockaddr_in name;
	int namelen = sizeof(name);
	char *retVal = NULL;
	
	if( getpeername( sock, (struct sockaddr *)&name, &namelen ) )
	{
		errLOG( "error obtaining peer socket information: %s", strerror(errno));
		retVal = NULL;
	}
	else
	{
		snprintf( dest, len, "%s", inet_ntoa( name.sin_addr ) );
		retVal = dest;
	}
	
	return retVal;
}

void hexLOG(void *_src, int size)
{
	u_char  buf[65],
			bufa[65],
			*lim,
			*p,
			*p1;
	int count;
	unsigned char *src = _src;

	p = buf;
	p1 = bufa;
	lim = &src[size];
	count = 0;
	while (src < lim)
	{
		if (*src < 0x20 || *src > 0x7E)
			*(p1++) = '.';
		else
			*(p1++) = *src;
		*(p1++) = ' ';
		*p1 = 0;
		sprintf(p,"%02X ",*src);
		p += 2;
		if (count < 20)
			count++;
		else
		{
			dbLOG( "DEBUG: HEX: %s", bufa);
			dbLOG( "DEBUG: HEX: %s", buf);
			p = buf;
			p1 = bufa;
			count = 0;
		}
		src++;
	}
	if (count)
	{
		dbLOG("DEBUG: HEX: %s",bufa);
		dbLOG("DEBUG: HEX: %s",buf);
	}
}

uint64_t htonll(uint64_t host_longlong)
{
    int x = 1;
 
    /* little endian */
    if(*(char *)&x == 1)
        return ((((uint64_t)htonl(host_longlong)) << 32) + htonl(host_longlong >> 32));
 
    /* big endian */
    else
        return host_longlong;
}
 
uint64_t ntohll(uint64_t host_longlong)
{
    int x = 1;
 
    /* little endian */
    if(*(char *)&x == 1)
        return ((((uint64_t)ntohl(host_longlong)) << 32) + ntohl(host_longlong >> 32));
 
    /* big endian */
    else
        return host_longlong;
 
}

char agaB2Ac( unsigned char b )
{
	char retVal = ' ';

	if( b < 10 )
		retVal = (b | 0x30);
	else if( b < 16 )
		retVal = (b + 55);

	return retVal;
}

int agaB2A( unsigned char b, char *a )
{
	unsigned char n1, n2;
	unsigned char c;

	n1 = (b >> 4); 
	n2 = b & 0x0F;

	a[0] = agaB2Ac( n1 );
	a[1] = agaB2Ac( n2 );

	return 2;
}

unsigned char agaA2B( char a )
{
	char o = a;
	unsigned char b = 0x80; // 8 bit set == err

	a = toupper(a);

	if( isdigit(a) )
		b = a & 0x0F;
	else if( a == ' ' ) {
		b = 0x0F;
	} else {
		a -= 55;
		if( a < 15 )
			b = a;
	}

	return b;
}

char agaB2AStr( unsigned char *s, int len, char *d ) 
{
	int retVal = 0;
	int i;

	for( i = 0; i < len; i++ ) {
		d += agaB2A( s[i], d );
	}

	return retVal;
}

static int lockUID()
{
	int retVal = 0;
	int lockCount = 0;
	int result = 0;

	if( uidLockFd == 0 ) {
		uidLockFd = open( agaUUIDLockPath, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP );
		if( uidLockFd < 0 ) {
			errLOG( "ERROR: libaga lockUID(): could not open lock file '%s': %s",
					agaUUIDLockPath, strerror(errno));
			retVal = -1;
		} 
	}

	if( uidLockFd > 0 ) {
		while( (result = flock( uidLockFd, LOCK_EX | LOCK_NB )) && lockCount < 10 ) {
			usleep(10000);
		}
		if( result ) {
			errLOG( "ERROR: libaga lockUID(): could not lock UID: %s", 
					strerror(errno));
			retVal = -1;
		}
	}

	return retVal;
}

// NOTE: 
// 		we close the lockfile on each unlock to prevent the
// 		lock file being moved or removed from causing us to lock a different
// 		file than the current file - this would result in two cores being able
// 		to lock the file at the same time because the inode identifier of the file
// 		would be different between instances of libaga invokation
static int unlockUID()
{
	int retVal = 0;

	if( uidLockFd > 0 ) {
		flock( uidLockFd, LOCK_UN );
		close( uidLockFd ); 
		uidLockFd = 0;
	}

	return retVal;
}

/**
 * Build a UUID that is unique across a server set as defined by "meta"
 * 		-	for a given machine/kernel this process is locked so only
 * 			one process at a time may use it. 
 * 		-	NOTE: 	this is not a thread based lock. If multi-threading 
 * 					is used then a semaphore lock will be required in this fucntion.
 * 		-	The format of the UUID is predictable and structured as such:
 * 			[4 bytes seconds since epoc][subsecond precision to 3 bytes][1 byte meta value]
 */
void agaBuildUID( char *dst, unsigned char meta )
{
    struct {
        unsigned char s[4];
        unsigned char ts[3];
        unsigned char svr;
    } data;
    union {
        int i;
        unsigned char a[4];
    } ua; 
    struct timeval tv; 
    unsigned char *p, *lim, *d; 

	if( lockUID() == 0 ) {
		gettimeofday(&tv, NULL);

		ua.i = htonl((int)tv.tv_sec);
		memcpy(data.s, ua.a, 4); 
		ua.i = htonl( (tv.tv_usec % 16777216) );
		memcpy(data.ts, &ua.a[1], 3 );
		data.svr = meta;

		p = (unsigned char *)&data;
		memset(dst, 0, 17);
		for( d = dst, lim = &p[8]; p < lim; p++ ) 
			d += agaB2A( *p, d );

		unlockUID();
	}
}   

static char *quickB2ATable[256] = { "00", "10", "20", "30", "40", "50", "60", "70", "80", "90", "00", "B0", "C0", "D0", "E0", " 0", "01", "11", "21", "31", "41", "51", "61", "71", "81", "91", "01", "B1", "C1", "D1", "E1", " 1", "02", "12", "22", "32", "42", "52", "62", "72", "82", "92", "02", "B2", "C2", "D2", "E2", " 2", "03", "13", "23", "33", "43", "53", "63", "73", "83", "93", "03", "B3", "C3", "D3", "E3", " 3", "04", "14", "24", "34", "44", "54", "64", "74", "84", "94", "04", "B4", "C4", "D4", "E4", " 4", "05", "15", "25", "35", "45", "55", "65", "75", "85", "95", "05", "B5", "C5", "D5", "E5", " 5", "06", "16", "26", "36", "46", "56", "66", "76", "86", "96", "06", "B6", "C6", "D6", "E6", " 6", "07", "17", "27", "37", "47", "57", "67", "77", "87", "97", "07", "B7", "C7", "D7", "E7", " 7", "08", "18", "28", "38", "48", "58", "68", "78", "88", "98", "08", "B8", "C8", "D8", "E8", " 8", "09", "19", "29", "39", "49", "59", "69", "79", "89", "99", "09", "B9", "C9", "D9", "E9", " 9", "00", "10", "20", "30", "40", "50", "60", "70", "80", "90", "00", "B0", "C0", "D0", "E0", " 0", "0B", "1B", "2B", "3B", "4B", "5B", "6B", "7B", "8B", "9B", "0B", "BB", "CB", "DB", "EB", " B", "0C", "1C", "2C", "3C", "4C", "5C", "6C", "7C", "8C", "9C", "0C", "BC", "CC", "DC", "EC", " C", "0D", "1D", "2D", "3D", "4D", "5D", "6D", "7D", "8D", "9D", "0D", "BD", "CD", "DD", "ED", " D", "0E", "1E", "2E", "3E", "4E", "5E", "6E", "7E", "8E", "9E", "0E", "BE", "CE", "DE", "EE", " E", "0 ", "1 ", "2 ", "3 ", "4 ", "5 ", "6 ", "7 ", "8 ", "9 ", "0 ", "B ", "C ", "D ", "E ", "  " };

int quickB2A( unsigned char *s, unsigned char *d, int dlen )
{
	unsigned char *lim;
	unsigned char *dstart = d;

	for( lim = d + dlen; d < lim; ) {
		memcpy( d, quickB2ATable[*s], 2 );
		s++;
		d += 2;
	}

	return d-dstart;
}

int sdbSpaceLeft( SIP_DYNAMIC_BUFFER *buff ) 
{
	int sz = 0;

	if( buff ) {
		if( buff->data ) {
			sz = buff->len - (buff->wp - buff->data);
		} 
	}

	return sz;
}

int sdbAddSpace( SIP_DYNAMIC_BUFFER *buff, int sz ) 
{
	int retVal = 0;
	int newsz;
	int wpo = 0;
	int rpo = 0;

	if( buff ) {
		newsz = buff->len + sz;
		if( buff->data ) {
			// if we have a data pointer already get the current
			// read/write pointer offsets
			wpo = buff->wp - buff->data;
			rpo = buff->rp - buff->data;
		}
		// reallocate the space
		buff->data = realloc( buff->data, newsz );
		if( buff->data ) {
			// recalculate the read/write offsets and reset the length element
			buff->len = newsz;
			buff->wp = buff->data + wpo;
			buff->rp = buff->data + rpo;
		} else {
			errLOG( "ERROR: MEM: could not allocate data for sip_dynamic_buffer newsz=%d sz=%d: %s", 
					newsz, sz, strerror(errno));
			retVal = myErrno;
		}
	}

	return retVal;
}

int sdbInit( SIP_DYNAMIC_BUFFER *buff, int sz, int fresh )
{
	int retVal = 0;

	if( buff ) {
		if( fresh ) {
			memset(buff, 0, sizeof(SIP_DYNAMIC_BUFFER));
			if( sz > 0 ) {
				retVal = sdbAddSpace( buff, sz );
			}
		} else {
			// reset the data pointers
			buff->wp = buff->data;
			buff->rp = buff->data;
			if( buff->len < sz ) {
				retVal = sdbAddSpace( buff, sz - buff->len);
			}
			memset(buff->data, 0, buff->len);
		}
	}

	return retVal;
}

int sdbAdd( SIP_DYNAMIC_BUFFER *buff, unsigned char *data, int len )
{
	int retVal = 0;

	if( buff ) {
		if( (len > 0) && data ) {
			if( sdbSpaceLeft(buff) < len ) {
				retVal = sdbAddSpace( buff, len );
			} 
			if( !retVal ) {
				memcpy( buff->wp, data, len );
				buff->wp += len;
			}
		} else {
			dbLOG( "DEBUG: sdbAdd(): len <= 0 || no data" );
		}
	}
	else {
		dbLOG( "DEBUG: sdbAdd(): no buffer" );
	}

	return retVal;
}

void sdbDestroy( SIP_DYNAMIC_BUFFER *buff ) 
{
	if( buff ) {
		if( buff->data ) {
			free(buff->data);
		}
		memset(buff, 0, sizeof(SIP_DYNAMIC_BUFFER));
	}
}

int sdbSize( SIP_DYNAMIC_BUFFER *buff ) 
{
	int retVal = 0;
	if( buff ) {
		retVal = buff->wp - buff->data;
	}
	return retVal;
}


