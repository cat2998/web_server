#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
void make_request_header(rio_t *rp, char *host, char *buf);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *host, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);

web_object_t *find_cache(char *path);
void send_cache(web_object_t *web_object, int serverfd);
void read_cache(web_object_t *web_object);
void write_cache(web_object_t *web_object);

web_object_t *rootp;
web_object_t *lastp;
int total_cache_size;

int main(int argc, char **argv)
{
	int listenfd, *connfdp;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	pthread_t tid;

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
		connfdp = malloc(sizeof(int));
		*connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("\n== Accepted connection from (%s, %s) ==\n\n", hostname, port);
		Pthread_create(&tid, NULL, thread, connfdp);
	}
}

/* Thread routine */
void *thread(void *vargp)
{
	int connfd = *((int *)vargp);
	Pthread_detach(pthread_self());
	Free(vargp);
	doit(connfd);
	Close(connfd);
	return NULL;
}

void doit(int fd)
{
	int serverfd, response_size;
	char buf[MAXLINE], host[MAXLINE], method[MAXLINE], uri[MAXLINE], port[MAXLINE], path[MAXLINE], version[MAXLINE];
	char *response_buf;
	rio_t rio;
	web_object_t *cached_object;

	/* HTTP 요청 헤더 읽기 */
	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);

	printf("========== Request headers ==========\n");
	printf("%s\n", buf);

	sscanf(buf, "%s %s %s", method, uri, version);
	if (strstr(uri, "favicon.ico") > 0)
		return;
	parse_uri(uri, host, port, path);

	/* 현재 요청이 캐싱된 요청인지 확인 */
	cached_object = find_cache(uri);
	if (cached_object)
	{
		printf("========== cache hits ==========\n");
		send_cache(cached_object, fd);
		read_cache(cached_object);
		return;
	}

	/* request header 만들기 */
	printf("========== send to server Request headers ==========\n");
	sprintf(buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
	make_request_header(&rio, host, buf);
	printf("%s", buf);

	if (strcasecmp(method, "GET") != 0)
	{
		clienterror(fd, method, "501", "Not implemented", "Tiny dose not implement this method");
		return;
	}

	/* 서버와 연결해서 request header 보내기 */
	serverfd = Open_clientfd(host, port);
	if (serverfd < 0)
	{
		clienterror(serverfd, method, "502", "Bad Gateway", "Failed to establish connection with the end server");
		return;
	}
	Rio_writen(serverfd, buf, strlen(buf));

	/* 서버에서 response header에서 content-lenght저장하고 header, body client에 전송 */
	Rio_readinitb(&rio, serverfd);
	while (strcmp(buf, "\r\n"))
	{
		Rio_readlineb(&rio, buf, MAXLINE);
		if (strcmp(buf, "Content-length: "))
			sscanf(buf, "Content-length: %d", &response_size);
		Rio_writen(fd, buf, strlen(buf));
	}
	response_buf = (char *)malloc(response_size);
	Rio_readnb(&rio, response_buf, response_size);
	Rio_writen(fd, response_buf, response_size);
	printf("===== from server, send to client response body =====\n%s", response_buf);

	/* 캐시할 수 있는 사이즈면 캐시저장 */
	if (response_size <= MAX_OBJECT_SIZE)
	{
		web_object_t *web_object = (web_object_t *)calloc(1, sizeof(web_object_t));
		web_object->response_ptr = response_buf;
		web_object->content_length = response_size;
		strcpy(web_object->uri, uri);
		write_cache(web_object);
	}
	else
		free(response_buf);

	close(serverfd);
}

void make_request_header(rio_t *rp, char *host, char *request_header)
{
	char buf[MAXLINE];
	int is_host = 0, is_user = 0, is_connect = 0, is_prox_connect = 0;

	while (strcmp(buf, "\r\n"))
	{
		Rio_readlineb(rp, buf, MAXLINE);
		if (strstr(buf, "Host: ") != NULL)
		{
			is_host = 1;
			sprintf(request_header, "%sHost: %s\r\n", request_header, host);
			continue;
		}
		if (strstr(buf, "User-Agent: ") != NULL)
		{
			is_user = 1;
			sprintf(request_header, "%s%s", request_header, user_agent_hdr);
			continue;
		}
		else if (strstr(buf, "Connection: ") != NULL)
		{
			is_connect = 1;
			sprintf(request_header, "%sConnection: close\r\n", request_header);
			continue;
		}
		else if (strstr(buf, "Proxy-Connection: ") != NULL)
		{
			is_prox_connect = 1;
			sprintf(request_header, "%sProxy-Connection: close\r\n", request_header);
			continue;
		}
		else if (strcmp(buf, "\r\n") != 0)
			sprintf(request_header, "%s%s", request_header, buf);
	}

	if (is_host == 0)
		sprintf(request_header, "%sHost: %s\r\n", request_header, host);
	if (is_user == 0)
		sprintf(request_header, "%s%s", request_header, user_agent_hdr);
	if (is_connect == 0)
		sprintf(request_header, "%sConnection: close\r\n", request_header);
	if (is_prox_connect == 0)
		sprintf(request_header, "%sProxy-Connection: close\r\n", request_header);

	sprintf(request_header, "%s\r\n", request_header);
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
	{
		if (strstr(uri, "/") == uri)
			strcpy(host, uri + 1);
		else
			strcpy(host, uri);
	}
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

web_object_t *find_cache(char *buf)
{
	if (!rootp)
		return NULL;

	web_object_t *current = rootp;
	while (strcmp(current->uri, buf))
	{
		if (!current->next)
			return NULL;

		current = current->next;
		if (!strcmp(current->uri, buf))
			return current;
	}
	return current;
}

void send_cache(web_object_t *web_object, int connfd)
{
	char buf[MAXLINE];

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, web_object->content_length);
	sprintf(buf, "%sContent-type : text/html\r\n\r\n", buf);

	Rio_writen(connfd, buf, strlen(buf));
	Rio_writen(connfd, web_object->response_ptr, web_object->content_length);
}

void read_cache(web_object_t *web_object)
{
	if (web_object == rootp)
		return;

	if (web_object->next)
	{
		web_object->prev->next = web_object->next;
		web_object->next->prev = web_object->prev;
	}
	else
		web_object->prev->next = NULL;

	web_object->next = rootp;
	rootp->prev = web_object;
	rootp = web_object;
	web_object->prev = NULL;
}

void write_cache(web_object_t *web_object)
{
	total_cache_size += web_object->content_length;

	while (total_cache_size > MAX_CACHE_SIZE)
	{
		total_cache_size -= lastp->content_length;
		lastp = lastp->prev;
		free(lastp->next);
		lastp->next = NULL;
	}

	if (!rootp)
		lastp = web_object;

	if (rootp)
	{
		web_object->next = rootp;
		rootp->prev = web_object;
	}
	rootp = web_object;
}