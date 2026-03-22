#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

#define MAXLINE 1024
#define MAX_TRACKER_PEERS 256

static char read_msg[MAXLINE];
static char fname[256];
int server_port;
char tracker_dir[256];

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

void peer_handler(int sock_child);
void handle_list_req(int sock_child);
char* xtrct_fname(const char *msg, const char *delimiter);
void handle_get_req(int sock_child, const char *file_name);
void tokenize_createmsg(const char *msg);
void handle_createtracker_req(int sock_child, const char *request_msg);
void tokenize_updatemsg(const char *msg);
void handle_updatetracker_req(int sock_child);

void read_config(void);
char* get_tracker_list(int *list_len);
int extract_tracker_metadata(TrackerMeta *items, int max_items);
int parse_tracker_file(const char *path, TrackerFileData *data);

int main(void) {
    pid_t pid;
    struct sockaddr_in server_addr, client_addr;
    int sockid;

    read_config(); // read server port from configuration file
    printf("Server port: %d\nTracker dir name: %s\n", server_port, tracker_dir); // print the read server port for verification
    int sock_child;
    socklen_t clilen = sizeof(client_addr);

    if ((sockid = socket(AF_INET, SOCK_STREAM, 0)) < 0) { //create socket connection oriented
        printf("socket cannot be created \n"); exit(0);
    }

    //socket created at this stage

    //now associate the socket with local port to allow listening incoming connections

    server_addr.sin_family = AF_INET; // assign address family

    server_addr.sin_port = htons(server_port); //change server port to NETWORK BYTE ORDER

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockid, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) { //bind and check error
        printf("bind failure\n"); exit(0);
    }

    printf("Tracker SERVER READY TO LISTEN INCOMING REQUEST.... \n");

    if (listen(sockid, 5) < 0) { //(parent) process listens at sockid and check error
        printf(" Tracker  SERVER CANNOT LISTEN\n"); exit(0);
    }

    while (1) { //accept  connection from every requester client
        if ((sock_child = accept(sockid, (struct sockaddr *) &client_addr, &clilen)) == -1) { /* accept connection and create a socket descriptor for actual work */
            printf("Tracker Cannot accept...\n"); exit(0);
        }

        if ((pid = fork()) == 0) { //New child process will serve the requester client. separate child will serve separate client
            close(sockid);   //child does not need listener

            peer_handler(sock_child); //child is serving the client.

            close(sock_child); // printf("n 1. closed");

            exit(0);         // kill the process. child process all done with work
        }

        close(sock_child);  // parent all done with client, only child will communicate with that client from now
    }  //accept loop ends
    return 0;
} // main fun ends


void peer_handler(int sock_child) { // function for file transfer. child process will call this function

    printf("Peer handler called.\n");
    //start handiling client request

    ssize_t length;

    length = read(sock_child, read_msg, MAXLINE);
    if (length <= 0) {
        return;
    }

    read_msg[length] = '\0';

    if ((!strcmp(read_msg, "REQ LIST")) || (!strcmp(read_msg, "req list")) || (!strcmp(read_msg, "<REQ LIST>")) || (!strcmp(read_msg, "<REQ LIST>\n"))) { //list command received
        handle_list_req(sock_child); // handle list request

        printf("list request handled.\n");
    }

    else if ((strstr(read_msg, "get") != NULL) || (strstr(read_msg, "GET") != NULL)) { // get command received
        char* fname = xtrct_fname(read_msg, " "); // extract filename from the command

        handle_get_req(sock_child, fname);
    }

    else if ((strstr(read_msg, "createtracker") != NULL) || (strstr(read_msg, "Createtracker") != NULL) || (strstr(read_msg, "CREATETRACKER") != NULL)) { // create command received
        handle_createtracker_req(sock_child, read_msg);
    }

    else if ((strstr(read_msg, "updatetracker") != NULL) || (strstr(read_msg, "Updatetracker") != NULL) || (strstr(read_msg, "UPDATETRACKER") != NULL)) { // get command received
        // tokenize_updatemsg(read_msg);

        // handle_updatetracker_req(sock_child);
    }

} //end client handler function

