#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int main (){
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in serverAddr, clientAddr;
	serverAddr.sin_port = htons(8000);
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;

	bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
	
	socklen_t addrlen = sizeof(clientAddr);
	char buffer[1024];
	while(true){
	int n = recvfrom(sockfd, buffer, 1024, 0, (struct sockaddr*)&clientAddr, &addrlen);
	buffer[n] = '\0';
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr, ip, sizeof(ip));

	std::cout << "Client: with address "<< inet_ntoa(clientAddr.sin_addr)<<" :"<< buffer << std::endl;
	sendto(sockfd, buffer, sizeof(buffer), 0 , (struct sockaddr*)&clientAddr, addrlen);
	}
	close(sockfd);
	return 0;
}
