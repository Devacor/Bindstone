#include "MV/Network/scriptHttp.h"
#include "MV/Network/download.h"

#include <jaiscript/core/http_transport.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace MV {
	namespace {
		class ScriptHttpTransport : public jai::http_transport {
		public:
			jai::http_reply exchange(const jai::http_request& a_request) override {
				HttpRequest request {MV::Url(a_request.url)};
				request.method = a_request.method;
				request.headers = a_request.headers;
				request.postBody = a_request.body;
				DownloadOptions options;
				options.timeout = std::chrono::milliseconds(std::max<long long>(1, std::llround(a_request.timeout_seconds * 1000.0)));
				options.maxRedirects = 0;
				auto download = downloadBlocking(request, options);
				jai::http_reply result;
				if (!download.received()) {
					result.error = download.error == DownloadError::TIMED_OUT ? "timed out" : download.errorMessage;
					return result;
				}
				result.status = download.response.status;
				result.headers = std::move(download.response.headerLines);
				result.body = std::move(download.response.body);
				return result;
			}
		};
	}

	std::shared_ptr<jai::http_transport> makeScriptHttpTransport() {
		return std::make_shared<ScriptHttpTransport>();
	}
}
