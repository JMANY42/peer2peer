#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

char server_address[50];
int server_port;
int update_tracker_interval;

void read_config(void);

int extract_md5_from_footer(const char *footer_start, char *md5_out, size_t md5_out_size);
void handle_get_tracker(char *argv[], int sockid);
void handle_list_trackers(int sockid);
int extract_md5_from_body(const char *body, char *md5_out, size_t md5_out_size);
void handle_create_tracker(char *argv[], int sockid);

int main(int argc,char *argv[]){

    read_config(); // read server address and port from configuration file

    printf("%s %d %d\n", server_address, server_port, update_tracker_interval); // print the read values for verification
	
    struct sockaddr_in server_addr;
	int sockid;

	if ((sockid = socket(AF_INET,SOCK_STREAM,0))==-1){ //create socket
		printf("socket cannot be created\n"); exit(1);
	}

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;//host byte order
    server_addr.sin_port = htons(server_port);// convert to network byte order

    if (inet_pton(AF_INET, server_address, &server_addr.sin_addr) != 1) {
		printf("Invalid server address\n"); exit(1);
	}

    if (connect(sockid ,(struct sockaddr *) &server_addr,sizeof(server_addr))==-1){//connect and error check
		printf("Cannot connect to server\n"); exit(1);
	}
   /* If connected successfully*/

    printf("%s", argv[1]);
	if(!strcmp(argv[1],"list")){ // if this is LIST command
        handle_list_trackers(sockid);

    }//end close
    else if(!strcmp(argv[1],"get")){ // if this is GET command
        handle_get_tracker(argv, sockid);
    }
    else if(!strcmp(argv[1], "createtracker")) {
        handle_create_tracker(argv, sockid);
    }
    else if(!strcmp(argv[1], "updatetracker")) {
        // handle_update_tracker(argv, sockid);
    }
}

void handle_create_tracker(char *argv[], int sockid) {
    if (argv[2] == NULL || argv[3] == NULL || argv[4] == NULL || argv[5] == NULL || argv[6] == NULL || argv[7] == NULL) {
        printf("Usage: createtracker <filename> <filesize> <description> <md5> <ip> <port>\n");
        close(sockid);
        exit(1);
    }

    char req_message[2048];
    snprintf(req_message,
             sizeof(req_message),
             "CREATETRACKER|%s|%s|%s|%s|%s|%s\n",
             argv[2],
             argv[3],
             argv[4],
             argv[5],
             argv[6],
             argv[7]);

    if ((write(sockid, req_message, strlen(req_message))) < 0) {
        printf("Send_request failure\n");
        close(sockid);
        exit(1);
    }

    char msg[1024];
    int bytes_read = read(sockid, msg, sizeof(msg) - 1);
    if (bytes_read < 0) {
        printf("Read failure\n");
        close(sockid);
        exit(1);
    }

    msg[bytes_read] = '\0';
    printf("Message from server:\n%s", msg);

    close(sockid);
    printf("Connection closed\n");
    exit(0);
} 

void handle_list_trackers(int sockid)
{
    char *list_req_msg = "<REQ LIST>\n";
    if ((write(sockid, list_req_msg, strlen(list_req_msg))) < 0)
    { // inform the server of the list request
        printf("Send_request failure\n");
        exit(1);
    }

    char msg[1024];
    int bytes_read = read(sockid, msg, sizeof(msg) - 1);
    if (bytes_read < 0)
    { // read what server has said

        printf("Read failure\n");
        exit(1);
    }
    msg[bytes_read] = '\0';
    printf("Message from server:\n%s", msg); // print the message from the server
    close(sockid);

    printf("Connection closed\n");

    exit(0);
}

