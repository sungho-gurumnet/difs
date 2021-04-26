#ifndef DIFS_HPP
#define DIFS_HPP

#include <iostream>
#include <ndn-cxx/security/command-interest-signer.hpp>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/util/scheduler.hpp>

namespace difs {

using std::shared_ptr;

class DIFS : boost::noncopyable
{
public:
  DIFS(const std::string& common_name)
  : m_common_name(common_name)
    , m_cmdSigner(m_keyChain)
    , m_scheduler(m_face.getIoService())
  {}

  void
  deleteFile(const ndn::Name& name);

  void
  getFile(const ndn::Name& name, std::ofstream& os);

  void
  putFile(const ndn::Name& name, std::istream& is);

  void
  run();

private:
  void
  onDeleteCommandTimeout(const ndn::Interest& interest);

  void
  onDeleteCommandNack(const ndn::Interest& interest);

  void
  onDeleteCommandResponse(const ndn::Interest& interest, const ndn::Data& data);

  void
  onGetCommandResponse(const ndn::Interest& interest, const ndn::Data& data);

  void
  onGetCommandNack(const ndn::Interest& interest);

  void
  onGetCommandTimeout(const ndn::Interest& interest);

  void
  onRegisterSuccess(const ndn::Name& prefix);

  void
  onRegisterFailed(const ndn::Name& prefix, const std::string& reason);

  // ndn::Interest
  // generateCommandInterest(const ndn::Name& commandPrefix, const std::string& command,
  //   const repo::RepoCommandParameter& commandParameter);

  void
  putFileSendManifest(const ndn::Name &prefix, const ndn::Interest &interest);

  void
  onPutFileInterest(const ndn::Name& prefix, const ndn::Interest& interest);

  void
  onPutFileInsertCommandResponse(const ndn::Interest& interest, const ndn::Data& data);
 
  void
  onPutFileInsertCommandTimeout(const ndn::Interest& interest);
 
  void
  onPutFileRegisterSuccess(const ndn::Name& prefix);

  void
  onPutFileRegisterFailed(const ndn::Name& prefix, const std::string& reason);
  
  void
  onPutCommandNack(const ndn::Interest& interest);

  void
  onPutCommandTimeout(const ndn::Interest& interest);

  void
  putFileStopProcess();

  void
  putFileSignData(ndn::Data& data);

  void
  onPutFileCheckCommandResponse(const ndn::Interest& interest, const ndn::Data& data);
  
  void
  onPutFileCheckCommandTimeout(const ndn::Interest& interest);
  
  // ndn::Interest
  // putFileGenerateCommandInterest(const ndn::Name& commandPrefix, const std::string& command,
  //                         const repo::RepoCommandParameter& commandParameter);

  // void
  // createManifestData(const ndn::Name& prefix, const ndn::Interest& interest);

  // void
  // getFileFetchData(const repo::Manifest& manifest, uint64_t segmentId);

  // ndn::Name
  // getFileSelectRepoName(const repo::Manifest& manifest, uint64_t segmentId);

  // void
  // getFileRun();

  // void
  // getFileOnManifest(const ndn::Interest& interest, const ndn::Data& data);

  // void
  // getFileOnManifestTimeout(const ndn::Interest& interest);

  // void
  // getFileOnUnversionedData(const ndn::Interest& interest, const ndn::Data& data);

  // bool 
  // getFileVerifyData(const ndn::Data& data);

  // void
  // getFileReadData(const ndn::Data& data);

  // void
  // getFileFetchNextData();

  // void
  // getFileOnTimeout(const ndn::Interest& interest);

  // void
  // signData(ndn::Data& data, bool useDigestSha256);

  void
  putFilePrepareNextData(uint64_t referenceSegmentNo);

  // void
  // signFirstData(ndn::Data& data);

  void
  putFileStartCheckCommand();

  void
  putFileonCheckCommandResponse(const ndn::Interest& interest, const ndn::Data& data);

  void
  putFileStartInsertCommand();

private:
  ndn::Face m_face;
  ndn::Name repoPrefix;
  ndn::Name m_dataPrefix;
  ndn::Name m_common_name;
  ndn::Name ndnName;
  bool m_verbose;
  bool useDigestSha256;
  bool hasTimeout;
  ndn::time::milliseconds m_interestLifetime;
  ndn::time::milliseconds m_timeout;
  int m_retryCount;
  ndn::time::milliseconds m_freshness_period;

  std::string identityForData;
  std::string identityForCommand;

  size_t blockSize;
  size_t m_currentSegmentNo;
  size_t m_bytes;
  std::istream* insertStream;

  ndn::time::milliseconds freshnessPeriod; 
  ndn::time::milliseconds m_checkPeriod;
  ndn::time::milliseconds interestLifetime;
  ndn::time::milliseconds timeout;

  ndn::KeyChain m_keyChain;
  ndn::security::CommandInterestSigner m_cmdSigner;
  bool m_isFinished;

  uint64_t m_processId;
  ndn::Scheduler m_scheduler;
// TODO: remove it
  using DataContainer = std::map<uint64_t, shared_ptr<ndn::Data>>;
  DataContainer m_data;
  std::string m_identity_for_data;
};

}// namespace difs

#endif  // DIFS_HPP