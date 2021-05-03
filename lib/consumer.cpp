#include "consumer.hpp"
#include <vector>

using namespace difs;

Consumer::Consumer(const repo::Manifest& manifest, std::ofstream& os) 
	: m_manifest(manifest)
	, m_os(os)
	{}

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
}

void
Consumer::onDataCommandResponse(const ndn::Interest& interest, const ndn::Data& data)
{	
	const auto& content = data.getContent();

	// TODO: seg num check && end num check
	int num = 0, endNum = 10;
	// map.insert(std::pair<int, std::tuple<const uint8_t*, size_t>>(num, std::make_tuple(content.value(), content.value_size())));
	map.insert(std::pair<int, const ndn::Block>(num, content));

	std::streambuf* buf;
	std::ostream m_os(buf);
	if(num == endNum) {
		for(auto iter = map.begin(); iter != map.end(); iter++) {
			m_os.write(reinterpret_cast<const char *>(iter->second.value()), iter->second.value_size());
			m_totalSize += iter->second.value_size();
			m_currentSegment += 1;
	  		if (m_verbose) {
	    		std::cerr << "LOG: received data = " << data.getName() << std::endl;
	  		}
		}

		std::cerr << "INFO: End of file is reached" << std::endl;
		std::cerr << "INFO: Total # of segments received: " << m_currentSegment << std::endl;
		std::cerr << "INFO: Total # bytes of content received: " << m_totalSize << std::endl;
	}
}

void
Consumer::onDataCommandTimeout(const ndn::Interest& interest)
{
	if(m_retryCount++ < 3) {
		fetch();
    	if (m_verbose) {
     	  std::cerr << "TIMEOUT: retransmit interest for " << interest.getName() << std::endl;
    	}
	} else {
		std::cerr << "TIMEOUT: last interest sent" << std::endl
		<< "TIMEOUT: abort fetching after " << 3 << " times of retry" << std::endl;
	}
}

void
Consumer::onDataCommandNack(const ndn::Interest& interest)
{
	if(m_retryCount++ < 3) {
		fetch();
    	if (m_verbose) {
     	  std::cerr << "TIMEOUT: retransmit interest for " << interest.getName() << std::endl;
    	}
	} else {
		std::cerr << "NACK: last interest sent" << std::endl
		<< "NACK: abort fetching after " << 3 << " times of retry" << std::endl;
	}
}