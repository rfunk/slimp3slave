/*
 * slimp3slave.c:
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <locale.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <curses.h>

#include "util.h"

#define SERVER_PORT    3483
#define CLIENT_PORT    34443

#define STATE_STARTUP 0

#define RECV_BUF_SIZE 65536
#define OUT_BUF_SIZE  131072
#define OUT_BUF_SIZE_90  115000
#define DISPLAY_SIZE 128
#define LINE_LENGTH 40
#define LINE_COUNT 2

typedef struct {
    char type;
    char reserved1;
    unsigned short wptr;
    unsigned short rptr;
    char reserved2[12];
} request_data_struct;

typedef struct {
    char type;
    char zero;
    /* optimizer wants to put two bytes here */
    unsigned long time;/* since startup, in 625kHz ticks = 0.625 * microsecs */
    char codeset; /* 0xff for JVC */
    char bits; /* 16 for JVC */
    /* optimizer wants to put two bytes here */
    unsigned long code;
    char macaddr[6];
} __attribute__((packed)) send_ir_struct; /* be smarter than the optimizer */

typedef struct {
    char type;
    char control;
    char reserved1[4];
    unsigned short wptr;
    char reserved2[2];
    unsigned short seq;
    char reserved3[6];
} receive_mpeg_header;

typedef struct {
    char type;
    char reserved1[5];
    unsigned short wptr;
    unsigned short rptr;
    unsigned short seq;
    char reserved2[6];
} packet_ack;


typedef struct {
    void * buf;
    int head;
    int tail;
    int size;
    int threshold;
} ring_buf;

/* lookup table to convert the VFD charset to Latin-1 */
/* (Sorry, other character sets not supported) */
char vfd2latin1[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 00 - 07 */
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, /* 08 - 0f */
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, /* 10 - 17 */
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, /* 18 - 1f */
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, /* 20 - 27 */
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, /* 28 - 2f */
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, /* 30 - 37 */
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, /* 38 - 3f */
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, /* 40 - 47 */
        0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /* 48 - 4f */
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, /* 50 - 57 */
        0x58, 0x59, 0x5a, 0x5b, 0xa5, 0x5d, 0x5e, 0x5f, /* 58 - 5f */
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, /* 60 - 67 */
        0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, /* 68 - 6f */
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, /* 70 - 77 */
        0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0xbb, 0xab, /* 78 - 7f */
        0xc4, 0xc3, 0xc5, 0xe1, 0xe5, 0x85, 0xd6, 0xf6, /* 80 - 87 */
        0xd8, 0xf8, 0xdc, 0x8b, 0x5c, 0x8d, 0x7e, 0xa7, /* 88 - 8f */
        0xc6, 0xe6, 0xa3, 0x93, 0xb7, 0x6f, 0x96, 0x97, /* 90 - 97 */
        0xa6, 0xc7, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, /* 98 - 9f */
        0xa0, 0xa1, 0xa2, 0xac, 0xa4, 0xb7, 0xa6, 0xa7, /* a0 - a7 */
        0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, /* a8 - af */
        0x2d, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb1, /* b0 - b7 */
        0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, /* b8 - bf */
        0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, /* c0 - c7 */
        0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, /* c8 - cf */
        0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, /* d0 - d7 */
        0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xa8, 0xb0, /* d8 - df */
        0xe0, 0xe4, 0xdf, 0xe3, 0xb5, 0xe5, 0x70, 0x67, /* e0 - e7 */
        0xe8, 0xe9, 0x6a, 0xa4, 0xa2, 0xed, 0xf1, 0xf6, /* e8 - ef */
        0x70, 0x71, 0xf2, 0xf3, 0xf4, 0xfc, 0xf6, 0xf7, /* f0 - f7 */
        0xf8, 0x79, 0xfa, 0xfb, 0xfc, 0xf7, 0xfe, 0xff  /* f8 - ff */
};

ring_buf * outbuf;
void * recvbuf;
int wptr;
int rptr;
int playmode = 3;

FILE * output_pipe = NULL;

int debug = 0;

int display = 0;

char *server_name = "127.0.0.1";
char *player_cmd = "madplay -Q -";

struct in_addr *server_addr = NULL;

char slimp3_display[DISPLAY_SIZE];
SCREEN *term = NULL;
WINDOW *slimwin = NULL;
WINDOW *errwin = NULL;
WINDOW *msgwin = NULL;
int using_curses = 0;

