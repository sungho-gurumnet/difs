#include "repo-command-parameter.hpp"
#include "repo-command-response.hpp"
#include "util.hpp"

#include "manifest/manifest.hpp"

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/command-interest-signer.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/util/scheduler.hpp>

#include "difs.hpp"
#include "consumer.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

#include <boost/asio.hpp>
#include <boost/iostreams/operations.hpp>
#include <boost/iostreams/read.hpp>
#include <boost/lexical_cast.hpp>

static const uint64_t DEFAULT_BLOCK_SIZE = 1000;
static const uint64_t DEFAULT_INTEREST_LIFETIME = 4000;
static const uint64_t DEFAULT_FRESHNESS_PERIOD = 10000;
static const uint64_t DEFAULT_CHECK_PERIOD = 1000;
static const size_t PRE_SIGN_DATA_COUNT = 11;

namespace difs {

using ndn::Name;
using ndn::Interest;
using ndn::Data;
using ndn::Block;

using std::bind;
using std::placeholders::_1;
using std::placeholders::_2;

using namespace repo;

static const int MAX_RETRY = 3;

class Error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

void
DIFS::onDeleteCommandNack(const Interest& interest)
{
  if (m_retryCount++ < MAX_RETRY) {
    deleteFile(interest.getName());
    if (m_verbose) {
      std::cerr << "NACK: retransmit interest for " << interest.getName() << std::endl;
    }
  } else {
    std::cerr << "NACK: last interest sent" << std::endl
    << "NACK: abort fetching after " << MAX_RETRY << " times of retry" << std::endl;
  }
}

void
DIFS::onDeleteCommandTimeout(const Interest& interest)
{
  if (m_retryCount++ < MAX_RETRY) {
    deleteFile(interest.getName());
    if (m_verbose) {
      std::cerr << "TIMEOUT: retransmit interest for " << interest.getName() << std::endl;
    }
  } else {
    std::cerr << "TIMEOUT: last interest sent" << std::endl
    << "TIMEOUT: abort fetching after " << MAX_RETRY << " times of retry" << std::endl;
  }
}

void
DIFS::onDeleteCommandResponse(const ndn::Interest& interest, const ndn::Data& data)
{
  RepoCommandResponse response(data.getContent().blockFromValue());
  int statusCode = response.getCode();
  if (statusCode == 404) {
    std::cerr << "Manifest not found" << std::endl;
    return;
  }
  else if (statusCode >= 400) {
    std::cerr << "delete command failed with code " << statusCode << interest.getName() << std::endl;
    return;
  }
}

void
DIFS::deleteFile(const Name& data_name)
{
  std::cout << "Delete " << data_name << std::endl;

  RepoCommandParameter parameter;
  parameter.setProcessId(0);  // FIXME: set process id properly
  parameter.setName(data_name);

  Name cmd = m_common_name;
  cmd.append("delete")
    .append(parameter.wireEncode());

  ndn::Interest commandInterest = m_cmdSigner.makeCommandInterest(cmd);

  commandInterest.setInterestLifetime(m_interestLifetime);
  commandInterest.setMustBeFresh(true);
  m_face.expressInterest(commandInterest,
                        std::bind(&DIFS::onDeleteCommandResponse, this, _1, _2),
                        std::bind(&DIFS::onDeleteCommandNack, this, _1), // Nack
                        std::bind(&DIFS::onDeleteCommandTimeout, this, _1));
}

void
DIFS::run()
{
  m_face.processEvents(m_timeout);
}

void
DIFS::onGetCommandResponse(const Interest& interest, const Data& data)
{
  auto content = data.getContent();
  std::string json(
    content.value_begin(),
    content.value_end()
  );

  if (json.length() == 0) {
    std::cerr << "Not found" << std::endl;
    return;
  }

  repo::Manifest manifest = Manifest::fromJson(json);

  Consumer consumer(manifest);
  consumer.fetch();
}

void
DIFS::onGetCommandNack(const Interest& interest)
{
  std::ofstream of;
  if (m_retryCount++ < MAX_RETRY) {
    getFile(interest.getName(), of);
    if (m_verbose) {
      std::cerr << "TIMEOUT: retransmit interest for " << interest.getName() << std::endl;
    }
  } else {
    std::cerr << "TIMEOUT: last interest sent" << std::endl
    << "TIMEOUT: abort fetching after " << MAX_RETRY << " times of retry" << std::endl;
  }
}

void
DIFS::onGetCommandTimeout(const Interest& interest)
{
  std::ofstream of;
  if (m_retryCount++ < MAX_RETRY) {
    getFile(interest.getName(), of);
    if (m_verbose) {
      std::cerr << "TIMEOUT: retransmit interest for " << interest.getName() << std::endl;
    }
  } else {
    std::cerr << "TIMEOUT: last interest sent" << std::endl
    << "TIMEOUT: abort fetching after " << MAX_RETRY << " times of retry" << std::endl;
  }
}

void
DIFS::getFile(const Name& data_name, std::ofstream& os)
{
  RepoCommandParameter parameter;
  parameter.setProcessId(0);  // FIXME: set process id properly
  parameter.setName(data_name);

  // Name cmd = m_common_name;
  Name cmd = data_name;
  cmd.append("get")
    .append(parameter.wireEncode());

  ndn::Interest commandInterest = m_cmdSigner.makeCommandInterest(cmd);

  commandInterest.setInterestLifetime(m_interestLifetime);
  commandInterest.setMustBeFresh(true);

  m_face.expressInterest(commandInterest,
                        std::bind(&DIFS::onGetCommandResponse, this, _1, _2),
                        std::bind(&DIFS::onGetCommandNack, this, _1),
                        std::bind(&DIFS::onGetCommandTimeout, this, _1));
}

void
DIFS::onPutCommandNack(const Interest& interest)
{
  std::istream* is;
  if (m_retryCount++ < MAX_RETRY) {
    putFile(interest.getName(), *is);
    if (m_verbose) {
      std::cerr << "TIMEOUT: retransmit interest for " << interest.getName() << std::endl;
    }
  } else {
    std::cerr << "TIMEOUT: last interest sent" << std::endl
    << "TIMEOUT: abort fetching after " << MAX_RETRY << " times of retry" << std::endl;
  }
}

void
DIFS::onPutCommandTimeout(const Interest& interest)
{
  std::istream* is;
  if (m_retryCount++ < MAX_RETRY) {
    putFile(interest.getName(), *is);
    if (m_verbose) {
      std::cerr << "TIMEOUT: retransmit interest for " << interest.getName() << std::endl;
    }
  } else {
    std::cerr << "TIMEOUT: last interest sent" << std::endl
    << "TIMEOUT: abort fetching after " << MAX_RETRY << " times of retry" << std::endl;
  }
}

void 
DIFS::putFile(const ndn::Name &name, std::istream &is)
{
  m_dataPrefix = ndnName;

  insertStream->seekg(0, std::ios::beg);
  auto beginPos = insertStream->tellg();
  insertStream->seekg(0, std::ios::end);
  m_bytes = insertStream->tellg() - beginPos;
  insertStream->seekg(0, std::ios::beg);

  if (m_verbose)
    std::cerr << "setInterestFilter for " << m_dataPrefix << std::endl;

  m_face.setInterestFilter(m_dataPrefix,
                           bind(&DIFS::onPutFileInterest, this, _1, _2),
                           bind(&DIFS::onPutFileRegisterSuccess, this, _1),
                           bind(&DIFS::onPutFileRegisterFailed, this, _1, _2));

  if (hasTimeout)
    m_scheduler.schedule(timeout, [this] { putFileStopProcess(); });

  m_face.processEvents();
}

void
DIFS::putFilePrepareNextData(uint64_t referenceSegmentNo)
{
  // make sure m_data has [referenceSegmentNo, referenceSegmentNo + PRE_SIGN_DATA_COUNT] Data
  if (m_isFinished)
    return;

  size_t nDataToPrepare = PRE_SIGN_DATA_COUNT;

  if (!m_data.empty()) {
    uint64_t maxSegmentNo = m_data.rbegin()->first;

    if (maxSegmentNo - referenceSegmentNo >= nDataToPrepare) {
      // nothing to prepare
      return;
    }

    nDataToPrepare -= maxSegmentNo - referenceSegmentNo;
  }

  for (size_t i = 0; i < nDataToPrepare && !m_isFinished; ++i) {
    uint8_t *buffer = new uint8_t[blockSize];
    auto readSize = boost::iostreams::read(*insertStream,
                                           reinterpret_cast<char*>(buffer), blockSize);
    if (readSize <= 0) {
      BOOST_THROW_EXCEPTION(Error("Error reading from the input stream"));
    }

    auto data = make_shared<ndn::Data>(Name(m_dataPrefix).appendSegment(m_currentSegmentNo));

    if (insertStream->peek() == std::istream::traits_type::eof()) {
      data->setFinalBlock(ndn::name::Component::fromSegment(m_currentSegmentNo));
      m_isFinished = true;
    }

    data->setContent(buffer, readSize);
    data->setFreshnessPeriod(freshnessPeriod);
    putFileSignData(*data);

    m_data.insert(std::make_pair(m_currentSegmentNo, data));

    ++m_currentSegmentNo;
    delete[] buffer;
  }
}

void
DIFS::putFileStartInsertCommand()
{
  RepoCommandParameter parameters;
  parameters.setName(m_dataPrefix);

  ndn::Interest commandInterest = generateCommandInterest(repoPrefix, "insert", parameters);
  m_face.expressInterest(commandInterest,
                         bind(&DIFS::onPutFileInsertCommandResponse, this, _1, _2),
                         bind(&DIFS::onPutFileInsertCommandTimeout, this, _1), // Nack
                         bind(&DIFS::onPutFileInsertCommandTimeout, this, _1));
}

void
DIFS::onPutFileInsertCommandResponse(const ndn::Interest& interest, const ndn::Data& data)
{
  RepoCommandResponse response(data.getContent().blockFromValue());
  auto statusCode = response.getCode();
  if (statusCode >= 400) {
    BOOST_THROW_EXCEPTION(Error("insert command failed with code " +
                                boost::lexical_cast<std::string>(statusCode)));
  }
  m_processId = response.getProcessId();

  m_scheduler.schedule(m_checkPeriod, [this] { putFileStartCheckCommand(); });
}

void
DIFS::onPutFileInsertCommandTimeout(const ndn::Interest& interest)
{
  BOOST_THROW_EXCEPTION(Error("command response timeout"));
}

void
DIFS::onPutFileInterest(const ndn::Name& prefix, const ndn::Interest& interest)
{
  if (interest.getName().size() == prefix.size()) {
    putFileSendManifest(prefix, interest);
    return;
  }

  uint64_t segmentNo;
  try {
    ndn::Name::Component segmentComponent = interest.getName().get(prefix.size());
    segmentNo = segmentComponent.toSegment();
  }
  catch (const tlv::Error& e) {
    if (m_verbose) {
      std::cerr << "Error processing incoming interest " << interest << ": "
                << e.what() << std::endl;
    }
    return;
  }

  putFilePrepareNextData(segmentNo);

  DataContainer::iterator item = m_data.find(segmentNo);
  if (item == m_data.end()) {
    if (m_verbose) {
      std::cerr << "Requested segment [" << segmentNo << "] does not exist" << std::endl;
    }
    return;
  }

  if (m_isFinished) {
    uint64_t final = m_currentSegmentNo - 1;
    item->second->setFinalBlock(ndn::name::Component::fromSegment(final));
  }
  m_face.put(*item->second);
}

void
DIFS::putFileSendManifest(const ndn::Name& prefix, const ndn::Interest& interest)
{
  BOOST_ASSERT(prefix == m_dataPrefix);

  if (prefix != interest.getName()) {
    if (m_verbose) {
      std::cerr << "Received unexpected interest " << interest << std::endl;
    }
    return;
  }

  ndn::Data data(interest.getName());
  auto blockCount = m_bytes / blockSize + (m_bytes % blockSize != 0);

  Manifest manifest(interest.getName().toUri(), 0, blockCount - 1);
  std::string json = manifest.toInfoJson();
  data.setContent((uint8_t*) json.data(), (size_t) json.size());
  data.setFreshnessPeriod(freshnessPeriod);
  putFileSignData(data);

  m_face.put(data);
}

void
DIFS::onRegisterSuccess(const Name& prefix)
{
  putFileStartInsertCommand();
}

void
DIFS::onPutFileRegisterFailed(const ndn::Name& prefix, const std::string& reason)
{
  BOOST_THROW_EXCEPTION(Error("onRegisterFailed: " + reason));
}

void
DIFS::putFileStopProcess()
{
  m_face.getIoService().stop();
}

void
DIFS::putFileSignData(ndn::Data& data)
{
  if (useDigestSha256) {
    m_keyChain.sign(data, ndn::signingWithSha256());
  }
  else if (identityForData.empty())
    m_keyChain.sign(data);
  else {
    m_keyChain.sign(data, ndn::signingByIdentity(identityForData));
  }
}

void
DIFS::putFileStartCheckCommand()
{
  auto parameter = RepoCommandParameter();
  parameter.setName(ndnName);
  ndn::Interest checkInterest = generateCommandInterest(repoPrefix, "insert check",
                                                        parameter
                                                          .setProcessId(m_processId));
  m_face.expressInterest(checkInterest,
                         bind(&DIFS::onPutFileCheckCommandResponse, this, _1, _2),
                         bind(&DIFS::onPutFileCheckCommandTimeout, this, _1), // Nack
                         bind(&DIFS::onPutFileCheckCommandTimeout, this, _1));
}

void
DIFS::onPutFileCheckCommandResponse(const ndn::Interest& interest, const ndn::Data& data)
{
  RepoCommandResponse response(data.getContent().blockFromValue());
  auto statusCode = response.getCode();
  if (statusCode >= 400) {
    BOOST_THROW_EXCEPTION(Error("Insert check command failed with code: " +
                                boost::lexical_cast<std::string>(statusCode)));
  }

  if (m_isFinished) {
    uint64_t insertCount = response.getInsertNum();

    // Technically, the check should not infer, but directly has signal from repo that
    // write operation has been finished

    if (insertCount == m_currentSegmentNo) {
      m_face.getIoService().stop();
      return;
    }
  }

  m_scheduler.schedule(m_checkPeriod, [this] { putFileStartCheckCommand(); });
}

void
DIFS::onPutFileCheckCommandTimeout(const ndn::Interest& interest)
{
  BOOST_THROW_EXCEPTION(Error("check response timeout"));
}

ndn::Interest
DIFS::putFileGenerateCommandInterest(const ndn::Name& commandPrefix, const std::string& command,
                                    const repo::RepoCommandParameter& commandParameter)
{
  Name cmd = commandPrefix;
  cmd
    .append(command)
    .append(commandParameter.wireEncode());
  ndn::Interest interest;

  if (identityForCommand.empty())
    interest = m_cmdSigner.makeCommandInterest(cmd);
  else {
    interest = m_cmdSigner.makeCommandInterest(cmd, ndn::signingByIdentity(identityForCommand));
  }

  interest.setInterestLifetime(interestLifetime);
  return interest;
}

// void
// DIFS::onInsertCommandTimeout(const ndn::Interest& interest)
// {
//   BOOST_THROW_EXCEPTION(Error("command response timeout"));
// }

// void
// DIFS::onInsertCommandNack(const ndn::Interest& interest)
// {
//   BOOST_THROW_EXCEPTION(Error("command response timeout"));
// }

// void
// DIFS::putFile(const ndn::Name& data_name, std::ifstream& is)
// {
//   std::cout << "Insert" << data_name << std::endl;

//   RepoCommandParameter parameter;
//   parameter.setProcessId(0);  // FIXME: set process id properly
//   parameter.setName(data_name);

//   Name cmd = m_common_name;
//   cmd.append("insert")
//     .append(parameter.wireEncode());

//   ndn::Interest commandInterest = m_cmdSigner.makeCommandInterest(cmd);

//   m_face.expressInterest(commandInterest,
//                          bind(&DIFS::onInsertCommandResponse, this, _1, _2),
//                          bind(&DIFS::onInsertCommandNack, this, _1), // Nack
//                          bind(&DIFS::onInsertCommandTimeout, this, _1));
// }
}



