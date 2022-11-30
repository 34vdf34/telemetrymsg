/*
 *  telemetryclient - Small example how to use TCP socket and binn 
 *                    to send and receive messages.
 *
 *  Copyright (C) 2022 Resilience Theatre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * [1] Payload serialization with binn (https://github.com/liteserver/binn)
 * [2] https://stackoverflow.com/questions/2597608/c-socket-connection-timeout
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h> 
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include "log.h"
#include "ini.h"
#include "binn.h"

#define MSG_SIZE 			300
#define FIFO_SIZE 			300 
#define IP_BUF_SIZE 			15
#define PAYLOAD_MSG_SIZE 		250
#define BUFSIZE 			128
#define PREPARE_CMD 			1
#define TERMINATE_CMD			2
#define ESTABLISH_AUDIO_AS_CLIENT_CMD	3
#define ESTABLISH_AUDIO_AS_SERVER_CMD	4
#define DISCONNECT_AUDIO_CMD		5
#define DISCONNECT_CMD 			6
#define NOTIFY_BEEP_CMD			7
#define TERMINATE_LOCAL_CMD		8


void write_fifo_out(char *message)
{
	int fd;
	int write_status=0;
	char* fifo_out_filename = "/tmp/telemetry_fifo_out";
	log_info("[%d] FIFO TX: %s",getpid(),message);
	char fifo_write_buf[100];
	memset(fifo_write_buf,0,100);
	sprintf(fifo_write_buf,"%s",message);
	fd = open(fifo_out_filename, O_WRONLY);
	if (fd == -1 )
		log_error("[%d] Cannot open FIFO for write!",getpid());
	write_status = write(fd, fifo_write_buf, strlen(fifo_write_buf));
	if (write_status == -1 )
		log_error("[%d] FIFO write error!",getpid());
	fsync(fd); 
	close(fd);
}

int read_fifo_in(char* myfifo, char *message, char* dest_ip, char *msg_payload_buf)
{
	int fd_in;
	char fifo_in[FIFO_SIZE];
	memset(fifo_in, 0, FIFO_SIZE);
	log_info("[%d] Waiting for fifo cmd",getpid());
	fd_in = open(myfifo, O_RDONLY);
	read(fd_in, fifo_in, FIFO_SIZE);
	fifo_in[strlen(fifo_in)-1] = '\0';
	close(fd_in);
    char *pt;
    pt = strtok (fifo_in,",");
    int c=0;
    while (pt != NULL) {
        if ( c == 0 ) {
			log_debug("[%d] FIFO RX PARSE #1 : %s ",getpid(),pt);
			sprintf(dest_ip,"%s",pt);
		}
        if ( c == 1 ) {
			log_debug("[%d] FIFO RX PARSE #2 : %s Len: %d ",getpid(),pt,strlen(pt));
			sprintf(message,"%s",pt);
		}
		if ( c == 2 ) {
			log_debug("[%d] FIFO RX PARSE #3 : %s Len: %d ",getpid(),pt,strlen(pt));
			sprintf(msg_payload_buf,"%s",pt);
		}
		c++;
        pt = strtok (NULL, ",");
    }
	log_info("[%d] FIFO RX: %s ",getpid(),message);
	return strlen(message);
}

int connect_with_timeout(int sockfd, const struct sockaddr *addr, socklen_t addrlen, unsigned int timeout_ms) {
    int rc = 0;
    int sockfd_flags_before;
    if((sockfd_flags_before=fcntl(sockfd,F_GETFL,0)<0)) return -1;
    if(fcntl(sockfd,F_SETFL,sockfd_flags_before | O_NONBLOCK)<0) return -1;
    do {
        if (connect(sockfd, addr, addrlen)<0) {
            if ((errno != EWOULDBLOCK) && (errno != EINPROGRESS)) {
                rc = -1;
            }
            else {
                struct timespec now;
                if(clock_gettime(CLOCK_MONOTONIC, &now)<0) { rc=-1; break; }
                struct timespec deadline = { .tv_sec = now.tv_sec,
                                             .tv_nsec = now.tv_nsec + timeout_ms*1000000l};
                do {
                    if(clock_gettime(CLOCK_MONOTONIC, &now)<0) { rc=-1; break; }
                    int ms_until_deadline = (int)(  (deadline.tv_sec  - now.tv_sec)*1000l
                                                  + (deadline.tv_nsec - now.tv_nsec)/1000000l);
                    if(ms_until_deadline<0) { rc=0; break; }
                    struct pollfd pfds[] = { { .fd = sockfd, .events = POLLOUT } };
                    rc = poll(pfds, 1, ms_until_deadline);
                    if(rc>0) {
                        int error = 0; socklen_t len = sizeof(error);
                        int retval = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
                        if(retval==0) errno = error;
                        if(error!=0) rc=-1;
                    }
                }
                while(rc==-1 && errno==EINTR);
                if(rc==0) {
                    errno = ETIMEDOUT;
                    rc=-1;
                }
            }
        }
    } while(0);
    if(fcntl(sockfd,F_SETFL,sockfd_flags_before)<0) return -1;
    return rc;
}

int query_server(char* query_buf, char* server_address, char* server_port, char* my_ip_address,int my_id, char *response,char *msg_payload_buf  )
{
	struct sockaddr_in servaddr;
	int sockfd;
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		log_error("[%d] Socket create.",getpid());
		exit(EXIT_FAILURE);
	} else {
		log_debug("[%d] Socket created ok!",getpid());
	}
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(atoi(server_port));
	servaddr.sin_addr.s_addr = inet_addr(server_address);
	
	if ( connect_with_timeout(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr), 1500) < 0 ) {	
		log_debug("[%d] Connection failed",getpid());
		sprintf(response,"%s,offline",server_address);
		log_debug("[%d] coult not connect, reply: %s ",getpid(),response);
		return strlen(response);
	} else {
		log_debug("[%d] Connected to server",getpid());
	}
	
	char outgoing_msg[MSG_SIZE];
	memset(outgoing_msg, 0, MSG_SIZE);
	binn *obj;
	obj = binn_object();
	binn_object_set_str(obj, "cmd", query_buf);
	binn_object_set_str(obj, "from_ip",my_ip_address);
	binn_object_set_int32(obj,"id",my_id);
	binn_object_set_str(obj, "msg_payload",msg_payload_buf);
	log_debug("[%d] Payload size: %d ",getpid(),binn_size(obj) );
	if (write(sockfd, binn_ptr(obj), binn_size(obj) ) < 0) {
		log_error("[%d] Write error on server send",getpid());
		exit(EXIT_FAILURE);
	} else {
		log_debug("[%d] Send is success! ",getpid());
	}
	binn_free(obj);
		char received_msg[MSG_SIZE];
		memset(received_msg, 0, MSG_SIZE);
		/* Set timeout for socket read*/
		struct timeval tv;
		tv.tv_sec = 6;
		tv.tv_usec = 0;
		setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
		if (read(sockfd, received_msg, MSG_SIZE) < 1) {
			log_error("[%d] Receive error from server",getpid());
			exit(EXIT_FAILURE);
		} else {
			binn *obj;
			obj = binn_open(received_msg);
			if (obj == NULL) {
				log_error("[%d] Malformed data",getpid());
				binn_free(obj);
			} else {
				sprintf(response,"%s,%s",server_address, binn_object_str(obj, "id") );
				binn_free(obj);
				log_debug("[%d] reply: %s ",getpid(),response);
			}
		}
	close(sockfd);
	return strlen(response);
}

