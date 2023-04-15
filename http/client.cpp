#include <cstdlib>
#include <memory>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <cstring>
#include <utility>
#include <iostream>
#include <string.h>
#include <sstream>

#include "client.hpp"
#include "connection.hpp"


/*

    URL

*/

URL::URL(char *url) {
    char *c = url;

    // protocol
    char *proto = c;
    for (; *c; c++) {
        if (*c == ':') {
            *c = '\0';
        }
        if (*c == '/' && *(c-1) == '/') {
            *c = '\0';
            *(c-1) = '\0';
            strcpy(this->protocol, proto);
            c++;
            break;
        }
    }

    // host
    char *host = c;
    for (; *c; c++) {
        if (*c == ':' || *c == '/') {
            if (*c == ':') {
                *c = '\0';
                c++;
            }
            strcpy(this->host, host);
            break;
        }
    }

    // port
    if (*c >= 48 || *c <= 57) {
        char charport[5];
        int index = 0;
        for (; *c; c++) {
            if (*c == '/') {
                charport[index] = '\0';
                this->port = atoi(charport);
                break;
            }
            if (index > 4) {
                throw "invalid port";
            }
            charport[index++] = *c;
        }
    }

    // path
    strcpy(this->path, c);

    if (this->port == 0) {
        if (strcmp(this->protocol, "http") == 0) {
            this->port = 80;
        } else if (strcmp(this->protocol, "https") == 0) {
            this->port = 443;
        }
    }
}


/*

    BUFFER

*/

Buffer::Buffer(size_t initial_size) {
    this->buf = new char[initial_size]{0};
    this->size = 0;
    this->cap = initial_size;
}

Buffer::~Buffer() {
    delete this->buf;
}

void Buffer::grow(size_t len) {
    size_t new_cap = this->size + len;
    this->buf = (char*) std::realloc(this->buf, new_cap);
    if (this->buf == nullptr) {
        throw "out of memory";
    }
    this->cap = new_cap;
}

void Buffer::append(char *data, size_t len) {
    if (this->size + len > this->cap) {
        this->grow(this->size*2 + len);
    }

    std::memcpy(&this->buf[this->size], data, len);
    this->size += len;
    this->buf[this->size] = '\0';
}

size_t Buffer::getSize() const {
    return this->size;
}

char* Buffer::getBuffer() const {
    return this->buf;
}

/*

    ResponseReader

*/

ResponseReader::ResponseReader(int con, Connection *conn) : con(con), conn(conn) {
    this->con = con;
}

void ResponseReader::ParseStatus(Response *resp, char *line) {
    bool setNextChar = true;
    char *ptrs[3] = {0};
    int index = 0;

    for (char *c = line; *c; c++) {
        if (*c == ' ') {
            *c = '\0';
            setNextChar = true;
            continue;
        }
        if (setNextChar) {
            ptrs[index++] = c;
            setNextChar = false;
            if (index == 3) {
                break;
            }
        }
    }

    resp->statusCode = atoi(ptrs[1]);
    strcpy(resp->proto, ptrs[0]);
    strcpy(resp->status, ptrs[2]);
}

void ResponseReader::ParseHeader(Response *resp, char *line) {
    char *key = line;
    bool nextCharIsVal = false;

    for (char *c = line; *c; c++) {
        if (*c == ':') {
            *c = '\0';
            nextCharIsVal = true;
            continue;
        }

        if (*c != ' ' && nextCharIsVal) {
            std::string k(key);
            std::string v(c);
            resp->headers[k] = v;
            return;
        }
    }
}

void ResponseReader::Parse(Response *resp) {
    int nline = 0;
    char *line = nullptr;
    bool nextCharIsline = true;

    for (char *c = resp->body.getBuffer(); *c; c++) {
        if (*c != '\n') {
            if (nextCharIsline) {
                line = c;
                nextCharIsline = false;
            }
            continue;
        }

        nextCharIsline = true;
        *c = '\0';
        if (*(c-1) == '\r')
            *(c-1) = '\0';

        if (*(c-2) == '\0') {
            resp->respBody = c+1;
            break;
        }

        if (nline == 0) {
            this->ParseStatus(resp, line);
        } else {
            this->ParseHeader(resp, line);
        }

        nline++;
    }

    resp->respBodySize = resp->body.getSize() - (resp->respBody - resp->body.getBuffer());
}

