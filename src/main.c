

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>

#include <sapi/embed/php_embed.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <linux/limits.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <libgen.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ansi-colors.h"
#include "log.h"

#define CONNMAX 1000
#define BYTES 1024

char *ROOT;
int listenfd, clients[CONNMAX];
void error(char *);
void startServer(char *);
void respond(int, int, char**);

void interrupt_handler() {
    printf(" %sExiting after interrupt...%s\n", ansi.green, ansi.reset);
    Py_Finalize();
    exit(0);
}

bool str_ends_with(char *string, const char *suffix) {
    int i;
    for (i = 0; ; i++) {
        if (string[i] == '\0') {break;}
    }
    int string_len = i;
    int suffix_len = strlen(suffix);
    for (i = 0; i < suffix_len; i++) {
        if (string[string_len-suffix_len+i] != suffix[i]) {
            return false;
        }
    }
    return true;
}
void prepend(char* s, const char* t) {
    size_t len = strlen(t);
    memmove(s + len, s, strlen(s) + 1);
    memcpy(s, t, len);
}
int main(int argc, char *argv[]) {
    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT, interrupt_handler);

    struct sockaddr_in clientaddr;
    socklen_t addrlen;
    char c;
    // Default Values PATH = ~/ and PORT=8080
    char PORT[6];
    ROOT = getenv("PWD");
    strcpy(PORT, "8080");

    int slot = 0;

    // Parsing the command line arguments
    while ((c = getopt(argc, argv, "p:r:")) != -1)
        switch (c) {
        case 'r':
            ROOT = malloc(strlen(optarg));
            strcpy(ROOT, optarg);
            break;
        case 'p':
            strcpy(PORT, optarg);
            break;
        case '?':
            fprintf(stderr, "Wrong arguments given!!!\n");
            exit(1);
        default:
            exit(1);
        }

    int i;
    for (i = 0; i < CONNMAX; i++)
        clients[i] = -1;
    // Initialize Python interpreter
    Py_Initialize();
    startServer(PORT);
    printf("Server started at port %s%s%s with root directory as %s%s%s\n\n", ansi.green, PORT, ansi.reset, ansi.green, ROOT, ansi.reset);

                        PyRun_SimpleString("import sys");
                        struct stat st = {0};
                        if (stat("/cache/http-server", &st) == -1) {
    mkdir("/var/cache/http-server", 0700);
    PyRun_SimpleString("sys.pycache_prefix=\"/var/cache/http-server\"");
}

    while (true) {
        addrlen = sizeof(clientaddr);
        clients[slot] = accept(listenfd, (struct sockaddr *)&clientaddr, &addrlen);

        if (clients[slot] < 0) {
            error("accept() error");
        } else {
            if (fork() == 0) {
                respond(slot, argc, argv);
                exit(0);
            }
        }

        while (clients[slot] != -1)
            slot = (slot + 1) % CONNMAX;
    }

    Py_Finalize();
    return 0;
}

void startServer(char *port) {
    struct addrinfo hints, *res, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        log_error("getaddrinfo()", strerror(errno));
        exit(1);
    }
    for (p = res; p != NULL; p = p->ai_next) {
        listenfd = socket(p->ai_family, p->ai_socktype, 0);
        const int reuse = 1;
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {printf("%sERROR%s: setsockopt(SO_REUSEADDR): %s\n", ansi.red, ansi.reset, strerror(errno));}
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {printf("%sERROR%s: setsockopt(SO_REUSEPORT): %s\n", ansi.red, ansi.reset, strerror(errno));}

        if (listenfd == -1) {
            continue;
        }
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
    }
    if (p == NULL) {
        log_error("bind() or socket()", strerror(errno));
        exit(1);
    }

    freeaddrinfo(res);

    // listen for incoming connections
    if (listen(listenfd, 1000000) != 0) {
        log_error("listen()", strerror(errno));
        exit(1);
    }
}
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

