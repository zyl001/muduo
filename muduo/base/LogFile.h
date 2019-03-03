// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGFILE_H
#define MUDUO_BASE_LOGFILE_H

#include <muduo/base/Mutex.h>
#include <muduo/base/Types.h>

#include <memory>

namespace muduo
{

namespace FileUtil
{
class AppendFile;
}

class LogFile : noncopyable
{
 public:
  LogFile(const string& basename,
          off_t rollSize,
          bool threadSafe = true,
          int flushInterval = 3,
          int checkEveryN = 1024);
  ~LogFile();

  void append(const char* logline, int len);
  void flush();
  bool rollFile();

 private:
  void append_unlocked(const char* logline, int len);

  static string getLogFileName(const string& basename, time_t* now);

  const string basename_;//日志文件名自定义部分
  const off_t rollSize_;//单个日志文件大小最大值
  const int flushInterval_;//flush时间间隔
  const int checkEveryN_;//每N条日志检查flush间隔

  int count_;

  std::unique_ptr<MutexLock> mutex_;
  time_t startOfPeriod_;//当前日志文件的具体到天的时间点
  time_t lastRoll_;//上个日志文件的具体到天的时间点
  time_t lastFlush_;//上次flush时间
  std::unique_ptr<FileUtil::AppendFile> file_;
  //每间隔24h生成一个新的日志文件
  const static int kRollPerSeconds_ = 60*60*24;
};

}  // namespace muduo
#endif  // MUDUO_BASE_LOGFILE_H
