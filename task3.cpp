#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_HOSTS 256
#define MAX_PORTS 1024

typedef struct {
  char hostname[NI_MAXHOST];
  char ip[INET_ADDRSTRLEN];
} Host;

typedef struct {
  Host hosts[MAX_HOSTS];
  int count;
} HostList;

HostList hostList;

void get_local_ip(char *buffer) {
  struct ifaddrs *ifap, *ifa;
  struct sockaddr_in *sa;
  getifaddrs(&ifap);
  for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr->sa_family == AF_INET) {
      sa = (struct sockaddr_in *)ifa->ifa_addr;
      if (strncmp(ifa->ifa_name, "lo", 2)) {
        strcpy(buffer, inet_ntoa(sa->sin_addr));
        break;
      }
    }
  }
  freeifaddrs(ifap);
}

void scan_host(char *ip) {
  struct sockaddr_in addr;
  int sockfd;

  // Get hostname
  struct hostent *host = gethostbyaddr(ip, strlen(ip), AF_INET);
  if (host) {
    Host newHost;
    strcpy(newHost.ip, ip);
    strcpy(newHost.hostname, host->h_name);
    hostList.hosts[hostList.count++] = newHost;
  }

  // Scan ports
  for (int port = 1; port <= 1024; port++) {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
      continue;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
      printf("Host: %s (IP: %s) - Port %d is open\n",
             host ? host->h_name : "Unknown", ip, port);
    }
    close(sockfd);
  }
}

void scan_network() {
  char base_ip[INET_ADDRSTRLEN];
  get_local_ip(base_ip);
  printf("Local IP: %s\n", base_ip);

  // Modify base_ip to get the network prefix
  char *last_dot = strrchr(base_ip, '.');
  if (last_dot)
    *last_dot = '\0';

  for (int i = 1; i <= 10; i++) { // Reduced number of hosts for testing
    char target_ip[INET_ADDRSTRLEN];
    snprintf(target_ip, sizeof(target_ip), "%s.%d", base_ip, i);
    printf("Scanning IP: %s\n", target_ip);
    scan_host(target_ip);
  }
}

int main() {
  hostList.count = 0;
  scan_network();

  printf("\nDiscovered Hosts:\n");
  for (int i = 0; i < hostList.count; i++) {
    printf("Hostname: %s, IP: %s\n", hostList.hosts[i].hostname,
           hostList.hosts[i].ip);
  }

  return 0;
}
