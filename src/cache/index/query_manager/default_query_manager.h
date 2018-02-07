/*
 * DefaultQueryManager.h
 *
 *  Created on: 24.05.2016
 *      Author: koerberm
 */

#ifndef CACHE_INDEX_QUERY_MANAGER_HYBRID_QUERY_MANAGER_H_
#define CACHE_INDEX_QUERY_MANAGER_HYBRID_QUERY_MANAGER_H_

#include "cache/index/index_cache_manager.h"
#include "cache/index/querymanager.h"
#include "cache/priv/shared.h"
#include "cache/priv/connection.h"

#include <string>
#include <unordered_map>
#include <map>
#include <list>
#include <vector>
#include <set>

class DefaultQueryManager;

/**
 * Describes a query where the whole result must be computed
 */
class CreateJob : public PendingQuery {
public:
	/**
	 * Creates a new instance
	 * @param request the request issued by the client
	 * @param nodes the currently available nodes
	 * @param cache the cache for this type of request
	 */
	CreateJob( BaseRequest &&request, const DefaultQueryManager &mgr );

	bool extend( const BaseRequest &req );
	bool is_affected_by_node( uint32_t node_id );
	uint64_t submit(const std::map<uint32_t, std::shared_ptr<Node>> &nmap);
	const BaseRequest& get_request() const;
private:
	BaseRequest request;
	const QueryRectangle orig_query;
	const double max_volume;
	const DefaultQueryManager &mgr;
};

/**
 * Models a query which can be completely answered from a single cache-entry.
 */
class DeliverJob : public PendingQuery {
public:
	DeliverJob( DeliveryRequest &&request, const IndexCacheKey &key );
	bool extend( const BaseRequest &req );
	bool is_affected_by_node( uint32_t node_id );
	uint64_t submit(const std::map<uint32_t, std::shared_ptr<Node>> &nmap);
	const BaseRequest& get_request() const;
private:
	DeliveryRequest request;
	uint32_t node;
};

/**
 * Models a query which' result is a combination
 * of more than one cache-entry or where some remainders must be computed.
 */
class PuzzleJob : public PendingQuery {
public:
	PuzzleJob( PuzzleRequest &&request, const std::vector<IndexCacheKey> &keys );
	bool extend( const BaseRequest &req );
	bool is_affected_by_node( uint32_t node_id );
	uint64_t submit(const std::map<uint32_t, std::shared_ptr<Node>> &nmap);
	const BaseRequest& get_request() const;
private:
	PuzzleRequest request;
	std::vector<IndexCacheKey> keys;
	std::vector<uint32_t> nodes_priorized;
	std::set<uint32_t> nodes;
};


/**
 * The query-manager manages all pending and running queries
 */
class DefaultQueryManager : public QueryManager {
	friend class CreateJob;
public:
	DefaultQueryManager(const std::map<uint32_t,std::shared_ptr<Node>> &nodes,IndexCacheManager &caches, bool enable_batching);
	void add_request( uint64_t client_id, const BaseRequest &req );
	void process_worker_query(WorkerConnection& con);
	bool use_reorg() const;
protected:
	std::unique_ptr<PendingQuery> recreate_job( const RunningQuery &query );
private:
	IndexCacheManager &caches;
	bool enable_batching;

	/**
	 * Creates a new job based on the given request and cache-query result
	 * @param req the request
	 * @param res the result of the cache-query
	 * @return a ready-to-schedule job satisfying the given request
	 */
	std::unique_ptr<PendingQuery> create_job(const BaseRequest &req, const CacheQueryResult<IndexCacheEntry>& res );
};


#endif /* CACHE_INDEX_QUERY_MANAGER_HYBRID_QUERY_MANAGER_H_ */