// namespace difs
// class Consumer : boost::noncopyable
// {
// public:
//   class Error : public std::runtime_error
//   {
//   public:
//     using std::runtime_error::runtime_error;
//   };

//   Consumer(const std::string& dataName, std::ostream& os,
//            bool verbose, bool versioned, bool single,
//            int interestLifetime, int timeout,
//            bool mustBeFresh = false,
//            bool canBePrefix = false)
//     // : m_dataName(dataName)
//     , m_os(os)
//     , m_verbose(verbose)
//     , m_isFinished(false)
//     , m_isFirst(true)
//     , m_interestLifetime(interestLifetime)
//     , m_timeout(timeout)
//     , m_currentSegment(0)
//     , m_totalSize(0)
//     , m_retryCount(0)
//     , m_mustBeFresh(mustBeFresh)
//     , m_canBePrefix(canBePrefix)
//   {
//   }

//   void
//   run();

// private:
//   void
//   fetchData(const Manifest& manifest, uint64_t segmentId);

//   ndn::Name
//   selectRepoName(const Manifest& manifest, uint64_t segmentId);

//   void
//   onManifest(const Interest& interest, const ndn::Data& data);

//   void
//   onManifestTimeout(const ndn::Interest& interest);

//   void
//   onUnversionedData(const ndn::Interest& interest, const ndn::Data& data);

