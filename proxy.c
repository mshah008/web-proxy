#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>

#define PORT 8081
#define MAX_SOCKS 2*1024
#define MAX_THREAD 20
#define MAX_CACHE 10000000
#define MAX_URL_LEN 2048
#define MYLOGGING
//TODO
// mutex for cache (single writer multiple readers pthread_rwlock_t)
// mutex for sockinfo
//freeing memory
//handling socket errors and memory errors
//handle all errors on linked lists
//Algorithm:
//the main function keeps track of the active connections and creates threads for handling any ready data that returns from select function
//each socket can nly be handled by one thread at the same time and therefore the thread can update the info structure the main function is maintaining about it
// using mutex for synchronizing access to cache and the socket info structure

//match all the frees with all the mallocs
// closing sockets
struct params_struct {
    struct sock_info_struct *sockinfo;
    int *thread_count;
};

struct content_st {
    unsigned char *content;
    unsigned int size;
    struct content_st *next;
};

struct sock_info_struct {
    int sockfd;
    struct sock_info_struct *assockinfo;
    int isclient;
    int beinghandled;
    struct sock_info_struct *next;
    struct sock_info_struct *previous;
    struct content_st *contents;
    char uri[1024];
};

struct sock_info_struct sockinfolist;
pthread_mutex_t mut;
struct cache_st cache;

#ifdef MYLOGGING
FILE *logfd;
pthread_mutex_t logmut;

int log(char str[])
{
    pthread_mutex_lock(&logmut); 
    fputs(str, logfd);
    //printf(str);
    fputs("\n", logfd);
    fflush(logfd);
    pthread_mutex_unlock(&logmut);
    return 0;
}
#else
int log(char str[])
{
    return 0;
}
#endif

//lock mut whenever sockinfo is going to be changed
//the functions here do not do that
struct sock_info_struct *sockinfo_add(struct sock_info_struct *first, int csockfd, int beinghandled, int isclient)
{
    struct sock_info_struct *newsockinfo;
    
    
    //pthread_mutex_lock(&mut);

    log("in sockinfo_add");
    newsockinfo = (struct sock_info_struct *) malloc(sizeof(struct sock_info_struct));
    log("malloced");
    memset(newsockinfo, 0, sizeof(struct sock_info_struct));
    log("memset");
    newsockinfo->next = first->next;
    newsockinfo->previous = first;
    first->next->previous = newsockinfo;
    first->next = newsockinfo;
    
    newsockinfo->sockfd = csockfd;
    newsockinfo->beinghandled = beinghandled;
    newsockinfo->isclient = isclient;
    log("returning from add sock");
    //pthread_mutex_unlock(&mut);    
    return newsockinfo;
}

int sockinfo_addassoc(struct sock_info_struct *first, struct sock_info_struct *cur, int ssockfd, int beinghandled, int isclient)
{
    struct sock_info_struct *newassock;
    assert(cur != NULL );
    
    log("adding new socket");
    //pthread_mutex_lock(&mut);
    cur->assockinfo = (struct sock_info_struct *) malloc(sizeof(struct sock_info_struct));
    newassock = cur->assockinfo;
    assert(cur->assockinfo);
    memset(newassock, 0, sizeof(struct sock_info_struct));
    first->next->previous = newassock;
    newassock->next = first->next;
    first->next = newassock;
    newassock->previous = first;
    newassock->beinghandled = beinghandled;
    newassock->isclient = isclient;
    newassock->assockinfo = cur;
    newassock->sockfd = ssockfd;
    //pthread_mutex_unlock(&mut);
}

int sockinfo_delsock(struct sock_info_struct *first, struct sock_info_struct *cur)
{
    struct sock_info_struct *assoc;
    
    log("deleting socket");
    if (!cur->isclient && cur->uri[0] != '\0' && cur->contents) {
        cache_add(&cache, cur->uri, cur->contents);
    }
    assoc = cur->assockinfo;
    cur->previous->next = cur->next;
    cur->next->previous = cur->previous;
    free(cur);
    if (assoc != NULL) {
        if (!assoc->isclient && assoc->uri[0] != '\0') {
            cache_add(&cache, assoc->uri, assoc->contents);
        }   
        assoc->next->previous = assoc->previous;
        assoc->previous->next = assoc->next;
        free(assoc);
    }
}