struct timeval uptime; /* time we started */

void initcurses(void) {
    term = newterm(NULL, stdout, stdin);
    if (term != NULL) {
	int screen_width, screen_height;
	int window_width = LINE_LENGTH+2;
	int window_height = LINE_COUNT+2;
	int org_x, org_y;

	using_curses = 1;

	cbreak();
	noecho();
	nonl();

	nodelay(stdscr, TRUE);
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	leaveok(stdscr, TRUE);
	curs_set(0);

	getmaxyx(stdscr, screen_height, screen_width);
	org_x = (screen_width - window_width) / 2;
	if (debug) {
	    int msg_x, msg_y;

	    org_y = 0;

	    msgwin = subwin(stdscr,
			    screen_height-window_height-1, screen_width,
			    org_y+window_height+1, 0);
	    getmaxyx(msgwin, msg_y, msg_x);
	    wsetscrreg(msgwin, 0, msg_y);
	    scrollok(msgwin, TRUE);
	    idlok(msgwin, TRUE);
	} else {
	    /* center subwindow in main window */
	    org_y = (screen_height - window_height - 1) / 2;
	}

	slimwin = subwin(stdscr, window_height, window_width, org_y, org_x);
	box(slimwin, 0, 0);

	if (screen_height > LINE_COUNT+2) {
	    /* create one-line error message subwindow */
	    errwin = subwin(stdscr, 1, screen_width, org_y+window_height, 0);
	}

	wrefresh(curscr);
  }
}

void exitcurses(void) {
    endwin();
    delscreen(term);
}

void warn(const char* fmt, ...) {
    va_list args;
    char buf[80];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf)-1] = '\0';

    if (!using_curses) {
	fputs(buf, stderr);
    } else if (msgwin != NULL) {
	int len = 0;
	waddstr(msgwin, buf);
	/* refresh only if the message ends with a newline */
	len = strlen(buf);
	if (len && (buf[len-1] == '\n')) wrefresh(msgwin);
    }
}

void werror(const char *s) {
    int err = errno;
    char buf[80];
    int len = 0;
    buf[0] = '\0';

    if (s == NULL) {
	/* passing a NULL pointer clears the error status */
	errno = 0;
    } else {
	snprintf(buf, sizeof(buf), "%s: %s\n", s, strerror(err));
	buf[sizeof(buf)] = '\0';
	len = strlen(buf);
    }

    if (using_curses) {
	if (s != NULL) {
	    beep();
	    warn(buf);
	}
	if (errwin != NULL) {
	    int cols = 0;
	    int rows = 0;

	    werase(errwin);

	    if (s != NULL) {
		getmaxyx(errwin, rows, cols);
		cols++;
		if (cols < len) {
		    buf[cols] = '\0';
		    len = strlen(buf);
		}
		wstandout(errwin);
		mvwaddstr(errwin, 0, (cols - len)/2, buf);
		wstandend(errwin);
	    }
	    wrefresh(errwin);
	}
    } else if (s != NULL) {
	fprintf(stderr, "\a%s\n", buf);
    }
}

void sig_handler(int sig) {
    switch (sig) {
    case SIGPIPE:
	warn("Ignoring sigpipe\n");
	break;
    case SIGCONT:
    case SIGWINCH:
	if (using_curses) wrefresh(curscr);
	break;
    default:
	if (using_curses) endwin();
	exit(1);
	break;
    }
    return;
}

ring_buf* ring_buf_create(int size, int threshold) {
    ring_buf * b;
    b = xmalloc(sizeof(ring_buf));
    b->buf = (void*)xcalloc(1, size); 
    b->size = size;
    b->threshold = threshold;
    b->head = b->tail = 0;
    return b;
}

void memxcpy(void *dest, void *src, int size) {
    int i;
    for(i = 0; i < size >> 1; i++) {
        *(char*)(dest + i*2) = *(char*)(src + i*2 + 1);
        *(char*)(dest + i*2 + 1) = *(char*)(src + i*2);
    }

}

