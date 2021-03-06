#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>

#ifndef SO_PEERSEC
#define SO_PEERSEC 31
#endif

#ifndef SCM_SECURITY
#define SCM_SECURITY 0x03
#endif

void usage(char *progname)
{
	fprintf(stderr, "usage:  %s [-n] [stream|dgram] port\n", progname);
	exit(1);
}

static const int on = 1;

int
main(int argc, char **argv)
{
	int sock;
	int result;
	struct sockaddr_in sin;
	socklen_t sinlen;
	int type;
	char byte;
	unsigned short port;
	int opt;
	bool nopeer = false;

	while ((opt = getopt(argc, argv, "n")) != -1) {
		switch (opt) {
		case 'n':
			nopeer = true;
			break;
		default:
			usage(argv[0]);
		}
	}

	if ((argc - optind) != 2)
		usage(argv[0]);

	if (!strcmp(argv[optind], "stream"))
		type = SOCK_STREAM;
	else if (!strcmp(argv[optind], "dgram"))
		type = SOCK_DGRAM;
	else
		usage(argv[0]);

	port = atoi(argv[optind + 1]);
	if (!port)
		usage(argv[0]);

	sock = socket(AF_INET, type, 0);
	if (sock < 0) {
		perror("socket");
		exit(1);
	}

	result = setsockopt(sock, SOL_IP, IP_PASSSEC, &on, sizeof(on));
	if (result < 0) {
		perror("setsockopt: SO_PASSSEC");
		close(sock);
		exit(1);
	}

	result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (result < 0) {
		perror("setsockopt: SO_PASSSEC");
		close(sock);
		exit(1);
	}

	bzero(&sin, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = INADDR_ANY;
	sinlen = sizeof(sin);
	if (bind(sock, (struct sockaddr *) &sin, sinlen) < 0) {
		perror("bind");
		close(sock);
		exit(1);
	}

	if (type == SOCK_STREAM) {
		if (listen(sock, SOMAXCONN)) {
			perror("listen");
			close(sock);
			exit(1);
		}

		do {
			int newsock;
			char peerlabel[256];
			socklen_t labellen = sizeof(peerlabel);

			sinlen = sizeof(sin);
			newsock = accept(sock, (struct sockaddr *)&sin,
					 &sinlen);
			if (newsock < 0) {
				perror("accept");
				close(sock);
				exit(1);
			}

			if (nopeer) {
				strcpy(peerlabel, "nopeer");
			} else {
				peerlabel[0] = 0;
				result = getsockopt(newsock, SOL_SOCKET, SO_PEERSEC, peerlabel,
						    &labellen);
				if (result < 0) {
					perror("getsockopt: SO_PEERSEC");
					exit(1);
				}
				printf("%s:  Got peer label=%s\n", argv[0], peerlabel);
			}

			result = read(newsock, &byte, 1);
			if (result < 0) {
				perror("read");
				exit(1);
			}

			result = write(newsock, peerlabel, strlen(peerlabel));
			if (result < 0) {
				perror("write");
				exit(1);
			}
			close(newsock);
		} while (1);
	} else {
		struct iovec iov;
		struct msghdr msg;
		struct cmsghdr *cmsg;
		char msglabel[256];
		union {
			struct cmsghdr cmsghdr;
			char buf[CMSG_SPACE(sizeof(msglabel))];
		} control;

		do {
			memset(&iov, 0, sizeof(iov));
			iov.iov_base = &byte;
			iov.iov_len = 1;
			memset(&msg, 0, sizeof(msg));
			msglabel[0] = 0;
			msg.msg_name = &sin;
			msg.msg_namelen = sizeof(sin);
			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;
			msg.msg_control = &control;
			msg.msg_controllen = sizeof(control);
			result = recvmsg(sock, &msg, 0);
			if (result < 0) {
				perror("recvmsg");
				exit(1);
			}
			if (nopeer) {
				strcpy(msglabel, "nopeer");
			}
			for (cmsg = CMSG_FIRSTHDR(&msg); cmsg;
			     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
				if (cmsg->cmsg_level == SOL_IP &&
				    cmsg->cmsg_type == SCM_SECURITY) {
					size_t len = cmsg->cmsg_len - CMSG_LEN(0);

					if (len > 0 && len < sizeof(msglabel)) {
						memcpy(msglabel, CMSG_DATA(cmsg), len);
						msglabel[len] = 0;
						printf("%s: Got SCM_SECURITY=%s\n",
						       argv[0], msglabel);
					}
				}
			}

			result = sendto(sock, msglabel, strlen(msglabel), 0,
					msg.msg_name, msg.msg_namelen);
			if (result < 0) {
				perror("sendto");
				exit(1);
			}
		} while (1);
	}

	close(sock);
	exit(0);
}