void handle_list_req(int sock_child) {

    int tracker_list_len = 0;
    char* tracker_list = get_tracker_list(&tracker_list_len);
    char response_header[64];
    sprintf(response_header, "<REP LIST %d>\n", tracker_list_len);
    char* response_footer = "<REP LIST END>\n";
    printf("Response header: %s", response_header);

    char* full_response = malloc(strlen(response_header) + strlen(tracker_list) + strlen(response_footer) + 1);
    full_response[0] = '\0';

    strcat(full_response, response_header);
    strcat(full_response, tracker_list);
    strcat(full_response, response_footer);

    if((write(sock_child,full_response,strlen(full_response))) < 0){ //inform the server of the list request
        printf("Send_request failure\n"); exit(1);
    }
}

void read_config(void) {
    FILE *config_file = fopen("serverThreadConfig.cfg", "r");
    
    if (config_file == NULL) {
        printf("Cannot open configuration file\n");
        exit(1);
    }
    

    // Read server port from first line
    char port_str[20];
    if (fgets(port_str, sizeof(port_str), config_file) == NULL) {
        printf("Error reading server port\n");
        fclose(config_file);
        exit(1);
    }
    server_port = atoi(port_str);

    if (fgets(tracker_dir, sizeof(tracker_dir), config_file) == NULL) {
        printf("Error reading tracker directory name\n");
        fclose(config_file);
        exit(1);
    }

    tracker_dir[strcspn(tracker_dir, "\r\n")] = '\0';
    
    fclose(config_file);
}

int extract_tracker_metadata(TrackerMeta *items, int max_items) {
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    if (tracker_dir == NULL || items == NULL || max_items <= 0) {
        return 0;
    }

    dir = opendir(tracker_dir);
    if (dir == NULL) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL && count < max_items) {
        char path[4096];
        TrackerFileData data;

        if (entry->d_name[0] == '.') {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", tracker_dir, entry->d_name);
        if (!parse_tracker_file(path, &data)) {
            continue;
        }

        // We parse all fields (including peers) and only expose these 3 in list output.
        if (data.filename[0] != '\0' && data.md5[0] != '\0') {
            strncpy(items[count].filename, data.filename, sizeof(items[count].filename) - 1);
            items[count].filename[sizeof(items[count].filename) - 1] = '\0';
            items[count].filesize = data.filesize;
            strncpy(items[count].md5, data.md5, sizeof(items[count].md5) - 1);
            items[count].md5[sizeof(items[count].md5) - 1] = '\0';
            count++;
        }
        printf("Extracted metadata for file: %s, filesize: %lld, md5: %s\n", data.filename, data.filesize, data.md5);
    }

    closedir(dir);
    printf("Total tracker files processed: %d\n", count);
    return count;
}

int parse_tracker_file(const char *path, TrackerFileData *data) {
    FILE *fp;
    char line[1024];

    if (path == NULL || data == NULL) {
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    printf("Parsing tracker file: %s\n", path);
    memset(data, 0, sizeof(*data));

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "Filename:", 9) == 0) {
            char *value = line + 9;
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            value[strcspn(value, "\r\n")] = '\0';
            strncpy(data->filename, value, sizeof(data->filename) - 1);
            data->filename[sizeof(data->filename) - 1] = '\0';
        } else if (strncmp(line, "Filesize:", 9) == 0) {
            char *value = line + 9;
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            data->filesize = atoll(value);
        } else if (strncmp(line, "Description:", 12) == 0) {
            char *value = line + 12;
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            value[strcspn(value, "\r\n")] = '\0';
            strncpy(data->description, value, sizeof(data->description) - 1);
            data->description[sizeof(data->description) - 1] = '\0';
        } else if (strncmp(line, "MD5:", 4) == 0) {
            char *value = line + 4;
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            value[strcspn(value, "\r\n")] = '\0';
            strncpy(data->md5, value, sizeof(data->md5) - 1);
            data->md5[sizeof(data->md5) - 1] = '\0';
        } else if (line[0] != '#' && strchr(line, ':') != NULL) {
            TrackerPeer peer;
            int matched;

            memset(&peer, 0, sizeof(peer));
            matched = sscanf(line, "%63[^:]:%d:%lld:%lld:%lld",
                             peer.ip,
                             &peer.port,
                             &peer.start_byte,
                             &peer.end_byte,
                             &peer.timestamp);
            if (matched == 5 && data->peer_count < MAX_TRACKER_PEERS) {
                data->peers[data->peer_count++] = peer;
            }
        }
    }
    printf("Parsed file: %s, filename: %s, filesize: %lld, md5: %s, peers: %d\n", path, data->filename, data->filesize, data->md5, data->peer_count);

    fclose(fp);
    return 1;
}