void ring_buf_write(ring_buf *b, void *data, int size) {
    int r;
    if(size > b->size) {
        /* paranoia */
        return;
    }

    if(b->tail > b->head) {
        r = b->tail - b->head - 1;
        if(r < size) {
            /* Not enough room in buffer */
            return;
        }
        else {
            memcpy(b->buf + b->head, data, size);
            b->head += size;
        }

    }
    else {
        r = b->size - b->head;
        if(r > size) r = size;
        memcpy(b->buf + b->head, data, r);
        b->head += r;
        if(r < size) {
            b->head = 0;
            ring_buf_write(b, data + r, size - r);
        }
    }
}

void ring_buf_reset(ring_buf *b) {
    b->tail = 0;
}

int ring_buf_nearly_full(ring_buf *b) {
    int s = b->head - b->tail;
    if(s < 0) {
        s += b->size;
    }
    warn("Size: %d\n", s);
    return s > b->threshold;
}

void ring_buf_get_data(ring_buf *b, void **p, int *size) {
    *p = b->buf + b->tail;
    *size = b->size - b->tail;
    if(b->head >= b->tail) {
        *size = b->head - b->tail;
    }
}

int ring_buf_empty(ring_buf * b) {
    return b->head == b->tail;
}

void ring_buf_consume(ring_buf *b, int amount) {
    b->tail = (b->tail + amount);
    if(b->tail >= b->size) b->tail -= b->size;
}


FILE * output_pipe_open() {
    FILE * f;
    f = popen(player_cmd, "w");
    if(f == NULL) {
        werror("Unable to open player");
        exit(1);
    }
    return f;
}

void output_pipe_write() {
    void * p;
    int size;
    int written;

    if(playmode != 0) return;
    
    ring_buf_get_data(outbuf, &p, &size);
    if(size == 0) {
        warn("No data to write!\n");
        return  ;
    };
    if(size > 4096) {
        size = 4096;
    }
    written = write(fileno(output_pipe), p, size);
    ring_buf_consume(outbuf, written);
}

void output_pipe_close(FILE * f) {
    fclose(f);
}

unsigned long curses2ir(int key) {
    unsigned long ir = 0;

    switch(key) {
    case '0': ir = 0x76899867; break;
    case '1': ir = 0x7689f00f; break;
    case '2': ir = 0x7689f00f; break;
    case '3': ir = 0x76898877; break;
    case '4': ir = 0x768948b7; break;
    case '5': ir = 0x7689c837; break;
    case '6': ir = 0x768928d7; break;
    case '7': ir = 0x7689a857; break;
    case '8': ir = 0x76896897; break;
    case '9': ir = 0x7689e817; break;
    case KEY_DOWN: ir = 0x7689b04f; break; /* arrow_down */
    case KEY_LEFT: ir = 0x7689906f; break; /* arrow_left */
    case KEY_RIGHT: ir = 0x7689d02f; break; /* arrow_right */
    case KEY_UP: ir = 0x7689e01f; break; /* arrow_up */
    //case KEY_NPAGE: ir = 0x768900ff; break; /* voldown */
    //case KEY_PPAGE: ir = 0x7689807f; break; /* volup */
    case '!': ir = 0x768940bf; break; /* power */
    case '[': ir = 0x7689c03f; break; /* rew */
    case ' ': ir = 0x768920df; break; /* pause */
    case ']': ir = 0x7689a05f; break; /* fwd */
    case KEY_IC: ir = 0x7689609f; break; /* add */
    case KEY_DC: ir = 0x7689609f; break; /* add */
    case '\r': ir = 0x768910ef; break; /* play */
    case '/': ir = 0x768958a7; break; /* search */
    case '?': ir = 0x7689d827; break; /* shuffle */
    case 'r': ir = 0x768938c7; break; /* repeat */
    case 's': ir = 0x7689b847; break; /* sleep */
    case KEY_HOME: ir = 0x76897887; break; /* now_playing */
    case '+': ir = 0x7689f807; break; /* size */
    //case '*': ir = 0x768904fb; break; /* brightness */

	/* non-IR key actions */
    case '\f': wrefresh(curscr); break; /* repaint screen */
    case 'v': /* toggle debug mode */
	exitcurses();
	debug = !debug;
	initcurses();
	break;
    case KEY_BACKSPACE: werror(NULL); break;
    //case 'E': errno=0; werror("Error test"); break;
    case 'q': exitcurses(); exit(0); /* quit */
  }

  return (unsigned long)ir;
}

