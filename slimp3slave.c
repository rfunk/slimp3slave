/*
 * slimp3slave.c:
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>

#include "util.h"

#define SERVER_PORT    3483
#define CLIENT_PORT    34443

#define STATE_STARTUP 0

#define RECV_BUF_SIZE 65536
#define OUT_BUF_SIZE  131072
#define OUT_BUF_SIZE_90  115000

typedef struct {
    char type;
    char reserved1;
    unsigned short wptr;
    unsigned short rptr;
    char reserved2[12];
} request_data_struct;

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

ring_buf * outbuf;
void * recvbuf;
int wptr;
int rptr;
int playmode = 3;

FILE * output_pipe = NULL;

int debug = 0;

char * server_name = "127.0.0.1";
char * player_cmd = "mpg123 --buffer 256 -";

struct in_addr * server_addr = NULL;

ring_buf * ring_buf_create(int size, int threshold) {
    ring_buf * b;
    b = xmalloc(sizeof(ring_buf));
    b->buf = (void*)xcalloc(1, size); 
    b->size = size;
    b->threshold = threshold;
    b->head = b->tail = 0;
    return b;
}

void memxcpy(void * dest, void * src, int size) {
    int i;
    for(i = 0; i < size >> 1; i++) {
        *(char*)(dest + i*2) = *(char*)(src + i*2 + 1);
        *(char*)(dest + i*2 + 1) = *(char*)(src + i*2);
    }

}

void ring_buf_write(ring_buf * b, void * data, int size) {
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

void ring_buf_reset(ring_buf * b) {
    b->tail = 0;
}

int ring_buf_nearly_full(ring_buf * b) {
    int s = b->head - b->tail;
    if(s < 0) {
        s += b->size;
    }
    fprintf(stderr, "Size: %d\n", s);
    return s > b->threshold;
}

void ring_buf_get_data(ring_buf * b, void ** p, int * size) {
    *p = b->buf + b->tail;
    *size = b->size - b->tail;
    if(b->head >= b->tail) {
        *size = b->head - b->tail;
    }
}

int ring_buf_empty(ring_buf * b) {
    return b->head == b->tail;
}

void ring_buf_consume(ring_buf * b, int  amount) {
    b->tail = (b->tail + amount);
    if(b->tail >= b->size) b->tail -= b->size;
}


FILE * output_pipe_open() {
    FILE * f;
    f = popen(player_cmd, "w");
    if(f == NULL) {
        perror("Unable to open player");
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
        fprintf(stderr, "No data to write!\n");
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


void send_packet(int s, char * b, int l) {
    struct sockaddr_in ina;

    ina.sin_family = AF_INET;
    ina.sin_port = htons(SERVER_PORT);
    ina.sin_addr = *server_addr; 

    if(sendto(s, b, l, 0, (const struct sockaddr*)&ina, sizeof(ina)) == -1) {
        perror("Could not send packet");
    };
}

void send_discovery(int s) {
    char pkt[18];

    memset(pkt, sizeof(pkt), 0);
    pkt[0] = 'd';
    pkt[2] = 1;
    pkt[3] = 0x11;

    if(debug) fprintf(stderr, "=> sending discovery request\n");


    send_packet(s, pkt, sizeof(pkt));
}

void request_data(int s) {
    request_data_struct pkt;

    if(ring_buf_nearly_full(outbuf)) {
        fprintf(stderr,"Ring buffer nearly full\n");
        return;
    }

    memset(&pkt, sizeof(request_data_struct), 0);
    pkt.type = 'r';
    pkt.wptr = outbuf->head >> 1;
    pkt.rptr = 0;

    fprintf(stderr, "=> requesting data for %d\n", pkt.wptr);
    send_packet(s, (void*)&pkt, sizeof(request_data_struct));
}


void send_ack(int s, unsigned short seq) {
    packet_ack pkt;

    memset(&pkt, sizeof(request_data_struct), 0);
    pkt.type = 'a';
    pkt.wptr = htons(outbuf->head >> 1);
    pkt.rptr = htons(outbuf->tail >> 1);
    pkt.seq = htons(seq);

    if(debug) fprintf(stderr, "=> sending ack for %d\n", seq); 
    send_packet(s, (void*)&pkt, sizeof(request_data_struct));

}

void say_hello(int s) {
    char pkt[18];

    memset(pkt, sizeof(pkt), 0);
    pkt[0] = 'h';
    pkt[1] = 1;
    pkt[2] = 0x11;

    send_packet(s, pkt, sizeof(pkt));

}


void receive_mpeg_data(int s, receive_mpeg_header* data, int bytes_read) {
    int addr;

    if(debug) fprintf(stderr, "Address: %d Control: %d Seq: %d \n", ntohs(data->wptr), data->control, ntohs(data->seq));
    if(data->control == 3) {
        ring_buf_reset(outbuf);
    }

    playmode = data->control;

    if(playmode == 0 || playmode == 1) {
        if(output_pipe == NULL) {
            fprintf(stderr, "Opening pipe\n");
            output_pipe = output_pipe_open(); 
        }
    }
    else {
        if(output_pipe != NULL) {
            fprintf(stderr, "Closing pipe\n");
            output_pipe_close(output_pipe);
            output_pipe = NULL;
        }
    }

    outbuf->head = htons(data->wptr) << 1;
    memcpy(outbuf->buf + outbuf->head, recvbuf + 18, bytes_read - 18);
    send_ack(s, ntohs(data->seq));
    
}

void read_packet(int s) {
    struct sockaddr ina;
    socklen_t slen;
    int bytes_read;

    bytes_read = recvfrom(s, recvbuf, RECV_BUF_SIZE, 0, NULL,0); // &ina, &slen);
    if(bytes_read == 0) {
        fprintf(stderr, "<= short packet");
    }
    else {
        switch(((char*)recvbuf)[0]) {
            case 'D':
                if(debug) fprintf(stderr, "<= discovery response\n"); 
                say_hello(s);
                break;
            case 'h':
                if(debug) fprintf(stderr, "<= hello\n");
                say_hello(s);
                break;
            case 'l':
                if(debug) fprintf(stderr, "<= LCD data\n");
                break;
            case 's':
                if(debug) fprintf(stderr, "<= stream control\n");
                break;
            case 'm':
                if(debug) fprintf(stderr, "<= mpeg data\n"); 
                receive_mpeg_data(s, recvbuf, bytes_read);
                break;
            case '2':
                if(debug) fprintf(stderr, "<= i2c data\n"); 
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


        FD_SET(s, &read_fds);
        if(s > n)  n = s ;

        FD_ZERO(&write_fds);
        if(output_pipe != NULL && !ring_buf_empty(outbuf) && playmode == 0) {
            p = fileno(output_pipe);
            FD_SET(p, &write_fds);
            if(p > n)  n = p ;
        }

        if(select(n + 1, &read_fds, &write_fds, NULL, NULL) == -1) {
             perror("select");
             abort();
        }
        if(FD_ISSET(s, &read_fds)) {
            /* fprintf(stderr, "Got packet !\n"); */
            read_packet(s);
        }
        else if(output_pipe != NULL ) {
            if(FD_ISSET(p, &write_fds)) {
                output_pipe_write();
            }
        }

    }
    

}

