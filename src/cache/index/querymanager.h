/*
 * querymanager.h
 *
 *  Created on: 10.07.2015
 *      Author: mika
 */

#ifndef QUERYMANAGER_H_
#define QUERYMANAGER_H_

#include "cache/index/index_cache_manager.h"
#include "cache/index/index_config.h"
#include "cache/priv/shared.h"
#include "cache/priv/connection.h"

#include <string>
#include <unordered_map>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <unordered_set>

class QueryManager;


/**
 * Models a query currently executed by a worker
 */
class RunningQuery {
	friend class QueryManager;
	friend class IndexQueryStats;
	friend class PendingQuery;

	static uint64_t next_id;

public:
	/**
	 * Creates a new instance and sets the given locks
	 * @param locks the locks to obtain
	 */
	RunningQuery();
	virtual ~RunningQuery();

	RunningQuery( const RunningQuery& ) = delete;
	RunningQuery( RunningQuery&& ) = delete;

	RunningQuery& operator=(const RunningQuery&) = delete;
	RunningQuery& operator=(RunningQuery&&) = delete;

	/**
	 * @return whether this query satisfies the given request
	 */
	bool satisfies( const BaseRequest &req ) const;

	/**
	 * Adds the given client as a consumer of this query's result
	 * @param client the id of the client-connection to add
	 */
	void add_client( uint64_t client );

	/**
	 * Adds the given clients as a consumer of this query's result
	 * @param clients the ids of the client-connections to add
	 */
	void add_clients( const std::set<uint64_t> &clients );

	/**
	 * Removes the given client as a consumer of this query's result
	 * @param client the id of the client-connection to remove
	 */
	bool remove_client(uint64_t client_id);

	/**
	 * @return the ids of the client-connections, consuming this query's result
	 */
	const std::set<uint64_t>& get_clients() const;

	/**
	 * @return whether this query has consuming clients
	 */
	bool has_clients() const;

	virtual const BaseRequest& get_request() const = 0;

	const uint64_t id;
private:
	std::set<uint64_t> clients;
	uint64_t time_created;
	uint64_t time_scheduled;
	uint64_t time_finished;
};

/**
 * Models a query queued for execution
 */
class PendingQuery : public RunningQuery {
public:
	/**
	 * Creates a new instance and sets the given locks
	 * @param locks the locks to obtain
	 */
	PendingQuery();
	virtual ~PendingQuery() = default;
	PendingQuery( const PendingQuery& ) = delete;
	PendingQuery( PendingQuery&& ) = default;

	PendingQuery& operator=(const PendingQuery&) = delete;
	PendingQuery& operator=(PendingQuery&&) = default;

	/**
	 * Extends this queries result dimension to satisfy the given request
	 * @param req the request to satisfy
	 * @return whether this query was extended
	 */
	virtual bool extend( const BaseRequest &req ) = 0;

	virtual uint64_t submit(const std::map<uint32_t, std::shared_ptr<Node>> &nmap) = 0;

	/**
	 * @return whether this query depends on the node with the given id (e.g. references a cache-entry)
	 */
	virtual bool is_affected_by_node( uint32_t node_id ) = 0;
};

/**
 * The query-manager manages all pending and running queries
 */
class QueryManager {
private:
	friend class RunningQuery;
	friend class PendingQuery;
	friend class CreateJob;
	friend class DefaultQueryManager;
	friend class LateQueryManager;
public:
	static std::unique_ptr<QueryManager> from_config( IndexCacheManager &mgr, const std::map<uint32_t,std::shared_ptr<Node>> &nodes, const IndexConfig &config );

	virtual ~QueryManager() = default;

	virtual bool use_reorg() const = 0;

	/**
	 * Creates a new instance
	 * @param caches the available cache
	 * @param nodes a reference to the attached nodes
	 */
	QueryManager(const std::map<uint32_t,std::shared_ptr<Node>> &nodes);

	/**
	 * Adds a new client-request to the processing pipeline. The manager
	 * checks all running and pending queries, if their result might be
	 * used to answer the new query. If not a new job is queued for execution.
	 * @param client_id the connection-id of the client issued this query
	 * @param req the query-spec
	 */
	virtual void add_request( uint64_t client_id, const BaseRequest &req ) = 0;

	/**
	 * Processes cache-requests from workers.
	 * @param con the worker-connection issued the cache-query
	 */
	virtual void process_worker_query(WorkerConnection& con) = 0;

	/**
	 * Schedules the jobs waiting for exectuion, according to their
	 * preferred node.
	 * @param worker_connections the currently available worker-connections
	 */
	virtual void schedule_pending_jobs();

	/**
	 * Handle if a worker failed (e.g. the connection was lost).
	 * If a query was currently exectued, it is rescheduled on a different worker
	 * @param worker_id the id of the worker that failed
	 */
	void worker_failed( uint64_t worker_id );

	/**
	 * Handle if a whole node went away. All queries referencing
	 * entries on the node or should be scheduled on it, are
	 * re-analyzed and executed on a different node.
	 * @param node_id the id of the node that failed
	 */
	void node_failed( const Node &node );

	/**
	 * Invoked after the computation of a result is finished.
	 * After this call no queries may be attached to this job.
	 * @param worker_id the id of the worker
	 */
	size_t close_worker( uint64_t worker_id );

	/**
	 * Releases the worker with the given id and returns the clients
	 * consuming the produced result. close_worker must have been invoked
	 * before calling this method
	 * @param worker_id the id of the worker
	 */
	std::set<uint64_t> release_worker( uint64_t worker_id, uint32_t node_id );

	/**
	 * Handles cancelled client requests. If there are no other clients
	 * waiting for the result of the requested query, the query is cancelled.
	 * @param client_id the id of the disconnected client
	 */
	void handle_client_abort( uint64_t client_id );

	/**
	 * @return the query-statistics
	 */
	SystemStats& get_stats();

	/**
	 * Resets the query-statistics
	 */
	void reset_stats();
protected:
	void add_query( std::unique_ptr<PendingQuery> query );

	/**
	 * Re-schedules a job after a worker-/node-failure
	 * @param query the query to reschedule
	 * @return a ready-to-schedule job satisfying the failed query
	 */
	virtual std::unique_ptr<PendingQuery> recreate_job( const RunningQuery &query ) = 0;

	const std::map<uint32_t,std::shared_ptr<Node>> &nodes;
	SystemStats stats;
private:
	std::unordered_map<uint64_t,std::unique_ptr<RunningQuery>> queries;
	std::unordered_map<uint64_t,std::unique_ptr<RunningQuery>> finished_queries;
	std::unordered_map<uint64_t,std::unique_ptr<PendingQuery>> pending_jobs;

};

#endif /* QUERYMANAGER_H_ */
