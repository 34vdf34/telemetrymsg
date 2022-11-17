 /*
  *  telemetryserver - Small example how to use TCP socket and binn 
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
#include "log.h"
#include "ini.h"
#include "binn.h"

#define SOCKADDR struct sockaddr
#define MSG_SIZE 			300
#define CMD_SIZE 			128
#define BUFSIZE 			128
#define PAYLOAD_MSG_SIZE 	250
#define IP_AND_MSG_SIZE		512
#define PREPARE_CMD 						1
#define TERMINATE_CMD						2
#define ESTABLISH_AUDIO_AS_CLIENT_CMD		3
#define ESTABLISH_AUDIO_AS_SERVER_CMD		4
#define DISCONNECT_AUDIO_CMD				5
#define DISCONNECT_CMD 						6
#define NOTIFY_BEEP_CMD						7
#define NO_ANSWER_CMD						8
#define ANSWER_CMD							9
#define REMOTE_HANGUP_CMD					10
#define STATE_AVAILABLE						1
#define STATE_BUSY							2
#define CLIENT_ROLE_CALL_ACTIVE				1
#define CLIENT_ROLE_CALL_INACTIVE			2
#define CLIENT_ROLE_CALL_FILENAME			"/tmp/CLIENT_CALL_ACTIVE"
#define MESSAGE_FIFO_OUT					"/tmp/message_fifo_out" 
#define FIFO_BUFFER_SIZE					210


void write_fifo_out(char *message)
{
	int fd;
	int write_status=0;
	char* fifo_out_filename = MESSAGE_FIFO_OUT;
	log_info("[%d] Msg FIFO TX : %s",getpid(),message);
	
	char fifo_write_buf[FIFO_BUFFER_SIZE];
	memset(fifo_write_buf,0,FIFO_BUFFER_SIZE);
	sprintf(fifo_write_buf,"%s",message);
	
	fd = open(fifo_out_filename, O_WRONLY);
	if (fd == -1 )
		log_error("[%d] Cannot open msg FIFO for write!",getpid());
	write_status = write(fd, fifo_write_buf, strlen(fifo_write_buf));
	if (write_status == -1 )
		log_error("[%d] Msg FIFO write error!",getpid());
	fsync(fd); 
	close(fd);
}

int check_call_status_from_client_role() {
	FILE *file;
	file = fopen(CLIENT_ROLE_CALL_FILENAME, "r");
    if (file) 
    {
        fclose(file);
        return CLIENT_ROLE_CALL_ACTIVE;
    }
    else
    {
        return CLIENT_ROLE_CALL_INACTIVE;
    }
    return 0;
}

void set_call_status(int status) {

	FILE *file;
	int write_status=0;
	char buf[1];
	
	if (status == STATE_BUSY)
	{
		file = fopen(CLIENT_ROLE_CALL_FILENAME, "w");
		memset(buf,0,1);
		write_status = write(file, buf, strlen(buf));
		if (write_status == -1 )
			log_error("[%d] unable to write status file",getpid());
		fsync(file); 
		close(file);
	} else {
		if (remove(CLIENT_ROLE_CALL_FILENAME) != 0) {
			log_error("[%d] unable to remove status file",getpid());
		}
	}
}

int run_receive_command(int id, int cmd_code) {
    char buf[BUFSIZE];
    char command_buffer[BUFSIZE];
    memset(buf,0,BUFSIZE);
    memset(command_buffer,0,BUFSIZE);
    if ( cmd_code == PREPARE_CMD )
		sprintf(command_buffer,"/opt/tunnel/connect.sh %d",id);
	if ( cmd_code == TERMINATE_CMD )
		sprintf(command_buffer,"/opt/tunnel/terminate.sh");
	if ( cmd_code == ESTABLISH_AUDIO_AS_CLIENT_CMD )
		sprintf(command_buffer,"/opt/tunnel/audio-on-as-client.sh");
	if ( cmd_code == ESTABLISH_AUDIO_AS_SERVER_CMD )
		sprintf(command_buffer,"/opt/tunnel/audio-on-as-server.sh");
	if ( cmd_code == DISCONNECT_AUDIO_CMD )
		sprintf(command_buffer,"/opt/tunnel/audio-off.sh");
	if ( cmd_code == DISCONNECT_CMD )
		sprintf(command_buffer,"/opt/tunnel/disconnect-%d.sh",id); 
	if ( cmd_code == ANSWER_CMD )
		sprintf(command_buffer,"/opt/tunnel/remote-answered.sh");
	if ( cmd_code == NO_ANSWER_CMD )
		sprintf(command_buffer,"/opt/tunnel/remote-no-answer.sh %d",id);
	if ( cmd_code == REMOTE_HANGUP_CMD )
		sprintf(command_buffer,"/opt/tunnel/remote-hangup.sh %d",id);
	if ( cmd_code == NOTIFY_BEEP_CMD )
		sprintf(command_buffer,"/opt/tunnel/beep.sh");
    
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
	// int node_state=STATE_AVAILABLE;
	int c=0;
	char *ini_file=NULL;
	int log_level=LOG_INFO;
	char *server_port=NULL;
	char *server_bind_ip=NULL;
	/* ini-file */
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
			log_info("[%d] telemetryserver",getpid());
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
	/* Create FIFO's */
	char* myfifo = MESSAGE_FIFO_OUT;
	mkfifo(myfifo, 0666);
	/* Load ini file*/ 
	ini_t *config = ini_load(ini_file);
	if (config == NULL ) {
		log_error("[%d] Cannot open ini-file, exiting.", getpid());
		return 0;
	}
	ini_sget(config, "server", "server_port", NULL, &server_port);
	ini_sget(config, "server", "server_bind_ip", NULL, &server_bind_ip);
	log_info("[%d] Binding to %s:%s",getpid(),server_bind_ip,server_port);
	
    int port = atoi(server_port);
    struct sockaddr_in servaddr;
    int sockfd;
    int connectfd;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		log_error("[%d] Socket create.",getpid());
        exit(EXIT_FAILURE);
    } else {
        log_debug("[%d] Socket created ok!",getpid());
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(server_bind_ip);       
    servaddr.sin_port = htons(port);

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
		log_error("[%d] setsockopt(SO_REUSEADDR) failed",getpid()); 

    if (bind(sockfd, (SOCKADDR*)&servaddr, sizeof(servaddr)) != 0) {
        log_error("[%d] Socket bind failed",getpid());
        exit(EXIT_FAILURE);
    } else {
        log_debug("[%d] Succesful binding",getpid());
    }

    if ((listen(sockfd, 1)) < 0) {
        log_error("[%d] Server listen failed",getpid());
        exit(EXIT_FAILURE);
    } else {
        log_debug("[%d] Server listening",getpid());
    }
    while (1) {
        log_info("[%d] Waiting client",getpid());
        if ((connectfd = accept(sockfd, (struct sockaddr *)NULL, NULL)) < 0) {
            log_error("[%d] Client connection failed",getpid());
            continue;
        }
        log_debug("[%d] Connected to client",getpid());
        char outgoing_msg[MSG_SIZE];
        char incoming_msg[MSG_SIZE];
        char command_code[CMD_SIZE];
        char from_ip[CMD_SIZE];
        char msg_payload[PAYLOAD_MSG_SIZE];
        char response[CMD_SIZE];    
        int id_code=0;
        memset(outgoing_msg, 0, MSG_SIZE);
        memset(incoming_msg, 0, MSG_SIZE);
        memset(command_code, 0, CMD_SIZE);
        memset(from_ip, 0, CMD_SIZE);
        memset(msg_payload, 0, PAYLOAD_MSG_SIZE);
        memset(response, 0, CMD_SIZE);
        if ((read(connectfd, incoming_msg, sizeof(incoming_msg))) < 0){
            log_error("[%d] Receive failed",getpid()); 
        } else {
			log_debug("[%d] Size of incoming msg %d ",getpid(),sizeof(incoming_msg) );
			/* Deserialize */
            char *deserialized_cmd;
            char *deserialized_from_ip;
            char *deserialized_msg_payload;
            binn *obj;
			obj = binn_open(incoming_msg);
			if (obj == NULL) {
				log_error("[%d] Malformed data",getpid() );
				binn_free(obj);
			}
			else {			
				deserialized_cmd = binn_object_str(obj, "cmd");
				deserialized_from_ip = binn_object_str(obj, "from_ip");
				id_code = binn_object_int32(obj, "id");
				deserialized_msg_payload = binn_object_str(obj, "msg_payload");
				log_info("[%d] Command: %s ",getpid(),deserialized_cmd);
				log_info("[%d] From IP: %s ",getpid(),deserialized_from_ip);
				log_info("[%d] From ID: %d ",getpid(),id_code);
				log_info("[%d] Message payload: %s ",getpid(),deserialized_msg_payload);
				binn_free(obj);
				strncpy(command_code,deserialized_cmd,CMD_SIZE);
				strncpy(from_ip,deserialized_from_ip,CMD_SIZE);
				strncpy(msg_payload,deserialized_msg_payload,PAYLOAD_MSG_SIZE);
			}
        }
		log_info("[%d] Command code: %s ",getpid(),command_code);
		memset(response, 0, CMD_SIZE);
		if ( strcmp(command_code,"status") == 0 ) {
			log_info("[%d] status command  ",getpid() );
			if ( check_call_status_from_client_role() == CLIENT_ROLE_CALL_ACTIVE ) {
				sprintf(response,"busy");
			}
			if ( check_call_status_from_client_role() == CLIENT_ROLE_CALL_INACTIVE ) {
				sprintf(response,"available");	
			}		
		}
		if ( strcmp(command_code,"prepare") == 0 && check_call_status_from_client_role() == CLIENT_ROLE_CALL_INACTIVE ) {
			log_info("[%d] prepare command  ",getpid() );
			run_receive_command(id_code, PREPARE_CMD);
			sprintf(response,"prepare_ready");
			set_call_status(STATE_BUSY);
		}
		if ( strcmp(command_code,"terminate") == 0 ) {
			log_info("[%d] terminate command  ",getpid() );
			run_receive_command(id_code, TERMINATE_CMD);
			sprintf(response,"terminate_ready");
			set_call_status(STATE_AVAILABLE);
		}
		if ( strcmp(command_code,"no_answer") == 0 ) {
			log_info("[%d] no_answer command  ",getpid() );
			run_receive_command(id_code, NO_ANSWER_CMD);
			sprintf(response,"no_answer_ready");
		}
		if ( strcmp(command_code,"answer") == 0 ) {
			log_info("[%d] no_answer command  ",getpid() );
			run_receive_command(id_code, ANSWER_CMD);
			sprintf(response,"answer_ready");
		}
		if ( strcmp(command_code,"hangup") == 0 ) {
			log_info("[%d] hangup command  ",getpid() );
			run_receive_command(id_code, REMOTE_HANGUP_CMD);
			sprintf(response,"hangup_ready");
			set_call_status(STATE_AVAILABLE);
		}
		if ( strcmp(command_code,"ring") == 0 ) {
			log_info("[%d] ring command  ",getpid() );
			log_info("[%d] From IP: %s  ",getpid(),from_ip );
			run_receive_command(id_code, NOTIFY_BEEP_CMD);
			sprintf(response,"ring_ready");
		}
		if ( strcmp(command_code,"message") == 0 ) {
			char ip_msg_out_buffer[IP_AND_MSG_SIZE];
			memset(ip_msg_out_buffer,0,IP_AND_MSG_SIZE);
			
			log_info("[%d] message command ",getpid() );
			sprintf(ip_msg_out_buffer,"%s,%s",from_ip,msg_payload);
			write_fifo_out(ip_msg_out_buffer);
			sprintf(response,"message_delivered");
		}		
		/* Serialized reply msg  */ 
		binn *obj;
		obj = binn_object();
		binn_object_set_str(obj, "id", response );
		if (write(connectfd,  binn_ptr(obj), binn_size(obj) ) < 0) {
            log_error("[%d] While sending to client",getpid()); 
        } else {
            log_debug("[%d] Client send success",getpid());
        }
		binn_free(obj);        
        log_info("[%d] Client exit success",getpid()); 
    }
}