struct cache_ent {
    char uri[MAX_URL_LEN];
    struct content_st *contents;
    int size;
    struct cache_ent *next;
    struct cache_ent *previous;
};

//first element always empty
struct cache_st {
    int size;
    struct cache_ent first;
    struct cache_ent *last;
};


pthread_rwlock_t cache_lock = PTHREAD_RWLOCK_INITIALIZER;
int cache_init(struct cache_st *cache)
{
    cache->size = 0;
    cache->first.size = 0;
    cache->first.next = &cache->first;
    cache->first.previous = &cache->first;
}

int cache_free_contents(struct content_st *content)
{
    struct content_st *prev, *cur;
    prev = cur = content;
    while (cur != NULL) {
        prev = cur;
        cur = cur->next;
        if (prev->content) {
            free(prev->content);
        }
        free(prev);
    }
    return 0;
}

int cache_add_contents(struct content_st **dst, unsigned char *src, int size)
{
    struct content_st **curdst;

    curdst = dst;
    while (*curdst != NULL) {
        curdst = &((*curdst)->next);
    }
    *curdst = (struct content_st *) malloc(sizeof(struct content_st));
    memset(*curdst, 0, sizeof(struct content_st));
    (*curdst)->size = size;
    (*curdst)->content = (unsigned char *)malloc(size);
    memcpy((*curdst)->content, src, size);
    
    
        
}
int cache_copy_contents(struct content_st **dst, struct content_st *src)
{
    struct content_st *cursrc, **curdst;
    int size;

    size = 0;
    
    curdst = dst;
    cursrc = src;
    while (cursrc) {
        *curdst = (struct content_st *) malloc(sizeof(struct content_st));
        memset(*curdst, 0, sizeof(struct content_st));
        (*curdst)->content = (unsigned char *) malloc(cursrc->size);
        (*curdst)->next = NULL;
        (*curdst)->size = cursrc->size;
        memcpy((*curdst)->content, cursrc->content, cursrc->size);
        size += cursrc->size;
        curdst = &((*curdst)->next);
        cursrc = cursrc->next;
    }
    return size;
}
int cache_check(struct cache_st *cache, char uri[], struct content_st **contents)
{
    struct cache_ent *cur;
    int found;
    struct cache_st *last;
    struct content_st *content_cur;
    int ret;

    found = 0;

    log("checking the cache");
    log(uri);
    ret = pthread_rwlock_rdlock(&cache_lock); 

    for (cur = cache->first.next; cur != &cache->first && ! found; cur = cur->next) {
        if (strcmp(cur->uri, uri) == 0) {
            found = 1;
            break;
        }
    }

    if (found) {
        //if (size < cur->size) {
        //    return 1;
        //}
        //memcpy(contents, cur->contents, cur->size);
        cache_copy_contents(contents, cur->contents);
        if (cur != cache->first.next) {
            //get cur out of list
            cur->previous->next = cur->next;
            cur->next->previous = cur->previous;
            //add it to the end
            cache->first.previous->next = cur;
            cur->previous = cache->first.previous;
            cur->next = &cache->first;
            cache->first.previous = cur;
        }
        pthread_rwlock_unlock(&cache_lock);
        log("found in cache");
        return 0;
    } else {
        pthread_rwlock_unlock(&cache_lock);
        return 1;
    }

}
    

