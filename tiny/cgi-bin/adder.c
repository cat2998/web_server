#include "../csapp.h"

int main(void)
{
	char *buf, *p, *ptr, *method;
	char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
	int nl = 0, n2 = 0;

	/* Extract the two arguments */
	if ((buf = getenv("QUERY_STRING")) != NULL)
	{
		p = strchr(buf, '&');
		*p = '\0';
		ptr = index(buf, '=');
		if (ptr)
		{
			strcpy(arg1, p - 1);
			strcpy(arg2, p + 3);
		}
		else
		{
			strcpy(arg1, buf);
			strcpy(arg2, p + 1);
		}
		nl = atoi(arg1);
		n2 = atoi(arg2);
	}

	/* Make the response body */
	sprintf(content, "QUERY_STRING=%s", buf);
	sprintf(content, "Welcome to add.com: ");
	sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
	sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, nl, n2, nl + n2);
	sprintf(content, "%sThanks for visiting!\r\n\r\n", content);

	/* Generate the HTTP response */
	printf("Connection: close\r\n");
	printf("Content-length: %d\r\n", (int)strlen(content));
	printf("Content-type: text/html\r\n\r\n");
	method = getenv("REQUEST_METHOD");
	if (strcasecmp(method, "HEAD") != 0)
		printf("%s", content);
	fflush(stdout);

	exit(0);
}