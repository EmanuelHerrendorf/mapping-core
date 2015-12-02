/*
 * common.h
 *
 *  Created on: 26.05.2015
 *      Author: mika
 */

#ifndef COMMON_H_
#define COMMON_H_

#include "util/binarystream.h"
#include "util/exceptions.h"
#include "util/log.h"
#include "operators/operator.h"
#include "datatypes/raster.h"
#include <memory>

#include <sstream>
#include <cstring>
#include <sys/select.h>
#include <errno.h>
#include <chrono>
#include <iostream>

//
// Provides helper functions for common tasks.
//

enum class CacheType : uint8_t { RASTER, POINT, LINE, POLYGON, PLOT, UNKNOWN };

class CacheCube;


class ExecTimer {
public:
	ExecTimer( std::string &&name );
	ExecTimer() = delete;
	ExecTimer( const ExecTimer& ) = delete;
	ExecTimer( ExecTimer&& ) = delete;
	ExecTimer& operator=(const ExecTimer& ) = delete;
	ExecTimer& operator=( ExecTimer&& ) = delete;
	~ExecTimer();
private:
	std::string name;
	std::chrono::time_point<std::chrono::system_clock> start;
};


class CacheCommon {
public:

	//
	// Creates a listening socket on the given port.
	//
	static int get_listening_socket(int port, bool nonblock = true, int backlog = 10);

	//
	// Returns a string-representation for the given query-rectange
	//
	static std::string qr_to_string( const QueryRectangle &rect );

	//
	// Returns a string-representation for the given spatio-temporal reference
	//
	static std::string stref_to_string( const SpatioTemporalReference &ref );

	//
	// Returns a string-representation of the given raster
	//
	static std::string raster_to_string( const GenericRaster &raster );

	static void set_uncaught_exception_handler();

	static std::string get_stacktrace();

	static bool resolution_matches( const GridSpatioTemporalResult &r1, const GridSpatioTemporalResult &r2 );

	static bool resolution_matches( const CacheCube &c1, const CacheCube &c2 );

	static bool resolution_matches( double scalex1, double scaley1, double scalex2, double scaley2 );

	//
	// Helper to read from a stream with a given timeout. Basically wraps
	// BinaryStream::read(T*,bool).
	// If the timeout is reached, a TimeoutException is thrown. If select()
	// gets interrupted an InterruptedException is thrown. Both are not
	// harmful to the underlying connection.
	// On a harmful error, a NetworkException is thrown.
	//
	template<typename T>
	static size_t read(T *t, UnixSocket &sock, int timeout, bool allow_eof = false) {
		struct timeval tv { timeout, 0 };
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(sock.getReadFD(), &readfds);
		BinaryStream &stream = sock;

		int ret = select(sock.getReadFD()+1, &readfds, nullptr, nullptr, &tv);
		if ( ret > 0 )
			return stream.read(t,allow_eof);
		else if ( ret == 0 )
			throw TimeoutException("No data available");
		else if ( errno == EINTR )
			throw InterruptedException("Select interrupted");
		else {
			throw NetworkException(concat("UnixSocket: read() failed: ", strerror(errno)));
		}
	}

private:
	CacheCommon() {};
	~CacheCommon() {};
};

#endif /* COMMON_H_ */