void init() {
    struct hostent * h;
    outbuf = ring_buf_create(OUT_BUF_SIZE, OUT_BUF_SIZE_90);
    recvbuf = (void*)xcalloc(1, RECV_BUF_SIZE);
    output_pipe = NULL;
    h = gethostbyname((const char *)server_name);
    if(h == NULL) {
        fprintf(stderr, "Unable to get address for %s\n", server_name);
        exit(1);
    }
    server_addr = (struct in_addr*)h->h_addr;
}

int server_connect() {
    int s;
    struct sockaddr_in my_addr;

    s = socket(AF_INET, SOCK_DGRAM, 0);

    if(s == -1) {
        perror("Could not open socket");
        return 1;
    }

    my_addr.sin_family = AF_INET;         
    my_addr.sin_port = htons(CLIENT_PORT); 
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), '\0', 8); 
    bind(s, (struct sockaddr *)&my_addr, sizeof(struct sockaddr));

    return s;
}

void usage() {
    fprintf(stderr,
"Usage: slimp3slave -h | -s serveraddress -v\n"
"\n"
"    -v                 verbose mode\n"
"    -s serveraddress   specifies server address\n"
"    -c playercmd       MP3 player command\n"
"\n"
"copyright (c) 2003 Paul Warren <pdw@ex-parrot.com>\n"
           );

}

void get_options(int argc, char **argv) {
    int opt;
    while((opt = getopt(argc, argv, "+vhs:c:")) != -1) {
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
        }
    }
}

int main(int argc, char** argv) {
    int s;

    get_options(argc, argv);

    init();

    s = server_connect();
    send_discovery(s);
    loop(s);

    return 0;
}