char* get_tracker_list(int *list_len) {
    static char response[8192];
    TrackerMeta items[128];
    *list_len = extract_tracker_metadata(items, 128);
    printf("list_len: %d\n", *list_len);
    response[0] = '\0';

    for (int i = 0; i < *list_len; i++) {
        char line[512];
        snprintf(line, sizeof(line), "<%s %lld %s>\n", items[i].filename, items[i].filesize, items[i].md5);
        if (strlen(response) + strlen(line) < sizeof(response)) {
            strcat(response, line);
        }
    }

    return response;
}

char* xtrct_fname(const char *msg, const char *delimiter) {
    char temp[256];
    strncpy(temp, msg, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *token = strtok(temp, delimiter);
    if (token != NULL) {
        token = strtok(NULL, delimiter);
        if (token != NULL) {
            strncpy(fname, token, sizeof(fname) - 1);
            fname[sizeof(fname) - 1] = '\0';
            fname[strcspn(fname, ">")] = '\0';
            printf("Extracted filename: %s\n", fname);
        }
    }
    return fname;
}

// TODO: transfer file with future file transfer algorithm used for actual p2p connections
void handle_get_req(int sock_child, const char *file_name) {
    char path[4096];
    FILE *fp;
    char buffer[4096];
    size_t nread;
    snprintf(path, sizeof(path), "%s/%s", tracker_dir, file_name);

    TrackerFileData data;
    parse_tracker_file(path, &data);

    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("Failed to open tracker file for get request: %s\n", path); exit(1);
    }

    char* response_header = "<REP GET BEGIN>\n";
    char response_footer[128];
    sprintf(response_footer, "<REP GET END %s>\n", data.md5);

    if (write(sock_child, response_header, strlen(response_header)) < 0) {
        printf("Send_request failure\n");
        fclose(fp);
        exit(1);
    }

    while ((nread = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        size_t total_written = 0;
        while (total_written < nread) {
            ssize_t nwritten = write(sock_child, buffer + total_written, nread - total_written);
            if (nwritten < 0) {
                printf("Send_request failure\n");
                fclose(fp);
                exit(1);
            }
            total_written += (size_t)nwritten;
        }
    }

    if (write(sock_child, "\n", 1) < 0) {
        printf("Send_request failure\n");
        fclose(fp);
        exit(1);
    }

    if (write(sock_child, response_footer, strlen(response_footer)) < 0) {
        printf("Send_request failure\n"); exit(1);
    }

    fclose(fp);
}

void handle_createtracker_req(int sock_child, const char *request_msg) {
    char req_copy[2048];
    char *tokens[7];
    int i = 0;

    if (request_msg == NULL) {
        const char *err = "<createtracker fail>\n";
        write(sock_child, err, strlen(err));
        return;
    }

    strncpy(req_copy, request_msg, sizeof(req_copy) - 1);
    req_copy[sizeof(req_copy) - 1] = '\0';
    req_copy[strcspn(req_copy, "\r\n")] = '\0';

    char *token = strtok(req_copy, "|");
    while (token != NULL && i < 7) {
        tokens[i++] = token;
        token = strtok(NULL, "|");
    }

    if (i != 7 || strcmp(tokens[0], "CREATETRACKER") != 0) {
        const char *err = "<createtracker fail>\n";
        write(sock_child, err, strlen(err));
        return;
    }

    const char *filename = tokens[1];
    const char *filesize_str = tokens[2];
    const char *description = tokens[3];
    const char *md5 = tokens[4];
    const char *ip = tokens[5];
    const char *port_str = tokens[6];

    long long filesize = atoll(filesize_str);
    int port = atoi(port_str);
    long long timestamp = (long long)time(NULL);

    if (filesize <= 0 || port <= 0) {
        const char *err = "<createtracker fail>\n";
        write(sock_child, err, strlen(err));
        return;
    }

    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", tracker_dir, filename);

    FILE *fp = fopen(path, "wx");
    if (fp == NULL) {
        const char *err = "<createtracker ferr>\n";
        write(sock_child, err, strlen(err));
        return;
    }

    fprintf(fp, "Filename: %s\n", filename);
    fprintf(fp, "Filesize: %lld\n", filesize);
    fprintf(fp, "Description: %s\n", description);
    fprintf(fp, "MD5: %s\n", md5);
    fprintf(fp, "%s:%d:0:%lld:%lld\n", ip, port, filesize, timestamp);
    fclose(fp);

    const char *ok = "<createtracker succ>\n";
    write(sock_child, ok, strlen(ok));
}