void send_packet(int s, char *b, int l) {
    struct sockaddr_in ina;

    ina.sin_family = AF_INET;
    ina.sin_port = htons(SERVER_PORT);
    ina.sin_addr = *server_addr; 

    if(sendto(s, b, l, 0, (const struct sockaddr*)&ina, sizeof(ina)) == -1) {
        werror("Could not send packet");
    };
}

void send_discovery(int s) {
    char pkt[18];

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 'd';
    pkt[2] = 1;
    pkt[3] = 0x11;

    if(debug) warn("=> sending discovery request\n");


    send_packet(s, pkt, sizeof(pkt));
}

void request_data(int s) {
    request_data_struct pkt;

    if(ring_buf_nearly_full(outbuf)) {
        warn("Ring buffer nearly full\n");
        return;
    }

    memset(&pkt, 0, sizeof(request_data_struct));
    pkt.type = 'r';
    pkt.wptr = outbuf->head >> 1;
    pkt.rptr = 0;

    warn("=> requesting data for %d\n", pkt.wptr);
    send_packet(s, (void*)&pkt, sizeof(request_data_struct));
}


void send_ack(int s, unsigned short seq) {
    packet_ack pkt;

    memset(&pkt, 0, sizeof(request_data_struct));
    pkt.type = 'a';
    pkt.wptr = htons(outbuf->head >> 1);
    pkt.rptr = htons(outbuf->tail >> 1);
    pkt.seq = htons(seq);

    if(debug) warn("=> sending ack for %d\n", seq); 
    send_packet(s, (void*)&pkt, sizeof(request_data_struct));

}

void say_hello(int s) {
    char pkt[18];

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 'h';
    pkt[1] = 1;
    pkt[2] = 0x11;

    send_packet(s, pkt, sizeof(pkt));

}

void send_ir(int s, char codeset, unsigned long code, int bits) {
    send_ir_struct pkt;
    struct timeval now;
    struct timezone tz;
    struct timeval diff;
    unsigned long usecs;
    unsigned long ticks;

    gettimeofday(&now, &tz);
    now.tv_sec -= 60 * tz.tz_minuteswest; /* canonicalize to GMT/UTC */
    if (now.tv_usec < uptime.tv_usec) {
	/* borrowing */
	now.tv_usec += 1000;
	now.tv_sec -= 1;
    }
    diff.tv_usec = now.tv_usec - uptime.tv_usec;
    diff.tv_sec = now.tv_sec - uptime.tv_sec;
    usecs = diff.tv_sec * 1000000L + diff.tv_usec;
    ticks = (unsigned int)(0.625 * (double)usecs);

    memset(&pkt, 0, sizeof(send_ir_struct));
    pkt.type = 'i';
    pkt.zero = 0;
    pkt.time = htonl(ticks);
    pkt.codeset = codeset;
    pkt.bits = (char)bits;
    pkt.code = htonl(code);

    if(debug) warn("=> sending IR code 0x%08x at tick %lu (%lu usec)\n",
		   code, ticks, usecs);

    send_packet(s, (void*)&pkt, sizeof(pkt));
}

void receive_mpeg_data(int s, receive_mpeg_header* data, int bytes_read) {

    if(debug) 
        warn("Address: %d Control: %d Seq: %d \n", ntohs(data->wptr), data->control, ntohs(data->seq));

    if(data->control == 3) {
        ring_buf_reset(outbuf);
    }

    playmode = data->control;

    if(playmode == 0 || playmode == 1) {
        if(output_pipe == NULL) {
            if(debug)
                warn("Opening pipe\n");
            output_pipe = output_pipe_open(); 
        }
    }
    else {
        warn("Playmode: %d\n", playmode);
        if(output_pipe != NULL) {
            if(debug)
                warn("Closing pipe\n");
            output_pipe_close(output_pipe);
            output_pipe = NULL;
        }
    }

    outbuf->head = htons(data->wptr) << 1;
    memcpy(outbuf->buf + outbuf->head, recvbuf + 18, bytes_read - 18);
    send_ack(s, ntohs(data->seq));
    
}

void show_display_buffer(char *ddram) {
    char line1[LINE_LENGTH+1];
    char *line2;
    char sepline[LINE_LENGTH+1];

    memset(line1, 0, LINE_LENGTH+1);
    strncpy(line1, ddram, LINE_LENGTH);   
    line2 = &(ddram[0x40]);
    line2[LINE_LENGTH] = '\0';

    if (using_curses) {
      mvwaddnstr(slimwin, 1, 1, line1, LINE_LENGTH);
      mvwaddnstr(slimwin, 2, 1, line2, LINE_LENGTH);
      wrefresh(slimwin);
    } else {
      memset(sepline, '-', LINE_LENGTH);
      sepline[LINE_LENGTH] = '\0';
      printf("\n+%s+\n|%s|\n|%s|\n+%s+\n", sepline, line1, line2, sepline);
    }
}

