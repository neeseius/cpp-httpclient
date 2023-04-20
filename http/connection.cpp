#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cstdlib>
#include <unistd.h>      /*FOR USING FORK for at a time send and receive messages*/
#include <errno.h>       /*USING THE ERROR LIBRARY FOR FINDING ERRORS*/
#include <malloc.h>      /*FOR MEMORY ALLOCATION */
#include <string.h>      /*using fgets funtions for geting input from user*/
#include <arpa/inet.h>   /*for using ascii to network bit*/
#include <sys/socket.h>  /*for creating sockets*/
#include <netdb.h>
#include <thread>
#include <sys/types.h>   /*for using sockets*/
#include <netinet/in.h>  /* network to asii bit */
#include <resolv.h>      /*server to find out the runner's IP address*/
#include "openssl/ssl.h" /*using openssl function's and certificates and configuring them*/
#include "openssl/err.h" /* helps in finding out openssl errors*/
#include <stdio.h>       /*standard i/o*/
#include <map>
#include <chrono>
#include <mutex>
#include <netinet/tcp.h>
#include <fcntl.h>

#include "connection.hpp"

static const SSL_METHOD *method = TLS_client_method();

class Resolver {
    std::mutex mut;
    using string = std::string;
    using steady_clock = std::chrono::steady_clock;

    struct Entry {
        in_addr_t                 addr;
        steady_clock::time_point  tp;
    };
    std::map<string, Entry> entries;

public:
    in_addr_t Resolve(char* hostname) {
        std::lock_guard<std::mutex> guard(mut);
        std::string name(hostname);
        auto it = entries.find(name);

        auto now = steady_clock::now();
        if (it != entries.end()) {
            auto et = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.tp);
            if ( et.count() < 2 ) {
                return it->second.addr;
            }
        }

        const struct hostent *host;
        host = gethostbyname(hostname);
        if (host == nullptr) {
            throw "invalid host";
        }

        in_addr_t addr = *((ulong*)host->h_addr);
        entries[name] = Entry{addr, steady_clock::now()};
        return addr;
    }
};


std::pair<int, NetError> socket_connect(char *hostname, int port) {
    static Resolver resolver;
    struct sockaddr_in addr;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return std::make_pair(sock, NetError{sock});

    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);

    int val = 1;
    setsockopt(
        sock,
        IPPROTO_TCP,
        TCP_NODELAY,
        (void*)&val,
        sizeof(int));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = resolver.Resolve(hostname);

    int retval = connect(sock, (struct sockaddr*)&addr, sizeof(addr));

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);

    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    retval = select(sock+1, NULL, &wfds, NULL, &tv);
    if (retval == 0) {
        close(sock);
        return std::make_pair(sock, NetError{"connect timeout"});
    }

    int err = 0;
    int l = sizeof(int);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, (void*)&err, (socklen_t*)&l);
    if (err != 0) {
        close(sock);
        return std::make_pair(sock, NetError{err});
    }

    flags &= ~O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);

    return std::make_pair(sock, NetError{});
}

Connection::~Connection() {}

PlainConnection::PlainConnection(int fd) {
    this->fd = fd;
}

PlainConnection::~PlainConnection() {
    this->Close();
}

int PlainConnection::Read(char *buf, size_t nbytes) {
    return recv(this->fd, buf, nbytes, 0);
}

int PlainConnection::Write(char *buf, size_t nbytes) {
    return write(this->fd, buf, nbytes);
}

void PlainConnection::Close() {
    if (closed) {
        return;
    }
    shutdown(this->fd, SHUT_RDWR);
    close(fd);
    this->closed = true;
}


SSLConnection::~SSLConnection() {
    this->Close();
    SSL_free(this->ssl);
    SSL_CTX_free(this->ctx);
}

void SSLConnection::Close() {
    if (closed) {
        return;
    }
    shutdown(this->fd, SHUT_RDWR);
    close(fd);
    this->closed = true;
}

SSLConnection::SSLConnection(int fd) {
    this->fd = fd;
    this->ctx = SSL_CTX_new(method);
    if (ctx == NULL) {
        throw ERR_get_error();
    }

    this->ssl = SSL_new(this->ctx);
    SSL_set_fd(this->ssl, fd);
    if (SSL_connect(this->ssl) == -1) {
        throw ERR_get_error();
    }
}

void SSLConnection::PrintErr(ulong err) {
    char buf[256];
    ERR_error_string(err, buf);
    printf("%s\n", buf);
}

int SSLConnection::Read(char *buf, size_t nbytes) {
    return SSL_read(this->ssl, buf, nbytes);
}

int SSLConnection::Write(char *buf, size_t nbytes) {
    return SSL_write(this->ssl, buf, nbytes);
}


void read_replies(SSLConnection *sslCon) {
    char reply[4096];
    int sz;
    for (;;) {
        sz = sslCon->Read(reply, 4096);
        if (sz <= 0) {
            return;
        }
        printf("%s", reply);
    }
}


// int main(int argc, char *argv[]) {
//     if (argc < 3) {
//         printf("ssl-connect [host] [port]\n");
//         return -1;
//     }

//     SSL_library_init();
//     OpenSSL_add_all_algorithms();
//     SSL_load_error_strings();

//     SSLConnection sslCon;

//     char *host = argv[1];
//     int port = atoi(argv[2]);
//     int sock;

//     try {
//         sock = socket_connect(host, port);
//     } catch (const char *e) {
//         printf("got %s\n", e);
//         return -1;
//     }

//     try {
//         sslCon.Establish(sock);
//     } catch (ulong i) {
//         sslCon.PrintErr(i);
//         return -1;
//     }

//     std::thread reply(read_replies, &sslCon);

//     char input[1024];
//     for (;;) {
//         size_t sz = read(STDIN_FILENO, input, 1024);
//         input[sz] = '\0';
//         if (strcmp(input, "quit\n") == 0) {
//             break;
//         }
//         sslCon.Write(input, sz);
//     }
//     sslCon.Close();
//     reply.join();
// }
