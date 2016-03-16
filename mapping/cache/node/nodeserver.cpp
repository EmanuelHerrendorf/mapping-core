/*
 * cacheserver.cpp
 *
 *  Created on: 11.05.2015
 *      Author: mika
 */

// project-stuff
#include "cache/node/node_manager.h"
#include "cache/node/nodeserver.h"
#include "cache/node/delivery.h"
#include "cache/priv/connection.h"

#include "datatypes/raster.h"
#include "datatypes/pointcollection.h"
#include "datatypes/linecollection.h"
#include "datatypes/polygoncollection.h"
#include "datatypes/plot.h"

#include "util/exceptions.h"
#include "util/make_unique.h"
#include "util/log.h"

#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>

////////////////////////////////////////////////////////////
//
// NODE SERVER
//
////////////////////////////////////////////////////////////

NodeServer::NodeServer(std::unique_ptr<NodeCacheManager> manager, uint32_t my_port, std::string index_host, uint32_t index_port,
	int num_threads) :
	shutdown(false), workers_up(false), my_id(-1), my_port(my_port), index_host(index_host), index_port(index_port),
	num_treads(num_threads), delivery_manager(my_port,*manager), manager(std::move(manager) ) {
	this->manager->set_self_port(my_port);
}

void NodeServer::worker_loop() {
	while (workers_up && !shutdown) {
		try {
			// Setup index connection
			auto idx_con = BlockingConnection::create(index_host,index_port,true,WorkerConnection::MAGIC_NUMBER,my_id);
			manager->get_worker_context().set_index_connection(idx_con.get());

			Log::debug("Worker connected to index-server");

			while (workers_up && !shutdown) {
				try {
					auto res = idx_con->read_timeout(2);
					process_worker_command(*idx_con, *res);
				} catch (const TimeoutException &te) {
					//Log::trace("Read on worker-connection timed out. Trying again");
				} catch (const InterruptedException &ie) {
					Log::info("Read on worker-connection interrupted. Trying again.");
				} catch (const NetworkException &ne) {
					// Re-throw network-error to outer catch.
					throw;
				} catch (const std::exception &e) {
					Log::error("Unexpected error while processing request: %s", e.what());
					auto msg = concat("Unexpected error while processing request: ", e.what());
					idx_con->write(WorkerConnection::RESP_ERROR, msg);
				}
			}
		} catch (const NetworkException &ne) {
			Log::info("Worker lost connection to index... Reconnecting. Reason: %s", ne.what());
		}
		std::this_thread::sleep_for(std::chrono::seconds(2));
	}
	Log::info("Worker done.");
}

void NodeServer::process_worker_command(BlockingConnection &index_con, BinaryReadBuffer &payload) {
	TIME_EXEC("RequestProcessing");
	uint8_t cmd = payload.read<uint8_t>();
	Log::debug("Processing command: %d", cmd);
	switch (cmd) {
		case WorkerConnection::CMD_CREATE: {
			BaseRequest cr(payload);
			Log::debug("Processing create-request: %s", cr.to_string().c_str());
			process_create_request(index_con,cr);
			break;
		}
		case WorkerConnection::CMD_PUZZLE: {
			PuzzleRequest pr(payload);
			Log::debug("Processing puzzle-request: %s", pr.to_string().c_str());
			process_puzzle_request(index_con,pr);
			break;
		}
		case WorkerConnection::CMD_DELIVER: {
			DeliveryRequest dr(payload);
			Log::debug("Processing delivery-request: %s", dr.to_string().c_str());
			process_delivery_request(index_con,dr);
			break;
		}
		default: {
			Log::error("Unknown command from index-server: %d. Dropping connection.", cmd);
			throw NetworkException(concat("Unknown command from index-server", cmd));
		}
	}
	Log::debug("Finished processing command: %d", cmd);
}

