/*
 * util.h
 *
 *  Created on: 03.12.2015
 *      Author: mika
 */

#ifndef CACHE_HYBRID_MANAGER_H_
#define CACHE_HYBRID_MANAGER_H_

#include "cache/node/node_manager.h"
#include "cache/priv/connection.h"
#include "cache/node/puzzle_util.h"

#include <string>
#include <memory>


class HybridCacheManager;

/**
 * A cache-wrapper extended with the needs of a cache-node.
 */
template<typename T>
class HybridCacheWrapper : public NodeCacheWrapper<T> {
public:
	/**
	 * Constructs a new instance
	 * @param mgr the parent manager
	 * @param cache the cache to wrap
	 * @param puzzler the puzzler-instance
	 */
	HybridCacheWrapper( HybridCacheManager &mgr, size_t size, CacheType type );
	virtual ~HybridCacheWrapper() = default;

	bool put(const std::string &semantic_id, const std::unique_ptr<T> &item, const QueryRectangle &query, const QueryProfiler &profiler);
	std::unique_ptr<T> query(GenericOperator &op, const QueryRectangle &rect, QueryProfiler &profiler);
	std::unique_ptr<T> process_puzzle( const PuzzleRequest& request, QueryProfiler &parent_profiler );
	MetaCacheEntry put_local(const std::string &semantic_id, const std::unique_ptr<T> &item, CacheEntry &&info );
	void remove_local(const NodeCacheKey &key);
private:
	HybridCacheManager &mgr;
};

class HybridCacheManager : public NodeCacheManager {
public:

	/**
	 * Constructs a new instance
	 * @param strategy the caching strategy to use
	 * @param raster_cache_size the maximum size of the raster cache (in bytes)
	 * @param point_cache_size the maximum size of the point cache (in bytes)
	 * @param line_cache_size the maximum size of the line cache (in bytes)
	 * @param polygon_cache_size the maximum size of the polygon cache (in bytes)
	 * @param plot_cache_size the maximum size of the plot cache (in bytes)
	 */
	HybridCacheManager( const std::string &strategy,
			size_t raster_cache_size, size_t point_cache_size, size_t line_cache_size,
			size_t polygon_cache_size, size_t plot_cache_size, size_t provenance_cache_size );
};


#endif /* CACHE_HYBRID_MANAGER_H_ */