void ResponseReader::readResponse(Response *resp) {
    size_t size = 0;
    char buf[4096];

    size = this->conn->Read(buf, 4096);
    buf[size] = '\0';
    resp->body.append(buf, size);

    this->Parse(resp);
}

/*

    Response

*/

Response::Response() : body(100) {}

char* Response::Body() {
    return this->respBody;
}

size_t Response::Size() {
    return this->respBodySize;
}


/*

    Request

*/


Request::Request(struct URL *url, char *method)
    : url(url),
      method(method) {}

void Request::SetBody(Buffer *body) {
    this->requestBody = body;
}

void Request::AddHeader(char *key, char *val) {
    this->headers[std::string(key)] = std::string(val);
}

struct URL* Request::URL() {
    return this->url;
}

void Request::MakeRequest(Buffer *buf) {
    buf->append(this->method, strlen(this->method));
    buf->append((char*)" ", 1);
    buf->append(this->url->path, strlen(this->url->path));
    buf->append((char*)" ", 1);
    buf->append((char*)"HTTP/1.1", 8);
    buf->append((char*)"\r\n", 2);

    this->AddHeader((char*)"User-Agent", (char*)"cppurl/0.0.1");
    this->AddHeader((char*)"Accept", (char*)"*/*");
    for (auto item : this->headers) {
        buf->append((char*)item.first.c_str(), item.first.length());
        buf->append((char*)": ", 2);
        buf->append((char*)item.second.c_str(), item.second.length());
        buf->append((char*)"\r\n", 2);
    }

    buf->append((char*)"\r\n", 2);
    if (this->requestBody != nullptr) {
        buf->append(this->requestBody->getBuffer(), this->requestBody->getSize());
    }
}


/*

    HttpClient

*/

HttpClient::~HttpClient() {
    for (auto it : this->connections) {
        delete it.second;
    }
}

std::pair<std::unique_ptr<Response>, NetError> HttpClient::request(Request *req) {
    Buffer httpBody(1024);
    req->AddHeader((char*)"Host", req->URL()->host);
    req->MakeRequest(&httpBody);

    std::stringstream hostportStream;
    hostportStream << req->URL()->host << ":" << req->URL()->port;
    std::string hostport = hostportStream.str();

    // Connection *connection = this->connections[hostport];
    // if (connection == nullptr) {
    //     int fd = socket_connect(req->URL()->host, req->URL()->port);
    //     if (fd <= 0) {
    //         throw "unable to connect";
    //     }
    //     if (strcmp(req->URL()->protocol, "https") == 0) {
    //         connection = new SSLConnection(fd);
    //     } else if (strcmp(req->URL()->protocol, "http") == 0) {
    //         connection = new PlainConnection(fd);
    //     } else {
    //         throw "unknown protocol";
    //     }
    //     this->connections[hostport] = connection;
    // }

    Connection *connection = nullptr;
    auto [fd, err] = socket_connect(req->URL()->host, req->URL()->port);
    if (err) {
        return std::make_pair(nullptr, err);
    }

    if (strcmp(req->URL()->protocol, "https") == 0) {
        connection = new SSLConnection(fd);
    } else if (strcmp(req->URL()->protocol, "http") == 0) {
        connection = new PlainConnection(fd);
    } else {
        throw "unknown protocol";
    }

    connection->Write(httpBody.getBuffer(), httpBody.getSize());
    ResponseReader reader(sock, connection);
    std::unique_ptr<Response> resp = std::make_unique<Response>();
    reader.readResponse(resp.get());
    connection->Close();
    return std::make_pair(std::move(resp), NetError{});
}