int run_local_command(int cmd_code) {
    char buf[BUFSIZE];
    char command_buffer[BUFSIZE];
    memset(buf,0,BUFSIZE);
    memset(command_buffer,0,BUFSIZE);

	if ( cmd_code == ESTABLISH_AUDIO_AS_CLIENT_CMD )
		sprintf(command_buffer,"/opt/tunnel/audio-on-as-client.sh");
	if ( cmd_code == ESTABLISH_AUDIO_AS_SERVER_CMD )
		sprintf(command_buffer,"/opt/tunnel/audio-on-as-server.sh");
	if ( cmd_code == DISCONNECT_AUDIO_CMD )
		sprintf(command_buffer,"/opt/tunnel/audio-off.sh");
	if ( cmd_code == TERMINATE_LOCAL_CMD )
		sprintf(command_buffer,"/opt/tunnel/terminate.sh");

    FILE *fp;
    if ((fp = popen(command_buffer, "r")) == NULL) {
        log_error("[%d] Pipe open error.", getpid()); 
        return -1;
    }
    while (fgets(buf, BUFSIZE, fp) != NULL) {
        log_info("[%d] Command output: %s",getpid(),buf); 
    }
    if (pclose(fp)) {
        log_error("[%d] Command execution error.", getpid());
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {

	int c=0;
	char *ini_file=NULL;
	int log_level=LOG_INFO;
	char *server_port=NULL;
	char *server_address=NULL;
	char *my_ip_address=NULL;
	char *my_id_str=NULL;
	int my_id=0;

	while ((c = getopt (argc, argv, "dhi:")) != -1)
	switch (c)
	{
		case 'd':
			log_level=LOG_DEBUG;
			break;
		case 'i':
			ini_file = optarg;
			break;
		case 'h':
			log_info("[%d] telemetryclient",getpid());
			log_info("[%d] Usage: -i [ini_file] ",getpid());
			log_info("[%d]        -d debug log ",getpid());
			return 1;
		break;
			default:
			break;
	}
	
	if (ini_file == NULL) 
	{
		log_error("[%d] ini file not specified, exiting.", getpid());
		return 0;
	}
	/* Set log level: LOG_INFO, LOG_DEBUG */
	log_set_level(log_level);
	/* Load ini file*/ 
	ini_t *config = ini_load(ini_file);
	if (config == NULL ) {
		log_error("[%d] Cannot open ini-file, exiting.", getpid());
		return 0;
	}
	ini_sget(config, "client", "server_address", NULL, &server_address);
	ini_sget(config, "client", "server_port", NULL, &server_port);
	ini_sget(config, "client", "my_ip", NULL, &my_ip_address);
	ini_sget(config, "client", "my_id", NULL, &my_id_str);
	log_debug("[%d] Server address: %s ",getpid(),server_address); // TODO: Make this dynamic from FIFO
	log_debug("[%d] Server port: %s",getpid(),server_port);
	log_debug("[%d] My IP: %s",getpid(),my_ip_address);
	log_debug("[%d] My ID: %s",getpid(),my_id_str);
	my_id=atoi(my_id_str);
	char* myfifo = "/tmp/telemetry_fifo_in";
	mkfifo(myfifo, 0666);
	char* myoutfifo = "/tmp/telemetry_fifo_out";
	mkfifo(myoutfifo, 0666);

	while ( 1 )
	{
		char *dest_ip;
		dest_ip=malloc(IP_BUF_SIZE);
		memset(dest_ip, 0, IP_BUF_SIZE);
		char *query_buf;
		query_buf=malloc(FIFO_SIZE);
		memset(query_buf, 0, FIFO_SIZE);
		int response_len=0;
		char *response_buf;
		response_buf=malloc(FIFO_SIZE);
		memset(response_buf, 0, FIFO_SIZE);
		char *msg_payload_buf;
		msg_payload_buf=malloc(PAYLOAD_MSG_SIZE);
		memset(msg_payload_buf, 0, PAYLOAD_MSG_SIZE);
		read_fifo_in(myfifo, query_buf,dest_ip, msg_payload_buf);
		if ( strcmp(dest_ip,"127.0.0.1") == 0 ) {
			if ( strcmp(query_buf,"connect_audio_as_client") == 0 ) {
				run_local_command(ESTABLISH_AUDIO_AS_CLIENT_CMD);
			}
			if ( strcmp(query_buf,"connect_audio_as_server") == 0 ) {
				run_local_command(ESTABLISH_AUDIO_AS_SERVER_CMD);
			}
			if ( strcmp(query_buf,"disconnect_audio") == 0 ) {
				run_local_command(DISCONNECT_AUDIO_CMD);
			}
                        if ( strcmp(query_buf,"terminate_local") == 0 ) {
                                run_local_command(TERMINATE_LOCAL_CMD);
                        }
			if ( strcmp(query_buf,"daemon_ping") == 0 ) {
				sprintf(response_buf,"telemetryclient_is_alive");
				sleep(1);
				write_fifo_out(response_buf);
			}
		} else {
			response_len = query_server(query_buf,dest_ip,server_port,my_ip_address,my_id,response_buf,msg_payload_buf);
			if (response_len > 0 )
				write_fifo_out(response_buf);
		}
		free(dest_ip);
		free(query_buf);
		free(response_buf);
		free(msg_payload_buf);
    }
    return EXIT_SUCCESS;
}