void receive_display_data(char * ddram, unsigned short *data, int bytes_read) {
    unsigned short *display_data;
    int n;
    int addr = 0; /* counter */

    if (bytes_read % 2) bytes_read--; /* even number of bytes */
    display_data = &(data[9]); /* display data starts at byte 18 */

    for (n=0; n<(bytes_read/2); n++) {
        unsigned short d; /* data element */
        unsigned char t, c;

        d = ntohs(display_data[n]);
        t = (d & 0x00ff00) >> 8; /* type of display data */
        c = (d & 0x0000ff); /* character/command */
        //if (debug) warn("[%02x]  Type: 0x%02x  Data: 0x%02x\n", addr, t, c);
        switch (t) {
            case 0x03: /* character */
                c = vfd2latin1[c];
                //if (debug) warn("====> character 0x%02x \"%c\"\n", c, c);
                if (!isprint(c)) c = ' ';
                if (addr <= DISPLAY_SIZE)
                    ddram[addr++] = c;
                break;
            case 0x02: /* command */
                switch (c) {
                    case 0x01: /* display clear */
                        memset(ddram, ' ', DISPLAY_SIZE);
                        addr = 0;
                        break;
                    case 0x02: /* cursor home */
                    case 0x03: /* cursor home */
                        addr = 0;
                        break;
                    case 0x10: /* cursor left */
                    case 0x11: /* cursor left */
                    case 0x12: /* cursor left */
                    case 0x13: /* cursor left */
                        addr--;
                        if (addr < 0) addr = 0;
                        break;
                    case 0x14: /* cursor right */
                    case 0x15: /* cursor right */
                    case 0x16: /* cursor right */
                    case 0x17: /* cursor right */
                        addr++;
                        if (addr > DISPLAY_SIZE) addr = DISPLAY_SIZE;
                        break;
                    default:
                        if ((c & 0x80) == 0x80) {addr = (c & 0x7f);}
                        break;
                }
            case 0x00: /* delay */
            default:
                break;
    }
    }

    show_display_buffer(ddram);
}

void read_key(int s) {
  int key = 0;
  unsigned long ir = 0;
  char codeset = 0xff;
  int bits = 16;

  while ((key = getch()) != ERR) {
    ir = curses2ir(key);
    if (ir != 0) send_ir(s, codeset, ir, bits);
  }
}

void read_packet(int s) {
    struct sockaddr ina;
    struct sockaddr_in *ina_in = NULL;
    socklen_t slen = sizeof(struct sockaddr);
    int bytes_read;

    bytes_read = recvfrom(s, recvbuf, RECV_BUF_SIZE, 0, &ina, &slen);
    if (ina.sa_family == AF_INET) {
        ina_in = (struct sockaddr_in *)(&ina);
    }
    if(bytes_read < 1) {
        if (bytes_read < 0) {
	    if (errno != EINTR)
		werror("recvfrom");
        } else {
            warn("Peer closed connection\n");
        }
    }
    else if(bytes_read < 18) {
        warn("<= short packet\n");
    }
    else if (ina_in->sin_addr.s_addr != server_addr->s_addr) {
        /* ignore this packet */
        if (debug) warn("<= packet from wrong server: %s\n",
                           inet_ntoa(ina_in->sin_addr));
    }
    else {
        switch(((char*)recvbuf)[0]) {
            case 'D':
                if(debug) warn("<= discovery response\n"); 
                say_hello(s);
                break;
            case 'h':
                if(debug) warn("<= hello\n");
                say_hello(s);
                break;
            case 'l':
                if(debug) warn("<= LCD data\n");
                if(display) 
                    receive_display_data(slimp3_display, recvbuf, bytes_read);
                break;
            case 's':
                if(debug) warn("<= stream control\n");
                break;
            case 'm':
                if(debug) warn("<= mpeg data\n"); 
                receive_mpeg_data(s, recvbuf, bytes_read);
                break;
            case '2':
                if(debug) warn("<= i2c data\n"); 
                break;
        }
    }

}

