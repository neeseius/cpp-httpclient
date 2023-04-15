#pragma once
#include <iostream>

#include <errno.h>
#include "openssl/ssl.h"

struct NetError {
    const int         code;
    const bool        isErr;
    const std::string message;

    NetError() : code(0), isErr(false) {}

    NetError(int errNo)
        : code(errNo)
        , isErr(true)
        , message(strerror(errNo))
    {
    }

    operator bool() const { return isErr; }

    const std::string& Error() const { return message; }
};

std::pair<int, NetError> socket_connect(char *hostname, int port);

class Connection {
protected:
    int fd =  0;
    bool closed = false;

public:
    virtual ~Connection() = 0;

    virtual int Read(char *buf, size_t nbytes) = 0;

    virtual int Write(char *buf, size_t nbytes) = 0;

    virtual void Close() = 0;
};


class PlainConnection: public Connection {

public:
    PlainConnection(int fd);

    ~PlainConnection();

    int Read(char *buf, size_t nbytes);

    int Write(char *buf, size_t nbytes);

    void Close();
};


class SSLConnection: public Connection {

private:
    SSL_CTX *ctx = nullptr;
    SSL *ssl = nullptr;
    bool closed = false;

public:
    SSLConnection(int fd);

    ~SSLConnection();

    void PrintErr(ulong err);

    int Read(char *buf, size_t nbytes);

    int Write(char *buf, size_t nbytes);

    void Close();
};
