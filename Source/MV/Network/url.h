#ifndef _MV_URL_H_
#define _MV_URL_H_

#ifdef _MSC_VER
#	pragma warning(disable:4786)
#endif

#include <vector>
#include <string>
#include <sstream>

namespace MV {

	/**
	A Uniform Resource Identifier, as specified in RFC 3986.

	The URI class provides methods for building URIs from their
	parts, as well as for splitting URIs into their parts.
	Furthermore, the class provides methods for resolving
	relative URIs against base URIs.

	The class automatically performs a few normalizations on
	all URIs and URI parts passed to it:
	* scheme identifiers are converted to lower case.
	* percent-encoded characters are decoded
	* optionally, dot segments are removed from paths (see normalize())

	\verbatim
	Example output
	--------------
	Original URI   : http://127.0.0.1:611/index.html?a=1&var2=%20aaa%20%A
	getHost        : 127.0.0.1
	getPort        : 611
	toString       : http://127.0.0.1:611/index.html?a=1&var2=%20aaa%20%A
	getScheme      : http
	getUserInfo    :
	getPath        : /index.html
	isRelative     : 0
	getAuthority   : 127.0.0.1:611
	getQuery       : a=1&var2= aaa
	getRawQuery    : a=1&var2=%20aaa%20%A
	getFragment    :
	getPathEtc     : /index.html?a=1&var2=%20aaa%20%A
	getPathAndQuery: /index.html?a=1&var2=%20aaa%20%A
	empty          : 0
	isIPAddress    : 1
	getPortAsString: 611
	\endverbatim
	*/
	class Url {
	public:
		/// Creates an empty URI.
		Url();

		/// Parses an URI from the given string. Throws a
		/// SyntaxException if the uri is not valid.
		Url(const std::string& uri);

		/// Parses an URI from the given string. Throws a
		/// SyntaxException if the uri is not valid.
		explicit Url(const char* uri);

		/// Creates an URI from its parts.
		Url(const std::string& scheme, const std::string& pathEtc);

		/// Creates an URI from its parts.
		Url(const std::string& scheme, const std::string& authority, const std::string& pathEtc);

		/// Creates an URI from its parts.
		Url(const std::string& scheme, const std::string& authority, const std::string& path, const std::string& query);

		/// Creates an URI from its parts.
		Url(const std::string& scheme, const std::string& authority, const std::string& path, const std::string& query, const std::string& fragment);

		/// Copy constructor. Creates an URI from another one.
		Url(const Url& uri);

		/// Creates an URI from a base URI and a relative URI, according to
		/// the algorithm in section 5.2 of RFC 3986.
		Url(const Url& baseURI, const std::string& relativeURI);

		~Url();

		/// Assignment operator.
		Url& operator = (const Url& uri);

		/// Parses and assigns an URI from the given string. Throws a
		/// SyntaxException if the uri is not valid.
		Url& operator = (const std::string& uri);

		/// Parses and assigns an URI from the given string. Throws a
		/// SyntaxException if the uri is not valid.
		Url& operator = (const char* uri);

		/// Swaps the URI with another one.
		void swap(Url& uri);

		/// Clears all parts of the URI.
		void clear();

		/// Returns a string representation of the URI.
		///
		/// Characters in the path, query and fragment parts will be
		/// percent-encoded as necessary.
		std::string toString() const;

		/// Returns the scheme part of the URI.
		const std::string& scheme() const;

		/// Sets the scheme part of the URI. The given scheme
		/// is converted to lower-case.
		///
		/// A list of registered URI schemes can be found
		/// at <http://www.iana.org/assignments/uri-schemes>.
		void scheme(const std::string& scheme);

		/// Returns the user-info part of the URI.
		const std::string& userInfo() const;

		/// Sets the user-info part of the URI.
		void userInfo(const std::string& userInfo);

		/// Returns the host part of the URI.
		const std::string& host() const;

		/// Sets the host part of the URI.
		void host(const std::string& host);

		/// Returns the port number part of the URI.
		///
		/// If no port number (0) has been specified, the
		/// well-known port number (e.g., 80 for http) for
		/// the given scheme is returned if it is known.
		/// Otherwise, 0 is returned.
		unsigned short port() const;

