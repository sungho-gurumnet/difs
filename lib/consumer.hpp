#ifndef DIFS_CONSUMER_HPP
#define DIFS_CONSUMER_HPP

#include "manifest/manifest.hpp"

#include <iostream>
#include <map> 
#include <tuple>

#include <ndn-cxx/encoding/block.hpp>
#include <ndn-cxx/security/command-interest-signer.hpp>
#include <ndn-cxx/face.hpp>

namespace difs {

class Consumer : boost::noncopyable {
  public:
	Consumer(const repo::Manifest& manifest, std::ofstream& os);

	void
	fetch();

  private:
	void onDataCommandResponse(const ndn::Interest& interest, const ndn::Data& data);

	void onDataCommandTimeout(const ndn::Interest& interest);

	void onDataCommandNack(const ndn::Interest& interest);

  private:
	// std::map<int, std::tuple<const uint8_t*, size_t>> map;
	std::ofstream& m_os;
	std::map<int, const ndn::Block> map;
	bool m_verbose;
	int m_currentSegment;
	int m_totalSize;
	int m_retryCount;
	ndn::Face m_face;
	repo::Manifest m_manifest;
};
} // namespace difs

#endif  // DIFS_CONSUMER_HPP