void NodeServer::process_create_request(BlockingConnection &index_con,
		const BaseRequest& request) {
	TIME_EXEC("RequestProcessing.create");
	auto op = GenericOperator::fromJSON(request.semantic_id);

	QueryProfiler profiler;
	switch ( request.type ) {
		case CacheType::RASTER: {
			auto res = op->getCachedRaster( request.query, profiler );
			finish_request( index_con, std::shared_ptr<const GenericRaster>(res.release()) );
			break;
		}
		case CacheType::POINT: {
			auto res = op->getCachedPointCollection( request.query, profiler );
			finish_request( index_con, std::shared_ptr<const PointCollection>(res.release()) );
			break;
		}
		case CacheType::LINE: {
			auto res = op->getCachedLineCollection(request.query, profiler );
			finish_request( index_con, std::shared_ptr<const LineCollection>(res.release()) );
			break;
		}
		case CacheType::POLYGON: {
			auto res = op->getCachedPolygonCollection(request.query, profiler );
			finish_request( index_con, std::shared_ptr<const PolygonCollection>(res.release()) );
			break;
		}
		case CacheType::PLOT: {
			auto res = op->getCachedPlot(request.query, profiler );
			finish_request( index_con, std::shared_ptr<const GenericPlot>(res.release()) );
			break;
		}
		default:
			throw ArgumentException(concat("Type ", (int) request.type, " not supported yet"));
	}
}

void NodeServer::process_puzzle_request(BlockingConnection &index_con,
		const PuzzleRequest& request) {
	TIME_EXEC("RequestProcessing.puzzle");
	QueryProfiler qp;
	switch ( request.type ) {
		case CacheType::RASTER: {
			auto res = manager->get_raster_cache().process_puzzle(request,qp);
			finish_request( index_con, std::shared_ptr<const GenericRaster>(res.release()) );
			break;
		}
		case CacheType::POINT: {
			auto res = manager->get_point_cache().process_puzzle(request,qp);
			finish_request( index_con, std::shared_ptr<const PointCollection>(res.release()) );
			break;
		}
		case CacheType::LINE: {
			auto res = manager->get_line_cache().process_puzzle(request,qp);
			finish_request( index_con, std::shared_ptr<const LineCollection>(res.release()) );
			break;
		}
		case CacheType::POLYGON: {
			auto res = manager->get_polygon_cache().process_puzzle(request,qp);
			finish_request( index_con, std::shared_ptr<const PolygonCollection>(res.release()) );
			break;
		}
		case CacheType::PLOT: {
			auto res = manager->get_plot_cache().process_puzzle(request,qp);
			finish_request( index_con, std::shared_ptr<const GenericPlot>(res.release()) );
			break;
		}
		default:
			throw ArgumentException(concat("Type ", (int) request.type, " not supported yet"));
	}
}

void NodeServer::process_delivery_request(BlockingConnection &index_con,
		const DeliveryRequest& request) {
	TIME_EXEC("RequestProcessing.delivery");
	NodeCacheKey key(request.semantic_id,request.entry_id);

	switch ( request.type ) {
		case CacheType::RASTER:
			finish_request( index_con, manager->get_raster_cache().get(key)->data );
			break;
		case CacheType::POINT:
			finish_request( index_con, manager->get_point_cache().get(key)->data );
			break;
		case CacheType::LINE:
			finish_request( index_con, manager->get_line_cache().get(key)->data );
			break;
		case CacheType::POLYGON:
			finish_request( index_con, manager->get_polygon_cache().get(key)->data );
			break;
		case CacheType::PLOT:
			finish_request( index_con, manager->get_plot_cache().get(key)->data );
			break;
		default:
			throw ArgumentException(concat("Type ", (int) request.type, " not supported yet"));
	}
}


template<typename T>
void NodeServer::finish_request(BlockingConnection& index_stream,
		const std::shared_ptr<const T>& item) {
	TIME_EXEC("RequestProcessing.finish");

	Log::debug("Processing request finished. Asking for delivery-qty");

	auto resp = index_stream.write_and_read(WorkerConnection::RESP_RESULT_READY);
	uint8_t cmd_qty = resp->read<uint8_t>();

	if (cmd_qty != WorkerConnection::RESP_DELIVERY_QTY)
		throw ArgumentException(
			concat("Expected command ", WorkerConnection::RESP_DELIVERY_QTY, " but received ", cmd_qty));

	uint32_t qty = resp->read<uint32_t>();
	uint64_t delivery_id = delivery_manager.add_delivery(item, qty);

	Log::debug("Sending delivery_id.");
	index_stream.write(WorkerConnection::RESP_DELIVERY_READY, delivery_id);
}