//   void
//   readData(const ndn::Data& data);

//   bool
//   verifyData(const ndn::Data& data);

//   void
//   fetchNextData();

// private:
//   ndn::Face m_face;
//   ndn::Name m_dataName;
//   std::ostream& m_os;
//   bool m_verbose;
//   bool m_isFinished;
//   bool m_isFirst;
//   std::array<uint8_t, util::HASH_SIZE> prevHash;
//   ndn::time::milliseconds m_interestLifetime;
//   ndn::time::milliseconds m_timeout;
//   uint64_t m_currentSegment;
//   int m_totalSize;
//   int m_retryCount;
//   bool m_mustBeFresh;
//   bool m_canBePrefix;

//   std::shared_ptr<Manifest> m_manifest;
//   uint64_t m_finalBlockId;

//   static constexpr int MAX_RETRY = 3;
// };

// void
// DIFS::getFileFetchData(const Manifest& manifest, uint64_t segmentId)
// {
//   auto repoName = selectRepoName(manifest, segmentId);
//   auto name = manifest.getName();
//   Interest interest(repoName.append("data").append(name).appendSegment(segmentId));
//   interest.setInterestLifetime(m_interestLifetime);
//   // interest.setMustBeFresh(true);

//   m_face.expressInterest(interest,
//                          std::bind(&DIFS::getFileonUnversionedData, this, _1, _2),
//                          std::bind(&DIFS::onNack, this, _1), // Nack
//                          std::bind(&DIFS::onTimeout, this, _1));
// }

