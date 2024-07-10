#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PACKET_SIZE 64
#define MAX_HOPS 30
#define TIMEOUT 5
#define TRIES 3

// 计算校验和
unsigned short checksum(void *b, int len) {
  unsigned short *buf = (unsigned short *)b;
  unsigned int sum = 0;
  unsigned short result;

  for (sum = 0; len > 1; len -= 2)
    sum += *buf++;
  if (len == 1)
    sum += *(unsigned char *)buf;
  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  result = ~sum;
  return result;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <hostname>" << std::endl;
    return 1;
  }

  int sockfd;
  struct sockaddr_in dest;
  struct hostent *host;

  host = gethostbyname(argv[1]);
  if (host == nullptr) {
    std::cerr << "Error: Unknown host " << argv[1] << std::endl;
    return 1;
  }

  dest.sin_family = AF_INET;
  dest.sin_port = 0;
  dest.sin_addr.s_addr = *(long *)(host->h_addr);

  sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sockfd < 0) {
    perror("socket");
    return 1;
  }

  char sendbuf[PACKET_SIZE];
  char recvbuf[PACKET_SIZE];
  struct icmp *icmp = (struct icmp *)sendbuf;
  memset(sendbuf, 0, PACKET_SIZE);

  icmp->icmp_type = ICMP_ECHO;
  icmp->icmp_code = 0;
  icmp->icmp_id = getpid();
  icmp->icmp_seq = 0;

  for (int ttl = 1; ttl <= MAX_HOPS; ++ttl) {
    if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
      perror("setsockopt");
      return 1;
    }

    struct sockaddr_in recv_addr;
    socklen_t addrlen = sizeof(recv_addr);
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
      perror("setsockopt");
      return 1;
    }

    std::cout << std::setw(3) << ttl;

    bool received_response = false;
    char ip_address[INET_ADDRSTRLEN] = "*";
    double rtt_times[TRIES] = {0.0, 0.0, 0.0};
    int recv_count = 0;

    for (int i = 0; i < TRIES; ++i) {
      auto start = std::chrono::high_resolution_clock::now();

      icmp->icmp_seq = ttl * TRIES + i;
      icmp->icmp_cksum = 0;
      icmp->icmp_cksum = checksum((unsigned short *)icmp, PACKET_SIZE);

      if (sendto(sockfd, sendbuf, PACKET_SIZE, 0, (struct sockaddr *)&dest,
                 sizeof(dest)) < 0) {
        perror("sendto");
        continue;
      }

      int n = recvfrom(sockfd, recvbuf, PACKET_SIZE, 0,
                       (struct sockaddr *)&recv_addr, &addrlen);
      if (n < 0) {
        std::cout << "  *";
        continue;
      }

      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> elapsed = end - start;
      rtt_times[recv_count] = elapsed.count() * 1000;
      inet_ntop(AF_INET, &(recv_addr.sin_addr), ip_address, INET_ADDRSTRLEN);

      struct ip *ip = (struct ip *)recvbuf;
      struct icmp *recv_icmp = (struct icmp *)(recvbuf + (ip->ip_hl << 2));

      if (recv_icmp->icmp_type == ICMP_ECHOREPLY) {
        received_response = true;
        break;
      } else if (recv_icmp->icmp_type == ICMP_TIME_EXCEEDED) {
        received_response = true;
      } else if (recv_icmp->icmp_type == ICMP_DEST_UNREACH &&
                 recv_icmp->icmp_code == ICMP_PORT_UNREACH) {
        received_response = true;
        std::cout << "  " << ip_address << "  " << std::fixed
                  << std::setprecision(3) << rtt_times[recv_count]
                  << " ms (Port unreachable)" << std::endl;
        close(sockfd);
        return 0;
      }

      recv_count++;
    }

    if (received_response) {
      std::cout << "  ";
      for (int j = 0; j < recv_count; ++j) {
        std::cout << std::fixed << std::setprecision(3) << rtt_times[j]
                  << " ms ";
      }
      std::cout << ip_address;
    } else {
      std::cout << "  *";
    }

    std::cout << std::endl;

    if (received_response &&
        strcmp(ip_address, inet_ntoa(dest.sin_addr)) == 0) {
      break;
    }
  }

  close(sockfd);
  return 0;
}
