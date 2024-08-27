#ifndef RAFT_H
#define RAFT_H

//#ifndef/#define/#endif 防止该头文件被重复引用 （防止代码块重复引用）
//“被重复引用”是指一个头文件在同一个cpp文件中被include了多次，这种错误常常是由于include嵌套造成的。
//#pragma once指令，在想要保护的文件开头写入（由编译器提供保证，非标准的预处理器指令）

//一般来说头文件提供接口，源文件提供实现
//#include< >格式：引用标准库头文件，编译器从标准库目录开始搜索（包括编译器设置的路径）
//#include" "格式：引用非标准库的头文件，编译器从用户的工作目录开始搜索(如果没有找到，再到系统路径下查找)
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ApplyMsg.h"
#include "Persister.h"
#include "boost/any.hpp"
#include "boost/serialization/serialization.hpp"
#include "config.h"
#include "monsoon.h"
#include "raftRpcUtil.h"
#include "util.h"
/// @brief //////////// 网络状态表示  todo：可以在rpc中删除该字段，实际生产中是用不到的.
constexpr int Disconnected =
    0;  // 方便网络分区的时候debug，网络异常的时候为disconnected，只要网络正常就为AppNormal，防止matchIndex[]数组异常减小
constexpr int AppNormal = 1;

///////////////投票状态

constexpr int Killed = 0;
constexpr int Voted = 1;   //本轮已经投过票了
constexpr int Expire = 2;  //投票（消息、竞选者）过期
constexpr int Normal = 3;

class Raft : public raftRpcProctoc::raftRpc {
 private:
  std::mutex m_mtx;
  std::vector<std::shared_ptr<RaftRpcUtil>> m_peers;  //需要与其他raft节点通信，这里保存与其他结点通信的rpc入口
  std::shared_ptr<Persister> m_persister;  //持久化层，负责raft数据的持久化
  int m_me;  //raft是以集群启动，这个用来标识自己的的编号
  int m_currentTerm;  //记录当前的term
  int m_votedFor;  //记录当前term给谁投票过
  std::vector<raftRpcProctoc::LogEntry> m_logs;  // 日志条目数组，包含了状态机要执行的指令集，以及收到领导时的任期号
  // 这两个状态所有结点都在维护，易失
  int m_commitIndex;
  int m_lastApplied;  // 已经汇报给状态机（上层应用）的log 的index

  // 这两个状态是由服务器来维护，易失
  std::vector<int> m_nextIndex;  // 这两个状态的下标1开始，因为通常commitIndex和lastApplied从0开始，应该是一个无效的index，因此下标从1开始
  std::vector<int> m_matchIndex;
  enum Status { Follower, Candidate, Leader };  //枚举, 默认是0,1,2
  // 保存当前身份
  Status m_status;

  std::shared_ptr<LockQueue<ApplyMsg>> applyChan;  // client从这里取日志（2B），client与raft通信的接口
  // ApplyMsgQueue chan ApplyMsg // raft内部使用的chan，applyChan是用于和服务层交互，最后好像没用上

  // 选举超时
  std::chrono::_V2::system_clock::time_point m_lastResetElectionTime;
  // 心跳超时，用于leader
  std::chrono::_V2::system_clock::time_point m_lastResetHearBeatTime;

  // 2D中用于传入快照点
  // 储存了快照中的最后一个日志的Index和Term
  int m_lastSnapshotIncludeIndex;
  int m_lastSnapshotIncludeTerm;

  // 协程
  std::unique_ptr<monsoon::IOManager> m_ioManager = nullptr;

