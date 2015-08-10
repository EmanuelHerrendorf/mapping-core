/*
 * distribution.cpp
 *
 *  Created on: 08.06.2015
 *      Author: mika
 */

#include <gtest/gtest.h>
#include <vector>
#include "test/unittests/cache/util.h"
#include "util/make_unique.h"
#include "cache/index/indexserver.h"
#include "cache/node/nodeserver.h"
#include "cache/priv/transfer.h"
#include "cache/priv/redistribution.h"
#include "cache/client.h"
#include "cache/index/reorg_strategy.h"

typedef std::unique_ptr<std::thread> TP;

class TestIdxServer : public IndexServer {
public:
	void trigger_reorg( uint32_t node_id, const ReorgDescription &desc ) {
		for ( auto &cc : control_connections ) {
			if ( cc.second->node->id == node_id ) {
				cc.second->send_reorg(desc);
				return;
			}
		}
		throw ArgumentException(concat("No node found for id ", node_id));
	}

	TestIdxServer( uint32_t port, ReorgStrategy &strat ) : IndexServer(port,strat) {}
};

class TestNodeServer : public NodeServer {
public:
	TestNodeServer( std::string my_host, uint32_t my_port, std::string index_host, uint32_t index_port ) :
		NodeServer(my_host,my_port,index_host,index_port,1), rcm( 5 * 1024 * 1024, my_host, my_port ) {};
	bool owns_current_thread();
	RemoteCacheManager rcm;
	std::thread::id my_id;
};


class TestCacheMan : public CacheManager {
public:
	CacheManager& get_instance_mgr( int i ) {
		return instances.at(i)->rcm;
	}

	virtual std::unique_ptr<GenericRaster> query_raster( const GenericOperator &op, const QueryRectangle &rect ) {
		for ( auto i : instances ) {
			if ( i->owns_current_thread() )
				return i->rcm.query_raster(op,rect);
		}
		throw ArgumentException("Unregistered instance called cache-manager");
	}
	virtual std::unique_ptr<GenericRaster> get_raster_local( const NodeCacheKey &key ) {
		for ( auto i : instances ) {
				if ( i->owns_current_thread() )
					return i->rcm.get_raster_local(key);
		}
		throw ArgumentException("Unregistered instance called cache-manager");
	}
	virtual void put_raster( const std::string &semantic_id, const std::unique_ptr<GenericRaster> &raster ) {
		for ( auto i : instances ) {
				if ( i->owns_current_thread() )
					return i->rcm.put_raster(semantic_id, raster);
		}
		throw ArgumentException("Unregistered instance called cache-manager");
	}
	virtual NodeCacheRef put_raster_local( const std::string &semantic_id, const std::unique_ptr<GenericRaster> &raster ) {
		for ( auto i : instances ) {
				if ( i->owns_current_thread() )
					return i->rcm.put_raster_local(semantic_id, raster);
		}
		throw ArgumentException("Unregistered instance called cache-manager");
	}
	virtual void remove_raster_local( const NodeCacheKey &key ) {
		for ( auto i : instances ) {
				if ( i->owns_current_thread() )
					return i->rcm.remove_raster_local(key);
		}
		throw ArgumentException("Unregistered instance called cache-manager");
	}

	virtual Capacity get_local_capacity() {
		for ( auto i : instances ) {
				if ( i->owns_current_thread() )
					return i->rcm.get_local_capacity();
		}
		throw ArgumentException("Unregistered instance called cache-manager");
	}

	void add_instance( TestNodeServer *inst ) {
		instances.push_back( inst );
	}
private:
	std::vector<TestNodeServer*> instances;
};


bool TestNodeServer::owns_current_thread() {
	for ( auto &t : workers ) {
		if ( std::this_thread::get_id() == t->get_id() )
			return true;
	}
	return (delivery_thread != nullptr && std::this_thread::get_id() == delivery_thread->get_id()) ||
		    std::this_thread::get_id() == my_id;
}

