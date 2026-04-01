#include "format.hpp"

int main() {
	int server_fd, client_fd;
	struct sockaddr_in addr, client_addr;
	uint32_t msg_length;
	uint8_t msg_type;
	char payload[MAX_PAYLOAD + 1];
	int opt = 1;
	char client_str[64];
    
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(PORT);

	bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
	listen(server_fd, 1);
	client_fd = accept(server_fd, NULL, NULL);
	format_addr(&client_addr, client_str, sizeof(client_str));
	printf("Client connected [%s]\n", client_str);
    
	if (recv_message(client_fd, &msg_length, &msg_type, payload) != 0) {
		printf("Handshake failed\n");
		
		goto clean;	
	}
    
	if (msg_type != MSG_HELLO) {
        	printf("Handshake failed: expected MSG_HELLO, got type %d\n", msg_type);
		goto clean;
	}
    
	printf("[%s]: Hello (nick: %s)\n",client_str, payload); 
	send_message(client_fd, 1+7, MSG_WELCOME, "Welcome");
    
	while (1) {
		if (recv_message(client_fd, &msg_length, &msg_type, payload) != 0) {
        	printf("Client disconnected\n");
        	break;
        } 
	switch (msg_type) {
        	case MSG_TEXT:
                printf("[%s]: %s\n", client_str, payload);
                break;
                
        case MSG_PING:
                printf("[%s]: PING\n", client_str);
                send_message(client_fd, 1 + 4, MSG_PONG, "PONG");
                break;
                
        case MSG_BYE:
                printf("[%s]: BYE\n", client_str);
                send_message(client_fd, 1 + 3, MSG_BYE, "BYE");
                goto clean;
                
        default:
                printf("[%s]: Unknown type %d\n",client_str, msg_type);
                break;
        }
    }
    
clean:
    close(client_fd);
    close(server_fd);
    printf("Server shutdown\n");
    return 0;
}