// ndn::Name
// DIFS::getFileSelectRepoName(const Manifest& manifest, uint64_t segmentId)
// {
//   auto repos = manifest.getRepos();
//   for (auto iter = repos.begin(); iter != repos.end(); ++iter)
//   {
//     auto start = iter->start;
//     auto end = iter->end;
//     if (start <= (int)segmentId && (int)segmentId <= end)
//     {
//       return Name(iter->name);
//     }
//   }

//   // Should not be here
//   return Name("");
// }

// void
// DIFS::getFileRun()
// {
//   // Get manifest
//   RepoCommandParameter parameter;
//   parameter.setName(m_dataName);
//   Interest interest = util::generateCommandInterest(Name("get"), "", parameter, m_interestLifetime);
  
//   std::cerr << interest << std::endl;

//   m_face.expressInterest(
//     interest,
//     std::bind(&DIFS::getFileonManifest, this, _1, _2),
//                          std::bind(&DIFS::onNack, this, _1), // Nack
//                          std::bind(&DIFS::onTimeout, this, _1));

//   // processEvents will block until the requested data received or timeout occurs
//   m_face.processEvents(m_timeout);
// }

// void
// DIFS::getFileOnManifest(const Interest& interest, const Data& data)
// {
//   auto content = data.getContent();
//   std::string json(
//     content.value_begin(),
//     content.value_end()
//   );