int cache_add(struct cache_st *cache, char uri[], struct content_st *contents)        
{
    //how to keep track of segmented response
    struct cache_ent *newnode;
    struct cache_ent *cur;
    char logmsg[1000];

    log("adding to cache");
    log(uri);
    pthread_rwlock_wrlock(&cache_lock);
    newnode = (struct cache_ent *) malloc(sizeof(struct cache_ent));
    memset(newnode, 0, sizeof(struct cache_ent));
    newnode->size = cache_copy_contents(&(newnode->contents), contents);
    sprintf(logmsg, "%d bytes msg added to cache", newnode->size);
    log(logmsg);
    if (newnode->size > MAX_CACHE) {
        cache_free_contents(newnode->contents);
        free(newnode);
        pthread_rwlock_unlock(&cache_lock);
        return 1;
    }
    cache->size += newnode->size;
    //memcpy(newnode->contents, contents, size);
    strcpy(newnode->uri, uri);
    cache->first.previous->next = newnode;
    newnode->previous = cache->first.previous;
    newnode->next = &cache->first;
    cache->first.previous = newnode;

    
    cur = cache->first.next;
    while (cache->size > MAX_CACHE && cur != &cache->first) {
        cache->size -= cur->size;
        cur->previous->next = cur->next;
        cur->next->previous = cur->previous;
        if (cur->contents) {
            cache_free_contents(cur->contents);
            cur->contents = NULL;
            free(cur);
        }
    }
    assert(cur != NULL);
    pthread_rwlock_unlock(&cache_lock);
}
        
        
void *serve_new_connection(void* args)
{
    //on exit you should release the locks, save cache, release memory, update thread count and beinghandled
    //add assoc
    int csockfd;
    fd_set fds;
    unsigned char buff[1024];
    unsigned char newreq[1024];
    char uri[1024];
    char server[1024];
    struct content_st *contents, *curcontents;
    char logmsg[1000];
    int ssockfd;
    int con;
    int contentexists;
    struct timeval timeout;
    struct sock_info_struct *sockinfo;
    struct sock_info_struct *assockinfo;
    ssize_t newread;
    int nread;
    int ret;
    struct hostent *saddr;
    struct sockaddr_in serv_addr;
    struct params_struct *params;
    int port;
    int nsent;
    int sent;
    int sentsofar;
    int first;

    params = args;
    sockinfo =  params->sockinfo;
    csockfd = sockinfo->sockfd;
    nread = 0;
    nsent = 0;
    int newsent;
    

    buff[sizeof(buff)-1] = '\0';
    log("serving new connection");
    //check associated sock, if does not exist create one
    if (sockinfo->assockinfo == NULL) {
        log("no assoc, finding the server");
        newread = recv(csockfd, buff, sizeof(buff) - 1, MSG_DONTWAIT);
        if (newread > SSIZE_MAX) {
            //handle errors
        }
        else if (newread == 0) {
            //handle errors
            close(sockinfo->sockfd);
            pthread_mutex_lock(&mut);
            sockinfo_delsock(&sockinfolist, sockinfo);
            *params->thread_count -= 1;
            free(params);
            pthread_mutex_unlock(&mut);
            //return 0;
            return NULL;
        } else if (newread < 0) {
            pthread_mutex_lock(&mut);
            *params->thread_count -= 1;
            free(params);
            sockinfo->beinghandled = 0;
            pthread_mutex_unlock(&mut);
            return NULL;
        } else {
            nread += newread;
        }
        
    
        //parse the request
        
        ret = parse_http_request(buff, nread, uri, server, &port, newreq);
        if (ret > 0) {
            log("request parsing error");
            close(sockinfo->sockfd);
            pthread_mutex_lock(&mut);
            sockinfo_delsock(&sockinfolist, sockinfo);
            *params->thread_count -= 1;
            free(params);
            pthread_mutex_unlock(&mut);
            //return 0;
            return NULL;

        }
        log("request parsed");
        //check the cache
        log("checking cache");
        ret = cache_check(&cache, uri, &contents);
        sprintf(logmsg, "contents=%d", contents);
        log(logmsg);
        if (ret == 0) {
            curcontents = contents;
            while (curcontents) {
                log("found in cache");
                sprintf(logmsg, "%d bytes from cache record", curcontents->size);
                log(logmsg);
                //fix the size in cache and here
                nsent = 0;
                sentsofar = 0;
                first = 1;
                while (first || (nsent > 0 && sentsofar < curcontents->size)) {
                    nsent = send(csockfd, curcontents->content + sentsofar, curcontents->size - sentsofar, 0);
                    sentsofar += nsent;
                    first = 0;
                }
                curcontents = curcontents->next;
            }
            cache_free_contents(contents);
            close(csockfd);
            pthread_mutex_lock(&mut);
            sockinfo_delsock(&sockinfolist, sockinfo);
            *params->thread_count -= 1;
            free(params);
            pthread_mutex_unlock(&mut);
            
            //return 1;
            return NULL;
            //delete the connection entry
        }
        log("not found in cache");
    //resolve server IP
        log("resolving host");
        log(server);
        saddr = gethostbyname(server);
        if (saddr == NULL) {
            log("not resolved");
            close(csockfd);
            pthread_mutex_lock(&mut);
            *params->thread_count -= 1;
            sockinfo_delsock(&sockinfolist, sockinfo);
            free(params);
            pthread_mutex_unlock(&mut);
            //return 1;
            return NULL;
        }
        log("server IP resolved");
    //connect to server
        ssockfd = socket(AF_INET, SOCK_STREAM, 0);
        
        if (ssockfd < 0) {
            log("not able to create socket");
            close(csockfd);
            pthread_mutex_lock(&mut);
            *params->thread_count -= 1;
            sockinfo_delsock(&sockinfolist, sockinfo);
            free(params);
            pthread_mutex_unlock(&mut);
            //return 1;
            return NULL;
        }
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        bcopy((char *)saddr->h_addr, 
           (char *)&serv_addr.sin_addr.s_addr,
                saddr->h_length);
        serv_addr.sin_port = htons(port);
        if (connect(ssockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
            log("not able to connect to server");
            close(csockfd);
            pthread_mutex_lock(&mut);
            *params->thread_count -= 1;
            sockinfo_delsock(&sockinfolist, sockinfo);
            free(params);
            pthread_mutex_unlock(&mut);
            //return 1;
            return NULL;
        }
        log("connected to server");
        
        nsent = 0;
        nread = strlen(newreq);
        while (nsent < nread && sent >= 0) {
            log("sending");
            sent = send(ssockfd, newreq+nsent, nread-nsent, 0);
            sprintf(logmsg, "%d out of %d sent", nsent + sent, nread);
            log(logmsg);
            if (sent > 0) {
                log("sent");
                nsent += sent;
            } else {
                log("nothing sent");
                break;
            }
        }
//        nread = 0;
//        first = 1;
//        while (first || nread > 0) 

//        recv(ssockfd, buff, sizeof(buff), NULL);
//        printf(buff);
        
        //add socket to the sockinfo
        sprintf(logmsg, "adding assoc %d", ssockfd);
        log(logmsg);
        pthread_mutex_lock(&mut);
        sockinfo_addassoc(&sockinfolist, sockinfo, ssockfd, 0, 0);
        sockinfo->beinghandled = 0;
        sockinfo->assockinfo->beinghandled = 0;
        strcpy(sockinfo->assockinfo->uri, uri);
        *params->thread_count -= 1;
        free(params);
        pthread_mutex_unlock(&mut);
        //return 0;
        return NULL;
        //add data to cache
    }
    log("old connection");
    assockinfo = sockinfo->assockinfo;
    ssockfd = assockinfo->sockfd;
    nread = 0;
    first = 1;
    contentexists = 0;
    while (first || nread > 0) {
        log("reading");
        nread = recv(sockinfo->sockfd, buff, sizeof(buff) - 1, MSG_DONTWAIT);
        sprintf(logmsg, "%d characters read", nread);
        log(logmsg);
        if (nread > 0) {
            log("writing");
            if (!sockinfo->isclient) {
                cache_add_contents(&(sockinfo->contents), buff, nread);
            
                contentexists = 1;
            }
            nsent = 0;
            while (nsent < nread) {
                newsent = send(assockinfo->sockfd, buff, nread, 0);
                sprintf(logmsg, "%d characters sent", newsent);
                log(logmsg);
                if (newsent <= 0) {
                    break;
                }
                nsent += newsent;
            }
        } else if (nread == 0 ) {
            close(sockinfo->sockfd);
            close(sockinfo->assockinfo->sockfd);
            pthread_mutex_lock(&mut);
            *params->thread_count -= 1;
            sockinfo_delsock(&sockinfolist, sockinfo);
            free(params);
            pthread_mutex_unlock(&mut);
            //return 0;
            return NULL;
        } 
        first = 0;
    }
    //read and write
    //if done get the mutex. delete from linked list
    //update thread count
    //update the cache
//    read(csockfd, 
    pthread_mutex_lock(&mut);
    *params->thread_count -= 1;
    sockinfo->beinghandled = 0;
    sockinfo->assockinfo->beinghandled = 0;
    pthread_mutex_unlock(&mut);
    free(params);
    return NULL;
}

int build_fdset(fd_set *fds, int mainfd, struct sock_info_struct *sockinfolist, int socknum)
{
    int i;
    char logmsg[1000];
    char tmpmsg[1000];
    struct sock_info_struct *cur;

    FD_ZERO(fds);
    FD_SET(mainfd, fds);
    strcpy(logmsg, "");

    //log("building fdset");
    cur = sockinfolist->next;
    for (cur = sockinfolist->next; cur != sockinfolist && cur != NULL; cur = cur->next) {
        //sprintf(logmsg, "cur=%d,sockinfolist=%d", cur, &sockinfolist);
        //log(logmsg);
        //exit(0);
        FD_SET(cur->sockfd, fds);
        sprintf(tmpmsg, " sock %d ", cur->sockfd);
        strcat(logmsg, tmpmsg);
    }
    //log(logmsg);
    
    return 0;
}
    
int parse_http_request(char request[], int reqlen, char uri[], char server[], int *port, unsigned char newreq[])
{
    //SHOULD it also support relative uris?
    char *tok;
    char *cur;
    char resource[1000];
    char tmpstr[1000];
    char protocol[1000];
    
    char tmprequest[1000];

    strcpy(tmprequest, request);

    log("parsing http request");
    log(request);
    if (strncmp(request, "GET ", 4) != 0){
        log("not a get request");
        return 1;
    }
    tok = strtok(tmprequest, " ");
    tok = strtok(NULL, " ");
    if (tok == NULL) {
        log("get request not following format");
        return 1;
    }
    strcpy(uri, tok);
    strcpy(tmpstr, uri);
    tok = strtok(NULL, "\r");
    if (tok == NULL) {
        log("bad req1");
        return 1;
    }
    strcpy(protocol, tok);
    
    tok = strtok(tmpstr, "/");
    tok = strtok(NULL, "/");
    if (tok == NULL) {
        log("slashes not found");
        return 1;
    }
    printf("server= %s", tok);

    strcpy(server, tok);
    tok = strtok(NULL, " ");
    if (tok == NULL) {
        log("no resource specified");
    }  
      
    strcpy(resource, "/");
    if (tok != NULL) { 
        strcat(resource, tok);
    }
    cur = strchr(server, ':');
    *port = 80;
    if (cur != NULL){
        *port = atoi(cur+1);
        *cur = '\0';
    }
        
    log(uri);
    log(server);    
    
    sprintf(newreq, "GET %s %s", resource, protocol);
    log(newreq);
    cur = strchr(request, '\r');
    if (cur == NULL) {
        log(request);
        log("bad request");
        return 1;
    }
    strcat(newreq, cur);

    log("newreq=");
    log(newreq);
    return 0;
}
int main(int agrc, const char *argv[])
{
    int mainfd, connfd;
    struct sockaddr_in proxy_addr;
    pthread_t pth;
    struct timeval timeout;
    int ret;
//    struct sock_info_struct sock_info[MAX_SOCKS];
    int socknum;
    struct timespec t1, t2;
    int sockid;
    int newsockfd;
    struct sock_info_struct *cur;
    struct sock_info_struct *newsockinfo;
    int thread_count;
    struct params_struct *newparams;
    char logmsg[1000];
    int nsock, readysocks;
    int changed;
    
    thread_count = 0;

    fd_set fds;

    t1.tv_sec = 0;
    t1.tv_nsec = 1000000;
    sockid = 0;

    socknum = 0;
    

    sockinfolist.next = &sockinfolist;
    sockinfolist.previous = &sockinfolist;

    cache_init(&cache);
    if (pthread_mutex_init(&mut, NULL) != 0)
    {
        return 1;
    }
   
#ifdef MYLOGGING
    logfd = fopen("proxy.log", "at");
    if (pthread_mutex_init(&logmut, NULL) != 0)
    {
        return 1;
    }
#endif
    sprintf(logmsg, "%d %d", (int)&sockinfolist, (int)sockinfolist.next);
    log(logmsg);
    mainfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&proxy_addr, '0', sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    proxy_addr.sin_port = htons(PORT); 
    struct params_struct params;

    log("setting up mainsocket");
    if (bind(mainfd, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr)) != 0) {
        log("cannot bind");
        return 1;
    }

    if (listen(mainfd, 10)  != 0) {
        log("cannot listen");
        return 1;
    }

    while(1)
    {
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        build_fdset(&fds, mainfd, &sockinfolist, socknum);
   
        
         
        //log("selecting");
        ret = select(sizeof(fds)*8, &fds, NULL, NULL, &timeout);
        //log("select returned");
        changed = 0;
        pthread_mutex_lock(&mut);
        if (thread_count < MAX_THREAD) {
            if (FD_ISSET(mainfd, &fds)) {
                log("received new connection");
                newsockfd = accept(mainfd, (struct sockaddr*)NULL, NULL); 
                socknum++;
                thread_count += 1;
                newparams = (struct params_struct *) malloc(sizeof(struct params_struct));
                memset(newparams, 0, sizeof(struct params_struct));
                newparams->thread_count = &thread_count;
                log("adding new socket to sockinfo");
                newsockinfo = sockinfo_add(&sockinfolist, newsockfd, 1, 1);
                log("new socket added to sockinfo");
                newparams->sockinfo = newsockinfo;
                log("creating thread for new connection");
                pthread_create(&pth,NULL, serve_new_connection, newparams);
            }
            changed = 1;
        }
        nsock = 0;
        readysocks = 0;
        for (cur = sockinfolist.next; cur != &sockinfolist && cur != NULL; cur = cur->next) {
            nsock += 1;
            if (thread_count < MAX_THREAD) {
                if (FD_ISSET(cur->sockfd, &fds) && !cur->beinghandled) {
                    thread_count += 1;
                    newparams = (struct params_struct *) malloc(sizeof(struct params_struct));
                    memset(newparams, 0, sizeof(struct params_struct));
                    newparams->thread_count = &thread_count;
                    newparams->sockinfo = cur;
                    log("creating thread for existing connection");
                    cur->beinghandled = 1;
                    if (cur->assockinfo) {
                        cur->assockinfo->beinghandled = 1;
                    }
                    pthread_create(&pth,NULL, serve_new_connection, newparams);
                    readysocks += 1;
                    changed = 1;

                
                }   
            }
        }
        pthread_mutex_unlock(&mut);
        if (changed) {
            //sprintf(logmsg, "%d sockets, %d readysocks, %d threads", nsock, readysocks, thread_count);
            //log(logmsg);
        }
        nanosleep(&t1, &t2);    
    }
    return 0;
}

