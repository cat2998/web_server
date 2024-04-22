#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
void make_request_header(rio_t *rp, char *buf);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *host, char *port, char *path);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
	int listenfd, connfd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;

	/* Check command line args */
	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	printf("== proxy start ==\n");
	printf("%s port using\n", argv[1]);

	listenfd = Open_listenfd(argv[1]);
	while (1)
	{
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		doit(connfd);
		Close(connfd);
	}
}

void doit(int fd)
{
	int is_static, clientfd;
	struct stat sbuf;
	char buf[MAXLINE], host[MAXLINE], method[MAXLINE], uri[MAXLINE], port[MAXLINE], path[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	char response_buf[MAX_OBJECT_SIZE];
	ssize_t response_size;
	rio_t rio;

	/* HTTP 요청 헤더 읽기 */
	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);
	sscanf(buf, "%s %s %s", method, uri, version);
	printf("Request headers\n");
	printf("%s\n", buf);
	parse_uri(uri, host, port, path);
	sprintf(buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
	// printf("%s\n", buf);
	make_request_header(&rio, buf);
	printf("Request headers\n");
	printf("%s\n", buf);

	if (strcasecmp(method, "GET") != 0)
	{
		clienterror(fd, method, "501", "Not implemented", "Tiny dose not implement this method");
		return;
	}

	clientfd = open_clientfd(host, port);
	if (clientfd < 0)
	{
		clienterror(clientfd, method, "502", "Bad Gateway", "Failed to establish connection with the end server");
		return;
	}
	Rio_writen(clientfd, buf, strlen(buf));

	Rio_readinitb(&rio, clientfd);
	response_size = Rio_readnb(&rio, response_buf, MAX_OBJECT_SIZE);
	// printf("\nresponse header size: %d\n", response_size);
	// read_requesthdrs(&rio);
	Rio_writen(fd, response_buf, response_size);

	close(clientfd);
}

void make_request_header(rio_t *rp, char *request_header)
{
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	while (strcmp(buf, "\r\n"))
	{
		Rio_readlineb(rp, buf, MAXLINE);
		if (strcmp(buf, "User-Agent:") == 0 || strcmp(buf, "Connection:") == 0 || strcmp(buf, "Proxy-Connection:") == 0)
			continue;
		strcat(request_header, buf);
	}
	strcat(request_header, user_agent_hdr);
	strcat(request_header, "Connection: close\r\n");
	strcat(request_header, "Proxy-Connection: close\r\n");
	strcat(request_header, "\r\n");
	return;
}

/* 에러 페이지 반환*/
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];

	/* HTTP response body 생성 */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor="
				  "ffffff"
				  ">\r\n",
			body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	/* HTTP response 전송 */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type : text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}

int parse_uri(char *uri, char *host, char *port, char *path)
{
	char *start, *slash, *dot;

	start = strstr(uri, "//");
	if (start == NULL)
		strcpy(host, uri);
	else
		strcpy(host, start + 2);

	strcpy(path, "/");
	dot = strstr(host, ":");
	slash = strstr(host, "/");
	if (slash != NULL)
	{
		strcpy(path, slash);
		*slash = '\0';
	}
	if (dot != NULL)
	{
		strcpy(port, dot + 1);
		*dot = '\0';
	}
	if (dot == NULL)
		strcpy(port, "80");
}