		/// Sets the port number part of the URI.
		void port(unsigned short port);

		const std::string portString() const;

		/// Returns the authority part (userInfo, host and port)
		/// of the URI.
		///
		/// If the port number is a well-known port
		/// number for the given scheme (e.g., 80 for http), it
		/// is not included in the authority.
		std::string authority() const;

		/// Parses the given authority part for the URI and sets
		/// the user-info, host, port components accordingly.
		void authority(const std::string& authority);

		/// Returns the path part of the URI.
		const std::string& path() const;

		/// Sets the path part of the URI.
		void path(const std::string& path);

		/// Returns the query part of the URI.
		std::string query() const;

		/// Sets the query part of the URI.
		void query(const std::string& query);

		/// Returns the unencoded query part of the URI.
		const std::string& rawQuery() const;

		/// Sets the query part of the URI.
		void rawQuery(const std::string& query);

		/// Returns the fragment part of the URI.
		const std::string& fragment() const;

		/// Sets the fragment part of the URI.
		void fragment(const std::string& fragment);

		/// Sets the path, query and fragment parts of the URI.
		void pathEtc(const std::string& pathEtc);

		/// Returns the path, query and fragment parts of the URI.
		std::string pathEtc() const;

		// Returns the path and query parts of the URI.
		std::string pathAndQuery() const;

		/// Resolves the given relative URI against the base URI.
		/// See section 5.2 of RFC 3986 for the algorithm used.
		void resolve(const std::string& relativeURI);

		/// Resolves the given relative URI against the base URI.
		/// See section 5.2 of RFC 3986 for the algorithm used.
		void resolve(const Url& relativeURI);

		/// Returns true if the URI is a relative reference, false otherwise.
		///
		/// A relative reference does not contain a scheme identifier.
		/// Relative references are usually resolved against an absolute
		/// base reference.
		bool isRelative() const;

		/// Returns true if the URI is empty, false otherwise.
		bool empty() const;

		/// Returns true if both URIs are identical, false otherwise.
		///
		/// Two URIs are identical if their scheme, authority,
		/// path, query and fragment part are identical.
		bool operator == (const Url& uri) const;

		/// Parses the given URI and returns true if both URIs are identical,
		/// false otherwise.
		bool operator == (const std::string& uri) const;

		/// Returns true if both URIs are identical, false otherwise.
		bool operator != (const Url& uri) const;

		/// Parses the given URI and returns true if both URIs are identical,
		/// false otherwise.
		bool operator != (const std::string& uri) const;

		/// Normalizes the URI by removing all but leading . and .. segments from the path.
		///
		/// If the first path segment in a relative path contains a colon (:),
		/// such as in a Windows path containing a drive letter, a dot segment (./)
		/// is prepended in accordance with section 3.3 of RFC 3986.
		void normalize();

		/// Places the single path segments (delimited by slashes) into the
		/// given vector.
		void pathSegments(std::vector<std::string>& segments);

		/// URI-encodes the given string by escaping reserved and non-ASCII
		/// characters. The encoded string is appended to encodedStr.
		static void encode(const std::string& str, const std::string& reserved, std::string& encodedStr);

		/// URI-decodes the given string by replacing percent-encoded
		/// characters with the actual character. The decoded string
		/// is appended to decodedStr.
		static void decode(const std::string& str, std::string& decodedStr, long _flags2);

		static bool isIPAddress(const std::string& str);

		/// If set to true, error messages in place of former throw-s will be shown
		/// using printf-s.
		void setPrintErrors(bool in);

	protected:

		/// Returns true if both uri's are equivalent.
		///
		bool equals(const Url& uri) const;

		/// Returns true if the URI's port number is a well-known one
		/// (for example, 80, if the scheme is http).
		bool isWellKnownPort() const;

		/// Returns the well-known port number for the URI's scheme,
		/// or 0 if the port number is not known.
		unsigned short getWellKnownPort() const;

		/// Parses and assigns an URI from the given string. Throws a
		/// SyntaxException if the uri is not valid.
		void parse(const std::string& uri);

