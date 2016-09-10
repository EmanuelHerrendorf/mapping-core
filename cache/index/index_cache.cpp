/*
 * index_cache.cpp
 *
 *  Created on: 07.08.2015
 *      Author: mika
 */

#include "cache/index/index_cache.h"
#include "cache/index/reorg_strategy.h"
#include "util/exceptions.h"
#include "util/log.h"

//////////////////////////////////////////////////////////////////
//
// IndexCacheKey
//
//////////////////////////////////////////////////////////////////

IndexCacheKey::IndexCacheKey(const std::string &semantic_id, const std::pair<uint32_t,uint64_t> &id) :
	semantic_id(semantic_id), id(id){
}

IndexCacheKey::IndexCacheKey(const std::string &semantic_id, uint32_t node_id, uint64_t entry_id) :
	semantic_id(semantic_id), id(node_id,entry_id) {
}

bool IndexCacheKey::operator <(const IndexCacheKey& l) const {
	return (id.first <  l.id.first) ||
		   (id.first == l.id.first && id.second <  l.id.second) ||
		   (id.first == l.id.first && id.second == l.id.second && semantic_id < l.semantic_id);
}

bool IndexCacheKey::operator ==(const IndexCacheKey& l) const {
	return id == l.id &&
		   semantic_id == l.semantic_id;
}

uint32_t IndexCacheKey::get_node_id() const {
	return id.first;
}

uint64_t IndexCacheKey::get_entry_id() const {
	return id.second;
}

std::string IndexCacheKey::to_string() const {
	return concat( "NodeCacheKey[ semantic_id: ", semantic_id, ", node_id: ", id.first, ", entry_id: ", id.second, "]");
}

//
// Index entry
//
IndexCacheEntry::IndexCacheEntry(const std::string &semantic_id, uint32_t node_id, uint64_t entry_id, const CacheEntry &ref) :
	CacheEntry(ref), semantic_id(semantic_id), id(node_id,entry_id) {
}

uint32_t IndexCacheEntry::get_node_id() const {
	return id.first;
}

uint64_t IndexCacheEntry::get_entry_id() const {
	return id.second;
}

//////////////////////////////////////////////////////////////////
//
// INDEX-CACHE
//
//////////////////////////////////////////////////////////////////

IndexCache::IndexCache( CacheType type ) : type(type) {
}


void IndexCache::put( const std::string &semantic_id, uint32_t node_id, uint64_t entry_id, const CacheEntry& entry ) {
	auto res = semantic_ids.emplace(semantic_id);

	std::shared_ptr<IndexCacheEntry> item( new IndexCacheEntry(*res.first,node_id,entry_id,entry) );
//	std::shared_ptr<IndexCacheEntry> item( new IndexCacheEntry(semantic_id,node_id,entry_id,entry) );

	this->put_int(semantic_id,item->id,item);
	get_node_entries(item->get_node_id()).insert(item);
}

std::shared_ptr<const IndexCacheEntry> IndexCache::get(const IndexCacheKey& key) const {
	return this->get_int(key.semantic_id,key.id);
}

void IndexCache::remove(const IndexCacheKey& key) {
	try {
		auto e = this->remove_int(key.semantic_id,key.id);
		remove_from_node(e);
	} catch ( const NoSuchElementException &nse ) {
		Log::warn("Removal of index-entry failed: %s", nse.what());
	}
}

void IndexCache::move(const IndexCacheKey& old_key, const IndexCacheKey& new_key) {
	auto entry = this->remove_int(old_key.semantic_id,old_key.id);
	remove_from_node(entry);
	entry->id = new_key.id;
	put_int(new_key.semantic_id,new_key.id,entry);
	get_node_entries(new_key.get_node_id()).insert(entry);
}

void IndexCache::remove_all_by_node(uint32_t node_id) {
	auto entries = get_node_entries(node_id);
	for ( auto &key : entries ) {
		this->remove_int(key->semantic_id,key->id);
	}
	entries_by_node.erase(node_id);
}

std::vector<std::shared_ptr<const IndexCacheEntry> > IndexCache::get_all() const {
	std::vector<std::shared_ptr<const IndexCacheEntry>> result;
	size_t size = 0;
	for ( auto &p : entries_by_node )
		size += p.second.size();

	result.reserve(size);
	for ( auto &p : entries_by_node )
		result.insert(result.end(), p.second.begin(), p.second.end());

	return result;
}

std::set<std::shared_ptr<const IndexCacheEntry>>& IndexCache::get_node_entries(uint32_t node_id) const {
	try {
		return entries_by_node.at(node_id);
	} catch ( const std::out_of_range &oor ) {
		return entries_by_node.emplace( node_id, std::set<std::shared_ptr<const IndexCacheEntry>>() ).first->second;
	}
}

void IndexCache::remove_from_node(const std::shared_ptr<IndexCacheEntry> &e ) {
	if ( get_node_entries(e->get_node_id()).erase(e) != 1 )
		throw NoSuchElementException("Entry not found in node-list.");
}

void IndexCache::update_stats(uint32_t node_id, const CacheStats &stats) {
	for ( auto &kv : stats.get_items() ) {
		std::pair<uint32_t,uint64_t> id(node_id,0);
		for ( auto &s : kv.second ) {
			id.second = s.entry_id;
			try {
				auto e = get_int(kv.first,id);
				e->access_count = s.access_count;
				e->last_access = s.last_access;
			}
			catch ( const NoSuchElementException &nse ) {
				// Nothing TO DO
			}
		}
	}
}
