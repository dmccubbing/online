/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>

#include <Poco/Condition.h>
#include <Poco/Mutex.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/SocketStream.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/TCPServer.h>
#include <Poco/Net/TCPServerConnection.h>
#include <Poco/Net/TCPServerConnectionFactory.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Process.h>
#include <Poco/String.h>
#include <Poco/Thread.h>
#include <Poco/Timespan.h>
#include <Poco/Timestamp.h>
#include <Poco/URI.h>
#include <Poco/Util/Application.h>
#include <Poco/Util/HelpFormatter.h>
#include <Poco/Util/Option.h>
#include <Poco/Util/OptionSet.h>

#include "LoadTest.hpp"
#include "LOOLProtocol.hpp"
#include "LOOLWSD.hpp"
#include "Util.hpp"

using namespace LOOLProtocol;

using Poco::Condition;
using Poco::Mutex;
using Poco::Net::HTTPClientSession;
using Poco::Net::HTTPRequest;
using Poco::Net::HTTPResponse;
using Poco::Net::Socket;
using Poco::Net::SocketOutputStream;
using Poco::Net::StreamSocket;
using Poco::Net::TCPServer;
using Poco::Net::TCPServerConnection;
using Poco::Net::WebSocket;
using Poco::Net::WebSocketException;
using Poco::Runnable;
using Poco::Thread;
using Poco::Timespan;
using Poco::Timestamp;
using Poco::URI;
using Poco::Util::Application;
using Poco::Util::HelpFormatter;
using Poco::Util::Option;
using Poco::Util::OptionSet;

class Output: public Runnable
{
public:
    Output(WebSocket& ws, Condition& cond, Mutex& mutex) :
        _ws(ws),
        _cond(cond),
        _mutex(mutex),
        _type(LOK_DOCTYPE_OTHER),
        _width(0),
        _height(0)
    {
    }

    void run() override
    {
        int flags;
        int n;
        Application& app = Application::instance();
        try
        {
            do
            {
                char buffer[100000];
                n = _ws.receiveFrame(buffer, sizeof(buffer), flags);

                if (n > 0 && (flags & WebSocket::FRAME_OP_BITMASK) != WebSocket::FRAME_OP_CLOSE)
                {
                    char *endl = (char *) memchr(buffer, '\n', n);
                    std::string response;
                    if (endl == nullptr)
                        response = std::string(buffer, n);
                    else
                        response = std::string(buffer, endl-buffer);
                    std::cout <<
                        Util::logPrefix() <<
                        "Client got " << n << " bytes: '" << response << "'" <<
                        (endl == nullptr ? "" : " ...") <<
                        std::endl;

                    if (response.find("status:") == 0)
                    {
                        parseStatus(response, _type, _numParts, _currentPart, _width, _height);
                        _cond.signal();
                    }
                }
            }
            while (n > 0 && (flags & WebSocket::FRAME_OP_BITMASK) != WebSocket::FRAME_OP_CLOSE);
            std::cout << Util::logPrefix() << "Finishing" << std::endl;
        }
        catch (WebSocketException& exc)
        {
            app.logger().error("WebSocketException: " + exc.message());
            _ws.close();
        }
    }

    WebSocket& _ws;
    Condition& _cond;
    Mutex& _mutex;
    LibreOfficeKitDocumentType _type;

    int _numParts;
    int _currentPart;
    int _width;
    int _height;
};

class Client: public Runnable
{
public:

    Client(LoadTest& app) :
        _app(app)
    {
    }