void handle_get_tracker(char *argv[], int sockid)
{
    char req_message[64];
    snprintf(req_message, sizeof(req_message), "<GET %s>\n", argv[2]);
    if ((write(sockid, req_message, strlen(req_message))) < 0)
    { // inform the server of the list request
        printf("Send_request failure\n");
        exit(1);
    }

    char chunk[1024];
    char *response = malloc(4096);
    size_t response_len = 0;
    size_t response_cap = 4096;
    int bytes_read;

    if (response == NULL)
    {
        printf("Memory allocation failure\n");
        exit(1);
    }

    while ((bytes_read = read(sockid, chunk, sizeof(chunk))) > 0)
    {
        if (response_len + (size_t)bytes_read + 1 > response_cap)
        {
            size_t new_cap = response_cap * 2;
            while (new_cap < response_len + (size_t)bytes_read + 1)
            {
                new_cap *= 2;
            }
            char *new_buf = realloc(response, new_cap);
            if (new_buf == NULL)
            {
                free(response);
                printf("Memory allocation failure\n");
                exit(1);
            }
            response = new_buf;
            response_cap = new_cap;
        }

        memcpy(response + response_len, chunk, (size_t)bytes_read);
        response_len += (size_t)bytes_read;
    }

    if (bytes_read < 0)
    {
        free(response);
        printf("Read failure\n");
        exit(1);
    }

    response[response_len] = '\0';

    char *begin_tag = "<REP GET BEGIN>\n";
    char *body_start = strstr(response, begin_tag);
    char *footer_start;
    char footer_md5[64];
    char body_md5[64];

    if (body_start == NULL)
    {
        printf("Invalid GET response: missing begin tag\n");
        free(response);
        close(sockid);
        exit(1);
    }

    body_start += strlen(begin_tag);
    footer_start = strstr(body_start, "<REP GET END ");
    if (footer_start == NULL)
    {
        printf("Invalid GET response: missing end tag\n");
        free(response);
        close(sockid);
        exit(1);
    }

    size_t body_len = (size_t)(footer_start - body_start);
    while (body_len > 0 && (body_start[body_len - 1] == '\n' || body_start[body_len - 1] == '\r'))
    {
        body_len--;
    }

    char *body_copy = malloc(body_len + 1);
    if (body_copy == NULL)
    {
        free(response);
        printf("Memory allocation failure\n");
        close(sockid);
        exit(1);
    }
    memcpy(body_copy, body_start, body_len);
    body_copy[body_len] = '\0';

    if (!extract_md5_from_footer(footer_start, footer_md5, sizeof(footer_md5)))
    {
        printf("Invalid GET response: cannot parse footer md5\n");
        free(body_copy);
        free(response);
        close(sockid);
        exit(1);
    }

    if (!extract_md5_from_body(body_copy, body_md5, sizeof(body_md5)))
    {
        printf("Invalid tracker file: missing MD5 line\n");
        free(body_copy);
        free(response);
        close(sockid);
        exit(1);
    }

    if (strcmp(footer_md5, body_md5) == 0)
    {
        char save_path[512];
        snprintf(save_path, sizeof(save_path), "./trackers/%s", argv[2]);

        FILE *out = fopen(save_path, "w");
        if (out == NULL)
        {
            printf("Failed to save tracker file\n");
            free(body_copy);
            free(response);
            close(sockid);
            exit(1);
        }

        fwrite(body_copy, 1, body_len, out);
        fclose(out);
        printf("Saved tracker file to %s\n", save_path);
    }
    else
    {
        printf("MD5 mismatch: header=%s file=%s. File not saved.\n", footer_md5, body_md5);
    }

    free(body_copy);
    free(response);
    close(sockid);

    printf("Connection closed\n");

    exit(0);
}

int extract_md5_from_footer(const char *footer_start, char *md5_out, size_t md5_out_size) {
    const char *prefix = "<REP GET END ";
    const char *value_start;
    const char *value_end;
    size_t len;

    if (footer_start == NULL || md5_out == NULL || md5_out_size == 0) {
        return 0;
    }

    if (strncmp(footer_start, prefix, strlen(prefix)) != 0) {
        return 0;
    }

    value_start = footer_start + strlen(prefix);
    value_end = strchr(value_start, '>');
    if (value_end == NULL) {
        return 0;
    }

    len = (size_t)(value_end - value_start);
    if (len == 0 || len >= md5_out_size) {
        return 0;
    }

    memcpy(md5_out, value_start, len);
    md5_out[len] = '\0';
    return 1;
}

int extract_md5_from_body(const char *body, char *md5_out, size_t md5_out_size) {
    const char *md5_line;
    const char *value_start;
    const char *value_end;
    size_t len;

    if (body == NULL || md5_out == NULL || md5_out_size == 0) {
        return 0;
    }

    md5_line = strstr(body, "MD5:");
    if (md5_line == NULL) {
        return 0;
    }

    value_start = md5_line + 4;
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }

    value_end = value_start;
    while (*value_end != '\0' && *value_end != '\n' && *value_end != '\r') {
        value_end++;
    }

    len = (size_t)(value_end - value_start);
    if (len == 0 || len >= md5_out_size) {
        return 0;
    }

    memcpy(md5_out, value_start, len);
    md5_out[len] = '\0';
    return 1;
}

void read_config(void) {
    FILE *config_file = fopen("clientThreadConfig.cfg", "r");
    
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
    
    // Read server address from second line
    if (fgets(server_address, 50, config_file) == NULL) {
        printf("Error reading server address\n");
        fclose(config_file);
        exit(1);
    }
    
    // Remove trailing newline from server_address
    int len = strlen(server_address);
    if (len > 0 && server_address[len - 1] == '\n') {
        server_address[len - 1] = '\0';
    }


    // Read update tracker interval from third line
    char interval_str[20];
    if (fgets(interval_str, sizeof(interval_str), config_file) == NULL) {
        printf("Error reading update tracker interval\n");
        fclose(config_file);
        exit(1);
    }
    update_tracker_interval = atoi(interval_str);
    
    fclose(config_file);
}