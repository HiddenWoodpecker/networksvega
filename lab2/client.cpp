#include "format.hpp"

int main (){
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in serverAddr;

	uint32_t length;
	uint8_t type;
	char payload[MAX_PAYLOAD];
	char input[1024];
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	
	if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
	    perror("Connection failed");
	    close(sockfd);
	    return 1;
	}
	
	const char* nickname = "client";
	send_message(sockfd, strlen(nickname) + 1, MSG_HELLO, nickname);
	if (recv_message(sockfd, &length, &type, payload) != 0) {
		printf("Failed to receive welcome message\n");
		close(sockfd);
		return 1;
	}
	if (type == MSG_WELCOME) {
		printf("%s\n", payload);
	}

	int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK);
	
	printf("> ");
	fflush(stdout);
		
	while (1) {
		if (fgets(input, sizeof(input), stdin)) {
			input[strcspn(input, "\n")] = '\0';			
			if (strcmp(input, "/quit") == 0) {
				send_message(sockfd, 1, MSG_BYE, "");
				printf("Disconnected\n");
				break;
			}
			else if (strcmp(input, "/ping") == 0) {
				send_message(sockfd, 1, MSG_PING, "");
				if (recv_message(sockfd, &length, &type, payload) == 0) {
					if (type == MSG_PONG) {
						printf("PONG\n");
					}
				}
			}
			else if (strlen(input) > 0) {
				send_message(sockfd, strlen(input) + 1, MSG_TEXT, input);
			}
		}
		
		usleep(10000);
	}
	
	close(sockfd);
	fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
	return 0;
}
