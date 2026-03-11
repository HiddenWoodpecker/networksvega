#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int main (){
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in serverAddr;
	serverAddr.sin_port = htons(8000);
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	
	char buffer[1024];
	char message[1024];
	while(true){
	std::cin>>message;
	sendto(sockfd, message, sizeof(message), 0 , (struct sockaddr*)&serverAddr, sizeof(serverAddr));
	recvfrom(sockfd, buffer, 1024, 0, NULL, NULL);
	std::cout << "SERVER: "<< buffer << std::endl;
	
	}	
	close(sockfd);
	return 0;
}
