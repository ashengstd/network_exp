#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#define MAX_PORTS 100 // Reduced for testing

typedef struct {
  char hostname[NI_MAXHOST];
  char ip[INET_ADDRSTRLEN];
} Host;

typedef struct {
  Host hosts[MAX_HOSTS];
  int count;
} HostList;

HostList hostList;

void get_local_ip_and_subnet_mask(char *ip_buffer, char *mask_buffer) {
  struct ifaddrs *ifap, *ifa;
  struct sockaddr_in *sa, *mask;
  getifaddrs(&ifap);
  for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr->sa_family == AF_INET) {
      sa = (struct sockaddr_in *)ifa->ifa_addr;
      mask = (struct sockaddr_in *)ifa->ifa_netmask;
      if (strncmp(ifa->ifa_name, "lo", 2)) {
        strcpy(ip_buffer, inet_ntoa(sa->sin_addr));
        strcpy(mask_buffer, inet_ntoa(mask->sin_addr));
        break;
      }
    }
  }
  freeifaddrs(ifap);
}

uint32_t ip_to_int(const char *ip) {
  struct sockaddr_in sa;
  inet_pton(AF_INET, ip, &(sa.sin_addr));
  return ntohl(sa.sin_addr.s_addr);
}

void int_to_ip(uint32_t ip, char *buffer) {
  struct in_addr in;
  in.s_addr = htonl(ip);
  inet_ntop(AF_INET, &in, buffer, INET_ADDRSTRLEN);
}

void scan_host(char *ip) {
  struct sockaddr_in addr;
  int sockfd;
  struct timeval timeout;
  fd_set fdset;

  // Get hostname
  struct hostent *host = gethostbyaddr(ip, strlen(ip), AF_INET);
  if (host) {
    Host newHost;
    strcpy(newHost.ip, ip);
    strcpy(newHost.hostname, host->h_name);
    hostList.hosts[hostList.count++] = newHost;
  }

  // Scan ports
  for (int port = 1; port <= MAX_PORTS; port++) {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
      continue;

    // Set socket to non-blocking
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int result = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (result < 0 && errno != EINPROGRESS) {
      close(sockfd);
      continue;
    }

    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);
    timeout.tv_sec = 0; // 1 second timeout
    timeout.tv_usec = 30000;

    if (select(sockfd + 1, NULL, &fdset, NULL, &timeout) == 1) {
      int so_error;
      socklen_t len = sizeof so_error;

      getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);

      if (so_error == 0) {
        printf("Host: %s (IP: %s) - Port %d is open\n",
               host ? host->h_name : "Unknown", ip, port);
      }
    }

    close(sockfd);
  }
}

void scan_network() {
  char local_ip[INET_ADDRSTRLEN];
  char subnet_mask[INET_ADDRSTRLEN];
  get_local_ip_and_subnet_mask(local_ip, subnet_mask);

  printf("Local IP: %s\n", local_ip);
  printf("Subnet Mask: %s\n", subnet_mask);

  uint32_t local_ip_int = ip_to_int(local_ip);
  uint32_t subnet_mask_int = ip_to_int(subnet_mask);
  uint32_t network_addr_int = local_ip_int & subnet_mask_int;
  uint32_t broadcast_addr_int = network_addr_int | ~subnet_mask_int;

  char network_addr[INET_ADDRSTRLEN];
  char broadcast_addr[INET_ADDRSTRLEN];
  int_to_ip(network_addr_int, network_addr);
  int_to_ip(broadcast_addr_int, broadcast_addr);

  printf("Network Address: %s\n", network_addr);
  printf("Broadcast Address: %s\n", broadcast_addr);

  for (uint32_t ip_int = network_addr_int + 1; ip_int < broadcast_addr_int;
       ip_int++) {
    char target_ip[INET_ADDRSTRLEN];
    int_to_ip(ip_int, target_ip);
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