void NodeServer::run() {
	Log::info("Starting Node-Server");

	delivery_thread = delivery_manager.run_async();

	while (!shutdown) {
		try {
			setup_control_connection();

			workers_up = true;
			for (int i = 0; i < num_treads; i++)
				workers.push_back(make_unique<std::thread>(&NodeServer::worker_loop, this));

			// Read on control
			while (!shutdown) {
				try {
					auto res = control_connection->read_timeout(2);
					process_control_command(*res);
				} catch (const TimeoutException &te) {
				} catch (const InterruptedException &ie) {
					Log::info("Interrupt on read from control-connection.");
				} catch (const NetworkException &ne) {
					Log::error("Error reading on control-connection. Reconnecting");
					break;
				}
			}
			workers_up = false;
			Log::debug("Waiting for worker-threads to terminate.");
			for (auto &w : workers) {
				w->join();
			}
			workers.clear();
		} catch (const NetworkException &ne_c) {
			Log::warn("Could not connect to index-server. Retrying in 5s. Reason: %s", ne_c.what());
			std::this_thread::sleep_for(std::chrono::seconds(5));
		}
	}
	delivery_manager.stop();
	delivery_thread->join();
	Log::info("Node-Server done.");
}

//
// Constrol connection
//

void NodeServer::process_control_command(BinaryReadBuffer &payload) {
	uint8_t cmd = payload.read<uint8_t>();
	switch (cmd) {
		case ControlConnection::CMD_REORG: {
			Log::debug("Received reorg command.");
			ReorgDescription d(payload);
			for (auto &rem_item : d.get_removals()) {
				handle_reorg_remove_item(rem_item);
			}
			for (auto &move_item : d.get_moves()) {
				handle_reorg_move_item(move_item);
			}
			control_connection->write(ControlConnection::RESP_REORG_DONE);
			break;
		}
		case ControlConnection::CMD_GET_STATS: {
			Log::debug("Received stats-request.");
			NodeStats stats = manager->get_stats_delta();
			control_connection->write(ControlConnection::RESP_STATS,stats);
			break;
		}
		default: {
			Log::error("Unknown control-command from index-server: %d. Dropping control-connection.", cmd);
			throw NetworkException("Unknown control-command from index-server");
		}
	}
}

void NodeServer::handle_reorg_remove_item( const TypedNodeCacheKey &item ) {
	Log::debug("Removing item from cache. Key: %s", item.to_string().c_str() );

	auto resp = control_connection->write_and_read(ControlConnection::RESP_REORG_REMOVE_REQUEST, item);
	uint8_t rc = resp->read<uint8_t>();

	if ( rc == ControlConnection::CMD_REMOVE_OK ) {
		switch (item.type) {
			case CacheType::RASTER:
				manager->get_raster_cache().remove_local(item);
				break;
			case CacheType::POINT:
				manager->get_point_cache().remove_local(item);
				break;
			case CacheType::LINE:
				manager->get_line_cache().remove_local(item);
				break;
			case CacheType::POLYGON:
				manager->get_polygon_cache().remove_local(item);
				break;
			case CacheType::PLOT:
				manager->get_plot_cache().remove_local(item);
				break;
			default:
				throw ArgumentException(concat("Type ", (int) item.type, " not supported yet"));
		}
	}
	else {
		Log::error("Index did not confirm removal. Skipping. Response was: %d", rc);
	}
}

