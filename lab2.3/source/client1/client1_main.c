#include	<stdlib.h>
#include	<inttypes.h>
#include 	<string.h>
#include 	<fcntl.h>
#include	"../errlib.h"
#include	"../sockwrap.h"
#include	"../myprotocol.h"

#define RBUF_LEN	2048
#define SBUF_LEN	512

/* define it to make all printf visible*/
/*#define DEBUG*/

#ifdef DEBUG
	#define DBG(x) printf x
#else
	#define DBG(x)
#endif


char *prog_name;

int main (int argc, char *argv[])
{
	char			buf[SBUF_LEN], rbuf[RBUF_LEN];	/* transmission and reception buffer */
	
	uint32_t		fsize, timestamp, totreceived;	/* file size, file last modification */
	uint16_t		tport_h, tport_n;				/* server port number */
	
	int 			s, result;						/* socket, utility variable */
    struct 			sockaddr_in	saddr;				/* server address structure */
    struct 			in_addr	sIPaddr; 				/* server IP addr. structure */

    struct timeval	tval = {TIMEOUT, 0};			/* socket timeout */
    
    FILE* fd;										/* file structure */

    prog_name = argv[0];

    if(argc < 4)
		err_quit("Usage: %s <server> <port> <file>", prog_name);

	/*Check if buffer is large enough*/
	if(SBUF_LEN < MAX_FILENAME_LEN + REQ_START_LEN + REQ_END_LEN)
		err_quit("Send buffer must be greater than max filename + protocol info");

    if (!inet_aton(argv[1], &sIPaddr))
    	err_quit("Invalid address");

    if (sscanf(argv[2], "%" SCNu16, &tport_h) != 1)
		err_quit("Invalid port number");
    tport_n = htons(tport_h);

    DBG(("Creating socket ... "));
    s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    DBG(("Socket number: %d\n",s));

    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port   = tport_n;
    saddr.sin_addr   = sIPaddr;

    /*Setting timeout both for receive and send(used to avoid infinite connect)*/
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *)&tval, sizeof(struct timeval));
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tval, sizeof(struct timeval));

	DBG(("Connecting to target address %s: %" SCNu16 "\n", inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port)));    
    Connect(s, (struct sockaddr *) &saddr, sizeof(saddr));

	for(int i=3; i < argc; i++) {

		DBG(("\n======================================\n"));

		/*Checking filename length*/
		if(strlen(argv[i]) > MAX_FILENAME_LEN)
			err_quit("Max filename legth: %d", MAX_FILENAME_LEN);

		/*Creating and sending request*/
		sprintf(buf, "%s%s%s", REQ_START, argv[i], REQ_END);
		Sendn(s, buf, strlen(buf), 0);
		DBG(("Request sent: %s\n", buf));

		/*Receive +/- */
		if(Recv(s, rbuf, SIGN_LEN, 0) != SIGN_LEN)
			err_quit("Error receiving request result");
		
		/*Checking negative answer*/
		if (!isOkSign(rbuf)) {
			if(Recv(s,rbuf+KO_LEN, KO_MSG_LEN - KO_LEN, 0) != KO_MSG_LEN - KO_LEN)
				err_quit("Bad error message length received");
			rbuf[KO_MSG_LEN] = '\0';
			if(!isKoAnswer(rbuf))
				err_quit("Bad server error message");
			DBG(("Server response for %s: %s\n", argv[i], rbuf));
			break;
		}

		/*Receive positive answer*/
		if(Recv(s, rbuf+OK_LEN, OK_MSG_LEN - OK_LEN, 0) != OK_MSG_LEN - OK_LEN)
			err_quit("Bad + length received");
		rbuf[OK_MSG_LEN] = '\0';

		/*Checking positive answer*/
		if(!isOkAnswer(rbuf))
			err_quit("Bad server success message");
		printf("Received file %s\n", argv[i]);
			
		/*Receiving file size*/
		if(Recv(s, &fsize, sizeof(uint32_t), 0) != sizeof(uint32_t))
			err_quit("Bad file size received");
		fsize = ntohl(fsize);
		printf("Received file size %" SCNu32 "\n", fsize);

		/*Creating local file */
		if((fd=fopen(argv[i], "w")) == NULL)
	    	err_sys("Error opening file %s\n", argv[i]);

		/*Receiving file content*/
		totreceived = 0;
	   	while (totreceived != fsize && (result = Recv(s, rbuf, fsize - totreceived < RBUF_LEN? fsize - totreceived : RBUF_LEN, 0)) > 0){
	   		if(fwrite(rbuf, sizeof(char), result, fd) != result)
	   			err_sys("Error writing on file %s\n", argv[i]);
	   		totreceived += result;
		}
		if(totreceived != fsize)
			err_quit("Error receiving file content");
		fclose(fd);

		/*Receiving timestamp*/
		if(Recv(s, &timestamp, sizeof(uint32_t), 0) != sizeof(uint32_t))
			err_quit("Error receiving last modification");
		
		printf("Received file timestamp %" SCNu32 "\n", ntohl(timestamp));
	}

	Close(s);
    exit(0);
}
