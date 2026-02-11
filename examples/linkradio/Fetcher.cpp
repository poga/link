#include "Fetcher.hpp"
#include <asio.hpp>
#include <fstream>
#include <sstream>

namespace linkradio
{

std::string httpGet(const std::string& host, const std::string& path, int port)
{
  try
  {
    asio::io_context io;
    asio::ip::tcp::resolver resolver(io);
    asio::ip::tcp::socket socket(io);

    auto endpoints = resolver.resolve(host, std::to_string(port));
    asio::connect(socket, endpoints);

    std::string request = "GET " + path + " HTTP/1.0\r\n"
                          "Host: " + host + "\r\n"
                          "Connection: close\r\n\r\n";
    asio::write(socket, asio::buffer(request));

    asio::error_code ec;
    asio::streambuf buf;
    while (asio::read(socket, buf, ec))
    {
    }

    std::ostringstream ss;
    ss << &buf;
    auto response = ss.str();

    // Strip HTTP headers
    auto headerEnd = response.find("\r\n\r\n");
    if (headerEnd != std::string::npos)
    {
      return response.substr(headerEnd + 4);
    }
    return {};
  }
  catch (...)
  {
    return {};
  }
}

bool httpDownload(const std::string& host, const std::string& path,
                  const std::string& localPath, int port)
{
  try
  {
    asio::io_context io;
    asio::ip::tcp::resolver resolver(io);
    asio::ip::tcp::socket socket(io);

    auto endpoints = resolver.resolve(host, std::to_string(port));
    asio::connect(socket, endpoints);

    std::string request = "GET " + path + " HTTP/1.0\r\n"
                          "Host: " + host + "\r\n"
                          "Connection: close\r\n\r\n";
    asio::write(socket, asio::buffer(request));

    asio::error_code ec;
    asio::streambuf buf;
    while (asio::read(socket, buf, ec))
    {
    }

    std::string response;
    {
      std::ostringstream ss;
      ss << &buf;
      response = ss.str();
    }

    auto headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    std::ofstream file(localPath, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(response.data() + headerEnd + 4,
               static_cast<std::streamsize>(response.size() - headerEnd - 4));
    return true;
  }
  catch (...)
  {
    return false;
  }
}

} // namespace linkradio
