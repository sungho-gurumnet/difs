#ifndef DIFS_CONSUMER_HPP
#define DIFS_CONSUMER_HPP

#include "manifest/manifest.hpp"

#include <ndn-cxx/security/command-interest-signer.hpp>
#include <ndn-cxx/face.hpp>

namespace difs {

class Consumer : boost::noncopyable {
  public:
	Consumer(const repo::Manifest& manifest);

	void
	fetch();

  private:
	void onDataCommandResponse(const ndn::Interest& interest, const ndn::Data& data);

	void onDataCommandTimeout(const ndn::Interest& interest);

	void onDataCommandNack(const ndn::Interest& interest);

  private:

	ndn::Face m_face;
	repo::Manifest m_manifest;
};
} // namespace difs

#endif  // DIFS_CONSUMER_HPP