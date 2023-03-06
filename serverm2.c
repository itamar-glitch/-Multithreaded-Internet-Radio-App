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

#define buffer_size 258
#define MAX_CLIENTS 100
#define EBUSY 16
//#define stream_rate 62500000 //in nanosec
//#define stream_rate 23500000
#define stream_rate 60000000
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
pthread_mutex_t listlock;
pthread_mutex_t permitlock;
int sockets_file_descriptors[MAX_CLIENTS];
struct sockaddr_in users_addr[MAX_CLIENTS];
char **list_of_stations=NULL;
int total_connected_clients=0;
uint16_t total_stations_number=0;
int stop_flag=0;
int max_socket_num;
uint32_t multicastip;
uint16_t udpport;
uint8_t permit=0;
pthread_t finish;
int new_song_sending=0;
//uint8_t songNameSizeforThread;
FILE** transmission_file_list;
int* station_socket_list;
int new_stream_rate;
///////////////////function gives permit for upload////////////////////////
int permit_upload(){
	if(permit==1){
		return 0;
	}
	int mtl;
	mtl=pthread_mutex_trylock(&permitlock);//try to up the flag of transmission
	if(mtl==EBUSY){//mutex locked return to car routine
		return 0;
	}
	permit=1;
	pthread_mutex_unlock(&permitlock);
	return 1;
}
/////////////////function checks if name of new song not present in list already//////////////////////////
int check_song_name(char* new_song,uint8_t _len_song){
	uint8_t len_song=_len_song;
	if(list_of_stations==NULL){
		return 0;
	}
	int i,j;
	for(i=0;i<total_stations_number;i++){
		if(len_song!=strlen(list_of_stations[i])){//look for existing song name with the same lenght as new
			continue;
		}
		for(j=0;j<len_song;j++){
			if(new_song[j]!=list_of_stations[i][j]){//if one of chars is different - stop
				break;
			}
			else if(j==len_song-1){
				return 1;
			}
		}
	}
	return 0;
}
///////////////////function that changes list of users/////////////////////
int list_manager_function(int new_tcp_socket, int add_rm){
	int i;
	if(add_rm==0){//add new client
		for(i=0;i<MAX_CLIENTS;i++){//find empty place in array for new file descriptor
			if(sockets_file_descriptors[i]==0){//place found
				sockets_file_descriptors[i]=new_tcp_socket;//save new file descriptor in list
				total_connected_clients+=1;//one place less in chat room
				if(max_socket_num<(new_tcp_socket+1)){//new socket has a biggest number in list
					max_socket_num=new_tcp_socket+1;
				}
				break;
			}
		}
	}
	else{//remove client
		for(i=0;i<MAX_CLIENTS;i++){//find client in array of file descriptors
			if(sockets_file_descriptors[i]==new_tcp_socket){//place found
				sockets_file_descriptors[i]=0;//delete file descriptor in list
				total_connected_clients-=1;//one place free in server
				break;
			}
		}
	}
	return 0;
}
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
int print_UI(){
	printf("\nChoose option number and press 'Enter':\n");
	printf("   p - Print database\n");
	printf("   q - Quit\n");
	return 0;
}
/////////////function that opens udp socket to play music//////////////////
void* station_function(){
	//uint32_t st_num=(uint32_t)_st_num;//place of station in list of songs
	uint32_t st_num=0;
	int station_socket;
	char buffer[1024];
	//FILE* transmission_file;
	int sn,sn2;
	//new_stream_rate=stream_rate/total_stations_number;
	new_stream_rate=stream_rate;
	struct timespec rqtp,rmtp={0,new_stream_rate};//transmission rate - nanosleep()
	struct sockaddr_in udp_sock_addr;
	socklen_t addr_size;
	udp_sock_addr.sin_family=AF_INET;
	udp_sock_addr.sin_port = htons(udpport); //16 bit port number
	udp_sock_addr.sin_addr.s_addr = multicastip+(st_num << 24); //multicast address for current station
	memset(udp_sock_addr.sin_zero, '\0', sizeof(udp_sock_addr.sin_zero));
	addr_size=sizeof(udp_sock_addr);
	int ttl_new=20;
	int* ptr_ttl_new=&ttl_new;
	int new_total_stations_number=total_stations_number;
	uint16_t last_multicast_address=(multicastip>>24);
	uint16_t second_multicast_address=(multicastip>>16) & 0x00ff;
	uint32_t new_multicastip=multicastip;
	uint16_t third_multicast_address=(multicastip>>8);
	third_multicast_address=(third_multicast_address<<24)>>24;
	for(st_num=0;st_num<total_stations_number;st_num++){
		transmission_file_list[st_num]=fopen(list_of_stations[st_num],"r");//open mp3 file to transmit
		if(transmission_file_list[st_num]==NULL){
		    perror("Error opening file");
		}
		station_socket_list[st_num]=socket(AF_INET, SOCK_DGRAM,0);//UDP socket - station that "plays" music to multicast
		if (station_socket < 0){
			perror("Error opening UDP socket");
		}
		if(setsockopt(station_socket_list[st_num],IPPROTO_IP,IP_MULTICAST_TTL,ptr_ttl_new,sizeof(int))<0){//change TTL of packet on IP level
			perror("Error while changing TTL");
		}
	}
	st_num=0;
	uint32_t k=1;
	uint32_t counter=0;
	memset(buffer,'\0',sizeof(buffer));
	while(stop_flag==0){//end of programm condition
		new_multicastip=multicastip;
		counter=0;
		last_multicast_address=(multicastip>>24);
		second_multicast_address=(multicastip>>16) & 0x00ff;
		for(st_num=0;st_num<new_total_stations_number;st_num++){
			if(last_multicast_address>255){
				second_multicast_address+=1;
				if(second_multicast_address>255){
					last_multicast_address=0;
					second_multicast_address=0;
					new_multicastip+=256;
					new_multicastip=(new_multicastip<<16)>>16;
					counter=0;
				}
				else{
					last_multicast_address=0;
					new_multicastip=new_multicastip+(k<<16);
					new_multicastip=(new_multicastip<<8)>>8;
					counter=0;
				}
			}
			new_total_stations_number=total_stations_number;
			udp_sock_addr.sin_addr.s_addr = new_multicastip+(counter << 24); //multicast address for current station
			counter+=1;
			last_multicast_address=last_multicast_address+1;
			if(stop_flag==1){
				break;
			}
			else if(feof(transmission_file_list[st_num])){//if EOF reload file
				fclose(transmission_file_list[st_num]);
				transmission_file_list[st_num]=fopen(list_of_stations[st_num],"r+");//open file to transmit
				continue;
			}
			
			sn2=fread(buffer,1,1024,transmission_file_list[st_num]);//take "buffer" from mp3 file
			sn=sendto(station_socket_list[st_num],buffer,sn2,0,(struct sockaddr *) &udp_sock_addr,sizeof(udp_sock_addr));//send buffer to chosen address
			if(sn<0){
				perror("Error sending to socket");
				close(station_socket_list[st_num]);
				fclose(transmission_file_list[st_num]);
				stop_flag=1;
				pthread_exit(NULL);
			}
			
			memset(buffer,'\0',sizeof(buffer));
		}
		nanosleep(&rqtp,&rmtp);//sleep to receive a needed rate
		rmtp.tv_nsec=new_stream_rate;
		rmtp.tv_sec=0;
		rqtp.tv_nsec=new_stream_rate;
		rqtp.tv_sec=0;
	}
	for(st_num=0;st_num<total_stations_number;st_num++){
		fclose(transmission_file_list[st_num]);
	}
}
///////////////////////////////////////////////////////////////////////////
int sending_newstation(uint8_t new_station_buffer[]){//sends NewStation packet to all users
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(sockets_file_descriptors[i]!=0){
			send(sockets_file_descriptors[i],new_station_buffer,3,0);
		}
	}
	new_song_sending=0;//release flag
}
//////////////////////////////////////////////////////////////////////////
/////////////function that do select() for specific user//////////////////
void* user_func(void* _new_tcp_socket){
	int new_tcp_socket=(int) _new_tcp_socket;
	uint8_t received_buffer[257];
	memset(received_buffer,'\0',sizeof(received_buffer));
	uint8_t hello_pckt[4];
	hello_pckt[3]=0;
	uint8_t new_station_buffer[3];
	uint8_t welcome_msg_buffer[9];
	uint8_t commandType;
	uint8_t songNameSize;
	uint16_t stationNumber;
	uint8_t replyStringSize;
	uint8_t invalid_msg_buffer[36];
	uint8_t wrong_msg_buffer[38];
	uint8_t timeout_buffer[17];
	uint8_t read_err_buffer[27];
	uint8_t thread_error_buffer[41];
	uint8_t received_song_buffer[1500];
	uint8_t invalid_size_buffer[20];
	/*invalid msg strings*/
	char* wrong_hello="Wrong message type received: Hello";
	char* unrecognized_type="Unrecognized type message received";
	char* wrong_station="There is no station with this number";
	char* timeout_occur="Timeout occured";
	char* read_err="Error reading from socket";//25
	char* thread_error="Error while creating new station thread";
	char* ival_size="Invalid size field";//18
	/********************/
	int inputfdusr;
	int reading,i;
	int case_opt=0;
	uint8_t replyType;
	struct timeval timeout;//for select()
	timeout.tv_sec=0;
	timeout.tv_usec=300000;
	fd_set fdsetusr;//set of file descriptors for select()
	FD_ZERO(&fdsetusr);
	FD_SET(new_tcp_socket,&fdsetusr);//add new file descriptor to fd_set
	int timeout_select=1;
	while(stop_flag==0){//end of programm condition
		if (timeout_select==1){//use this select() if timeout needed
			inputfdusr=select(max_socket_num,&fdsetusr,NULL,NULL,&timeout);
		}
		else{//use this select() if no need in timeout
			inputfdusr=select(max_socket_num,&fdsetusr,NULL,NULL,NULL);
		}
		while(new_song_sending==1);//dont move there thread that sends NewStation message to all clients
		if(inputfdusr==0){//Timeout - close connection - Hello message is not received
			perror("Timeout occured");
			replyType = 3;
			replyStringSize=15;
			timeout_buffer[0]=replyType;
			timeout_buffer[1]=replyStringSize;
			int i=2;
			for(i=2;i<replyStringSize+2;i++){
				timeout_buffer[i]=timeout_occur[i-2];
			}
			list_manager_function(new_tcp_socket, 1);
			send(new_tcp_socket,timeout_buffer,sizeof(timeout_buffer),0);
			close(new_tcp_socket);
			print_UI();
			pthread_exit(NULL);
		}
		else if(inputfdusr==-1){/*error in select func*/
			list_manager_function(new_tcp_socket, 1);
			close(new_tcp_socket);
			perror("Error in select function of user thread");
			print_UI();
			pthread_exit(NULL);
		}
		if(case_opt==0){//connection case - look for hello packet
			reading=recv(new_tcp_socket,hello_pckt,4,0);//read the "hello" message from new client
			if(reading<0){//close the problem socket
				list_manager_function(new_tcp_socket, 1);
				close(new_tcp_socket);
				perror("ERROR reading from socket");
				print_UI();
				pthread_exit(NULL);
			}
			else if(reading==0){
				list_manager_function(new_tcp_socket, 1);
				close(new_tcp_socket);
				perror("Connection closed by client");
				print_UI();
				pthread_exit(NULL);
			}
			else{//message received
				commandType=hello_pckt[0];
				if(commandType!=0 || hello_pckt[3]!=0){//invalid type of message - close connection
					list_manager_function(new_tcp_socket, 1);
					close(new_tcp_socket);
					perror("Invalid 'Hello' message");
					print_UI();
					pthread_exit(NULL);
				}
				/////////////////////////////////////send here Welcome packet/////////////////////////////////////
				replyType = 0;
				welcome_msg_buffer[0]=replyType;//uint8_t

				welcome_msg_buffer[1]=((total_stations_number & 0xff00) >> 8);//uint16_t
				welcome_msg_buffer[2]=total_stations_number & 0x00ff;

				welcome_msg_buffer[3]=(multicastip >> 24);//uint32_t
				welcome_msg_buffer[4]=(multicastip >> 16) & 0x00ff;
				welcome_msg_buffer[5]=(multicastip << 16) >>24;
				welcome_msg_buffer[6]=((multicastip << 16) >> 16) & 0x00ff;

				welcome_msg_buffer[7]=((udpport & 0xff00) >> 8);//uint16_t
				welcome_msg_buffer[8]=udpport & 0x00ff;

				if(send(new_tcp_socket,welcome_msg_buffer,sizeof(welcome_msg_buffer),0)==-1){//send here Welcome packet
					list_manager_function(new_tcp_socket, 1);
					perror("Error sending 'Welcome message'");
					close(new_tcp_socket);
					print_UI();
					pthread_exit(NULL);
				}
				case_opt=1;//connection done
				timeout_select=0;//no need in timeout anymore
			}
			FD_SET(new_tcp_socket,&fdsetusr);//reset fd_set
		}
		else{//here all other packets will be received
			memset(received_buffer,'\0',sizeof(received_buffer));
			while(new_song_sending==1);//dont move there thread that sends NewStation message to all clients
			reading=recv(new_tcp_socket,received_buffer,sizeof(received_buffer),0);//read new message from a client
			if(reading==0){
				list_manager_function(new_tcp_socket, 1);
				perror("Connection closed by client");
				close(new_tcp_socket);
				print_UI();
				pthread_exit(NULL);
			}
			else if(reading==-1){
				perror("Error reading from socket");
				/*replyType = 3;
				replyStringSize=34;
				read_err_buffer[0]=replyType;
				read_err_buffer[1]=replyStringSize;
				for(i=2;i<replyStringSize+2;i++){
					read_err_buffer[i]=read_err[i-2];
				}
				list_manager_function(new_tcp_socket, 1);
				send(new_tcp_socket,read_err_buffer,sizeof(read_err_buffer),0);*/
				close(new_tcp_socket);
				print_UI();
				pthread_exit(NULL);
			}
			commandType=received_buffer[0];//take the type of message from buffer
			switch(commandType){
				case 0://hello message - wrong case
					list_manager_function(new_tcp_socket, 1);
					perror("Wrong message type received: Hello");
					replyType = 3;
					replyStringSize=34;
					invalid_msg_buffer[0]=replyType;
					invalid_msg_buffer[1]=replyStringSize;
					int i=2;
					for(i=2;i<replyStringSize+2;i++){
						invalid_msg_buffer[i]=wrong_hello[i-2];
					}
					send(new_tcp_socket,invalid_msg_buffer,sizeof(invalid_msg_buffer),0);
					close(new_tcp_socket);
					print_UI();
					pthread_exit(NULL);

				case 1://ask song message
					/*if(strnlen(received_buffer,)!=3){//check validity
						list_manager_function(new_tcp_socket, 1);
						perror("Too much bytes in AskSong message");
						replyType = 3;
						replyStringSize=34;
						invalid_msg_buffer[0]=replyType;
						invalid_msg_buffer[1]=replyStringSize;
						for(i=2;i<replyStringSize+2;i++){
							invalid_msg_buffer[i]=unrecognized_type[i-2];
						}
						send(new_tcp_socket,invalid_msg_buffer,sizeof(invalid_msg_buffer),0);
						close(new_tcp_socket);
						print_UI();
						pthread_exit(NULL);
						
					}*/
					stationNumber=received_buffer[1];
					stationNumber=stationNumber<<8;
					stationNumber=stationNumber+received_buffer[2];
					if(total_stations_number<stationNumber || stationNumber<0){//wrong station number - close connection
						list_manager_function(new_tcp_socket, 1);
						perror("There is no station with this number");
						replyType = 3;
						replyStringSize=36;
						wrong_msg_buffer[0]=replyType;
						wrong_msg_buffer[1]=replyStringSize;
						for(i=2;i<replyStringSize+2;i++){
							wrong_msg_buffer[i]=wrong_station[i-2];
						}
						send(new_tcp_socket,wrong_msg_buffer,sizeof(wrong_msg_buffer),0);
						close(new_tcp_socket);
						print_UI();
						pthread_exit(NULL);
					}
					replyType = 1;//send announce
					songNameSize=strlen(list_of_stations[stationNumber]);//uint8_t 
					uint8_t* announce_buffer=(uint8_t*)malloc((songNameSize+2)*sizeof(uint8_t));
					announce_buffer[0]=replyType;
					announce_buffer[1]=songNameSize;
					for(i=2;i<songNameSize+2;i++){
						announce_buffer[i]=list_of_stations[stationNumber][i-2];
					}
					while(new_song_sending==1);//dont move there thread that sends NewStation message to all clients
					send(new_tcp_socket,announce_buffer,songNameSize+2,0);
					free(announce_buffer);
					FD_SET(new_tcp_socket,&fdsetusr);//reset fd_set
					memset(received_buffer,'\0',sizeof(received_buffer));
					break;
				case 2:;//up song message
					/*lets read the song data from upsong msg*/
					int j;
					j=0;
					uint32_t songSize=0; //in bytes
					int name_check=0;
					int up_status;
					uint8_t permitsong_buffer[2];
					char* song_name_buffer=(char*)malloc(songNameSize+1);
					memset(song_name_buffer,'\0',sizeof(song_name_buffer));
					songSize=received_buffer[1];//uint32_t
					songSize=songSize<<8;
					songSize=songSize+received_buffer[2];
					songSize=songSize<<8;
					songSize=songSize+received_buffer[3];
					songSize=songSize<<8;
					songSize=songSize+received_buffer[4];
					songNameSize=received_buffer[5];//uint8_t
					/*if(strnlen(received_buffer)!=(6+songNameSize)){//check validity
						list_manager_function(new_tcp_socket, 1);
						perror("Too much bytes in Upsong message");
						replyType = 3;
						replyStringSize=34;
						invalid_msg_buffer[0]=replyType;
						invalid_msg_buffer[1]=replyStringSize;
						for(i=2;i<replyStringSize+2;i++){
							invalid_msg_buffer[i]=unrecognized_type[i-2];
						}
						send(new_tcp_socket,invalid_msg_buffer,sizeof(invalid_msg_buffer),0);
						close(new_tcp_socket);
						print_UI();
						pthread_exit(NULL);
						
					}
					else if(songSize<=0 || songNameSize<5){
						list_manager_function(new_tcp_socket, 1);
						perror("Wrong size field in Upsong message");
						replyType = 3;
						replyStringSize=34;
						invalid_msg_buffer[0]=replyType;
						invalid_msg_buffer[1]=replyStringSize;
						for(i=2;i<replyStringSize+2;i++){
							invalid_msg_buffer[i]=unrecognized_type[i-2];
						}
						send(new_tcp_socket,invalid_msg_buffer,sizeof(invalid_msg_buffer),0);
						close(new_tcp_socket);
						print_UI();
						pthread_exit(NULL);
					}*/
						/////////////////////////////////////////////////////check validity!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
					if(songSize<2000 || songSize>1e+7 || songNameSize<5 || songNameSize>255){
						//invalid_size_buffer[20];
						//ival_size="Invalid size field";//18
						replyStringSize=18;
						invalid_msg_buffer[0]=3;
						invalid_msg_buffer[1]=replyStringSize;
						for(i=2;i<replyStringSize+2;i++){
							invalid_size_buffer[i]=ival_size[i-2];
						}
						while(new_song_sending==1);//dont move there thread that sends NewStation message to all clients
						send(new_tcp_socket,invalid_msg_buffer,sizeof(invalid_msg_buffer),0);
					}
					else{
						for(j=0;j<songNameSize;j++){//loads song into buffer
							song_name_buffer[j]=received_buffer[j+6];
						}
						song_name_buffer[songNameSize]='\0';
						/********************************************/
						replyType = 2;
						
						name_check=check_song_name(song_name_buffer,songNameSize);//check if there no such song in buffer
					}
					if(name_check==0){//song doesnt exist
						up_status=permit_upload();//try to start download process
					}
					else{//song already in list
						up_status=0;
					}
					if(up_status==0){//another upload in progress || invalid name
						permitsong_buffer[0]=replyType;
						permitsong_buffer[1]=0;
						while(new_song_sending==1);//dont move there thread that sends NewStation message to all clients
						send(new_tcp_socket,permitsong_buffer,sizeof(permitsong_buffer),0);
					}
					else{//user can upload song
						memset(received_song_buffer,'\0',sizeof(received_song_buffer));
						uint32_t received_byte_size=0;
						uint32_t received_total_size=0;
						permitsong_buffer[0]=replyType;
						permitsong_buffer[1]=1;
						send(new_tcp_socket,permitsong_buffer,sizeof(permitsong_buffer),0);//permition sent
						timeout.tv_sec=3;
						timeout.tv_usec=0;
						FILE* new_song;
						new_song=fopen(song_name_buffer,"w");//create new mp3 file
						timeout.tv_sec=3;
						timeout.tv_usec=0;
						FD_SET(new_tcp_socket,&fdsetusr);
						printf("\nDownload in progress...\n");
						/****************************************************************************************/
						//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!/////////////upload////////////////////////////////////
						/****************************************************************************************/
						while(received_total_size<songSize){//receiving song
							inputfdusr=select(max_socket_num,&fdsetusr,NULL,NULL,&timeout);//need for case of timeout 
							if(inputfdusr==0){//Timeout - disconnect
								perror("Timeout occured");
								replyType = 3;
								replyStringSize=15;
								timeout_buffer[0]=replyType;
								timeout_buffer[1]=replyStringSize;
								int i=2;
								for(i=2;i<replyStringSize+2;i++){
									timeout_buffer[i]=timeout_occur[i-2];
								}
								list_manager_function(new_tcp_socket, 1);
								send(new_tcp_socket,timeout_buffer,sizeof(timeout_buffer),0);
								close(new_tcp_socket);
								fclose(new_song);
								free(song_name_buffer);
								permit=0;//upload finished - release flag
								print_UI();
								pthread_exit(NULL);
							}
							else if(inputfdusr==-1){/*error in select func*/
								list_manager_function(new_tcp_socket, 1);
								close(new_tcp_socket);
								perror("Error in select function of user thread");
								print_UI();
								pthread_exit(NULL);
							}
							received_byte_size=recv(new_tcp_socket,received_song_buffer,sizeof(received_song_buffer)-1,MSG_DONTWAIT);//receive chunk of song
							received_total_size+=received_byte_size;//count received bytes
							for(i=0;i<received_byte_size;i++){
								fprintf(new_song,"%c",received_song_buffer[i]);
							}
							memset(received_song_buffer,'\0',sizeof(received_song_buffer));
							FD_SET(new_tcp_socket,&fdsetusr);
							timeout.tv_sec=3;
							timeout.tv_usec=0;
						}
						fclose(new_song);
						printf("\nNew song succesfully downloaded\n");
						/****************************************************************************************************************************/
						if(list_of_stations==NULL){
							list_of_stations=(char**)malloc(sizeof(char*));//allocate list of songs
						}
						else{
							list_of_stations=(char**)realloc(list_of_stations,(sizeof(char*)*(total_stations_number+1)));//realloc list of song to bigger list
						}
						list_of_stations[total_stations_number]=(char*)malloc((songNameSize+1)*sizeof(char));//place for new song name
						printf("\nNew song is: ");
						for(j=0;j<songNameSize;j++){//loads new song into stations list
							list_of_stations[total_stations_number][j]=song_name_buffer[j];
							printf("%c",list_of_stations[total_stations_number][j]);
						}
						printf("\n");
						list_of_stations[total_stations_number][songNameSize]='\0';
						transmission_file_list=(FILE**)realloc(transmission_file_list,(total_stations_number+1)*sizeof(FILE*));
						transmission_file_list[total_stations_number]=fopen(list_of_stations[total_stations_number],"r");//open mp3 file to transmit
						station_socket_list=(int*)realloc(station_socket_list,(total_stations_number+1)*sizeof(int));
						station_socket_list[total_stations_number]=socket(AF_INET, SOCK_DGRAM,0);//UDP socket - station that "plays" music to multicast
						if (station_socket_list[total_stations_number] < 0){
							perror("Error opening UDP socket");
						}
						int ttl_new=20;
						int* ptr_ttl_new=&ttl_new;
						if(setsockopt(station_socket_list[total_stations_number],IPPROTO_IP,IP_MULTICAST_TTL,ptr_ttl_new,sizeof(int))<0){//change TTL of packet on IP level
							perror("Error while changing TTL");
						}
						/*pthread_t new_station;
						if(pthread_create(&new_station,NULL,station_function,total_stations_number)!=0){//creates new station
							perror("Error while creating new station thread");//39
							free(list_of_stations[total_stations_number]);
							list_of_stations=(char**)realloc(list_of_stations,(sizeof(char*)*(total_stations_number)));//realloc list of song to bigger list
							list_manager_function(new_tcp_socket, 1);
							replyType = 3;
							replyStringSize=39;
							invalid_msg_buffer[0]=replyType;
							invalid_msg_buffer[1]=replyStringSize;
							for(i=2;i<replyStringSize+2;i++){
								thread_error_buffer[i]=thread_error[i-2];
							}
							send(new_tcp_socket,thread_error_buffer,sizeof(thread_error_buffer),0);
							close(new_tcp_socket);
							print_UI();
							pthread_exit(NULL);
							
						}*/
						total_stations_number+=1;
						/****************************************************************************************/
						//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!send NewStations////////////////////////////////////
						/****************************************************************************************/
						replyType = 4;
						new_station_buffer[0]=replyType;
						new_station_buffer[1]=(total_stations_number & 0xff00) >>8;
						new_station_buffer[2]=total_stations_number & 0x00ff;
						new_song_sending=1;
						sending_newstation(new_station_buffer);//the function will send NewStation to all users
						permit=0;//upload finished - release flag
						print_UI();
					}
					free(song_name_buffer);
					FD_SET(new_tcp_socket,&fdsetusr);//reset fd_set
					break;
				default://unrecognized message - disconnect client
					list_manager_function(new_tcp_socket, 1);
					perror("Unrecognized type message received");
					replyType = 3;
					replyStringSize=34;

					invalid_msg_buffer[0]=replyType;
					invalid_msg_buffer[1]=replyStringSize;
					for(i=2;i<replyStringSize+2;i++){
						invalid_msg_buffer[i]=unrecognized_type[i-2];
					}
					send(new_tcp_socket,invalid_msg_buffer,sizeof(invalid_msg_buffer),0);
					close(new_tcp_socket);
					print_UI();
					pthread_exit(NULL);
			}
		}
	}
}
///////////////////////////////////////////////////////////////////////////
///////////////////////////End function////////////////////////////////////
void* end_func(){//closes all stuff
	while(stop_flag==0);{
		sleep(1);
	}
	sleep(1);
	while(permit==1);
	permit=1;
	int i;
	if(pthread_mutex_destroy(&listlock)!=0){
		perror("Error destroying mutex\n");
		exit(0);
	}
	if(pthread_mutex_destroy(&permitlock)!=0){
		perror("Error destroying mutex\n");
		exit(0);
	}
	if(total_stations_number>0){
		for(i=0;i<total_stations_number;i++){
			free(list_of_stations[i]);
		}
		free(list_of_stations);
	}
	for(i=0;i<MAX_CLIENTS;i++){
		if(sockets_file_descriptors[i]!=0){
			close(sockets_file_descriptors[i]);
		}
	}
	return 0;
}
///////////////////////////////////////////////////////////////////////////
int create_end_thread(){
	while(pthread_create(&finish,NULL,end_func,NULL)!=0){
		perror("Error while creating end manager thread");
	}
	stop_flag=1;
	return 0;
}
//////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[]){
	if(argc<3){
		printf("\n");
		printf(" Wrong argument input");
		printf("\n");
		printf("./radio_server <tcpport> <multicastip> <udpport> <file1> <file2> ...");
		printf("\n");
		exit(0);
	}
	if(pthread_mutex_init(&listlock,NULL)!=0){
		perror("Error creating mutex\n");
		return -1;
	}
	if(pthread_mutex_init(&permitlock,NULL)!=0){
		perror("Error creating mutex\n");
		return -1;
	}
	int tcpport=atoi(argv[1]);
	multicastip=inet_addr(argv[2]);
	udpport=atoi(argv[3]);
	total_stations_number=argc-4;
	int i=4;
	int j=0;
	int counter=0;
	char choose;
	in_addr_t address_user;
	if(argc-3>0){
		list_of_stations=(char**)malloc((argc-4)*sizeof(char*));//allocate list of songs
		for(i=4;i<argc;i++){//read all the song names
			list_of_stations[i-4]=(char*)malloc((strnlen(argv[i],255)+2)*sizeof(char));//allocate name of song in list
			for(j=0;j<strlen(argv[i]);j++){//loads song into buffer
				list_of_stations[i-4][j]=argv[i][j];
			}
			list_of_stations[i-4][strnlen(argv[i],255)+1]='\0';
		}
	}
	memset(sockets_file_descriptors,'\0',sizeof(sockets_file_descriptors));
	int welcome_socket;
	int new_tcp_socket;
	transmission_file_list=(FILE**)malloc(total_stations_number*sizeof(FILE*));
	station_socket_list=(int*)malloc(total_stations_number*sizeof(int));
	pthread_t new_station;
	if(pthread_create(&new_station,NULL,station_function,i)!=0){//create threads that  would open sockets udp and play music/
		perror("Error while creating station thread");
		create_end_thread();
	}
	/////////////////////////////////////////welcome socket init/////////////////////////////////////////////////////////////////////
	struct sockaddr_in server_addr;
	socklen_t addr_size=sizeof(server_addr);
	server_addr.sin_family=AF_INET;
	server_addr.sin_port = htons(tcpport); //16 bit TCP port number
	server_addr.sin_addr.s_addr = htons(INADDR_ANY); //inet_iddr(): converts the Internet host address from IPv4 into binary data
	memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));

	//opening welcome socket
	welcome_socket=socket(AF_INET, SOCK_STREAM,0);
	if (welcome_socket < 0){
		perror("Error opening socket");
	}
	//binding of welcome_socket
	if(bind(welcome_socket, (struct sockaddr *) &server_addr,sizeof(server_addr))<0){//
		close(welcome_socket);
		perror("Error, cann't assign address to socket");
	}
	//Building a "welcome" socket
	if(listen(welcome_socket,MAX_CLIENTS)<0){
		close(welcome_socket);
		perror("Error in 'listen' command");
	}
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	fd_set fdset;//set of file descriptors for select()
	FD_ZERO(&fdset);
	FD_SET(welcome_socket,&fdset);
	FD_SET(0,&fdset);
	max_socket_num=welcome_socket+1;
	int inputfd;
	print_UI();
	while(stop_flag==0){//wait for connect or user input
		inputfd=select(max_socket_num,&fdset,NULL,NULL,NULL);
		if(inputfd==-1){/*error in select func*/
			perror("Error in select function");
			close(welcome_socket);
			create_end_thread();
		}
		else{//some socket recieved data
			if(FD_ISSET(welcome_socket,&fdset)){//new client in welcome socket
				for(i=0;i<MAX_CLIENTS;i++){
					if(sockets_file_descriptors[i]==0){
						new_tcp_socket=accept(welcome_socket, (struct sockaddr *) &users_addr[i], &addr_size);//client connected
						break;
					}
				}
				if(new_tcp_socket==-1){
					perror("Error during creation new socket");
					FD_SET(welcome_socket,&fdset);//reset fd_set
					continue;
				}
				else if(total_connected_clients==MAX_CLIENTS){//server is full
					close(new_tcp_socket);
					FD_SET(welcome_socket,&fdset);//reset fd_set
					perror("No more place in room");
					continue;
				}
				pthread_t new_user;
				list_manager_function(new_tcp_socket,0);
				if(pthread_create(&new_user,NULL,user_func,new_tcp_socket)!=0){//creating new thread for new connection
					perror("Error while creating user thread");
					close(new_tcp_socket);
					list_manager_function(new_tcp_socket,1);
					FD_SET(welcome_socket,&fdset);//reset fd_set
					continue;
				}
				FD_SET(welcome_socket,&fdset);//reset fd_set
				FD_SET(0,&fdset);//reset fd_set
			}
			else if(FD_ISSET(0,&fdset)){/*'Enter' was pressed*/
				FD_SET(0,&fdset);//reset fd_set
				FD_SET(welcome_socket,&fdset);//reset fd_set
				/////////////////////////////////user options!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
				scanf("%c",&choose);
				if(choose=='p'){//print database
					printf("\nStation number     Song name\n");
					if(total_stations_number==0){}
					else{
						for(i=0;i<total_stations_number;i++){
							printf("       %d            ",i);
							for(j=0;j<strlen(list_of_stations[i]);j++){
								printf("%c",list_of_stations[i][j]);
							}
							printf("\n");
						}
					}
					if(total_connected_clients==0){//no connections
						printf("\nThere are no connected clients\n");
					}
					else{
						printf("\nNumber of connected clients: %d\n",total_connected_clients);
						printf("IP addresses:\n");
						for(i=0;i<MAX_CLIENTS;i++){
							if(counter==total_connected_clients){
								break;
							}
							else if(sockets_file_descriptors[i]!=0){
								//address_user=users_addr[i].sin_addr.s_addr;
								printf("     %s\n",inet_ntoa(users_addr[i].sin_addr));
							}
						}
						printf("\n");
					}
					counter=0;
				}
				else if(choose=='q'){
					printf("\nInterrupt by user\n");
					close(welcome_socket);
					create_end_thread();
					break;
				}
				else{
					perror("wrong input");
				}
				print_UI();
			}
		}
	}
	pthread_join(finish,NULL);
	exit(0);
}