int cstdout = STDOUT_FILENO;
void respond(int n, int argc, char **argv) {
    char mesg[99999], *reqline[3], data_to_send[BYTES];
    int rcvd, fd, bytes_read;

    memset((void *)mesg, (int)'\0', 99999);

    rcvd = recv(clients[n], mesg, 99999, 0);

    const char *resp_200_header = "HTTP/1.0 200 OK\n\n";

    const char *resp_400_header = "HTTP/1.0 400 Bad Request\n\n";
    const char *default_resp_400_page = "<html><body><center><b>400</b><p>Bad Request</p></center></html></body>\n";

    const char *resp_404_header = "HTTP/1.0 404 Not Found\n\n";
    const char *default_resp_404_page = "<html><body><center><b>404</b><p>Not Found</p></center></html></body>\n";

    const char *resp_500_header = "HTTP/1.0 500 Internal Server Error\n\n";
    const char *default_resp_500_page = "<html><body><center><b>500</b><p>Internal Server Error</p></center></html></body>\n";

    if (rcvd < 0) {
        printf("%sERROR%s: recv(): %s\n", ansi.red, ansi.reset, strerror(errno));
    } else if (rcvd == 0) {
        log_error("recv()", "Client disconnected unexpectedly");
    } else {
        reqline[0] = strtok(mesg, " \t\n");
        if (true) {
            reqline[1] = strtok(NULL, " \t");
            reqline[2] = strtok(NULL, " \t\n");
            if (strncmp(reqline[2], "HTTP/1.0", 8) != 0 && strncmp(reqline[2], "HTTP/1.1", 8) != 0) {
                    write(clients[n], resp_400_header, strlen(resp_400_header));
                    write(clients[n], default_resp_400_page, strlen(default_resp_400_page));
            } else {
                prepend(reqline[1], ROOT);
                char *url = "";
                char *url_params_str = "";
                char *qmark = strchr(reqline[1], '?');
                if (qmark != NULL) {
                    url = strtok(reqline[1], "?");
                    if (reqline[1][qmark-reqline[1]+1] != '\0') {
                        url_params_str = strtok(NULL, "");
                    }
                strcpy(reqline[1], url);
                } else {
                    url = reqline[1];
                }
                char *token = strtok(url_params_str, "&");
                char *url_params[100] = {0}; int url_params_count = 0; int i = 0;
                while(token != NULL) {
                    url_params[i] = token;
                    token = strtok(NULL, "&");
                    url_params_count++; i++;
                }

                DIR *folder;
                if ((folder = opendir(reqline[1])) != NULL) {
                    closedir(folder);
                    if (!str_ends_with(reqline[1], "/")) {
                        strcat(reqline[1], "/");
                    }
                    strcat(reqline[1], "index.html");
                }
                printf("Client accessed file: %s%s%s\n\n", ansi.blue, reqline[1], ansi.reset);

                if ((fd = open(reqline[1], O_RDONLY)) != -1) {
                    if (str_ends_with(reqline[1], ".py")) {
                        Py_Initialize();
                        char import_path[999] = "";
                        sprintf(import_path, "sys.path.append(\"%s\")", ROOT);
                        PyRun_SimpleString(import_path);
                        PyObject *page_module = PyImport_ImportModule(strtok(basename(reqline[1]), "."));

                        PyObject *type, *value, *traceback;
                        PyErr_Fetch(&type, &value, &traceback);
                        if (type != NULL) {
                            PyErr_Restore(type, value, traceback);
                            PyErr_Print();
                            printf("\n");
                            write(clients[n], resp_500_header, strlen(resp_500_header));
                            write(clients[n], default_resp_500_page, strlen(default_resp_500_page));
    shutdown(clients[n], SHUT_RDWR);
    close(clients[n]);
    clients[n] = -1;
    return;
                        }
                        PyObject *page_main_func = PyObject_GetAttrString(page_module, "http_main");
                        PyObject *page_url_params = PyDict_New();
                        PyObject *param = NULL;
                        char *param_name = "";
                        char *param_val = "";
                        for (int i = 0; i != url_params_count; ++i) {
                            param_name = strtok(url_params[i], "=");
                            param_val = strtok(NULL, "=");
                            param = Py_BuildValue("s", (const char*) param_val);
                            PyDict_SetItemString(page_url_params, param_name, param);
                        }
                        PyErr_Fetch(&type, &value, &traceback);
                        if (type != NULL) {
                            PyErr_Restore(type, value, traceback);
                            PyErr_Print();
                            write(clients[n], resp_500_header, strlen(resp_500_header));
                            write(clients[n], default_resp_500_page, strlen(default_resp_500_page));
    shutdown(clients[n], SHUT_RDWR);
    close(clients[n]);
    clients[n] = -1;
    return;
                        }
                        PyObject *result = PyObject_CallObject(page_main_func, Py_BuildValue("(sOs)", reqline[1], page_url_params, reqline[0]));

                        PyErr_Fetch(&type, &value, &traceback);
                        if (type != NULL) {
                            PyErr_Restore(type, value, traceback);
                            PyErr_Print();
                            printf("\n");
                            write(clients[n], resp_500_header, strlen(resp_500_header));
                            write(clients[n], default_resp_500_page, strlen(default_resp_500_page));
                        } else {
                            char *page_content = (char*)PyUnicode_AsUTF8(result);
                            write(clients[n], resp_200_header, strlen(resp_200_header));
                            write(clients[n], page_content, strlen(page_content));
                        }
                        Py_Finalize();
                    } else if (str_ends_with(reqline[1], ".php")) {

	int pipefd[2];

	// pipe to buffer php stdout
	if (pipe2(pipefd, O_NONBLOCK) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}

	// copy stdout before overriding
	cstdout = dup(STDOUT_FILENO);

	// override stdout with pipe
	if(dup2(pipefd[1], STDOUT_FILENO) == -1){
		perror("dup2");
		exit(EXIT_FAILURE);
	}

	PHP_EMBED_START_BLOCK(argc, argv)

	zend_file_handle file_handle;
	zend_stream_init_filename(&file_handle, reqline[1]);

	if (php_execute_script(&file_handle) != 1) {
		printf("Failed to execute PHP script.\n");
	}

	PHP_EMBED_END_BLOCK()

	// restore stdout and close pipe input
	if(dup2(cstdout, STDOUT_FILENO) == -1){
		perror("dup2 restore");
		exit(EXIT_FAILURE);
	}

	char buf[2048];
	int bytes;
                            write(clients[n], "HTTP/1.0 200 OK\n", strlen("HTTP/1.0 200 OK\n"));
                            write(clients[n], "Content-Type:text/html\n\n", strlen("Content-Type:text/html\n\n"));
	while ((bytes = read(pipefd[0], &buf, 2048)) != -1)
	{
                            write(clients[n], (char*) &buf, strlen((char*) &buf));
	}

	// close pipe output
	close(pipefd[0]);
                    } else {
                        // Server as static
                        write(clients[n], resp_200_header, strlen(resp_200_header));
                        while ((bytes_read = read(fd, data_to_send, BYTES)) > 0) {
                            write(clients[n], data_to_send, bytes_read);
                        }
                    }
                } else {
                    write(clients[n], resp_404_header, strlen(resp_404_header));
                    write(clients[n], default_resp_404_page, strlen(default_resp_404_page));
                }
            }
        }
    }
    shutdown(clients[n], SHUT_RDWR);
    close(clients[n]);
    clients[n] = -1;
}