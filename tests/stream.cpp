#include "catch.hpp"
#include "stream.hpp"
#include "TestConnection.hpp"
#include "gsl.h"
#include <sstream>
#include <algorithm>


using namespace std;
using namespace gsl;


static vector<ubyte> subscribeMsg(const std::string& topic, ushort msgId) {
    vector<ubyte> msg{0x8b}; //fixed header sans remaining length
    const auto remainingLength = topic.size() + 2 /*topic len*/ + 2 /*msgId*/ + 1 /*qos*/;
    msg.emplace_back(remainingLength);

    msg.emplace_back(msgId >> 8);
    msg.emplace_back(msgId & 0xff);

    msg.emplace_back(topic.size() >> 8);
    msg.emplace_back(topic.size() & 0xff);
    copy(topic.cbegin(), topic.cend(), back_inserter(msg));

    msg.emplace_back(0); //qos

    return msg;
}


int readInto(MqttStream& stream, span<const ubyte> bytes) {
    copy(bytes.cbegin(), bytes.cend(), stream.begin());
    return bytes.size();
}


TEST_CASE("MQTT in 2 packets") {
    MqttServer<TestConnection> server;
    TestConnection connection;
    MqttStream stream{128};

    const auto subscribe = subscribeMsg("top", 33);
    server.newMessage(connection, subscribe);

    const vector<ubyte> bytes1{
        0x3c, 15, //fixed header
        0x00, 0x03, 't', 'o', 'p', //topic name
        0x00, 0x21, //message ID
        1, 2, 3 //first part of payload
    };
    const vector<ubyte> bytes2{4, 5, 6, 7, 8}; //2nd part of payload

    const auto numBytes1 = readInto(stream, bytes1);
    stream.handleMessages(numBytes1, server, connection);
    REQUIRE(connection.payloads == vector<Payload>{});

    const auto numBytes2 = readInto(stream, bytes2);
    stream.handleMessages(numBytes2, server, connection);
    REQUIRE(connection.payloads == (vector<Payload>{{1, 2, 3, 4, 5, 6, 7, 8}}));
}


TEST_CASE("Broken header and two messages") {
    MqttServer<TestConnection> server;
    TestConnection connection;
    MqttStream stream{128};

    connection.connected = true; //easier than sending conneciton packet

    const auto subscribe = subscribeMsg("top", 33);
    server.newMessage(connection, subscribe);

    const vector<ubyte> bytes1{0x3c}; //half of header
    const vector<ubyte> bytes2{
        15, //2nd half of fixed header
        0x00, 0x03, 't', 'o', 'p', //topic name
        0x00, 0x21, //message ID
        1, 2, 3, 4, 5, 6, 7, 8,
        0xe0, 0, //header for disconnect
    };

    const auto numBytes1 = readInto(stream, bytes1);
    stream.handleMessages(numBytes1, server, connection);
    REQUIRE(connection.payloads == vector<Payload>{});
    REQUIRE(connection.connected == true);

    const auto numBytes2 = readInto(stream, bytes2);
    stream.handleMessages(numBytes2, server, connection);
    REQUIRE(connection.payloads == (vector<Payload>{{1, 2, 3, 4, 5, 6, 7, 8}}));
    REQUIRE(connection.connected == false);
}


TEST_CASE("bug from rust impl") {
    MqttServer<TestConnection> server;
    TestConnection connection;
    MqttStream stream{128};

    connection.connected = true; //easier than sending conneciton packet

    const vector<ubyte> bytes{48, 30, 0, 12, 108, 111, 97, 100, 116, 101, 115, 116, 47, 49, 54, 54};
    const auto numBytes = readInto(stream, bytes);
    stream.handleMessages(numBytes, server, connection);
    REQUIRE(connection.payloads == vector<Payload>{});
}


TEST_CASE("bug header size") {
    MqttServer<TestConnection> server;
    TestConnection connection;
    MqttStream stream{128};

    const auto subscribe = subscribeMsg("top", 33);
    server.newMessage(connection, subscribe);

    const vector<ubyte> bytes1{
        0x3c, 10, //fixed header
        0x00, 0x03, 't', 'o', 'p', //topic name
        0x00, 0x21, //message ID
        1, 2, 3, //payload
        0x3c, 15 //fixed header of the 2nd msg
    };
    const vector<ubyte> bytes2{
        0x00, 0x03, 't', 'o', 'p', //topic name
        0x00, 0x21, //message ID
        1, 2, 3, 4, 5, 6, 7, 8 //payload
   };

    const auto numBytes1 = readInto(stream, bytes1);
    stream.handleMessages(numBytes1, server, connection);
    const auto numBytes2 = readInto(stream, bytes2);
    stream.handleMessages(numBytes2, server, connection);

    REQUIRE(connection.payloads == (vector<Payload>{{1, 2, 3}, {1, 2, 3, 4, 5, 6, 7, 8}}));
}