//   if (json.length() == 0) {
//     std::cerr << "Not found" << std::endl;
//     return;
//   }

//   auto manifest = Manifest::fromJson(json);
//   m_manifest = std::make_shared<Manifest>(manifest);

//   m_finalBlockId = manifest.getEndBlockId();
//   std::cerr << "final block: " << m_finalBlockId;

//   fetchData(manifest, m_currentSegment);
// }

// // void
// // DIFS::getFileOnManifestTimeout(const Interest& interest)
// // {
// //   if (m_retryCount++ < MAX_RETRY) {
// //     // Retransmit the interest
// //     RepoCommandParameter parameter;
// //     parameter.setName(m_dataName);
// //     Interest interest = util::generateCommandInterest(Name("get"), "", parameter, m_interestLifetime);
    
// //     std::cerr << interest << std::endl;

// //     m_face.expressInterest(
// //       interest,
// //       std::bind(&DIFS::getFileonManifest, this, _1, _2),
// //       std::bind(&DIFS::getFileonManifestTimeout, this, _1),
// //       std::bind(&DIFS::getFileonManifestTimeout, this, _1));
// //     if (m_verbose) {
// //       std::cerr << "TIMEOUT: retransmit interest for manifest"<< std::endl;
// //     }
// //   }
// //   else {
// //     std::cerr << "TIMEOUT: last interest sent for manifest" << std::endl;
// //     std::cerr << "TIMEOUT: abort fetching after " << MAX_RETRY
// //               << " times of retry" << std::endl;
// //   }
// // }

// void
// DIFS::getFileOnUnversionedData(const Interest& interest, const Data& data)
// {
//   fetchNextData();
//   if (!verifyData(data)) {
//     BOOST_THROW_EXCEPTION(Error("Error verifying hash chain"));
//   }
//   readData(data);
// }

// bool 
// DIFS::getFileVerifyData(const Data& data)
// {
//   bool ret;
//   auto content = data.getContent();

//   if (m_isFirst) {
//     m_isFirst = false;
//     ret = true;
//   } else {
//     ret = util::verifyHash(content.value(), content.value_size(), prevHash);
//   }
//   for (int i = 0; i < util::HASH_SIZE; i += 1) {
//     prevHash[i] = content.value()[i];
//   }

//   return ret;
// }

// void
// DIFS::getFileReadData(const Data& data)
// {
//   const auto& content = data.getContent();
//   m_os.write(reinterpret_cast<const char*>(content.value() + util::HASH_SIZE), content.value_size() - util::HASH_SIZE);
//   m_totalSize += content.value_size() - util::HASH_SIZE;

//   if (m_verbose) {
//     std::cerr << "LOG: received data = " << data.getName() << std::endl;
//   }

//   if (m_isFinished) {
//     std::cerr << "INFO: End of file is reached" << std::endl;
//     std::cerr << "INFO: Total # of segments received: " << m_currentSegment + 1  << std::endl;
//     std::cerr << "INFO: Total # bytes of content received: " << m_totalSize << std::endl;
//   }
// }

// void
// DIFS::getFileFetchNextData()
// {
//   if (m_currentSegment >= m_finalBlockId) {
//     m_isFinished = true;
//   } else {
//     m_retryCount = 0;
//     m_currentSegment += 1;
//     fetchData(*m_manifest, m_currentSegment);
//   }
// }

// // void
// // DIFS::getFileOnTimeout(const Interest& interest)
// // {
// //   if (m_retryCount++ < MAX_RETRY) {
// //     // Retransmit the interest
// //     fetchData(*m_manifest, m_currentSegment);
// //     if (m_verbose) {
// //       std::cerr << "TIMEOUT: retransmit interest for " << interest.getName() << std::endl;
// //     }
// //   }
// //   else {
// //     std::cerr << "TIMEOUT: last interest sent for segment #" << m_currentSegment << std::endl;
// //     std::cerr << "TIMEOUT: abort fetching after " << MAX_RETRY
// //               << " times of retry" << std::endl;
// //   }
// // }

// }

// class NdnPutFile : boost::noncopyable
// {
// public:
//   class Error : public std::runtime_error
//   {
//   public:
//     using std::runtime_error::runtime_error;
//   };

