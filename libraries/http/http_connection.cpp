#include "libraries/common.h"
#include "libraries/network/include/network.h"
#include "resource.h"
#include "resource_manager.h"
#include "http_connection.h"

constexpr int buffer_size = 4096;

namespace NetZ
{
namespace Http
{
  HttpConnection::HttpConnection(TcpSocket&& _socket, SocketService* _service, ResourceManager* rMgr)
    : socket(std::move(_socket))
    , resource_mgr(rMgr)
    , service(_service)
  {
  }

  void HttpConnection::Start()
  {
    Read(HttpParser::ParseState::RequestParsing);
  }

  void HttpConnection::Stop()
  {
    socket.Close();
  }

  void HttpConnection::Read(HttpParser::ParseState state)
  {
    char buffer[buffer_size];
    socket.Receive(buffer, buffer_size, 0, [this, &state, &buffer](int bytes_transferred, const std::error_code& ec)
    {
      service->ResetTimer(socketTimeoutTimer);
      if (bytes_transferred > 0 && !ec)
      {
        InputBuffer input(buffer, bytes_transferred);
        if (state == HttpParser::ParseState::RequestParsing && !HttpParser::ParseRequestLine(input, request))
        {
          if (input.sc != HttpStatusCode::ok)
          {
            response.statusCode = input.sc;
            WriteDefaultResponse();
            return;
          }
          else Read(HttpParser::ParseState::RequestParsing);
        }
        while (input.offset != input.End())
        {
          if (!HttpParser::ParseNextHeader(input, request))
          {
            if (input.sc != HttpStatusCode::ok)
            {
              response.statusCode = input.sc;
              WriteDefaultResponse();
              return;
            }
            else Read(HttpParser::ParseState::HeaderParsing);
          }
        }
        if (request.method == "GET")
        {
          if (!resource_mgr->GetResource(request, response))
          {
            response.statusCode = HttpStatusCode::not_found;
            WriteDefaultResponse();
          }
          else Write(resource_mgr->ToResource()->ToBuffer());
        }
        else if (request.method == "POST")
        {
          resource_mgr->AddResource(request, response);
          Write(resource_mgr->ToResource()->ToBuffer());
        }
      }
    });
  }

  void HttpConnection::Write(InputBuffer&& data)
  {
    socket.Send(static_cast<const char*>(data), data.buffer.size(), 0, [this](int bytes_transferred, const std::error_code& ec)
    {
      service->ResetTimer(socketTimeoutTimer);
      if (ec == std::errc::operation_canceled)
      {
        Stop();
      }
    });
  }

  void HttpConnection::WriteDefaultResponse()
  {
    auto replyString = response.GetDefaultReply();
    Write(InputBuffer(replyString.c_str(), replyString.length()));
  }
}
}
