#ifndef _MV_SCRIPT_HTTP_H_
#define _MV_SCRIPT_HTTP_H_

#include <memory>

namespace jai {
	class http_transport;
}

namespace MV {
	// The HTTP stack scripts' spider::catch / sling / get / post send through: one blocking download per exchange
	// (downloadBlocking: MV's sockets, TLS verified against the platform's roots), following no redirect itself.
	std::shared_ptr<jai::http_transport> makeScriptHttpTransport();
}

#endif
