#include "tcp_sponge_socket.hh"
#include "util.hh"

#include <cstdlib>
#include <iostream>

using namespace std;

void get_URL(const string &host, const string &path)
{
    // Your code here.

    // You will need to connect to the "http" service on
    // the computer whose name is in the "host" string,
    // then request the URL path given in the "path" string.

    // Then you'll need to print out everything the server sends back,
    // (not just one call to read() -- everything) until you reach
    // the "eof" (end of file).

    // 创建socket
    auto sc = CS144TCPSocket();

    // 连接到服务器
    auto addr = Address(host, "http");
    sc.connect(addr);

    // 往socket中写请求（服务器会从socket中读请求）
    sc.write("GET ");
    sc.write(path);
    sc.write(" HTTP/1.1\r\n");
    sc.write("Host: ");
    sc.write(host);
    sc.write(" \r\n");
    sc.write("Connection: close\r\n\r\n");

    // 服务器把消息写到socket中后，client从里面读，只要没读完就会继续（end of file）
    // 并将读到的信息打印出来
    while (!sc.eof())
    {
        printf("%s", sc.read().c_str());
    }

    sc.shutdown(SHUT_WR);

    sc.wait_until_closed();

    // cerr << "Function called: get_URL(" << host << ", " << path << ").\n";
    // cerr << "Warning: get_URL() has not been implemented yet.\n";
}

int main(int argc, char *argv[])
{
    try
    {
        if (argc <= 0)
        {
            abort(); // For sticklers: don't try to access argv[0] if argc <= 0.
        }

        // The program takes two command-line arguments: the hostname and "path" part of the URL.
        // Print the usage message unless there are these two arguments (plus the program name
        // itself, so arg count = 3 in total).
        if (argc != 3)
        {
            cerr << "Usage: " << argv[0] << " HOST PATH\n";
            cerr << "\tExample: " << argv[0] << " stanford.edu /class/cs144\n";
            return EXIT_FAILURE;
        }

        // Get the command-line arguments.
        const string host = argv[1];
        const string path = argv[2];

        // Call the student-written function.
        get_URL(host, path);
    }
    catch (const exception &e)
    {
        cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
