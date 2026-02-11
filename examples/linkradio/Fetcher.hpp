#pragma once

#include <string>

namespace linkradio
{

// Fetch text content from an HTTP URL (http:// only, no TLS).
// Returns empty string on failure.
std::string httpGet(const std::string& host, const std::string& path, int port = 80);

// Download binary content to a local file path.
// Returns true on success.
bool httpDownload(const std::string& host, const std::string& path,
                  const std::string& localPath, int port = 80);

} // namespace linkradio
