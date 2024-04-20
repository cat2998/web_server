/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

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

	printf("== tiny start ==\n");
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
	int is_static;
	struct stat sbuf;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	rio_t rio;

	/* HTTP 요청 헤더 읽기 */
	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);
	sscanf(buf, "%s %s %s", method, uri, version);
	printf("Request headers\n");
	printf("%s", buf);
	if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0)
	{
		clienterror(fd, method, "501", "Not implemented", "Tiny dose not implement this method");
		return;
	}
	read_requesthdrs(&rio);

	/* 정적 콘텐츠 확인 */
	is_static = parse_uri(uri, filename, cgiargs);
	if (stat(filename, &sbuf) < 0)
	{
		clienterror(fd, filename, "404", "NOT found", "Tiny couldn't find this file");
		return;
	}

	if (is_static == 1)
	{															   // 정적 콘텐츠 요청
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) // 파일이 일반 파일이 아니거나 파일 소유자에 의해 실행 가능하지 않은 경우
		{
			clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
			return;
		}
		serve_static(fd, filename, sbuf.st_size, method);
	}
	else
	{ // 동적 콘텐츠 요청
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
		{
			clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
		}
		serve_dynamic(fd, filename, cgiargs, method);
	}
}

/* 요청 헤더를 읽고 무시*/
void read_requesthdrs(rio_t *rp)
{
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	while (strcmp(buf, "\r\n"))
	{
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	}
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

int parse_uri(char *uri, char *filename, char *cgiargs)
{
	char *ptr;
	if (!strstr(uri, "cgi-bin")) // cgi-bin 이 uri 에 존재하지 않을 때, URI를 기반으로 파일 시스템에서 파일을 찾는 경로를 생성하고, 만약 URI가 / 로 끝나면 "home.html" 파일을 요청
	{							 // 정적 콘텐츠 요청
		strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		if (uri[strlen(uri) - 1] == '/')
			strcat(filename, "home.html");
		return 1;
	}
	else
	{ // 동적 컨텐츠 요청
		ptr = index(uri, '?');
		if (ptr)
		{
			strcpy(cgiargs, ptr + 1);
			*ptr = '\0';
		}
		else
			strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		return 0;
	}
}

void serve_static(int fd, char *filename, int filesize, char *method)
{
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXBUF];

	/* response 헤더 생성 및 전송 */
	get_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	printf("Response headers: \n");
	printf("%s", buf);
	Rio_writen(fd, buf, strlen(buf));

	if (strcasecmp(method, "HEAD") != 0)
	{
		/* response body 전송 */
		srcfd = Open(filename, O_RDONLY, 0);
		// srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
		srcp = (char *)malloc(filesize);
		rio_readn(srcfd, srcp, filesize);
		Close(srcfd);
		Rio_writen(fd, srcp, filesize); // 주소 srcp에서 시작하는 filesize 바이트를 클라이언트의 연결식별자로 복사. 즉, 클라이언트에게 파일 전송
		// Munmap(srcp, filesize);
		free(srcp);
	}
}

void get_filetype(char *filename, char *filetype)
{
	if (strstr(filename, ".html"))
		strcpy(filetype, "text/html");
	else if (strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
	else if (strstr(filename, ".png"))
		strcpy(filetype, "image/png");
	else if (strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpeg");
	else if (strstr(filename, ".mp4"))
		strcpy(filetype, "video/mp4");
	else
		strcpy(filetype, "text/plain");
}
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
	char buf[MAXLINE], *emptylist[] = {NULL};

	/* HTTP response 초기 값 전송 */
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Server: Tiny Web Server\r\n");
	Rio_writen(fd, buf, strlen(buf));
	if (Fork() == 0)
	{
		setenv("QUERY_STRING", cgiargs, 1);
		setenv("REQUEST_METHOD", method, 1);
		Dup2(fd, STDOUT_FILENO); // fd가 가리키는 파일을 표준 출력(STDOUT_FILENO)이 가리키는 파일로 복제. 즉, fd가 가리키는 파일과 표준 출력이 같은 파일을 가리키게 됨. 이로써, 표준 출력으로의 모든 출력이 해당 파일로 리디렉션됨
		Execve(filename, emptylist, environ);
	}
	wait(NULL);
}