		/// Parses and sets the user-info, host and port from the given data.
		///
		void parseAuthority(std::string::const_iterator& it, const std::string::const_iterator& end);

		/// Parses and sets the host and port from the given data.
		///
		void parseHostAndPort(std::string::const_iterator& it, const std::string::const_iterator& end);

		/// Parses and sets the path from the given data.
		///
		void parsePath(std::string::const_iterator& it, const std::string::const_iterator& end);

		/// Parses and sets the path, query and fragment from the given data.
		///
		void parsePathEtc(std::string::const_iterator& it, const std::string::const_iterator& end);

		/// Parses and sets the query from the given data.
		///
		void parseQuery(std::string::const_iterator& it, const std::string::const_iterator& end);

		/// Parses and sets the fragment from the given data.
		///
		void parseFragment(std::string::const_iterator& it, const std::string::const_iterator& end);

		/// Appends a path to the URI's path.
		///
		void mergePath(const std::string& path);

		/// Removes all dot segments from the path.
		///
		void removeDotSegments(bool removeLeading = true);

		/// Places the single path segments (delimited by slashes) into the
		/// given vector.
		static void pathSegments(const std::string& path, std::vector<std::string>& segments);

		/// Builds the path from the given segments.
		///
		void buildPath(const std::vector<std::string>& segments, bool leadingSlash, bool trailingSlash);

		static const std::string RESERVED_PATH;
		static const std::string RESERVED_QUERY;
		static const std::string RESERVED_FRAGMENT;
		static const std::string ILLEGAL;

	private:
		// Replaces all characters in str with their lower-case counterparts.
		template <class S>
		S& toLowerInPlace3(S& str) {
			typename S::iterator it = str.begin();
			typename S::iterator end = str.end();
			// originally Ascii::toLower() was used.
			while (it != end) { *it = tolower(*it); ++it; }
			return str;
		}
	private:
		std::string    _scheme;
		std::string    _userInfo;
		std::string    _host;
		unsigned short _port;
		std::string    _path;
		std::string    _query;
		std::string    _fragment;
		long           _flags;
	};

	inline const std::string& Url::scheme() const
	{
		return _scheme;
	}


	inline const std::string& Url::userInfo() const
	{
		return _userInfo;
	}


	inline const std::string& Url::host() const
	{
		return _host;
	}


	inline const std::string& Url::path() const
	{
		return _path;
	}


	inline const std::string& Url::rawQuery() const
	{
		return _query;
	}


	inline const std::string& Url::fragment() const
	{
		return _fragment;
	}


	inline void swap(Url& u1, Url& u2)
	{
		u1.swap(u2);
	}

	inline std::ostream& operator<<(std::ostream& os, const Url& obj) {
		os << obj.toString();
		return os;
	}

	inline std::istream& operator>>(std::istream& a_is, Url& a_obj) {
		std::string urlString;
		a_is >> urlString;
		a_obj = Url(urlString);
		return a_is;
	}

} // end namespace MV

  /*
  Code of this file is based on Poco library, file: URI.h.
  Original copyright is included below.

  //
  // URI.h
  //
  // $Id: //poco/1.4/Foundation/include/Poco/URI.h#1 $
  //
  // Library: Foundation
  // Package: URI
  // Module:  URI
  //
  // Definition of the URI class.
  //
  // Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
  // and Contributors.
  //
  // Permission is hereby granted, free of charge, to any person or organization
  // obtaining a copy of the software and accompanying documentation covered by
  // this license (the "Software") to use, reproduce, display, distribute,
  // execute, and transmit the Software, and to prepare derivative works of the
  // Software, and to permit third-parties to whom the Software is furnished to
  // do so, all subject to the following:
  //
  // The copyright notices in the Software and this entire statement, including
  // the above license grant, this restriction and the following disclaimer,
  // must be included in all copies of the Software, in whole or in part, and
  // all derivative works of the Software, unless such copies or derivative
  // works are solely in the form of machine-executable object code generated by
  // a source language processor.
  //
  // THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  // IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  // FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
  // SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
  // FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
  // ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  // DEALINGS IN THE SOFTWARE.
  //
  */

#endif