void run_node_thread(TestNodeServer *ns) {
	ns->my_id = std::this_thread::get_id();
	ns->run();
}

TEST(DistributionTest,TestRedistibution) {

	CapacityReorgStrategy reorg;


	std::unique_ptr<TestCacheMan> cm = make_unique<TestCacheMan>();
	TestIdxServer is(12346,reorg);
	TestNodeServer    ns1( "localhost", 12347, "localhost", 12346 );
	TestNodeServer    ns2( "localhost", 12348, "localhost", 12346 );

	cm->add_instance(&ns1);
	cm->add_instance(&ns2);

	CacheManager::init( std::move(cm), make_unique<CacheAll>() );

	TestCacheMan &tcm = dynamic_cast<TestCacheMan&>(CacheManager::getInstance());

	std::vector<TP> ts;
	ts.push_back( make_unique<std::thread>(&IndexServer::run, &is) );
	std::this_thread::sleep_for( std::chrono::milliseconds(500));
	ts.push_back( make_unique<std::thread>(run_node_thread, &ns1) );
	std::this_thread::sleep_for( std::chrono::milliseconds(500));
	ts.push_back( make_unique<std::thread>(run_node_thread, &ns2) );
	std::this_thread::sleep_for( std::chrono::milliseconds(500));

	std::string bbox_str("1252344.2712499984,5009377.085000001,2504688.5424999986,6261721.356250001");
	std::string json = "{\"type\":\"projection\",\"params\":{\"src_projection\":\"EPSG:4326\",\"dest_projection\":\"EPSG:3857\"},\"sources\":{\"raster\":[{\"type\":\"source\",\"params\":{\"sourcename\":\"world1\",\"channel\":0}}]}}";
	std::string timestr("2010-06-06T18:00:00.000Z");
	epsg_t epsg = EPSG_WEBMERCATOR;
	uint32_t width = 256, height = 256;
	time_t timestamp = parseIso8601DateTime(timestr);
	double bbox[4];

	CacheClient cc("localhost",12346);

	parseBBOX(bbox, bbox_str, epsg, false);
	QueryRectangle qr(
		SpatialReference(epsg, bbox[0], bbox[1], bbox[2], bbox[3]),
		TemporalReference(TIMETYPE_UNIX, timestamp, timestamp),
		QueryResolution::pixels(width, height)
	);

	std::string sem_id = GenericOperator::fromJSON(json)->getSemanticId();

	//Should hit 1st node
	cc.get_raster(json,qr,GenericOperator::RasterQM::EXACT);

	NodeCacheKey key1(sem_id, 1);

	try {
		tcm.get_instance_mgr(0).get_raster_local(key1);
	} catch ( NoSuchElementException &nse ) {
		FAIL();
	}

	ReorgDescription rod;
	ReorgItem ri(ReorgResult::Type::RASTER, sem_id, 1, 1, "localhost", 12347);
	rod.add_item( ri );

	is.trigger_reorg(2, rod);

	std::this_thread::sleep_for( std::chrono::milliseconds(500) );

	// Assert moved
	try {
		tcm.get_instance_mgr(0).get_raster_local(key1);
		FAIL();
	} catch ( NoSuchElementException &nse ) {
	}

	try {
		tcm.get_instance_mgr(1).get_raster_local(key1);
	} catch ( NoSuchElementException &nse ) {
		FAIL();
	}


	ns2.stop();
	ns1.stop();
	is.stop();

	for ( TP &t : ts )
		t->join();
}


