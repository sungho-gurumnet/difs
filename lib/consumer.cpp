#include "consumer.hpp"

using namespace difs;

Consumer::Consumer(const repo::Manifest& manifest): m_manifest(manifest){

}

void
Consumer::fetch()
{
  auto repos = m_manifest.getRepos();
  int end_block_id = m_manifest.getEndBlockId();

  for (auto iter = repos.begin(); iter != repos.end(); ++iter)
  {
	for(int segment_id = iter->start; segment_id < iter->end; segment_id++) {
		ndn::Interest interest(ndn::Name(iter->name).append("data")
					.append(m_manifest.getName()).appendSegment(segment_id));

		m_face.expressInterest(interest,
								std::bind(&Consumer::onDataCommandResponse, this, _1, _2),
								std::bind(&Consumer::onDataCommandNack, this, _1), // Nack
								std::bind(&Consumer::onDataCommandTimeout, this, _1));
	}
  }

//   auto repoName = selectRepoName(manifest, segmentId);
//   auto name = manifest.getName();
}

void
Consumer::onDataCommandResponse(const ndn::Interest& interest, const ndn::Data& data)
{
//   const auto& content = data.getContent();
//   m_os.write(reinterpret_cast<const char*>(content.value()), content.value_size());
//   m_totalSize += content.value_size();
//   if (m_verbose) {
//     std::cerr << "LOG: received data = " << data.getName() << std::endl;
//   }
//   if (m_isFinished) {
//     std::cerr << "INFO: End of file is reached" << std::endl;
//     std::cerr << "INFO: Total # of segments received: " << m_currentSegment + 1  << std::endl;
//     std::cerr << "INFO: Total # bytes of content received: " << m_totalSize << std::endl;
//   }
}

void
Consumer::onDataCommandTimeout(const ndn::Interest& interest)
{
	// TODO
}

void
Consumer::onDataCommandNack(const ndn::Interest& interest)
{
	// TODO
}