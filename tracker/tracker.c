/*
 * tracker.c - Centralized Tracker Server for P2P File Sharing
 * CS 4390 - Computer Networks
 *
 * This server maintains tracker files that record which peers
 * are sharing which files. It handles 4 commands from peers:
 *   1. REQ LIST      - list all tracker files
 *   2. GET           - send a specific tracker file
 *   3. createtracker - create a new tracker file
 *   4. updatetracker - update peer info in a tracker file
 *
 * Uses pthreads for concurrent client handling.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define MAXLINE 4096
#define MAX_TRACKER_PEERS 256
#define DEAD_PEER_TIMEOUT 900

typedef struct {
    char filename[256];
    long long filesize;
    char md5[64];
} TrackerMeta;

typedef struct {
    char ip[64];
    int port;
    long long start_byte;
    long long end_byte;
    long long timestamp;
} TrackerPeer;

typedef struct {
    char filename[256];
    long long filesize;
    char description[512];
    char md5[64];
    TrackerPeer peers[MAX_TRACKER_PEERS];
    int peer_count;
} TrackerFileData;

int server_port;
char tracker_dir[256];

void  read_config(void);
void *peer_handler(void *arg);
void  handle_list_req(int sock);
void  handle_get_req(int sock, const char *file_name);
void  handle_createtracker_req(int sock, const char *msg);
void  handle_updatetracker_req(int sock, const char *msg);
int   parse_tracker_file(const char *path, TrackerFileData *data);
int   write_tracker_file(const char *path, const TrackerFileData *data);
int   extract_tracker_metadata(TrackerMeta *items, int max_items);
char *extract_filename_from_get(const char *msg);
void  remove_dead_peers(TrackerFileData *data);

static int write_all(int fd, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, buf + total, len - total);
        if (n < 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    read_config();
    printf("[config] port = %d, tracker_dir = %s\n", server_port, tracker_dir);

    int sockid;
    struct sockaddr_in server_addr;

    sockid = socket(AF_INET, SOCK_STREAM, 0);
    if (sockid < 0) { perror("socket"); exit(1); }

    int yes = 1;
    setsockopt(sockid, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(server_port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockid, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind"); exit(1);
    }

    if (listen(sockid, 10) < 0) { perror("listen"); exit(1); }
    printf("Tracker SERVER READY TO LISTEN INCOMING REQUEST....\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t clilen = sizeof(client_addr);
        int sock_child = accept(sockid, (struct sockaddr *)&client_addr, &clilen);
        if (sock_child == -1) { perror("accept"); continue; }

        printf("[main] connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        int *client_sock = malloc(sizeof(int));
        if (!client_sock) { perror("malloc"); close(sock_child); continue; }
        *client_sock = sock_child;

        pthread_t tid;
        if (pthread_create(&tid, NULL, peer_handler, client_sock) != 0) {
            perror("pthread_create"); close(sock_child); free(client_sock); continue;
        }
        pthread_detach(tid);
    }

    close(sockid);
    return 0;
}

void *peer_handler(void *arg) {
    int sock = *(int *)arg;
    free(arg);

    char read_msg[MAXLINE];
    ssize_t length = read(sock, read_msg, sizeof(read_msg) - 1);
    if (length <= 0) { close(sock); return NULL; }

    read_msg[length] = '\0';
    read_msg[strcspn(read_msg, "\r\n")] = '\0';
    printf("[handler] received: \"%s\"\n", read_msg);

    if (strcasecmp(read_msg, "REQ LIST") == 0 ||
        strcasecmp(read_msg, "<REQ LIST>") == 0) {
        handle_list_req(sock);
    }
    else if (strncasecmp(read_msg, "createtracker", 13) == 0) {
        handle_createtracker_req(sock, read_msg);
    }
    else if (strncasecmp(read_msg, "updatetracker", 13) == 0) {
        handle_updatetracker_req(sock, read_msg);
    }
    else if (strncasecmp(read_msg, "GET ", 4) == 0 ||
             strncasecmp(read_msg, "<GET ", 5) == 0) {
        char *fname = extract_filename_from_get(read_msg);
        if (fname) handle_get_req(sock, fname);
        else { const char *e = "<GET invalid>\n"; write_all(sock, e, strlen(e)); }
    }
    else {
        printf("[handler] unknown command: %s\n", read_msg);
    }

    close(sock);
    return NULL;
}

void read_config(void) {
    FILE *fp = fopen("serverThreadConfig.cfg", "r");
    if (!fp) { printf("Cannot open serverThreadConfig.cfg\n"); exit(1); }

    char buf[256];
    if (!fgets(buf, sizeof(buf), fp)) { printf("Error reading port\n"); fclose(fp); exit(1); }
    server_port = atoi(buf);

    if (!fgets(tracker_dir, sizeof(tracker_dir), fp)) { printf("Error reading dir\n"); fclose(fp); exit(1); }
    tracker_dir[strcspn(tracker_dir, "\r\n")] = '\0';

    fclose(fp);
}

int parse_tracker_file(const char *path, TrackerFileData *data) {
    if (!path || !data) return 0;
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    memset(data, 0, sizeof(*data));
    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Filename:", 9) == 0) {
            char *v = line + 9; while (*v == ' ' || *v == '\t') v++;
            v[strcspn(v, "\r\n")] = '\0';
            strncpy(data->filename, v, sizeof(data->filename) - 1);
        } else if (strncmp(line, "Filesize:", 9) == 0) {
            data->filesize = atoll(line + 9);
        } else if (strncmp(line, "Description:", 12) == 0) {
            char *v = line + 12; while (*v == ' ' || *v == '\t') v++;
            v[strcspn(v, "\r\n")] = '\0';
            strncpy(data->description, v, sizeof(data->description) - 1);
        } else if (strncmp(line, "MD5:", 4) == 0) {
            char *v = line + 4; while (*v == ' ' || *v == '\t') v++;
            v[strcspn(v, "\r\n")] = '\0';
            strncpy(data->md5, v, sizeof(data->md5) - 1);
        } else if (line[0] == '#') {
            continue;
        } else if (strchr(line, ':') && data->peer_count < MAX_TRACKER_PEERS) {
            TrackerPeer p; memset(&p, 0, sizeof(p));
            if (sscanf(line, "%63[^:]:%d:%lld:%lld:%lld",
                       p.ip, &p.port, &p.start_byte, &p.end_byte, &p.timestamp) == 5) {
                data->peers[data->peer_count++] = p;
            }
        }
    }
    fclose(fp);
    return 1;
}

int write_tracker_file(const char *path, const TrackerFileData *data) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;

    fprintf(fp, "Filename: %s\n",    data->filename);
    fprintf(fp, "Filesize: %lld\n",  data->filesize);
    fprintf(fp, "Description: %s\n", data->description);
    fprintf(fp, "MD5: %s\n",         data->md5);
    fprintf(fp, "#list of peers follows next\n");
    for (int i = 0; i < data->peer_count; i++) {
        fprintf(fp, "%s:%d:%lld:%lld:%lld\n",
                data->peers[i].ip, data->peers[i].port,
                data->peers[i].start_byte, data->peers[i].end_byte,
                data->peers[i].timestamp);
    }
    fclose(fp);
    return 1;
}

void remove_dead_peers(TrackerFileData *data) {
    long long now = (long long)time(NULL);
    int w = 0;
    for (int i = 0; i < data->peer_count; i++) {
        if (now - data->peers[i].timestamp <= DEAD_PEER_TIMEOUT)
            data->peers[w++] = data->peers[i];
        else
            printf("[cleanup] removed dead peer %s:%d\n", data->peers[i].ip, data->peers[i].port);
    }
    data->peer_count = w;
}

int extract_tracker_metadata(TrackerMeta *items, int max_items) {
    DIR *dir = opendir(tracker_dir);
    if (!dir) return 0;

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < max_items) {
        if (entry->d_name[0] == '.') continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", tracker_dir, entry->d_name);
        TrackerFileData data;
        if (!parse_tracker_file(path, &data)) continue;
        if (data.filename[0] == '\0' || data.md5[0] == '\0') continue;
        strncpy(items[count].filename, data.filename, sizeof(items[count].filename) - 1);
        items[count].filesize = data.filesize;
        strncpy(items[count].md5, data.md5, sizeof(items[count].md5) - 1);
        count++;
    }
    closedir(dir);
    return count;
}

void handle_list_req(int sock) {
    TrackerMeta items[128];
    int count = extract_tracker_metadata(items, 128);

    char response[8192];
    int off = 0;
    off += snprintf(response + off, sizeof(response) - off, "<REP LIST %d>\n", count);
    for (int i = 0; i < count; i++) {
        off += snprintf(response + off, sizeof(response) - off,
                        "<%d %s %lld %s>\n", i + 1, items[i].filename, items[i].filesize, items[i].md5);
    }
    off += snprintf(response + off, sizeof(response) - off, "<REP LIST END>\n");

    write_all(sock, response, (size_t)off);
    printf("[LIST] sent %d entries\n", count);
}

char *extract_filename_from_get(const char *msg) {
    static char fname[256];
    const char *s = msg;
    if (*s == '<') s++;
    if (strncasecmp(s, "GET ", 4) != 0) return NULL;
    s += 4;
    while (*s == ' ') s++;
    strncpy(fname, s, sizeof(fname) - 1);
    fname[sizeof(fname) - 1] = '\0';
    size_t len = strlen(fname);
    while (len > 0 && (fname[len-1] == '>' || fname[len-1] == ' ')) fname[--len] = '\0';
    return (len > 0) ? fname : NULL;
}

void handle_get_req(int sock, const char *file_name) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", tracker_dir, file_name);

    TrackerFileData data;
    if (!parse_tracker_file(path, &data)) {
        const char *err = "<GET invalid>\n";
        write_all(sock, err, strlen(err));
        return;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) { const char *e = "<GET invalid>\n"; write_all(sock, e, strlen(e)); return; }

    const char *header = "<REP GET BEGIN>\n";
    write_all(sock, header, strlen(header));

    char buffer[4096]; size_t nread;
    while ((nread = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (write_all(sock, buffer, nread) < 0) { fclose(fp); return; }
    }
    fclose(fp);

    write_all(sock, "\n", 1);
    char footer[128];
    snprintf(footer, sizeof(footer), "<REP GET END %s>\n", data.md5);
    write_all(sock, footer, strlen(footer));
    printf("[GET] sent %s\n", file_name);
}

void handle_createtracker_req(int sock, const char *msg) {
    char copy[2048];
    strncpy(copy, msg, sizeof(copy) - 1); copy[sizeof(copy) - 1] = '\0';

    char *start = copy;
    if (*start == '<') start++;
    size_t slen = strlen(start);
    if (slen > 0 && start[slen - 1] == '>') start[slen - 1] = '\0';

    const char *delim = (strchr(start, '|') != NULL) ? "|" : " ";
    char *tokens[7]; int i = 0;
    char *tok = strtok(start, delim);
    while (tok && i < 7) { tokens[i++] = tok; tok = strtok(NULL, delim); }

    if (i != 7) {
        const char *err = "<createtracker fail>\n";
        write_all(sock, err, strlen(err)); return;
    }

    const char *filename = tokens[1];
    long long filesize   = atoll(tokens[2]);
    const char *desc     = tokens[3];
    const char *md5      = tokens[4];
    const char *ip       = tokens[5];
    int port             = atoi(tokens[6]);
    long long ts         = (long long)time(NULL);

    if (filesize <= 0 || port <= 0) {
        const char *err = "<createtracker fail>\n";
        write_all(sock, err, strlen(err)); return;
    }

    char path[4096];
    if (strstr(filename, ".track")) snprintf(path, sizeof(path), "%s/%s", tracker_dir, filename);
    else snprintf(path, sizeof(path), "%s/%s.track", tracker_dir, filename);

    FILE *fp = fopen(path, "wx");
    if (!fp) {
        const char *err = "<createtracker ferr>\n";
        write_all(sock, err, strlen(err)); return;
    }

    fprintf(fp, "Filename: %s\n", filename);
    fprintf(fp, "Filesize: %lld\n", filesize);
    fprintf(fp, "Description: %s\n", desc);
    fprintf(fp, "MD5: %s\n", md5);
    fprintf(fp, "#list of peers follows next\n");
    fprintf(fp, "%s:%d:0:%lld:%lld\n", ip, port, filesize, ts);
    fclose(fp);

    const char *ok = "<createtracker succ>\n";
    write_all(sock, ok, strlen(ok));
    printf("[createtracker] created %s\n", path);
}

void handle_updatetracker_req(int sock, const char *msg) {
    char copy[2048];
    strncpy(copy, msg, sizeof(copy) - 1); copy[sizeof(copy) - 1] = '\0';

    char *start = copy;
    if (*start == '<') start++;
    size_t slen = strlen(start);
    if (slen > 0 && start[slen - 1] == '>') start[slen - 1] = '\0';

    const char *delim = (strchr(start, '|') != NULL) ? "|" : " ";
    char *tokens[6]; int i = 0;
    char *tok = strtok(start, delim);
    while (tok && i < 6) { tokens[i++] = tok; tok = strtok(NULL, delim); }

    if (i != 6) {
        const char *err = "<updatetracker fail>\n";
        write_all(sock, err, strlen(err)); return;
    }

    const char *filename = tokens[1];
    long long start_byte = atoll(tokens[2]);
    long long end_byte   = atoll(tokens[3]);
    const char *ip       = tokens[4];
    int port             = atoi(tokens[5]);

    if (start_byte < 0 || end_byte < start_byte || port <= 0) {
        char err[256]; snprintf(err, sizeof(err), "<updatetracker %s fail>\n", filename);
        write_all(sock, err, strlen(err)); return;
    }

    char path[4096];
    if (strstr(filename, ".track")) snprintf(path, sizeof(path), "%s/%s", tracker_dir, filename);
    else snprintf(path, sizeof(path), "%s/%s.track", tracker_dir, filename);

    TrackerFileData data;
    if (!parse_tracker_file(path, &data)) {
        char err[256]; snprintf(err, sizeof(err), "<updatetracker %s ferr>\n", filename);
        write_all(sock, err, strlen(err)); return;
    }

    remove_dead_peers(&data);

    int found = 0;
    long long ts = (long long)time(NULL);
    for (int j = 0; j < data.peer_count; j++) {
        if (strcmp(data.peers[j].ip, ip) == 0 && data.peers[j].port == port) {
            data.peers[j].start_byte = start_byte;
            data.peers[j].end_byte   = end_byte;
            data.peers[j].timestamp  = ts;
            found = 1; break;
        }
    }

    if (!found && data.peer_count < MAX_TRACKER_PEERS) {
        TrackerPeer *p = &data.peers[data.peer_count++];
        strncpy(p->ip, ip, sizeof(p->ip) - 1);
        p->port = port; p->start_byte = start_byte;
        p->end_byte = end_byte; p->timestamp = ts;
    }

    if (!write_tracker_file(path, &data)) {
        char err[256]; snprintf(err, sizeof(err), "<updatetracker %s fail>\n", filename);
        write_all(sock, err, strlen(err)); return;
    }

    char ok[256]; snprintf(ok, sizeof(ok), "<updatetracker %s succ>\n", filename);
    write_all(sock, ok, strlen(ok));
    printf("[updatetracker] updated %s\n", path);
}
