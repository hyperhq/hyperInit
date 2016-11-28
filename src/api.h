#ifndef _HYPERSTART_API_H_
#define _HYPERSTART_API_H_

#define APIVERSION 4242

// control command id
enum {
	GETVERSION,
	STARTPOD,
	GETPOD,
	STOPPOD_DEPRECATED,
	DESTROYPOD,
	RESTARTCONTAINER,
	EXECCMD,
	CMDFINISHED,
	READY,
	ACK,
	ERROR,
	WINSIZE,
	PING,
	PODFINISHED,
	NEXT,
	WRITEFILE,
	READFILE,
	NEWCONTAINER,
	KILLCONTAINER,
	ONLINECPUMEM,
	SETUPINTERFACE,
	SETUPROUTE,
	REMOVECONTAINER,
};

/*
 * control message format
 * | ctrl id | length  | payload (length-8)      |
 * | . . . . | . . . . | . . . . . . . . . . . . |
 * 0         4         8                         length
 */
#define CONTROL_HEADER_SIZE		8
#define CONTROL_HEADER_LENGTH_OFFSET	4

/*
 * stream message format
 * | stream sequence | length  | payload (length-12)     |
 * | . . . . . . . . | . . . . | . . . . . . . . . . . . |
 * 0                 8         12                        length
 */
#define STREAM_HEADER_SIZE		12
#define STREAM_HEADER_LENGTH_OFFSET	8

/*
 * vsock listening ports
 */
#define HYPER_VSOCK_CTL_PORT		2718
#define HYPER_VSOCK_MSG_PORT		2719

#endif /* _HYPERSTART_API_H_ */