 public:
  void AppendEntries1(const raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply);  //日志同步 + 心跳 rpc(重点函数)
  void applierTicker();  //定期向状态机写入日志，重点函数
  bool CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot);  //快照相关，非重点
  void doElection();  //发起选举
  /**
   * \brief 发起心跳，只有leader才需要发起心跳
   */
  void doHeartBeat();   //leader定时发起心跳
  // 每隔一段时间检查睡眠时间内有没有重置定时器，没有则说明超时了
  // 如果有则设置合适睡眠时间：睡眠到重置时间+超时时间
  void electionTimeOutTicker();  //监控是否该发起选举了(重点函数)
  std::vector<ApplyMsg> getApplyLogs();
  int getNewCommandIndex();
  void getPrevLogInfo(int server, int *preIndex, int *preTerm);
  void GetState(int *term, bool *isLeader);  //看当前节点是否是leader
  void InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args,
                       raftRpcProctoc::InstallSnapshotResponse *reply);
  void leaderHearBeatTicker();  //检查是否需要发起心跳（leader）(重点函数)
  void leaderSendSnapShot(int server);  
  void leaderUpdateCommitIndex();   //leader更新commitIndex
  bool matchLog(int logIndex, int logTerm);  //对应Index的日志是否匹配，只需要Index和Term就可以知道是否匹配
  void persist();  //持久化(重点函数)
  //变成candidate之后需要让其他结点给自己投票(重点函数)
  void RequestVote(const raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *reply);  
  bool UpToDate(int index, int term);   //判断当前节点是否含有最新的日志
  int getLastLogIndex();
  int getLastLogTerm();
  void getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm);
  int getLogTermFromLogIndex(int logIndex);
  int GetRaftStateSize();
  int getSlicesIndexFromLogIndex(int logIndex);  //设计快照之后logIndex不能与在日志中的数组下标相等了，根据logIndex找到其在日志数组中的位置

  // 请求其他结点的投票(重点函数)
  bool sendRequestVote(int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,
                       std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum);
  //Leader发送心跳后，对心跳的回复进行对应的处理(重点函数)
  bool sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                         std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums);

  // rf.applyChan <- msg //不拿锁执行  可以单独创建一个线程执行，但是为了同意使用std:thread
  // ，避免使用pthread_create，因此专门写一个函数来执行
  void pushMsgToKvServer(ApplyMsg msg);  //给上层的kvserver层发送消息
  void readPersist(std::string data);
  std::string persistData();

  void Start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader);  // 发布发来一个新日志

  // Snapshot the service says it has created a snapshot that has
  // all info up to and including index. this means the
  // service no longer needs the log through (and including)
  // that index. Raft should now trim its log as much as possible.
  // index代表是快照apply应用的index,而snapshot代表的是上层service传来的快照字节流，包括了Index之前的数据
  // 这个函数的目的是把安装到快照里的日志抛弃，并安装快照数据，同时更新快照下标，属于peers自身主动更新，与leader发送快照不冲突
  // 即kv-server主动发起，请求raft（持久层）保存snapshot里面的数据，index是用来表示snapshot快照执行到了哪条命令
  void Snapshot(int index, std::string snapshot);

 public:
  // 重写基类方法,因为rpc远程调用真正调用的是这个方法
  //序列化，反序列化等操作rpc框架都已经做完了，因此这里只需要获取值然后真正调用本地方法即可。
  void AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProctoc::AppendEntriesArgs *request,
                     ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) override;
  void InstallSnapshot(google::protobuf::RpcController *controller,
                       const ::raftRpcProctoc::InstallSnapshotRequest *request,
                       ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done) override;
  void RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProctoc::RequestVoteArgs *request,
                   ::raftRpcProctoc::RequestVoteReply *response, ::google::protobuf::Closure *done) override;

 public:
  void init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me, std::shared_ptr<Persister> persister,
            std::shared_ptr<LockQueue<ApplyMsg>> applyCh);  //初始化(关注)

 private:
  // for persist

  class BoostPersistRaftNode {
   public:
    friend class boost::serialization::access;
    // When the class Archive corresponds to an output archive, the
    // & operator is defined similar to <<.  Likewise, when the class Archive
    // is a type of input archive the & operator is defined similar to >>.
    template <class Archive>
    void serialize(Archive &ar, const unsigned int version) {
      ar &m_currentTerm;
      ar &m_votedFor;
      ar &m_lastSnapshotIncludeIndex;
      ar &m_lastSnapshotIncludeTerm;
      ar &m_logs;
    }
    int m_currentTerm;
    int m_votedFor;
    int m_lastSnapshotIncludeIndex;
    int m_lastSnapshotIncludeTerm;
    std::vector<std::string> m_logs;
    std::unordered_map<std::string, int> umap;

   public:
  };
};

#endif  // RAFT_H