//   NdnPutFile()
//     : isSingle(false)
//     , useDigestSha256(false)
//     , freshnessPeriod(DEFAULT_FRESHNESS_PERIOD)
//     , interestLifetime(DEFAULT_INTEREST_LIFETIME)
//     , hasTimeout(false)
//     , timeout(0)
//     , blockSize(DEFAULT_BLOCK_SIZE)
//     , insertStream(nullptr)
//     , isVerbose(false)
//     , m_scheduler(m_face.getIoService())
//     , m_timestampVersion(toUnixTimestamp(system_clock::now()).count())
//     , m_processId(0)
//     , m_checkPeriod(DEFAULT_CHECK_PERIOD)
//     , m_currentSegmentNo(0)
//     , m_isFinished(false)
//     , m_cmdSigner(m_keyChain)
//   {
//   }

//   void
//   run();

// private:
//   void
//   prepareHashes();
  
//   void
//   prepareNextData(uint64_t referenceSegmentNo);

//   void
//   startInsertCommand();

//   void
//   onInsertCommandResponse(const ndn::Interest& interest, const ndn::Data& data);

//   void
//   onInsertCommandTimeout(const ndn::Interest& interest);

//   void
//   sendManifest(const ndn::Name& prefix, const ndn::Interest& interest);

//   void
//   onRegisterSuccess(const ndn::Name& prefix);


//   void
//   stopProcess();

//   void
//   signData(ndn::Data& data);

//     void
//   signFirstData(ndn::Data& data);

//   void
//   startCheckCommand();

//   void
//   onCheckCommandResponse(const ndn::Interest& interest, const ndn::Data& data);

//   void
//   onCheckCommandTimeout(const ndn::Interest& interest);

//   ndn::Interest
//   generateCommandInterest(const ndn::Name& commandPrefix, const std::string& command,
//                           const RepoCommandParameter& commandParameter);

// public:
//   bool isSingle;
//   bool useDigestSha256;
//   std::string identityForCommand;
//   milliseconds freshnessPeriod;
//   milliseconds interestLifetime;
//   bool hasTimeout;
//   milliseconds timeout;
//   size_t blockSize;
//   ndn::Name ndnName;
//   std::string difsKey;
//   std::istream* insertStream;
//   bool isVerbose;

// private:
//   ndn::Face m_face;
//   ndn::Scheduler m_scheduler;
//   ndn::KeyChain m_keyChain;
//   uint64_t m_timestampVersion;
//   uint64_t m_processId;
//   milliseconds m_checkPeriod;
//   size_t m_currentSegmentNo;
//   bool m_isFinished;
//   ndn::Name m_dataPrefix;

//   size_t m_bytes;
//   size_t m_firstSize;

//   using DataContainer = std::map<uint64_t, shared_ptr<ndn::Data>>;
//   DataContainer m_data;
//   ndn::security::CommandInterestSigner m_cmdSigner;
// };

// void
// DIFS::putFileprepareHashes()
// {
//   int dataSize = blockSize - util::HASH_SIZE;
//   std::array<uint8_t,util::HASH_SIZE> hash;
//   std::array<uint8_t,util::HASH_SIZE> prevHash;
//   uint8_t *buffer = new uint8_t[blockSize];

//   int position;
//   for (position = dataSize; position < (int)m_bytes ; position += dataSize) {
//     if (!hashes.empty()) {
//       prevHash = hashes.front();
//     }
//     memcpy(buffer, prevHash.data(), util::HASH_SIZE);
//     // This part is to read from the behind.
//     insertStream->seekg(-position, std::ios::end);
//     auto readSize = boost::iostreams::read(*insertStream, reinterpret_cast<char*>(buffer + util::HASH_SIZE), dataSize);
//     if (readSize <= 0) {
//       BOOST_THROW_EXCEPTION(Error("Error reading from the input stream"));
//     }

//     std::streambuf* buf;
//     buf = std::cout.rdbuf();
//     std::ostream os(buf);

//     //std::cout << "Content: ";
//     //os.write(reinterpret_cast<const char *>(buffer), blockSize);
//     //std::cout << std::endl;

//     //std::ios_base::fmtflags g(std::cout.flags());
//     //std::cout << "Content(hex): " << std::hex;
//     //for (int i = 0; i < (int)blockSize; i += 1) {
//       //printf("%02x", buffer[i]);
//     //}
//     //std::cout.flags(g);
//     //std::cout << std::endl;

//     hash = util::calcHash(buffer, blockSize);

//     //std::cout << (buffer+util::HASH_SIZE) << std::endl;

//     //std::cout << "Hash: " << std::hex;
//     //for (const auto& s : hash) {
//       //printf("%02x", s);
//     //}
//     //std::cout << std::endl;
//     hashes.push_front(hash);
//   }

//   // save first block size
//   // If position >= m_bytes, only one block is generated and no hash chain
//   m_firstSize = m_bytes - (position - dataSize);
//   //std::cout << "first data size = " << m_firstSize << std::endl;
//   insertStream->seekg(0, std::ios::beg);
// }

