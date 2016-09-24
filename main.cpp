#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/epoll.h>
#include <errno.h>

struct fileinfo {
    int fd;
    off_t size;
};

struct connectioninfo {
    int fd;
    off_t offset;
    fileinfo file;
};

void error(const char *msg) {
    perror(msg);
    exit(0);
}


int main(int argc, char *argv[]) {
    if (argc < 5) {
       fprintf(stderr,"usage %s hostname port connections directory\n", argv[0]);
       exit(0);
    }
    int portno = atoi(argv[2]);
    int connections = atoi(argv[3]);
    
    struct hostent *server;
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    
    fprintf(stdout,"counting files…\n");
    DIR * dir = opendir(argv[4]);
    long start = telldir(dir);
    int entrys = 0;
    struct dirent * entry;
    while ((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] != '.') {
            entrys++;
        }
    }
    int entrys2 = entrys;
    seekdir(dir, start);
    fprintf(stdout,"found %i files\n",entrys);
    
    fprintf(stdout,"opening files…\n");
    fileinfo * files = new fileinfo[entrys];
    size_t size = 1;
    char * filename = new char[size];
    while ((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] != '.') {
            entrys--;
            
            int realsize = snprintf(filename, size, "%s/%s", argv[4], entry->d_name) +1;
            if(size < realsize) {
                size = realsize;
                free(filename);
                filename = new char[size];
                snprintf(filename, size, "%s/%s", argv[4], entry->d_name);
            }
            files[entrys].fd = open(filename, 0);
            struct stat buf;
            fstat(files[entrys].fd, &buf);
            files[entrys].size = buf.st_size;
        }
    }
    free(filename);
    closedir(dir);

    
    fprintf(stdout,"opening connections…\n");
    connectioninfo * sockets = new connectioninfo[connections];
    int epoll = epoll_create1(0);
    for(int i = 0; i < connections; i++) {
        sockets[i].fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
        if (sockets[i].fd < 0) {
            error("ERROR opening socket");
        }
        connect(sockets[i].fd,(struct sockaddr *) &serv_addr,sizeof(serv_addr));
        
        sockets[i].offset = 0;
        sockets[i].file = files[i % entrys2];
        
        struct epoll_event event;
        event.data.ptr = &sockets[i];
        event.events = EPOLLOUT;
        epoll_ctl(epoll, EPOLL_CTL_ADD, sockets[i].fd, &event);
    }
    
    #define MAX_EVENTS 1000
    struct epoll_event events[MAX_EVENTS];
    fprintf(stdout,"entering loop…\n");
    while (true) {
        int numEvents = epoll_wait(epoll, events, MAX_EVENTS, -1);
        
        for(int i = 0; i < numEvents; i++) {
            connectioninfo &con = *((connectioninfo*) events[i].data.ptr);
            //fprintf(stdout, "sending to connection %i\n", con.fd);
            ssize_t result = sendfile(con.fd, con.file.fd, &con.offset, con.file.size - con.offset);
            if(result = -1){
                //fprintf(stderr, "sendfile error %i (%s)\n", errno, strerror(errno));
            }
            if (con.offset >= con.file.size) {
                con.offset = 0;
            }
        }
    }
    
    
}
