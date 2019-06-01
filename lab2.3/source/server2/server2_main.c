#include 	<stdlib.h>
#include    <inttypes.h>
#include 	<string.h>
#include 	<sys/stat.h>
#include	<sys/sendfile.h>
#include    "../errlib.h"
#include    "../sockwrap.h"
#include	"../myprotocol.h"

#define RBUF_LEN 512			/* receiving buffer length */
#define SBUF_LEN 2048			/* trasmitting buffer length */
#define BACKLOG 5				/* listen backlog for the server */
#define SENDFILE_MAX 0x7ffff000 /* max file size to be sent using sendfile()*/

/* define it to make all printf visible*/
/*#define DEBUG*/

#ifdef DEBUG
	#define DBG(x) printf x
#else
	#define DBG(x)
#endif

char *prog_name;

void routine(int s);

int main (int argc, char *argv[]) {
	int 				conn_request_skt, s, pid;	/* passive socket, socket, pid for fork */
    uint16_t 			lport_h, lport_n;			/* ports used by server */
    socklen_t 			addrlen;					/* socklen_t structure*/
    struct sockaddr_in 	saddr, caddr;				/* server and client addresses */ 
	struct timeval		tval = {TIMEOUT, 0}; 		/* socket timeout*/

    prog_name = argv[0];

    if(argc != 2)
    	err_quit("Usage: %s <port number>\n", prog_name);

    /*Checking receive buffer length*/
    if(RBUF_LEN < MAX_FILENAME_LEN + REQ_START_LEN + REQ_END_LEN)
		err_quit("Receive buffer must be greater than max filename + protocol info");

    if (sscanf(argv[1], "%" SCNu16, &lport_h) != 1)
		err_quit("Invalid port number");
    lport_n = htons(lport_h);

    DBG(("Creating socket ...\n"));
    conn_request_skt = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    DBG(("Socket number: %u\n", conn_request_skt));

    /* bind the socket to any local IP address */
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family      = AF_INET;
    saddr.sin_port        = lport_n;
    saddr.sin_addr.s_addr = INADDR_ANY;
    DBG(("Binding to address %s: %" SCNu16 "\n", inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port)));
    Bind(conn_request_skt, (struct sockaddr *) &saddr, sizeof(saddr));
	
    /* listen */
    DBG (("Listening at socket %d with backlog = %d\n", conn_request_skt, BACKLOG));
    Listen(conn_request_skt, BACKLOG);

    addrlen = sizeof(struct sockaddr_in);

    /*Setting to ignore SIGPIPE, this way server does not crash*/
    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		DBG(("Cannot catch SIGPIPE signal\n"));

    /*Setting to ignore SIGCHLD, this way server does not crash*/
    if(signal(SIGCHLD,SIG_IGN) == SIG_ERR)
    	DBG(("Cannot catch SIGCHLD signal"));

    for (;;) {
		
		s = Accept(conn_request_skt, (struct sockaddr *) &caddr, &addrlen);

		DBG(("\n====================================\n\n"));
		DBG(("Accepted connection from %s:%" SCNu16 "\n", inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port)));
		DBG(("New socket: %u\n",s));

		/*Setting new socker receive timeout*/
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tval, sizeof(struct timeval));

		if((pid=fork()) == 0) {
			/* Child serves client on socket s */
			routine(s);
			close(s);
			exit(0);
		} else if (pid < 0) {
			/* PARENT: Error forking, he serves the client*/
			DBG(("[parent] Error forking for socket %d, trying to satisfy request myself\n", s));
			routine(s);
		} else
			/* PARENT: Ok forking*/
			DBG(("[parent] Successfully forked for socket %d\n", s));
		close(s);
    }

	exit(0);
}

