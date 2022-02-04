#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#define SIZE 1400
#define LAST_MSGS_SIZE 500
#define RTT_EVAL_FREQ 10

static int *last_ack;
static int *last_segment_sent;
static int *ending_segment;
static int *is_time_out;
static int *seg_to_time;
static int *can_time;
static int *is_timed_received;

static double *rtt;
static double *swnd;

void send_file_data(int sockfd, struct sockaddr_in addr, double rtt_eval)
{

	last_ack = mmap(NULL, sizeof *last_ack, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*last_ack = 0;

	last_segment_sent = mmap(NULL, sizeof *last_segment_sent, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*last_segment_sent = 0;

	ending_segment = mmap(NULL, sizeof *ending_segment, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*ending_segment = -2;

	is_time_out = mmap(NULL, sizeof *is_time_out, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*is_time_out = 0;

	seg_to_time = mmap(NULL, sizeof *seg_to_time, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*seg_to_time = 0;

	can_time = mmap(NULL, sizeof *can_time, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*can_time = 1;

	is_timed_received = mmap(NULL, sizeof *is_timed_received, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*is_timed_received = 0;

	rtt = mmap(NULL, sizeof *rtt, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*rtt = rtt_eval;

	swnd = mmap(NULL, sizeof *swnd, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*swnd = 10;


	int n;
	socklen_t alen = sizeof(addr);
	char buffer[SIZE];

	printf("Trying to get filename\n");
	n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr*) &addr, &alen);
	printf("Filename received! %s\n",buffer);
	FILE *fp;
	char *filename = buffer;//"moon.jpg";//buffer;
	// Creating a file.
	fp = fopen(filename, "rb");
	printf("Sending ...\n");



	// ------------------------------------ PARENT ------------------------------
	int id = fork();
	if(id!=0)
	{
		int nb_bytes_read, score, b, i, start_buffer = 0, end_buffer = 0, temp_pointer = 0, size_left = SIZE, nb_msgs_buffered = 0, zz, nb_bytes_total=0; //start and end of buffer are those of the *data* in the buff
		char str[] = "000000";
		char test[SIZE];
		char last_msgs[SIZE*LAST_MSGS_SIZE]; // on peut stocker jusqu'à LAST_MSGS_SIZE messages
		char buffer_buffer[SIZE-6];
		int segment = 0, flight_size = 0, reseg = 0, aaaa=0, last_seg_timed = 0, first_msg_to_be_retransmited=0;
		clock_t start, end;
		double throughput;
		clock_t start_clock, end_clock;



		memset(buffer, 0, sizeof(buffer));
		memset(buffer_buffer, 0, sizeof(buffer_buffer));
		memset(last_msgs, 0, sizeof(last_msgs));
		memset(test, 0, sizeof(test));



		// Sending the data
		while(!feof(fp))
		{
			if(*is_timed_received && last_seg_timed != *seg_to_time) // if timed segment is received and wans't already timed
			{
				end_clock = clock();
				*rtt = ((double)(end_clock-start_clock)/CLOCKS_PER_SEC);
				last_seg_timed = *seg_to_time;
				*is_timed_received = 0;
			}

			while(((segment-*last_ack >= *swnd) || *is_time_out) && segment!=0)
			{
				// Just wait until it can send again OR if time is out, and retransmits according to swnd
				if(*is_time_out)
				{
					flight_size = 0;
					*can_time = 1; // ack have been lost, we can't estimate the RTT properly

					temp_pointer = ((*last_ack) % (LAST_MSGS_SIZE)) * SIZE; // the real beginning of the buffer is
					reseg = *last_ack;

					*is_time_out = 0;
					for(i=0; i < (segment - *last_ack); i++)
					{
						
						while((flight_size >= *swnd) && !*is_time_out) //XOR
						{
							flight_size = flight_size - (reseg - (*last_ack + flight_size)); // updates the flight_size with receiving acks
						} // wait for acks ; if time is out, breaks free


						if(*is_time_out)
						{
							break;
						} // time is out, we need to resend from last ack
						
						// re-sending
						memcpy(buffer, &last_msgs[temp_pointer], SIZE);
						sendto(sockfd, buffer, SIZE, 0, (struct sockaddr*)&addr, sizeof(addr));
						flight_size++;
						temp_pointer = (temp_pointer + SIZE) % (SIZE*LAST_MSGS_SIZE);
						reseg++;
					}
				}
			}

			segment++;
			i=0;

			// Writes the segment number in str
			score = segment;
			while(score)
			{
				b = score % 10;
				str[5-i] = b+'0';
				score /= 10;
				i++;
			}

			// Writes the segment number in the buffer, then the data
			strcpy(buffer, str);
			nb_bytes_read = fread(buffer_buffer, sizeof(char), sizeof(buffer_buffer), fp);
			memcpy(&buffer[6], buffer_buffer, nb_bytes_read);

			nb_bytes_total += nb_bytes_read; // For throughput calculations

			// Starts the clock if available
			if(segment % RTT_EVAL_FREQ == 0 && *can_time == 1){
				*can_time = 0;
				*seg_to_time = segment;
				start_clock = clock();
			}


			// Sends the data
			n = sendto(sockfd, buffer, nb_bytes_read+6, 0, (struct sockaddr*)&addr, sizeof(addr));

			// Copies sent data into the "last_msgs" buffer
			*last_segment_sent = segment;
			memcpy(&last_msgs[end_buffer], buffer, SIZE);
			end_buffer = (end_buffer + n)%(SIZE*LAST_MSGS_SIZE);

		}

		*ending_segment = segment;
		printf("Total nb segments sent = %d\n", segment);

		flight_size = 0; 
		*can_time = 1; // we stop the clock, because we won't handle it for the last few segments to retransmit
		

		// last while, in case of the one of the last segments sent not ACKed (it's the same retransmitting loop as it was inside the "while(!feof)")
		while(*ending_segment!=-1) 
		{
			// Just wait until it can send again OR if time is out, and does THINGS according to plan for retransmit
			if(*is_time_out)
			{
				flight_size = 0;
				temp_pointer = ((*last_ack) % (LAST_MSGS_SIZE)) * SIZE; // the real beginning of the buffer is
				i=0;
				reseg = *last_ack;
				*is_time_out = 0;


				for(i=0 ; i < (segment - *last_ack) ; i++)
				{
					while((flight_size >= *swnd) && !*is_time_out)
					{
						flight_size = flight_size - (reseg - (*last_ack + flight_size));
					}
					if(*is_time_out)
					{
						break;
					} // time is out, we need to resend from last ack

					size_left = SIZE;
					if((end_buffer - temp_pointer) < SIZE && temp_pointer < end_buffer) //if it is the last msg that is lost ()
					{
						size_left = (end_buffer - temp_pointer);
					}
					memcpy(buffer, &last_msgs[temp_pointer], size_left);
					sendto(sockfd, buffer, size_left, 0, (struct sockaddr*)&addr, sizeof(addr));
					flight_size++;
					reseg++;
					temp_pointer = (temp_pointer + size_left) % (SIZE*LAST_MSGS_SIZE);
					
				}
			}
		}


		end = clock();
		throughput = nb_bytes_total/((double)(end-start)/CLOCKS_PER_SEC);

		printf("\nTotal nb segments sent = %d\nThroughput ~= %d ko/s\n", segment, (int)(throughput/1000));

		// Sending the 'END'
		bzero(buffer, sizeof(buffer));
		strcpy(buffer, "FIN");
		for(i=5;i<10;i++)
		{
			usleep(i);
			sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr*)&addr, sizeof(addr));
		}
		printf("msg sent:%s\n",buffer);
		fclose(fp);
		kill(id, SIGTERM);
		munmap(last_ack, sizeof *last_ack); 
		munmap(last_segment_sent, sizeof *last_segment_sent);
		munmap(is_time_out, sizeof *is_time_out);
		munmap(ending_segment, sizeof *ending_segment);
		munmap(seg_to_time, sizeof *seg_to_time);
		munmap(can_time, sizeof *can_time);
		munmap(is_timed_received, sizeof *is_timed_received);
		munmap(rtt, sizeof *rtt);
		munmap(swnd, sizeof *swnd);
		return;
	// ------------------------------------ CHILD ------------------------------
	}else
	{
		socklen_t addr_size;
		struct timeval tv;
		int n, ack, select_status, last_segment_evaluated = 0, nb_duplicates = 0, is_same_ack = 0, is_inflated=0;
		char buffer[SIZE];
		fd_set fdread;
		double ssthresh = 99999.0;

		// Perpetually checks for ACKs
		do{
			tv.tv_sec = 0;
			tv.tv_usec = 2500000*(*rtt);
			FD_ZERO(&fdread);
			FD_SET(sockfd, &fdread);

			select_status = select(sockfd+1, &fdread, NULL, NULL, &tv);

			switch(select_status)
			{
				case -1:
					perror("[ERROR] select error\n");
					exit(1);
					break;

				case 0: // Timeout, I.E. nothing to read
					ssthresh = *swnd/3 * 2;
					if(*swnd>15.0)
					{
						*swnd = *swnd - 1;
					}
					*is_time_out = 1;
					break;

				default: // Someting is available
					n = recvfrom(sockfd, buffer, SIZE, 0, (struct sockaddr*)&addr, &addr_size);
					sscanf(&buffer[3], "%d", &ack);

					//----------------------------------------------------------------------
					//CHRONO
					if(*can_time == 0 && ack!=last_segment_evaluated) // if clock is started AND if it's the first time that we received this ack
					{
						if(ack == *seg_to_time) // bingo! raise the flag!
						{
							*is_timed_received = 1;
							last_segment_evaluated = ack;
						}else if(ack > *seg_to_time){ // timed segment was multiple-acked, we shall try again
							*can_time = 1;
						}
					}
					//CHRONO
					//----------------------------------------------------------------------
					//CONFIRM ACKS
					if(*last_ack < ack && ack <= *last_segment_sent){ 	// ack that acks stuff
						*last_ack = ack;
						is_same_ack = 0;
						nb_duplicates = 0;
						if(is_inflated)
						{
							*swnd=ssthresh;
							is_inflated = 0;
						}else if(*swnd<ssthresh){
							*swnd += 3.0;
						}else{ 
							*swnd += 1.0;
						} 
						
					}else if(ack == *last_ack){ 						//duplicate acks
						nb_duplicates += 1;
						if(nb_duplicates >= 3 && !(*is_time_out)){
							*is_time_out = 1;
							if(!is_same_ack){
								ssthresh = *swnd/3 * 2;
								is_same_ack = 1;
							}
							*swnd = ssthresh + nb_duplicates;
						}

					}
					break;
					//CONFIRM ACKS
					//----------------------------------------------------------------------

			}
		}while(*ending_segment != *last_ack);
		printf("Last ack received!\n");
		*ending_segment = -1;
	}
}

int main(int argc, char *argv[]){

	// Defining the IP and Port
	int port = atoi(argv[1]);

	// Defining variables
	int server_sockfd, server_data_sockfd, UDP_fd;
	struct sockaddr_in server_addr, client_addr, server_addr_data, UDP_addr;
	socklen_t addr_size = sizeof(client_addr);
	char buffer[SIZE];
	int valid = 1;
	int e, n, i, new_port, port_data_fd;
	double rtt;
	clock_t start, end;
	
	
	// Creating a UDP socket
	server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &valid, sizeof(int));
	if (server_sockfd < 0){
		perror("[ERROR] socket error");
		exit(1);
	}


	memset(&server_addr, 0, sizeof(server_addr));
	memset(&client_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	e = bind(server_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
	if (e < 0){
		perror("[ERROR] bind error");
		exit(1);
	}

	// Receiving
	n = recvfrom(server_sockfd, buffer, SIZE, 0, (struct sockaddr*)&client_addr, &addr_size);
	if (strncmp(buffer, "SYN",3) == 0 )
	{

		//---------------------------------- TO COMPLETE -----------------------------------
		memset(&server_addr_data, 0, sizeof(server_addr));
		bzero(buffer, sizeof(buffer));
//_____________________________________________________BEGIN HANDSHAKE_______________________________________________________________________________


		UDP_fd = socket(AF_INET, SOCK_DGRAM, 0);


		UDP_addr.sin_family= AF_INET;
		UDP_addr.sin_addr.s_addr= htonl(INADDR_ANY);


		do
		{
			new_port = (rand() % 9000) + 1000; // WARNING : this random method is not uniform
			UDP_addr.sin_port= htons(new_port);
			printf("new_port is %d\n", new_port);
		}while(bind(UDP_fd, (struct sockaddr*) &UDP_addr, sizeof(UDP_addr)) < 0);
		printf("Bind done!\n");

		//get port nb and send it
		char msg[11] = "SYN-ACK";
		sprintf(&msg[7], "%d", new_port);

		start = clock();
		sendto(server_sockfd, msg, 11, 0, (struct sockaddr*) &client_addr, sizeof(client_addr));

		n = recvfrom(server_sockfd, buffer, SIZE, 0, (struct sockaddr*) &client_addr, &addr_size);
		end = clock();

		if(!strncmp( buffer, "ACK",3) == 0){
			printf("[WARNING] not SYN\n");
		}

//__________________________________________________END HANDSHAKE__________________________________________________________________________________
		
		close(server_sockfd);
		rtt = ((double)(end-start)/CLOCKS_PER_SEC);
		send_file_data(UDP_fd, UDP_addr, rtt);
		close(UDP_fd);

	}else{
		printf("[ERROR] message not 'SYN'");
	}

	return 0;
}
