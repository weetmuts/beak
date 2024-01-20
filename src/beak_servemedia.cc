/*
 Copyright (C) 2020 Fredrik Öhrström

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <Magick++.h>

#include "beak.h"
#include "beak_implementation.h"
#include "backup.h"
#include "filesystem_helpers.h"
#include "log.h"
#include "media.h"
#include "storagetool.h"
#include "system.h"

static ComponentId SERVEMEDIA = registerLogComponent("servemedia");

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sstream>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>


struct ServeMedia
{
    BeakImplementation *beak_ {};
    MediaDatabase db_;
    Settings *settings_ {};
    Monitor *monitor_ {};
    FileSystem *fs_ {};
    System *sys_ {};
    int num_ {};

    ServeMedia(BeakImplementation *beak, Settings *settings, Monitor *monitor, FileSystem *fs, System *sys)
        : beak_(beak), db_(fs, sys), settings_(settings), monitor_(monitor), fs_(fs), sys_(sys)
    {
    }

    void start();
};

const char *response_200 = "HTTP/1.1 200 OK\nContent-Type: text/html; charset=utf-8\n\n<html><body><i>Hello!</i></body></html>";
const char *response_400 = "HTTP/1.1 400 Bad Request\nContent-Type: text/html; charset=utf-8\n\n<html><body><i>Bad Request!</i></body></html>";
const char *response_404 = "HTTP/1.1 404 Not Found\nContent-Type: text/html; charset=utf-8\n\n<html><body><i>Not Found!</i></body></html>";

void *handle_request(int fd)
{
    ssize_t n;
    char buffer[255];
    const char *response;

    n = recv(fd, buffer, sizeof(buffer), 0);
    if(n < 0) {
        perror("recv()");
        return 0;
    }

    buffer[n] = 0;
    printf("recv() %s\n", buffer);

    response = response_400;

    string s(buffer), token;
    istringstream ss(s);
    vector<string> token_list;
    for(int i = 0; i < 3 && ss; i++) {
        ss >> token;
        printf("token %d %s\n", i, token.c_str());
        token_list.push_back(token);
    }

    printf("URKA >%s< >%s<\n", token_list[0].c_str(), token_list[2].c_str());
    if(token_list.size() == 3
       && token_list[0] == "GET"
       && token_list[2].substr(0, 4) == "HTTP")
    {
        if(token_list[1] == "/index.html")
        {
            response = response_200;
            printf("OK 200\n");
        } else {
            response = response_404;
            printf("ERROR 404\n");
        }
    }

    n = write(fd, response, strlen(response));
    if(n < 0)
    {
        perror("write()");
        return 0;
    }
    shutdown(fd, SHUT_RDWR);
    return 0;
}

void ServeMedia::start()
{
    /*
    int sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in servaddr;

    if(sockfd < 0)
    {
        perror("socket() error");
        exit(EXIT_FAILURE);
    }

    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        error(SERVEMEDIA, "setsockopt(SO_REUSEADDR) failed");
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8080);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind()");
        exit(EXIT_FAILURE);
    }

    info(SERVEMEDIA, "http://localhost:8080/index.html\n");

    if (listen(sockfd, 1000) < 0)
    {
        perror("listen()");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_storage clieaddr;
    int cliefd;
    char s[INET_ADDRSTRLEN];
    socklen_t cliesize;

    while(true)
    {
        cliesize = sizeof(clieaddr);
        info(SERVEMEDIA, "Waiting for connect...\n");
        cliefd = accept(sockfd, (struct sockaddr *)&clieaddr, &cliesize);
        if(cliefd < 0)
        {
            perror("accept()");
            continue;
        }

        inet_ntop(clieaddr.ss_family, (void *)&((struct sockaddr_in *)&clieaddr)->sin_addr, s, sizeof(s));

        printf("HANDLING accept() %s\n", s);
        handle_request(cliefd);
    }
    */
}

RC BeakImplementation::serveMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin);

    Path *root = settings->from.origin;

    ServeMedia serve_media(this, settings, monitor, local_fs_, sys_);

    FileStat origin_dir_stat;
    local_fs_->stat(root, &origin_dir_stat);
    if (!origin_dir_stat.isDirectory())
    {
        usageError(SERVEMEDIA, "Not a directory: %s\n", root->c_str());
    }

    info(SERVEMEDIA, "Serving media inside %s\n", root->c_str());

    serve_media.start();

    return rc;
}