TEST(DistributionTest,TestRemoteNodeFetch) {
	CapacityReorgStrategy reorg;

	std::unique_ptr<TestCacheMan> cm = make_unique<TestCacheMan>();
	TestIdxServer     is(12346,reorg);
	TestNodeServer    ns1( "localhost", 12347, "localhost", 12346 );
	TestNodeServer    ns2( "localhost", 12348, "localhost", 12346 );

	cm->add_instance(&ns1);
	cm->add_instance(&ns2);

	CacheManager::init( std::move(cm), make_unique<CacheAll>() );


	std::vector<TP> ts;
	ts.push_back( make_unique<std::thread>(&IndexServer::run, &is) );
	std::this_thread::sleep_for( std::chrono::milliseconds(500));
	ts.push_back( make_unique<std::thread>(run_node_thread, &ns1) );
	std::this_thread::sleep_for( std::chrono::milliseconds(500));
	ts.push_back( make_unique<std::thread>(run_node_thread, &ns2) );
	std::this_thread::sleep_for( std::chrono::milliseconds(500));

	std::string bbox_str("1252344.2712499984,5009377.085000001,2504688.5424999986,6261721.356250001");
	std::string json = "{\"type\":\"projection\",\"params\":{\"src_projection\":\"EPSG:4326\",\"dest_projection\":\"EPSG:3857\"},\"sources\":{\"raster\":[{\"type\":\"source\",\"params\":{\"sourcename\":\"world1\",\"channel\":0}}]}}";
	std::string timestr("2010-06-06T18:00:00.000Z");
	epsg_t epsg = EPSG_WEBMERCATOR;
	uint32_t width = 256, height = 256;
	time_t timestamp = parseIso8601DateTime(timestr);
	double bbox[4];

	CacheClient cc("localhost",12346);

	parseBBOX(bbox, bbox_str, epsg, false);
	QueryRectangle qr(
		SpatialReference(epsg, bbox[0], bbox[1], bbox[2], bbox[3]),
		TemporalReference(TIMETYPE_UNIX, timestamp, timestamp),
		QueryResolution::pixels(width, height)
	);

	//Should hit 1st node
	cc.get_raster(json,qr,GenericOperator::RasterQM::EXACT);



	std::this_thread::sleep_for( std::chrono::milliseconds(500));

	//Should hit 2nd node
	cc.get_raster(json,qr,GenericOperator::RasterQM::EXACT);

	ns2.stop();
	ns1.stop();
	is.stop();

	for ( TP &t : ts )
		t->join();
}

TEST(DistributionTest,TestCapacityReorg) {

	CapacityReorgStrategy reorg;

	std::shared_ptr<Node> n1 = std::shared_ptr<Node>( new Node(1, "localhost", 42,   NodeStats(30)) );
	std::shared_ptr<Node> n2 = std::shared_ptr<Node>( new Node(2, "localhost", 4711, NodeStats(30)) );

	std::map<uint32_t, std::shared_ptr<Node>> nodes;
	nodes.emplace(1, n1);
	nodes.emplace(2, n2);



	IndexCache cache(reorg);

	// Entry 1
	NodeCacheKey     k1("key",1);
	CacheEntryBounds b1( SpatialReference(EPSG_LATLON, 0, 0, 45, 45), TemporalReference(TIMETYPE_UNIX,0,10) );
	CacheEntry       c1(b1,10);
	NodeCacheRef     r1(k1,c1);
	IndexCacheEntry  e1(1,r1);

	// Entry 2
	NodeCacheKey     k2("key",2);
	CacheEntryBounds b2( SpatialReference(EPSG_LATLON, 45, 0, 90, 45), TemporalReference(TIMETYPE_UNIX,0,10) );
	CacheEntry       c2(b2,10);
	NodeCacheRef     r2(k2,c2);
	IndexCacheEntry  e2(1,r2);

	cache.put(e1);
	cache.put(e2);


	ASSERT_TRUE( cache.requires_reorg(nodes) );
	auto res = cache.reorganize(nodes);

	ASSERT_TRUE( res.size() == 1 );
	ASSERT_TRUE( res.at(0).node_id == 2 );
	ASSERT_TRUE( res.at(0).get_items().size() == 1 );
	ASSERT_TRUE( res.at(0).get_items().at(0).from_cache_id == 1 );


}