// void
// DIFS::prepareNextData(uint64_t referenceSegmentNo, size_t blockSize)
// {
//   // make sure m_data has [referenceSegmentNo, referenceSegmentNo + PRE_SIGN_DATA_COUNT] Data
//   if (m_isFinished)
//     return;

//   size_t nDataToPrepare = PRE_SIGN_DATA_COUNT;

//   if (!m_data.empty()) {
//     uint64_t maxSegmentNo = m_data.rbegin()->first;

//     // if what is left is less than nDataToPrepare than return. 
//     if (maxSegmentNo - referenceSegmentNo >= nDataToPrepare) {
//       // nothing to prepare
//       return;
//     }

//     nDataToPrepare -= maxSegmentNo - referenceSegmentNo;
//   }

//   auto dataSize = blockSize - util::HASH_SIZE;
//   for (size_t i = 0; i < nDataToPrepare && !m_isFinished; ++i) {
//     auto segNo = referenceSegmentNo + i;

//     //std::cout << "segno: " << segNo << std::endl;
//     //std::cout << "hashes size: " << hashes.size() << std::endl;

//     uint8_t *buffer = new uint8_t[blockSize];
//     std::list<std::array<uint8_t,util::HASH_SIZE>> hashes;
//     std::array<uint8_t,util::HASH_SIZE> hash;
//     if (!hashes.empty()) {
//       hash = hashes.front();
//       hashes.pop_front();
//     } else {
//       hash = {0};
//       m_isFinished = true;
//     }


//     memcpy(buffer, &hash, repo::util::HASH_SIZE);

//     auto toRead = dataSize;
//     if (segNo == 0) {
//       toRead = m_firstSize;
//     }

//     auto readSize = boost::iostreams::read(*insertStream,
//                                            reinterpret_cast<char*>(buffer + repo::util::HASH_SIZE), toRead);
//     if (readSize <= 0) {
//       BOOST_THROW_EXCEPTION(Error("Error reading from the input stream"));
//     }

//     auto data = make_shared<ndn::Data>(Name(m_dataPrefix).appendSegment(m_currentSegmentNo));
//     //std::cerr<<"data name is "<<data.getName()<<std::endl;
//     //std::cerr<<"data full name "<<data.getFullName() << std::endl;
//     if (m_isFinished) {
//       std::cout << "Finished" << std::endl;
//       data->setFinalBlock(ndn::name::Component::fromSegment(m_currentSegmentNo));
//     }

//     data->setContent(buffer, toRead + util::HASH_SIZE);
//     data->setFreshnessPeriod(m_freshness_period);
//     if(segNo == 0) {
//       signFirstData(*data);
//     } else {
//       signData(*data);
//     }

//     m_data.insert(std::make_pair(m_currentSegmentNo, data));

//     ++m_currentSegmentNo;
//     delete[] buffer;
//   }
// }


// void
// DIFS::onRegisterSuccess(const ndn::Name& data_name)
// {
//   startInsertCommand(data_name);
// }

// void
// DIFS::onRegisterFailed(const ndn::Name& prefix, const std::string& reason)
// {
//   BOOST_THROW_EXCEPTION(Error("onRegisterFailed: " + reason));
// }

// void
// DIFS::startInsertCommand(const ndn::Name& data_name)
// {
//   RepoCommandParameter parameters;
//   parameters.setName(data_name);

//   ndn::Interest commandInterest = generateCommandInterest(m_common_name, "insert", parameters);
//   m_face.expressInterest(commandInterest,
//                          bind(&DIFS::putFileonInsertCommandResponse, this, _1, _2),
//                          std::bind(&DIFS::onNack, this, _1), // Nack
//                          std::bind(&DIFS::onTimeout, this, _1));
// }

// // void
// // DIFS::putFileonInsertCommandResponse(const ndn::Interest& interest, const ndn::Data& data)
// // {
// //   RepoCommandResponse response(data.getContent().blockFromValue());
// //   auto statusCode = response.getCode();
// //   if (statusCode >= 400) {
// //     BOOST_THROW_EXCEPTION(Error("insert command failed with code " +
// //                                 boost::lexical_cast<std::string>(statusCode)));
// //   }
// //   m_processId = response.getProcessId();

// //   m_scheduler.schedule(m_checkPeriod, [this] { startCheckCommand(); });
// // }

// // void
// // DIFS::putFileonInsertCommandTimeout(const ndn::Interest& interest)
// // {
// //   BOOST_THROW_EXCEPTION(Error("command response timeout"));
// // }
// void
// DIFS::sendManifest(const ndn::Name& prefix, const ndn::Interest& interest, size_t blockSize, size_t m_bytes)
// {
//   if (prefix != interest.getName()) {
//     if (m_verbose) {
//       std::cerr << "Received unexpected interest " << interest << std::endl;
//     }
//     return;
//   }

//   ndn::Data data(interest.getName());
//   auto dataSize = blockSize - util::HASH_SIZE;
//   auto blockCount = m_bytes / dataSize + (m_bytes % dataSize != 0);

