#ifndef __MY_PROTOCOL__
#define __MY_PROTOCOL__

#define TIMEOUT						15

#define OK_MSG						"+OK\r\n"
#define KO_MSG 						"-ERR\r\n"
#define REQ_START   				"GET "
#define REQ_END						"\r\n"

#define KO 							"-"
#define OK 							"+"

#define MAX_FILENAME_LEN			255
#define REQ_START_LEN 				strlen(REQ_START)
#define REQ_END_LEN 				strlen(REQ_END)
#define OK_LEN 						strlen(OK)
#define KO_LEN 						strlen(KO)
#define SIGN_LEN					strlen(OK)
#define OK_MSG_LEN 					strlen(OK_MSG)
#define KO_MSG_LEN					strlen(KO_MSG)

#define isOkRequestStart(buf) 		(strncmp(buf, REQ_START, REQ_START_LEN) == 0)
#define isOkRequestEnd(buf, length)	(strncmp(buf+length-REQ_END_LEN, REQ_END, REQ_END_LEN) == 0)
#define isOkFilename(buf)			(strchr(buf+REQ_START_LEN, '/') == NULL)
#define isOkAnswer(buf)				(strncmp(buf, OK_MSG, OK_LEN) == 0)
#define isKoAnswer(buf)				(strncmp(buf, KO_MSG, KO_LEN) == 0)
#define isOkSign(buf)				(strncmp(buf, OK, OK_LEN) == 0)

#endif