void loop(int s) {
    fd_set read_fds;
    fd_set write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    for(;;) {
        int n = 0;
        int p;

	FD_SET(0, &read_fds); /* watch stdin */
        FD_SET(s, &read_fds);
        if(s > n)  n = s ;

        FD_ZERO(&write_fds);
        if(output_pipe != NULL && !ring_buf_empty(outbuf) && playmode == 0) {
            p = fileno(output_pipe);
            FD_SET(p, &write_fds);
            if(p > n)  n = p ;
        }

        if(select(n + 1, &read_fds, &write_fds, NULL, NULL) == -1) {
	  if (errno != EINTR) {
	    werror("select");
	    abort();
	  }
        }
        if(FD_ISSET(s, &read_fds)) {
            read_packet(s);
        }
	else if (FD_ISSET(0, &read_fds)) {
	    read_key(s);
	}
	else if(output_pipe != NULL) {
	    if(FD_ISSET(p, &write_fds)) {
                output_pipe_write();
	    }
        }

    }
    

}

void init() {
    struct hostent *h;
    struct timezone tz;

    outbuf = ring_buf_create(OUT_BUF_SIZE, OUT_BUF_SIZE_90);
    recvbuf = (void*)xcalloc(1, RECV_BUF_SIZE);
    output_pipe = NULL;
    h = gethostbyname((const char *)server_name);
    if(h == NULL) {
        warn("Unable to get address for %s\n", server_name);
        exit(1);
    }
    server_addr = (struct in_addr*)h->h_addr;

    memset(slimp3_display, ' ', DISPLAY_SIZE);
    slimp3_display[DISPLAY_SIZE] = '\0';

    setlocale(LC_ALL, ""); /* so that isprint() works properly */

    if (display) initcurses();

    /* save start time for sending IR packet */
    gettimeofday(&uptime, &tz);
    uptime.tv_sec -= 60 * tz.tz_minuteswest; /* canonicalize to GMT/UTC */
}

int server_connect() {
    int s;
    struct sockaddr_in my_addr;

    s = socket(AF_INET, SOCK_DGRAM, 0);

    if(s == -1) {
        werror("Could not open socket");
        return 1;
    }

    my_addr.sin_family = AF_INET;         
    my_addr.sin_port = htons(CLIENT_PORT); 
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), '\0', 8); 
    if(bind(s, (struct sockaddr *)&my_addr, sizeof(struct sockaddr))) {
        werror("Unable to bind to port: ");
    }


    return s;
}

void usage() {
    fprintf(stderr,
"Usage: slimp3slave -h | -l -s serveraddress -v\n"
"\n"
"    -v                 verbose mode\n"
"    -l                 enable LCD display output\n"
"    -s serveraddress   specifies server address\n"
"    -c playercmd       MP3 player command\n"
"\n"
"Special Keys:\n"
"    !                  Power\n"
"    Enter              Play\n"
"    Space              Pause\n"
"    Insert             Add\n"
"    Delete             Add\n"
"    Home               Now Playing\n"
"    [                  Rewind\n"
"    ]                  Forward\n"
"    /                  Search\n"
"    ?                  Shuffle\n"
"    r                  Repeat\n"
"    s                  Sleep\n"
"    +                  Size (double-size not usable)\n"
"    ^L                 Refresh Screen\n"
"    v                  Debug mode Toggle\n"
"    Backspace          Clear Error Message\n"
"    q                  Quit Program\n"
"\n"
"copyright (c) 2003 Paul Warren <pdw@ex-parrot.com>\n"
           );

}

void get_options(int argc, char **argv) {
    int opt;
    while((opt = getopt(argc, argv, "+vlhs:c:")) != -1) {
        switch(opt) {
            case 'h':
                usage();
                exit(0);

            case 'v':
                debug = 1;
                break;

            case 's':
                server_name = xstrdup(optarg);
                break;

            case 'c':
                player_cmd = xstrdup(optarg);
                break;

            case 'l':
                display = 1;
                break;

        }
    }
}

int main(int argc, char** argv) {
    int s;

    get_options(argc, argv);

    signal(SIGPIPE, sig_handler);
    signal(SIGCONT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGWINCH, sig_handler);

    init();

    s = server_connect();
    send_discovery(s);
    loop(s);

    return 0;
}