void NodeServer::handle_reorg_move_item( const ReorgMoveItem& item ) {
	uint64_t new_cache_id;

	Log::debug("Moving item from node %d to node %d. Key: %s:%d ", item.from_node_id, my_id,
		item.semantic_id.c_str(), item.entry_id);


	// Send move request
	try {
		auto del_con = BlockingConnection::create(item.from_host,item.from_port, true, DeliveryConnection::MAGIC_NUMBER);
		auto resp = del_con->write_and_read(DeliveryConnection::CMD_MOVE_ITEM,TypedNodeCacheKey(item));
		uint8_t del_resp = resp->read<uint8_t>();

		switch (del_resp) {
			case DeliveryConnection::RESP_OK: {
				CacheEntry ce(*resp);
				switch (item.type) {
					case CacheType::RASTER:
						new_cache_id = manager->get_raster_cache().put_local(
							item.semantic_id, GenericRaster::deserialize(*resp), std::move(ce)).entry_id;
						break;
					case CacheType::POINT:
						new_cache_id = manager->get_point_cache().put_local(
							item.semantic_id, make_unique<PointCollection>(*resp), std::move(ce)).entry_id;
						break;
					case CacheType::LINE:
						new_cache_id = manager->get_line_cache().put_local(
							item.semantic_id, make_unique<LineCollection>(*resp), std::move(ce)).entry_id;
						break;
					case CacheType::POLYGON:
						new_cache_id = manager->get_polygon_cache().put_local(
							item.semantic_id, make_unique<PolygonCollection>(*resp), std::move(ce)).entry_id;
						break;
					case CacheType::PLOT:
						new_cache_id = manager->get_plot_cache().put_local(
							item.semantic_id, GenericPlot::deserialize(*resp), std::move(ce)).entry_id;
						break;
					default:
						throw ArgumentException(concat("Type ", (int) item.type, " not supported yet"));
				}
				break;
			}
			case DeliveryConnection::RESP_ERROR: {
				std::string msg = resp->read<std::string>();
				throw NetworkException(
					concat("Could not move item", item.semantic_id, ":", item.entry_id, " from ",
						item.from_host, ":", item.from_port, ": ", msg));
			}
			default:
				throw NetworkException(concat("Received illegal response from delivery-node: ", del_resp));
		}
		confirm_move(*del_con, item, new_cache_id);
	} catch (const NetworkException &ne) {
		Log::error("Could not process move: %s", ne.what());
		return;
	}
}

void NodeServer::confirm_move(BlockingConnection &del_stream, const ReorgMoveItem& item, uint64_t new_id) {
	// Notify index
	ReorgMoveResult rr(item.type, item.semantic_id, item.from_node_id, item.entry_id, my_id, new_id);
	auto iresp = control_connection->write_and_read(ControlConnection::RESP_REORG_ITEM_MOVED,rr);

	try {
		uint8_t rc = iresp->read<uint8_t>();
		if ( rc == ControlConnection::CMD_MOVE_OK ) {
			Log::debug("Reorg of item finished. Notifying delivery instance.");
			del_stream.write(DeliveryConnection::CMD_MOVE_DONE);
		}
		else {
			Log::warn("Index could not handle reorg of: %s:%d", item.semantic_id.c_str(), item.entry_id);
			switch ( item.type ) {
			case CacheType::RASTER: manager->get_raster_cache().remove_local(item); break;
			case CacheType::POINT: manager->get_point_cache().remove_local(item); break;
			case CacheType::LINE: manager->get_line_cache().remove_local(item); break;
			case CacheType::POLYGON: manager->get_polygon_cache().remove_local(item); break;
			case CacheType::PLOT: manager->get_plot_cache().remove_local(item); break;
			default: throw ArgumentException(concat("Type ", (int) item.type, " not supported yet"));
			}
		}
	} catch (const NetworkException &ne) {
		Log::error("Could not confirm reorg of raster-item to delivery instance.");
	}
}

void NodeServer::setup_control_connection() {
	Log::info("Connecting to index-server: %s:%d", index_host.c_str(), index_port);

	// Establish connection
	NodeHandshake hs = manager->create_handshake();
	this->control_connection = BlockingConnection::create(index_host,index_port,true,ControlConnection::MAGIC_NUMBER,hs);


	Log::debug("Waiting for response from index-server");
	// Read node-id
	auto resp = control_connection->read();

	uint8_t rc = resp->read<uint8_t>();
	if (rc == ControlConnection::CMD_HELLO) {
		resp->read(&my_id);
		manager->set_self_host(resp->read<std::string>());
		Log::info("Successfuly connected to index-server. My Id is: %d", my_id);
	}
	else
		throw NetworkException(concat("Index returned unknown response-code to: ", rc));
}

std::unique_ptr<std::thread> NodeServer::run_async() {
	return make_unique<std::thread>(&NodeServer::run, this);
}

void NodeServer::stop() {
	Log::info("Node-server shutting down.");
	shutdown = true;
}

NodeServer::~NodeServer() {
	stop();
}
