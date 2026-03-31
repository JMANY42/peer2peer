/*
 * peer.c - P2P Peer Client Program
 * CS 4390 - Computer Networks
 *
 * Usage:
 *   ./peer list
 *   ./peer get <filename.track>
 *   ./peer createtracker <filename> <filesize> <desc> <md5> <ip> <port>
 *   ./peer updatetracker <filename> <start> <end> <ip> <port>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

char server_address[50];
int server_port;
int update_tracker_interval;

void read_config(void);
int  connect_to_tracker(void);
void handle_list_trackers(int sockid);
void handle_get_tracker(char *argv[], int sockid);
void handle_create_tracker(char *argv[], int sockid);
void handle_update_tracker(char *argv[], int sockid);
int  extract_md5_from_footer(const char *footer_start, char *md5_out, size_t md5_out_size);
int  extract_md5_from_body(const char *body, char *md5_out, size_t md5_out_size);
void print_usage(void);

static int write_all(int fd, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, buf + total, len - total);
        if (n < 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        exit(1);
    }

    read_config();
    printf("[config] server=%s port=%d interval=%d\n",
           server_address, server_port, update_tracker_interval);

    int sockid = connect_to_tracker();

    if (strcmp(argv[1], "list") == 0) {
        handle_list_trackers(sockid);
    }
    else if (strcmp(argv[1], "get") == 0) {
        if (argc < 3) { printf("Usage: ./peer get <filename.track>\n"); close(sockid); exit(1); }
        handle_get_tracker(argv, sockid);
    }
    else if (strcmp(argv[1], "createtracker") == 0) {
        if (argc < 8) { printf("Usage: ./peer createtracker <filename> <filesize> <desc> <md5> <ip> <port>\n"); close(sockid); exit(1); }
        handle_create_tracker(argv, sockid);
    }
    else if (strcmp(argv[1], "updatetracker") == 0) {
        if (argc < 7) { printf("Usage: ./peer updatetracker <filename> <start> <end> <ip> <port>\n"); close(sockid); exit(1); }
        handle_update_tracker(argv, sockid);
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        close(sockid); exit(1);
    }

    return 0;
}

void print_usage(void) {
    printf("Usage:\n");
    printf("  ./peer list\n");
    printf("  ./peer get <filename.track>\n");
    printf("  ./peer createtracker <filename> <filesize> <desc> <md5> <ip> <port>\n");
    printf("  ./peer updatetracker <filename> <start> <end> <ip> <port>\n");
}

int connect_to_tracker(void) {
    int sockid = socket(AF_INET, SOCK_STREAM, 0);
    if (sockid == -1) { perror("socket"); exit(1); }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(server_port);

    if (inet_pton(AF_INET, server_address, &server_addr.sin_addr) != 1) {
        printf("Invalid server address: %s\n", server_address); exit(1);
    }

    if (connect(sockid, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Cannot connect to server"); exit(1);
    }

    printf("Connected to tracker at %s:%d\n", server_address, server_port);
    return sockid;
}

void read_config(void) {
    FILE *fp = fopen("clientThreadConfig.cfg", "r");
    if (!fp) { printf("Cannot open clientThreadConfig.cfg\n"); exit(1); }

    char buf[64];

    if (!fgets(buf, sizeof(buf), fp)) { printf("Error reading port\n"); fclose(fp); exit(1); }
    server_port = atoi(buf);

    if (!fgets(server_address, sizeof(server_address), fp)) { printf("Error reading address\n"); fclose(fp); exit(1); }
    server_address[strcspn(server_address, "\r\n")] = '\0';

    if (!fgets(buf, sizeof(buf), fp)) { printf("Error reading interval\n"); fclose(fp); exit(1); }
    update_tracker_interval = atoi(buf);

    fclose(fp);
}

void handle_list_trackers(int sockid) {
    const char *msg = "REQ LIST";
    if (write_all(sockid, msg, strlen(msg)) < 0) { printf("Send failure\n"); exit(1); }

    char buf[4096];
    ssize_t n = read(sockid, buf, sizeof(buf) - 1);
    if (n < 0) { printf("Read failure\n"); exit(1); }

    buf[n] = '\0';
    printf("Tracker file list:\n%s", buf);

    close(sockid);
    printf("Connection closed\n");
}

void handle_create_tracker(char *argv[], int sockid) {
    char req[2048];
    snprintf(req, sizeof(req), "createtracker %s %s %s %s %s %s",
             argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);

    if (write_all(sockid, req, strlen(req)) < 0) { printf("Send failure\n"); close(sockid); exit(1); }

    char resp[1024];
    ssize_t n = read(sockid, resp, sizeof(resp) - 1);
    if (n < 0) { printf("Read failure\n"); close(sockid); exit(1); }

    resp[n] = '\0';
    printf("Server response: %s", resp);
    close(sockid);
    printf("Connection closed\n");
}

void handle_update_tracker(char *argv[], int sockid) {
    char req[2048];
    snprintf(req, sizeof(req), "updatetracker %s %s %s %s %s",
             argv[2], argv[3], argv[4], argv[5], argv[6]);

    if (write_all(sockid, req, strlen(req)) < 0) { printf("Send failure\n"); close(sockid); exit(1); }

    char resp[1024];
    ssize_t n = read(sockid, resp, sizeof(resp) - 1);
    if (n < 0) { printf("Read failure\n"); close(sockid); exit(1); }

    resp[n] = '\0';
    printf("Server response: %s", resp);
    close(sockid);
    printf("Connection closed\n");
}

void handle_get_tracker(char *argv[], int sockid) {
    char req[256];
    snprintf(req, sizeof(req), "GET %s", argv[2]);
    if (write_all(sockid, req, strlen(req)) < 0) { printf("Send failure\n"); exit(1); }

    char chunk[1024];
    char *response = malloc(4096);
    size_t resp_len = 0, resp_cap = 4096;
    ssize_t bytes_read;

    if (!response) { printf("Memory failure\n"); exit(1); }

    while ((bytes_read = read(sockid, chunk, sizeof(chunk))) > 0) {
        if (resp_len + (size_t)bytes_read + 1 > resp_cap) {
            resp_cap *= 2;
            char *tmp = realloc(response, resp_cap);
            if (!tmp) { free(response); printf("Memory failure\n"); exit(1); }
            response = tmp;
        }
        memcpy(response + resp_len, chunk, (size_t)bytes_read);
        resp_len += (size_t)bytes_read;
    }
    if (bytes_read < 0) { free(response); printf("Read failure\n"); exit(1); }
    response[resp_len] = '\0';

    char *begin_tag = "<REP GET BEGIN>\n";
    char *body_start = strstr(response, begin_tag);
    if (!body_start) {
        printf("Invalid response: missing begin tag\n");
        free(response); close(sockid); exit(1);
    }
    body_start += strlen(begin_tag);

    char *footer_start = strstr(body_start, "<REP GET END ");
    if (!footer_start) {
        printf("Invalid response: missing end tag\n");
        free(response); close(sockid); exit(1);
    }

    size_t body_len = (size_t)(footer_start - body_start);
    while (body_len > 0 && (body_start[body_len-1] == '\n' || body_start[body_len-1] == '\r'))
        body_len--;

    char *body = malloc(body_len + 1);
    if (!body) { free(response); printf("Memory failure\n"); exit(1); }
    memcpy(body, body_start, body_len);
    body[body_len] = '\0';

    char footer_md5[64], body_md5[64];

    if (!extract_md5_from_footer(footer_start, footer_md5, sizeof(footer_md5))) {
        printf("Cannot parse footer MD5\n");
        free(body); free(response); close(sockid); exit(1);
    }
    if (!extract_md5_from_body(body, body_md5, sizeof(body_md5))) {
        printf("Cannot find MD5 in tracker file\n");
        free(body); free(response); close(sockid); exit(1);
    }

    if (strcmp(footer_md5, body_md5) == 0) {
        mkdir("trackers", 0755);
        char save_path[512];
        snprintf(save_path, sizeof(save_path), "trackers/%s", argv[2]);

        FILE *out = fopen(save_path, "w");
        if (!out) { printf("Failed to save\n"); free(body); free(response); close(sockid); exit(1); }
        fwrite(body, 1, body_len, out);
        fclose(out);
        printf("Tracker file saved to %s\n", save_path);
        printf("MD5 verified: %s\n", footer_md5);
    } else {
        printf("MD5 MISMATCH! footer=%s body=%s — file NOT saved\n", footer_md5, body_md5);
    }

    free(body); free(response);
    close(sockid);
    printf("Connection closed\n");
}

int extract_md5_from_footer(const char *footer, char *md5_out, size_t md5_out_size) {
    const char *prefix = "<REP GET END ";
    if (!footer || !md5_out || md5_out_size == 0) return 0;
    if (strncmp(footer, prefix, strlen(prefix)) != 0) return 0;

    const char *val = footer + strlen(prefix);
    const char *end = strchr(val, '>');
    if (!end) return 0;

    size_t len = (size_t)(end - val);
    if (len == 0 || len >= md5_out_size) return 0;

    memcpy(md5_out, val, len);
    md5_out[len] = '\0';
    return 1;
}

int extract_md5_from_body(const char *body, char *md5_out, size_t md5_out_size) {
    if (!body || !md5_out || md5_out_size == 0) return 0;

    const char *line = strstr(body, "MD5:");
    if (!line) return 0;

    const char *val = line + 4;
    while (*val == ' ' || *val == '\t') val++;

    const char *end = val;
    while (*end && *end != '\n' && *end != '\r') end++;

    size_t len = (size_t)(end - val);
    if (len == 0 || len >= md5_out_size) return 0;

    memcpy(md5_out, val, len);
    md5_out[len] = '\0';
    return 1;
}