    void run() override
    {
        Timestamp start;
        std::vector<std::string> uris(_app.getDocList());
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(uris.begin(), uris.end(), g);
        if (uris.size() > _app.getNumDocsPerClient())
            uris.resize(_app.getNumDocsPerClient());
        while (true)
        {
            std::shuffle(uris.begin(), uris.end(), g);
            for (auto i : uris)
            {
                if (start.isElapsed(_app.getDuration() * Timespan::HOURS))
                    break;

                testDocument(i);
            }
            if (start.isElapsed(_app.getDuration() * Timespan::HOURS))
                break;
        }
    }

private:
    void testDocument(const std::string& document)
    {
        URI uri(_app.getURL());

        HTTPClientSession cs(uri.getHost(), uri.getPort());
        HTTPRequest request(HTTPRequest::HTTP_GET, "/ws");
        HTTPResponse response;
        WebSocket ws(cs, request, response);

        Condition cond;
        Mutex mutex;

        Thread thread;
        Output output(ws, cond, mutex);

        thread.start(output);

        if (document[0] == '/')
            sendTextFrame(ws, "load " + document);
        else
            sendTextFrame(ws, "load uri=" + document);

        sendTextFrame(ws, "status");

        mutex.lock();
        cond.wait(mutex);
        mutex.unlock();

        std::cout << Util::logPrefix() << "Got status, size=" << output._width << "x" << output._height << std::endl;

        sendTextFrame(ws, "tile width=256 height=256 tileposx=0 tileposy=0 tilewidth=10000 tileheight=10000");

        Thread::sleep(10000);

        std::cout << Util::logPrefix() << "Shutting down" << std::endl;

        ws.shutdown();
        thread.join();
    }

    void sendTextFrame(WebSocket& ws, const std::string& s)
    {
        ws.sendFrame(s.data(), s.size());
    }

    LoadTest& _app;
};

LoadTest::LoadTest() :
    _numClients(20),
    _numDocsPerClient(500),
    _duration(6),
    _url("http://localhost:" + std::to_string(LOOLWSD::DEFAULT_PORT_NUMBER) + "/ws")
{
}

LoadTest::~LoadTest()
{
}

unsigned LoadTest::getNumDocsPerClient() const
{
    return _numDocsPerClient;
}

unsigned LoadTest::getDuration() const
{
    return _duration;
}

std::string LoadTest::getURL() const
{
    return _url;
}

std::vector<std::string> LoadTest::getDocList() const
{
    return _docList;
}

void LoadTest::defineOptions(OptionSet& options) 
{
    Application::defineOptions(options);

    options.addOption(
        Option("doclist", "", "file containing URIs or pathnames of documents to load, - for stdin")
            .required(true)
            .repeatable(false)
            .argument("file"));

    options.addOption(
        Option("numclients", "", "number of simultaneous clients to simulate")
            .required(false)
            .repeatable(false)
            .argument("number"));

    options.addOption(
        Option("numdocs", "", "number of sequential documents per client")
            .required(false)
            .repeatable(false)
            .argument("number"));

    options.addOption(
        Option("duration", "", "duration in hours")
            .required(false)
            .repeatable(false)
            .argument("hours"));

    options.addOption(
        Option("server", "", "URI of LOOL server")
            .required(false)
            .repeatable(false)
            .argument("uri"));
}

void LoadTest::handleOption(const std::string& name, const std::string& value)
{
    Application::handleOption(name, value);

    if (name == "help")
        {
            displayHelp();
            exit(Application::EXIT_OK);
        }
    else if (name == "doclist")
        _docList = readDocList(value);
    else if (name == "numclients")
        _numClients = std::stoi(value);
    else if (name == "numdocs")
        _numDocsPerClient = std::stoi(value);
    else if (name == "duration")
        _duration = std::stoi(value);
    else if (name == "url")
        _url = value;
}

void LoadTest::displayHelp()
{
    HelpFormatter helpFormatter(options());
    helpFormatter.setCommand(commandName());
    helpFormatter.setUsage("OPTIONS");
    helpFormatter.setHeader("LibreOffice On-Line WebSocket server load test.");
    helpFormatter.format(std::cout);
}

int LoadTest::main(const std::vector<std::string>& args)
{
    Thread *clients[_numClients];

    for (unsigned i = 0; i < _numClients; i++)
    {
        clients[i] = new Thread();
        clients[i]->start(*(new Client(*this)));
    }

    for (unsigned i = 0; i < _numClients; i++)
    {
        clients[i]->join();
    }

    return Application::EXIT_OK;
}

std::vector<std::string> LoadTest::readDocList(const std::string& filename)
{
    std::vector<std::string> result;

    std::ifstream infile;
    std::istream *input;
    if (filename == "-")
        input = &std::cin;
    else
    {
        infile.open(filename);
        input = &infile;
    }

    while (!input->eof())
    {
        std::string s;
        *input >> std::ws;
        if (input->eof())
            break;
        *input >> s;
        result.push_back(s);
    }

    if (filename == "-")
        infile.close();

    return result;
}

POCO_APP_MAIN(LoadTest)

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */