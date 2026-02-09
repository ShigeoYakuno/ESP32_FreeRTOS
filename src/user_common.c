#include "user_common.h"

#define RADIX_MIN 2
#define RADIX_MAX 36

char	const	__numchr[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";




char * strrev(char *buf)
{
	char	c, *p = buf, *s = buf;

	while (*s != '\0') ++s;			/* 文字列の終端の検出 */
	while (--s > p) {
		c = *p; *p = *s; *s = c;	/* swap(p, s) */
		++p;
	}
	return buf;
}

char *ultoa(unsigned long val, char *buf, int radix)
{
	char	*p = buf;

	if (RADIX_MIN <= radix && radix <= RADIX_MAX) {
		do {
			*p++ = __numchr[val % radix];
		} while (val /= radix);
	}
	*p = '\0';
	return strrev(buf);
}

char *ltoa(long val, char *buf, int radix)
{
	char	*p = buf;

	if (radix == 10 && val < 0) {
		val = -val;
		*p++ = '-';
	}
	ultoa((unsigned long)val, p, radix);
	return buf;
}

/* ======================================================================
    ltoa 代替
   ====================================================================== */
void *my_ltoa( long value, void *asc, unsigned short keta )
{
	unsigned char	i, hi, lo;
	unsigned char *	p = (unsigned char*)asc + keta;

	for( i=0; i<8; i++, value >>= 8 ) {
		if( keta == 0 ) break;
		keta--;
		p--;
		lo = (unsigned char)((unsigned char)value & 15);
		*p = (unsigned char)( lo >= 10 ? ('A' + lo - 10) : ('0' + lo) );
		if( keta == 0 ) break;
		keta--;
		p--;
		hi = (unsigned char)((unsigned char)value >> 4);
		*p = (unsigned char)( hi >= 10 ? ('A' + hi - 10) : ('0' + hi) );
	}
	return asc;
}

/* ======================================================================
    itoa 代替
   ====================================================================== */
void *my_itoa( long value, void *asc, unsigned short keta )
{
	unsigned char	temp[ 22 ];
	unsigned short	size;
	unsigned char *	s = asc;

	size = strlen( ltoa( value, (char*)temp, 10 ) );
	memset( s, 0x30, keta );
	strcpy( (char*)&s[keta-size], (char*)temp );
	return asc;
}





static int pr_sub(char *buf, int size, const char *str, int len, int width, char zero)
{
    if (width != 0 && len < width) {
        if (size < width - len) {
            memset(buf, zero, size);
            return size;
        }
        memset(buf, zero, width - len);
        if (size < width) {
            memcpy(buf + width - len, str, size - (width - len));
            return size;
        }
        memcpy(buf + width - len, str, len);
        return width;
    }

    if (size < len) {
        memcpy(buf, str, size);
        return size;
    }
    memcpy(buf, str, len);
    return len;
}

static int pr_bin(char *buf, int size, unsigned long d, int width, char zero)
{
    char s[32];
    char *p;

    p = &s[31];
    do {
        *p-- = (d & 1) + '0';
    } while ((d >>= 1) != 0);

    return pr_sub(buf, size, p+1, &s[31] - p, width, zero);
}

static int pr_hex(char *buf, int size, unsigned long d, int width, char zero, BOOL cap)
{
    char s[8];
    char *p;
    const char *hex;

    hex = cap ? "0123456789ABCDEF" : "0123456789abcdef";
    p = &s[7];
    do {
        *p-- = hex[d & 0x0f];
    } while ((d >>= 4) != 0);

    return pr_sub(buf, size, p+1, &s[7] - p, width, zero);
}

static int pr_dec(char *buf, int size, long d, int width, char zero)
{
    char s[11];
    char *p;
    BOOL m;

    if (d < 0) {
        m = TRUE;
        d = -d;
    } else {
        m = FALSE;
    }

    p = &s[10];
    do {
        *p-- = (d % 10) + '0';
    } while ((d /= 10) != 0);

    if (m) {
        *p-- = '-';
        d = -d;
        zero = ' ';
    }

    return pr_sub(buf, size, p+1, &s[10] - p, width, zero);
}

static int pr_udec(char *buf, int size, unsigned long d, int width, char zero)
{
    char s[10];
    char *p;

    p = &s[9];
    do {
        *p-- = (d % 10) + '0';
    } while ((d /= 10) != 0);

    return pr_sub(buf, size, p+1, &s[9] - p, width, zero);
}


void dlong_div( DLONG *s, unsigned long dst, DLONG *a )
{

    UW temp = 0;
    int i = 31;
    DLONG ss;

	if( dst & 0x80000000 ) {
		ss.lower  = s->lower >> 1;
		ss.lower |= s->upper << 31;
		ss.upper  = s->upper >>1;
		dst>>=1;
	}
	else
		ss = *s;

	a->upper = ss.upper / dst;
	temp = ss.upper % dst;
	a->lower = 0;
	while( i>=0 ) {
		a->lower<<=1;
		if( temp < dst ) {
			temp = temp<<1 | ((ss.lower>>i) & 0x01L );
			i--;
		}
		if( temp >= dst ) {
			a->lower |= temp / dst;
			temp %= dst;
		}
	}
}



void dlong_mul( unsigned long src, unsigned long dst,  DLONG *a )
{
    UW temp;
    UW carry;

	a->upper = (src>>16) * (dst>>16);
	a->lower = (src&0xffffL) * (dst&0xffffL);
	carry = a->lower;

	temp = (src&0xffffL) * (dst>>16);
	a->upper += temp>>16;
	a->lower += (temp&0xffffL)<<16;
	if( carry > a->lower )
		a->upper++;
	carry = a->lower;

	temp = (src>>16) * (dst&0xffffL);
	a->upper += temp>>16;
	a->lower += (temp&0xffffL)<<16;
	if( carry > a->lower )
		a->upper++;
}