//   Manifest manifest(interest.getName().toUri(), 0, blockCount - 1);
//   std::string json = manifest.toInfoJson();
//   data.setContent((uint8_t*) json.data(), (size_t) json.size());
//   data.setFreshnessPeriod(m_freshness_period);
//   signData(data);

//   m_face.put(data);
// }

// void
// DIFS::onPutFileInterest(const ndn::Name& prefix, const ndn::Interest& interest)
// {
//   if (interest.getName().size() == prefix.size()) {
//     sendManifest(prefix, interest);
//     return;
//   }

//   uint64_t segmentNo;
//   try {
//     ndn::Name::Component segmentComponent = interest.getName().get(prefix.size());
//     segmentNo = segmentComponent.toSegment();
//   }
//   catch (const tlv::Error& e) {
//     if (m_verbose) {
//       std::cerr << "Error processing incoming interest " << interest << ": "
//                 << e.what() << std::endl;
//     }
//     return;
//   }

//   prepareNextData(segmentNo);

//   DataContainer::iterator item = m_data.find(segmentNo);
//   if (item == m_data.end()) {
//     if (m_verbose) {
//       std::cerr << "Requested segment [" << segmentNo << "] does not exist" << std::endl;
//     }
//     return;
//   }

//   if (m_isFinished) {
//     uint64_t final = m_currentSegmentNo - 1;
//     item->second->setFinalBlock(ndn::name::Component::fromSegment(final));
//   }
//   m_face.put(*item->second);
// }

// // void
// // DIFS::putFilestopProcess()
// // {
// //   m_face.getIoService().stop();
// // }

// void
// DIFS::signData(ndn::Data& data, bool useDigestSha256)
// {
//   if (useDigestSha256) {
//     //clock_t start, end;
//     //start =clock();
//     m_keyChain.sign(data, ndn::signingWithSha256());
//     //end = clock();
//     //double result = (double)(end-start);
//     //printf("sign with sha256: %f\n", result/CLOCKS_PER_SEC); 
//   }
//   else if (m_identity_for_data.empty())
//     m_keyChain.sign(data);
//   else {
//     //clock_t start, end;
//     //start =clock();
//     m_keyChain.sign(data, ndn::signingByIdentity(m_identity_for_data));
//     //end = clock();
//     //double result = (double)(end-start);
//     //printf("sign with identity: %f\n", result/CLOCKS_PER_SEC); 
  
//   }
// }

// void
// DIFS::signFirstData(ndn::Data& data)
// {
// if (m_identity_for_data.empty())
//     m_keyChain.sign(data);
//   else {
//     m_keyChain.sign(data, ndn::signingByIdentity(m_identity_for_data));
//   }
// }

// void
// DIFS::putFilestartCheckCommand()
// {
//   auto parameter = RepoCommandParameter();
//   parameter.setName(ndnName);
//   ndn::Interest checkInterest = generateCommandInterest(m_common_name, "insert check",
//                                                         parameter
//                                                           .setProcessId(m_processId));
//   m_face.expressInterest(checkInterest,
//                          bind(&DIFS::putFileonCheckCommandResponse, this, _1, _2),
//                          std::bind(&DIFS::onNack, this, _1), // Nack
//                          std::bind(&DIFS::onTimeout, this, _1));
// }

// void
// DIFS::putFileonCheckCommandResponse(const ndn::Interest& interest, const ndn::Data& data)
// {
//   RepoCommandResponse response(data.getContent().blockFromValue());
//   auto statusCode = response.getCode();
//   if (statusCode >= 400) {
//     BOOST_THROW_EXCEPTION(Error("Insert check command failed with code: " +
//                                 boost::lexical_cast<std::string>(statusCode)));
//   }

//   if (m_isFinished) {
//     uint64_t insertCount = response.getInsertNum();

//     // Technically, the check should not infer, but directly has signal from repo that
//     // write operation has been finished

//     if (insertCount == m_currentSegmentNo) {
//       m_face.getIoService().stop();
//       return;
//     }
//   }

//   m_scheduler.schedule(m_checkPeriod, [this] { startCheckCommand(); });
// }

// // void
// // DIFS::putFileonCheckCommandTimeout(const ndn::Interest& interest)
// // {
// //   BOOST_THROW_EXCEPTION(Error("check response timeout"));
// // }

// // ndn::Interest
// // DIFS::putFilegenerateCommandInterest(const ndn::Name& commandPrefix, const std::string& command,
// //                                     const RepoCommandParameter& commandParameter)
// // {
// //   Name cmd = commandPrefix;
// //   cmd
// //     .append(command)
// //     .append(commandParameter.wireEncode());
// //   ndn::Interest interest;

// //   if (identityForCommand.empty())
// //     interest = m_cmdSigner.makeCommandInterest(cmd);
// //   else {
// //     interest = m_cmdSigner.makeCommandInterest(cmd, ndn::signingByIdentity(identityForCommand));
// //   }

// //   interest.setInterestLifetime(interestLifetime);