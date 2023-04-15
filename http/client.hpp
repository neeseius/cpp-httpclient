#pragma once

#include <memory>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <unordered_map>
#include <iostream>
#include "connection.hpp"


// URL
struct URL {
    char protocol[10] = {0};
    char host[128] = {0};
    char path[128] = "GET";
    int port = 443;

    URL(char *url);
    char *String();
};


// Buffer
class Buffer {
private:
    size_t size;
    size_t cap;
    char *buf;

public:
    Buffer(size_t initial_size);
    ~Buffer();
    void grow(size_t len);
    void append(char *data, size_t len);
    size_t getSize() const;
    char *getBuffer() const;
};


class Request {
private:
    std::unordered_map<std::string, std::string> headers;
    Buffer *requestBody = nullptr;
    struct URL *url;
    char *method;

public:
    Request(struct URL *url, char *method);
    void AddHeader(char *key, char *val);
    void SetBody(Buffer *body);
    struct URL* URL();
    void MakeRequest(Buffer *buf);
};


// Response
class Response {
private:

public:
    std::unordered_map<std::string, std::string> headers;
    Buffer body;
    char *respBody = nullptr;
    size_t respBodySize = 0;
    size_t statusCode = 0;
    char status[24] = {0};
    char proto[24] = {0};

    Response();
    char* Body();
    size_t Size();
};


// ResponseReader
class ResponseReader {
private:
    int con;
    Connection *conn;
    char *body;
    void ParseStatus(Response *resp, char *line);
    void ParseHeader(Response *resp, char *line);
    void Parse(Response *resp);

public:
    ResponseReader(int c, Connection *conn);
    void readResponse(Response *resp);
};


// HttpClient
class HttpClient {
private:
    int sock = 0;
    struct sockaddr_in addr;
    std::unordered_map<std::string, Connection*> connections;

public:
    std::pair<std::unique_ptr<Response>, NetError> request(Request *req);
    ~HttpClient();
};
