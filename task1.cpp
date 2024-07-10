#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <netdb.h>
#include <netinet/ip_icmp.h>
#include <numeric>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define PACKET_SIZE 64
#define PING_SLEEP_RATE 1000000 // 1 second in microseconds

// Calculate checksum
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

// Create ICMP packet
void create_packet(struct icmp *icmph, int seq, int length) {
  icmph->icmp_type = ICMP_ECHO;
  icmph->icmp_code = 0;
  icmph->icmp_id = getpid();
  icmph->icmp_seq = seq;
  icmph->icmp_cksum = 0;
  icmph->icmp_cksum = checksum(icmph, length);
}

// Resolve hostname to IP address
bool resolve_hostname(const char *hostname, char *ip_addr) {
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_RAW;
  hints.ai_protocol = IPPROTO_ICMP;

  int err = getaddrinfo(hostname, nullptr, &hints, &res);
  if (err != 0) {
    std::cerr << "getaddrinfo: " << gai_strerror(err) << std::endl;
    return false;
  }

  struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
  inet_ntop(AF_INET, &(addr->sin_addr), ip_addr, INET_ADDRSTRLEN);
  freeaddrinfo(res);
  return true;
}

// Ping function
void ping(const char *ip_addr, int counts, int length) {
  int sockfd;
  struct sockaddr_in addr;
  struct icmp icmph;
  char send_packet[PACKET_SIZE];
  char recv_packet[1024];
  struct sockaddr_in recv_addr;
  socklen_t addr_len = sizeof(recv_addr);

  sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sockfd < 0) {
    perror("Socket error");
    return;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, ip_addr, &addr.sin_addr);

  std::cout << "PING " << ip_addr << " (" << ip_addr << ") " << length << "("
            << sizeof(struct ip) + length << ") bytes of data." << std::endl;

  int sent_packets = 0, received_packets = 0;
  std::vector<int> times;

  for (int i = 0; i < counts; ++i) {
    memset(send_packet, 0, PACKET_SIZE);
    create_packet((struct icmp *)send_packet, i, length);

    auto send_time = std::chrono::high_resolution_clock::now();
    if (sendto(sockfd, send_packet, length, 0, (struct sockaddr *)&addr,
               sizeof(addr)) <= 0) {
      perror("Sendto error");
      continue;
    }
    sent_packets++;

    if (recvfrom(sockfd, recv_packet, sizeof(recv_packet), 0,
                 (struct sockaddr *)&recv_addr, &addr_len) <= 0) {
      perror("Recvfrom error");
      continue;
    }

    auto recv_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        recv_time - send_time)
                        .count();
    times.push_back(duration);

    struct ip *ip_hdr = (struct ip *)recv_packet;
    struct icmp *icmp_hdr = (struct icmp *)(recv_packet + (ip_hdr->ip_hl << 2));

    if (icmp_hdr->icmp_type == ICMP_ECHOREPLY &&
        icmp_hdr->icmp_id == getpid()) {
      std::cout << length << " bytes from " << ip_addr << ": icmp_seq=" << i
                << " ttl=" << (int)ip_hdr->ip_ttl << " time=" << duration
                << " ms" << std::endl;
      received_packets++;
    } else {
      std::cout << "Received packet with ICMP type " << icmp_hdr->icmp_type
                << " code " << icmp_hdr->icmp_code << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(PING_SLEEP_RATE));
  }

  close(sockfd);

  // Print statistics
  int loss = ((sent_packets - received_packets) * 100) / sent_packets;
  auto minmax = std::minmax_element(times.begin(), times.end());
  int min_time = times.empty() ? 0 : *minmax.first;
  int max_time = times.empty() ? 0 : *minmax.second;
  int avg_time =
      times.empty()
          ? 0
          : std::accumulate(times.begin(), times.end(), 0) / times.size();

  std::cout << "--- " << ip_addr << " ping statistics ---" << std::endl;
  std::cout << sent_packets << " packets transmitted, " << received_packets
            << " received, " << loss << "% packet loss, time " << counts << "ms"
            << std::endl;
  if (!times.empty()) {
    std::cout << "rtt min/avg/max = " << min_time << "/" << avg_time << "/"
              << max_time << " ms" << std::endl;
  }
}

// Resolve the input address (IP or hostname) to an IP address string
bool resolve_address(const char *address, char *ip_addr) {
  if (inet_pton(AF_INET, address, ip_addr) == 1) {
    // Address is already a valid IP address
    strncpy(ip_addr, address, INET_ADDRSTRLEN);
    return true;
  } else {
    // Try resolving as a hostname
    return resolve_hostname(address, ip_addr);
  }
}

int main(int argc, char *argv[]) {
  int opt;
  const char *dstIP = nullptr;
  int length = 56;
  int counts = 4;

  while ((opt = getopt(argc, argv, "l:n:")) != -1) {
    switch (opt) {
    case 'l':
      length = atoi(optarg);
      break;
    case 'n':
      counts = atoi(optarg);
      break;
    default:
      std::cerr << "Usage: " << argv[0] << " dstIP -l length -n counts\n";
      return EXIT_FAILURE;
    }
  }

  if (optind >= argc) {
    std::cerr << "Expected destination IP address or hostname after options\n";
    return EXIT_FAILURE;
  }

  dstIP = argv[optind];

  char ip_addr[INET_ADDRSTRLEN];
  if (!resolve_address(dstIP, ip_addr)) {
    std::cerr << "Failed to resolve address: " << dstIP << std::endl;
    return EXIT_FAILURE;
  }

  ping(ip_addr, counts, length);

  return 0;
}