void routine(int s) {

	char			rbuf[RBUF_LEN];		/* reception buffer*/

    int	 			n, count;			/* utility variable*/
    uint32_t 		timestamp, fsize;	/* file last modification, file size*/

    struct stat 	filestat;			/* struct to read file info */

    FILE* fd;							/* file structure */

	for (count=0;; count=0) {
		/*Receiving request until protocol request end pattern*/
		do {
	    	n = recv(s, rbuf+count , RBUF_LEN-1-count, 0);
			if (n < 0) {
				/*Timeout socket or bad request*/
				if(count == 0)
	    	   		DBG(("Error receiving request on socket %d\n", s));
	    	   	else 
	    	   		DBG(("Request end not corrent on socket %d\n", s));
	    		return;	
			}
			if (n == 0) {
				/*Client has closed*/
	    	   	printf("(%s) - connection closed by client: ending service of client\n", prog_name);
	    	   	return;
	    	}
	    	if(count == 0 && (count+=n) >= REQ_START_LEN && !isOkRequestStart(rbuf)) {
	    		/*Bad request start*/
	    		rbuf[count]= '\0';
	    	   	DBG(("Received bad request on socket %d: %s\n", s, rbuf));
	    		   if(sendn(s, KO_MSG, KO_MSG_LEN, 0) != KO_MSG_LEN)
						DBG(("Write error while sending error to socket %d\n", s));
					else
						DBG(("Bad request from socket %d\n", s));
	    	}
	    } while(!isOkRequestEnd(rbuf, count));
	    rbuf[count-REQ_END_LEN] = '\0';
	    DBG(("Received good request on socket %d: %s\n", s, rbuf));

	    /*Checking file if exist, if info are retrievable and if is smaller than the biggest UINT32*/
	    if(!isOkFilename(rbuf) || (fd=fopen(rbuf+REQ_START_LEN, "r")) == NULL ||
	    	fstat(fileno(fd), &filestat) != 0 ||
	    	filestat.st_size > UINT32_MAX) {
	   		if(sendn(s, KO_MSG, KO_MSG_LEN, 0) != KO_MSG_LEN)
	      		DBG(("Error sending file not compliant message on socket %d\n", s));
	      	else
	      		DBG(("File not compliant on socket %d\n", s));
	      	if(fd != NULL)
	      		fclose(fd);
	      	return;
		}

		/*Sending OK message*/
		if(sendn(s, OK_MSG, OK_MSG_LEN, 0) != OK_MSG_LEN){
			DBG(("Error sending accepted request msg to socket %d\n", s));
			fclose(fd);
			return;
		}
		DBG(("Sent accept msg on socket %d\n", s));
		fsize = htonl(filestat.st_size);

		/*Sending file size*/
		if(sendn(s, &fsize, sizeof(uint32_t), 0) != sizeof(uint32_t)) {
			DBG(("Error sending file size on socket %d\n", s));
			fclose(fd);
			return;
		}
		DBG(("Sent file size on socket %d: %lu\n", s, filestat.st_size));
		
		/*Sending file with sendfile if is small enough, otherwise using buffer*/
		if(filestat.st_size > SENDFILE_MAX) {
			char buf[SBUF_LEN];
			int tosend;
			while((tosend = fread(buf, sizeof(char), SBUF_LEN, fd)) > 0) {
				if(sendn(s, buf, tosend, 0) != tosend) {
	    	     	DBG(("Error sending file content on socket %d\n", s));
	    	     	fclose(fd);
	    	     	return;
	    	  	}
			}
		} else {
			if(sendfile(s, fileno(fd), NULL, filestat.st_size) < 0) {
				DBG(("Error sendfile()"));
				fclose(fd);
				return;
			}
		}
		fclose(fd);
		timestamp = htonl(filestat.st_mtime);

		/*Sending file timestamp*/
		if(sendn(s, &timestamp, sizeof(uint32_t), 0) != sizeof(uint32_t)) {
			DBG(("Error sending file timestamp on socket %d\n", s));
			return;
		}
		DBG(("Timestamp correctly sent on socket %d: %lu\n", s, filestat.st_mtime));
	}
}
