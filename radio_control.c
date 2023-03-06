#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <netinet/tcp.h>
/*
Hello:
	uint8_t commandType = 0;
	uint16_t reserved = 0;
	uint8_t hello_pckt[3];
	uint8_t commandType;

AskSong:
	uint8_t commandType = 1;
	uint16_t stationNumber;

UpSong:
	uint8_t commandType = 2;
	uint32_t songSize; //in bytes
	uint8_t songNameSize;
	char songName[songNameSize];

Welcome:
	uint8_t replyType = 0;
	uint16_t numStations;
	uint32_t multicastGroup;
	uint16_t portNumber;

Announce:
	uint8_t replyType = 1;
	uint8_t songNameSize;
	char songName[songNameSize];

PermitSong:
	uint8_t replyType = 2;
	uint8_t permit;

InvalidCommand:
	uint8_t replyType = 3;
	uint8_t replyStringSize;
	char replyString[replyStringSize];

NewStations:
	uint8_t replyType = 4;
	uint16_t newStationNumber
*/
int stop_flag=0;
uint32_t multicastGroup;
uint16_t portNumber;
uint32_t current_station=0;
uint16_t numStations;
///////////////////////////////////////////////////////////////////////////
int print_UI(){
	printf("\nChoose an option number and press 'Enter':\n");
	printf("   1.Check song name\n");
	printf("   2.Change station\n");
	printf("   3.Upload new song\n");
	printf("   4.Quit\n");
	return 0;
}
///////////////////////////////////////////////////////////////////////////
void* station_listner(){//playing music
	FILE *fp;
	fp=popen("play -t mp3 -> /dev/null 2>&1","w");//open pipe to app
	int udp_socket;
	int reading;
	uint8_t received_buffer[1400];
	uint32_t playing_station=current_station;
	udp_socket=socket(AF_INET,SOCK_DGRAM,0);
	if (udp_socket < 0){
		perror("Error opening socket");
		stop_flag=1;
		pthread_exit(NULL);
	}
	struct ip_mreq mreq;//struct for socket options
	struct sockaddr_in multi_addr;//struct for receiving messages
	struct sockaddr_in sock_addr;//struct for binding socket
	socklen_t addr_size;
	addr_size=sizeof(multi_addr);
	sock_addr.sin_family=AF_INET;
	multi_addr.sin_family=AF_INET;
	sock_addr.sin_port = htons(portNumber); //16 bit TCP port number
	multi_addr.sin_port = htons(portNumber); //16 bit TCP port number
	sock_addr.sin_addr.s_addr = htons(INADDR_ANY); 
	multi_addr.sin_addr.s_addr = multicastGroup; 
	uint32_t last_multicast_address=multicastGroup>>24;
	uint32_t new_current_station=current_station>>24;
	uint32_t second_multicast_address=(multicastGroup>>16) & 0x00ff;
	uint32_t k=1;
	uint32_t new_multicastGroup=multicastGroup;
	memset(sock_addr.sin_zero, '\0', sizeof(sock_addr.sin_zero));
	if(bind(udp_socket, (struct sockaddr *) &sock_addr,sizeof(sock_addr))<0){//Bind address
		close(udp_socket);
		perror("Error, cann't assign address to socket");
		stop_flag=1;
		pthread_exit(NULL);
	}
	mreq.imr_multiaddr.s_addr=multicastGroup;
	mreq.imr_interface.s_addr=htonl(INADDR_ANY);
	setsockopt(udp_socket,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq));//join multicast group
	while(stop_flag==0){//end of programm condition
		if(playing_station!=current_station){//station was changed
			playing_station=current_station;
			new_current_station=current_station>>24;
			setsockopt(udp_socket,IPPROTO_IP,IP_DROP_MEMBERSHIP,&mreq,sizeof(mreq));//prune multicast group
			if((last_multicast_address+new_current_station)>255){//overflow in last octet, need to change previous octet of ip address
				last_multicast_address+=new_current_station;
				new_current_station=last_multicast_address-255;
				second_multicast_address+=new_current_station;
				if(second_multicast_address>255){
					new_current_station=second_multicast_address-255;
					new_multicastGroup+=256;
					new_multicastGroup=(new_multicastGroup<<16)>>16;
					new_multicastGroup=new_multicastGroup+((new_current_station-1)<<24);
					multi_addr.sin_addr.s_addr =new_multicastGroup;
					mreq.imr_multiaddr.s_addr=new_multicastGroup;
					last_multicast_address=multicastGroup>>24;
					new_multicastGroup=multicastGroup;
					second_multicast_address=(multicastGroup>>16) & 0x00ff;
				}
				else{
					new_multicastGroup=new_multicastGroup+(k<<16);
					new_multicastGroup=(new_multicastGroup<<8)>>8;
					new_multicastGroup=new_multicastGroup+((new_current_station-1)<<24);
					multi_addr.sin_addr.s_addr =new_multicastGroup;
					mreq.imr_multiaddr.s_addr=new_multicastGroup;
					last_multicast_address=multicastGroup>>24;
					new_multicastGroup=multicastGroup;
				}
			}
			else{
				multi_addr.sin_addr.s_addr = multicastGroup+current_station;
				mreq.imr_multiaddr.s_addr=multicastGroup+current_station;
			}
			setsockopt(udp_socket,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq));//join multicast group
		}
		reading=recvfrom(udp_socket,received_buffer,1400,0,(struct sockaddr *) &multi_addr,&addr_size);//read new chunk of song
		if(reading==0){
			perror("Connection closed by server");
			close(udp_socket);
			stop_flag=1;
			pthread_exit(NULL);
		}
		else if(reading==-1){
			perror("Error reading from socket");
			close(udp_socket);
			stop_flag=1;
			pthread_exit(NULL);
		}
		fwrite(received_buffer,sizeof(uint8_t),reading,fp);//write to pipe
	}
	close(udp_socket);
	pthread_exit(NULL);
}
///////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[]){
	if(argc!=3){
		printf("\n");
		printf(" Wrong argument input");
		printf("\n");
		printf("./radio_controller <server_ip> <tcpport> ");
		printf("\n");
		exit(0);
	}
	int announce_waiting=0;
	int permit_waiting=0;
	pthread_t radio_listener;
	int serverip=inet_addr(argv[1]);
	int tcpport=atoi(argv[2]);
	int tcp_socket;
	int i;
	int sn;
	uint8_t commandType = 0;
	char choose;
	uint8_t hello_buffer[3];
	hello_buffer[0]=commandType;
	hello_buffer[1]=0;
	hello_buffer[2]=0;
	uint8_t received_msg_buffer[257];
	memset(received_msg_buffer,'\0',sizeof(received_msg_buffer));
	char user_input[257];
	memset(user_input,'\0',sizeof(user_input));
	uint8_t songNameSize;
	char* songName;
	uint8_t replyStringSize;
	char* replyString;
	uint16_t newStationNumber;
	uint8_t permit;
	uint32_t songSize;
	uint32_t sent_songSize=0;
	uint8_t sending_song_buffer[1024];
	int newstation_waiting=0;
	int reading;
	FILE* fp2;
	struct timespec rqtp,rmtp={0,8000000};
	struct timeval timeout;//for select()
	timeout.tv_sec=0;
	timeout.tv_usec=300000;
	struct sockaddr_in client_addr;
	memset(client_addr.sin_zero, '\0', sizeof(client_addr.sin_zero));
	socklen_t addr_size;
	client_addr.sin_family=AF_INET;
	client_addr.sin_port = htons(tcpport); //16 bit TCP port number
	client_addr.sin_addr.s_addr = serverip; 
	tcp_socket=socket(AF_INET, SOCK_STREAM,0);
	if (tcp_socket < 0){
		perror("Error opening socket");
	}
	addr_size=sizeof(client_addr);
	if((connect(tcp_socket, (struct sockaddr *) &client_addr, addr_size))<0){//connecting to server
		close(tcp_socket);
		perror("Connection ERROR");
		exit(0);
	}
	sn=send(tcp_socket,hello_buffer,3,0);//sending 'Hello' message
	if(sn<0){
		close(tcp_socket);
		perror("Error sending to socket");
		exit(0);
	}
	fd_set fdset;//set of file descriptors for select()
	FD_ZERO(&fdset);
	FD_SET(tcp_socket,&fdset);
	int inputfd;
	int timeout_select=0;
	int number;
	inputfd=select(tcp_socket+1,&fdset,NULL,NULL,&timeout);//waiting for 'Welcome' packet
	if(inputfd==0){//Timeout - close connection
		close(tcp_socket);
		perror("Error sending to socket");
		exit(0);
	}
	else if(inputfd==-1){/*error in select func*/
		close(tcp_socket);
		perror("Error in select function");
		exit(0);
	}
	reading=recv(tcp_socket,received_msg_buffer,sizeof(received_msg_buffer),0);//receive message from server
	if(reading<0){//close the problem socket
		close(tcp_socket);
		perror("ERROR reading from socket");
		exit(0);
	}
	else if(reading==0){
		close(tcp_socket);
		perror("Connection closed by server");
		exit(0);
	}
	commandType=received_msg_buffer[0];
	/*Welcome:
		uint8_t replyType = 0;
		uint16_t numStations;
		uint32_t multicastGroup;
		uint16_t portNumber;
	*/
	if(commandType!=0){
		perror("Invalid message type received");
		close(tcp_socket);
		exit(0);
	}
	numStations=received_msg_buffer[1];
	numStations=numStations<<8;
	numStations+=received_msg_buffer[2];
	
	multicastGroup=received_msg_buffer[3];
	multicastGroup=multicastGroup<<8;
	multicastGroup+=received_msg_buffer[4];
	multicastGroup=multicastGroup<<8;
	multicastGroup+=received_msg_buffer[5];
	multicastGroup=multicastGroup<<8;
	multicastGroup+=received_msg_buffer[6];

	portNumber=received_msg_buffer[7];
	portNumber=portNumber<<8;
	portNumber+=received_msg_buffer[8];
	
	if(numStations<0 || multicastGroup<=0 || portNumber<=0){
		close(tcp_socket);
		perror("Invalid welcome message");
		exit(0);
	}
	if(numStations>0){//there station to listen
		if(pthread_create(&radio_listener,NULL,station_listner,NULL)!=0){
			perror("Error while creating station thread");
			close(tcp_socket);
			exit(0);
		}
	}
	FD_SET(tcp_socket,&fdset);
	FD_SET(0,&fdset);
	printf("\nNumber of stations: %u\n",numStations);
	printf("Multicast address: %u.%u.%u.%u\n",received_msg_buffer[6],received_msg_buffer[5],received_msg_buffer[4],received_msg_buffer[3]);
	printf("UDP port number: %u\n",portNumber);
	print_UI();
	while(stop_flag==0){//programm stop condition
		if (timeout_select==1){//use this select() if timeout needed
			FD_SET(tcp_socket,&fdset);
			FD_SET(0,&fdset);
			if(newstation_waiting==1){
				timeout.tv_sec=2;
				timeout.tv_usec=0;
			}
			else{
				timeout.tv_sec=0;
				timeout.tv_usec=300000;
			}
			inputfd=select(tcp_socket+1,&fdset,NULL,NULL,&timeout);
		}
		else{//use this select() if no need in timeout
			FD_SET(tcp_socket,&fdset);
			FD_SET(0,&fdset);
			inputfd=select(tcp_socket+1,&fdset,NULL,NULL,NULL);
		}
		if(inputfd==0){//Timeout - close connection
			close(tcp_socket);
			stop_flag=1;
			perror("Timeout occured");
			pthread_join(radio_listener,NULL);
			exit(0);
		}
		else if(inputfd==-1){/*error in select func*/
			close(tcp_socket);
			stop_flag=1;
			perror("Error in select function");
			pthread_join(radio_listener,NULL);
			exit(0);
		}
		else if(FD_ISSET(0,&fdset)){//'Enter' was pressed
			FD_SET(0,&fdset);
			memset(received_msg_buffer,'\0',sizeof(received_msg_buffer));
			reading=read(0,received_msg_buffer,sizeof(received_msg_buffer));//read user input
			choose=(char)received_msg_buffer[0];
			if(reading<0){//close the problem socket
				close(tcp_socket);
				stop_flag=1;
				perror("ERROR reading from stdin");
				pthread_join(radio_listener,NULL);
				exit(0);
			}
			else if(reading==0 || atoi(&choose)>4 || atoi(&choose)<1){
				printf("\nWrong input: %u\n",received_msg_buffer[0]);
				print_UI();
			}
			else if(numStations==0 && (atoi(&choose)==1 || atoi(&choose)==2)){
				printf("\nThere is no availible stations. You can only upload song or quit\n");
				print_UI();
			}
			else{
				memset(user_input,'\0',sizeof(user_input));
				switch(atoi(&choose)){
					case 1://Check song name
						printf("\nType number of station from 0 to %d: ",numStations-1);
						scanf("%d",&number);
						if(number<0 || number>=numStations){
							printf("\nWrong input\n");
							print_UI();
						}
						else{
							//AskSong:
								//uint8_t commandType = 1;
							uint16_t stationNumber;
							uint8_t ask_song_buffer[3];
							timeout_select=1;
							commandType = 1;
							stationNumber=number;
							ask_song_buffer[0]=commandType;
							ask_song_buffer[1]=stationNumber >> 8;
							ask_song_buffer[2]=stationNumber & 0x00ff;
							send(tcp_socket,ask_song_buffer,3,0);
							announce_waiting=1;//waiting for response - timeout will be ON
							//timeout.tv_sec=0;
							//timeout.tv_usec=300000;
						}						
						break;
					case 2://Change station
						printf("\nType number of station from 0 to %d: ",numStations-1);
						scanf("%d",&number);
						if(number<0 || number>=numStations){//invalid station number
							printf("\nWrong input\n");
						}
						else{
							current_station=number<<24;
						}
						print_UI();
						break;
					case 3://Upload new song
						printf("\nPlease type the song name in format '$$$$.mp3'(maximum lenght 255 chars): ");
						scanf("%s",user_input);
						if(strlen(user_input)<4 || user_input[strlen(user_input)-4]!='.' || user_input[strlen(user_input)-3]!='m' || user_input[strlen(user_input)-2]!='p' || user_input[strlen(user_input)-1]!='3'){
							printf("\nWrong file format, please enter name of MP3 file\n");
							print_UI();
							break;
						}
						songName=(char*)malloc((strlen(user_input)+1)*sizeof(char));
						for(i=0;i<strlen(user_input);i++){
							songName[i]=user_input[i];
						}
						songName[strlen(user_input)]='\0';
						fp2=fopen(songName,"r");
						if(fp2==NULL){
							printf("\nNo file with this name\n");
							print_UI();
							break;
						}
						fseek(fp2,0L,SEEK_END);
						songSize=ftell(fp2);
						fclose(fp2);
						//UpSong:
							//uint8_t commandType = 2;
							//uint32_t songSize; //in bytes
							//uint8_t songNameSize;
							//char songName[songNameSize];
						commandType = 2;
						songNameSize=strlen(user_input);
						uint8_t* upsong_msg_buffer=(uint8_t*)malloc((songNameSize+6)*sizeof(uint8_t));
						upsong_msg_buffer[0]=commandType;
						upsong_msg_buffer[1]=songSize>>24;
						upsong_msg_buffer[2]=(songSize>>16) & 0x00ff;
						upsong_msg_buffer[3]=(songSize<<16)>>24;
						upsong_msg_buffer[4]=(songSize<<24)>>24;
						/*upsong_msg_buffer[4]=songSize>>24;
						upsong_msg_buffer[3]=(songSize>>16) & 0x00ff;
						upsong_msg_buffer[2]=(songSize<<16)>>24;
						upsong_msg_buffer[1]=(songSize<<24)>>24;*/
						upsong_msg_buffer[5]=songNameSize;
						for(i=0;i<songNameSize;i++){
							upsong_msg_buffer[i+6]=user_input[i];
						}
						send(tcp_socket,upsong_msg_buffer,songNameSize+6,0);//sending UpSong
						timeout_select=1;
						permit_waiting=1;
						//timeout.tv_sec=0;
						//timeout.tv_usec=300000;
						break;
					case 4://Quit option
						close(tcp_socket);
						stop_flag=1;
						printf("\n\nBye");
						pthread_join(radio_listener,NULL);
						exit(0);
						break;
				}
			}
		}
		else if(FD_ISSET(tcp_socket,&fdset)){//new message arrived in TCP socket
			//FD_SET(tcp_socket,&fdset);
			memset(received_msg_buffer,'\0',sizeof(received_msg_buffer));
			reading=recv(tcp_socket,received_msg_buffer,1,0);//receive type of message from server
			if(reading<0){//close the problem socket
				close(tcp_socket);
				stop_flag=1;
				perror("ERROR reading from socket");
				pthread_join(radio_listener,NULL);
				exit(0);
			}
			else if(reading==0){
				close(tcp_socket);
				stop_flag=1;
				perror("Connection closed by server");
				pthread_join(radio_listener,NULL);
				exit(0);
			}
			commandType=received_msg_buffer[0];
			switch(commandType){
				case 0://Welcome
					close(tcp_socket);
					stop_flag=1;
					perror("Invalid message received: 'Welcome' type");
					pthread_join(radio_listener,NULL);
					exit(0);
					break;
				case 1://Announce
					/*Announce:
						uint8_t replyType = 1;
						uint8_t songNameSize;
						char songName[songNameSize];
					*/
					if(announce_waiting==0){//AskSong was not sent before
						close(tcp_socket);
						stop_flag=1;
						perror("Announce received without sending AskSong");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					announce_waiting=0;
					reading=recv(tcp_socket,received_msg_buffer,1,MSG_DONTWAIT);//receive uint8_t songNameSize
					if(reading<0){//close the problem socket
						close(tcp_socket);
						stop_flag=1;
						perror("ERROR reading from socket");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					else if(reading==0){
						close(tcp_socket);
						stop_flag=1;
						perror("Connection closed by server");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					songNameSize=received_msg_buffer[0];
					if(songNameSize<=0){
						close(tcp_socket);
						stop_flag=1;
						perror("\nUnknown message type received");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					reading=recv(tcp_socket,received_msg_buffer,songNameSize,MSG_DONTWAIT);//receive name of song
					if(reading<0){//close the problem socket
						close(tcp_socket);
						stop_flag=1;
						perror("ERROR reading from socket");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					else if(reading==0){
						close(tcp_socket);
						stop_flag=1;
						perror("Connection closed by server");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					if(songNameSize!=reading){
						close(tcp_socket);
						stop_flag=1;
						perror("\nNot enough characters message");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					songName=(char*)malloc((songNameSize+1)*sizeof(char));
					memset(songName,'\0',sizeof(songName));
					for(i=0;i<songNameSize;i++){
						songName[i]=(char)received_msg_buffer[i];
					}
					songName[songNameSize]='\0';
					printf("\nSong name is: ");
					for(i=0;i<songNameSize;i++){
						printf("%c",songName[i]);
					}
					printf("\n");
					free(songName);
					timeout_select=0;
					print_UI();
					break;
				case 2://PermitSong
					/*PermitSong:
						uint8_t replyType = 2;
						uint8_t permit;
					*/
					if(permit_waiting==0){
						close(tcp_socket);
						stop_flag=1;
						perror("PermitSong received without sending UpSong");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					permit_waiting=0;
					reading=recv(tcp_socket,received_msg_buffer,1,MSG_DONTWAIT);//receive permit status
					if(reading<0){//close the problem socket
						close(tcp_socket);
						stop_flag=1;
						perror("ERROR reading from socket");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					else if(reading==0){
						close(tcp_socket);
						stop_flag=1;
						perror("Connection closed by server");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					permit=received_msg_buffer[0];
					if(permit==0){
						printf("\nCan't upload song, server refuse\n");
						sent_songSize=0;
						timeout_select=0;
						newstation_waiting=0;
						print_UI();
						//fclose(fp2);
						break;
					}
					else if(permit!=1){
						printf("\nError in 'PermitSong' message: unknown value in 'permit'");
						stop_flag=1;
						close(tcp_socket);
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					else{
						printf("\nUpload in progress...\n");
						fp2=fopen(songName,"r");
						if(fp2==NULL){
							printf("\nError opening file\n");
							stop_flag=1;
							close(tcp_socket);
							pthread_join(radio_listener,NULL);
							exit(0);
							break;
						}
						while(sent_songSize<songSize){//sending song at given rate
							memset(sending_song_buffer,'\0',sizeof(sending_song_buffer));
							reading=fread(sending_song_buffer,1,1024,fp2);//take "buffer" from mp3 file
							if(feof(fp2)){
								sent_songSize=songSize;
							}
							sn=send(tcp_socket,sending_song_buffer,reading,0);
							if(sn<0){
								perror("Error sending to socket");
								close(tcp_socket);
								fclose(fp2);
								stop_flag=1;
								free(songName);
								pthread_join(radio_listener,NULL);
								exit(0);
							}
							sent_songSize+=sn;
							usleep(8000);
						}
						free(songName);
					}
					printf("\nUpload end.\n");
					sent_songSize=0;
					timeout_select=1;
					newstation_waiting=1;
					fclose(fp2);
					break;
				case 3://InvalidCommand
					/*InvalidCommand:
						uint8_t replyType = 3;
						uint8_t replyStringSize;
						char replyString[replyStringSize];
					*/
					replyStringSize=received_msg_buffer[1];
					replyString=(char*)malloc((replyStringSize+1)*sizeof(char));
					for(i=0;i<replyStringSize;i++){
						replyString[i]=received_msg_buffer[i+2];
					}
					replyString[replyStringSize]='\0';
					printf("\nInvalid message received: %s",replyString);
					free(replyString);
					stop_flag=1;
					close(tcp_socket);
					pthread_join(radio_listener,NULL);
					exit(0);
					break;
				case 4://NewStations
					/*NewStations:
						uint8_t replyType = 4;
						uint16_t newStationNumber
					*/
					reading=recv(tcp_socket,received_msg_buffer,2,MSG_DONTWAIT);//check if there message received
					if(reading<0){//close the problem socket
						close(tcp_socket);
						stop_flag=1;
						perror("ERROR reading from socket");
						pthread_join(radio_listener,NULL);
						exit(0);
					}
					else if(reading==0){
						close(tcp_socket);
						stop_flag=1;
						perror("Connection closed by server");
						pthread_join(radio_listener,NULL);
						exit(0);
						break;
					}
					if(newstation_waiting==1){
						newstation_waiting=0;
						timeout_select=0;
						timeout.tv_sec=2;
						timeout.tv_usec=0;
					}
					newStationNumber=received_msg_buffer[0];
					newStationNumber=newStationNumber<<8;
					newStationNumber+=received_msg_buffer[1];
					printf("\nNew station is online. Total number of stations is %d\n",newStationNumber);
					print_UI();
					numStations=newStationNumber;
					break;
				default://wrong message type
					close(tcp_socket);
					stop_flag=1;
					perror("\nUnknown message type received");
					pthread_join(radio_listener,NULL);
					exit(0);
					break;
			}
		}
	}
	close(tcp_socket);
	printf("\n\nBye");
	pthread_join(radio_listener,NULL);
	exit(0);
}
