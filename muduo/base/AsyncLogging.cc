// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/AsyncLogging.h>
#include <muduo/base/LogFile.h>
#include <muduo/base/Timestamp.h>

#include <stdio.h>

using namespace muduo;

AsyncLogging::AsyncLogging(const string& basename,
                           off_t rollSize,
                           int flushInterval)
  : flushInterval_(flushInterval),
    running_(false),
    basename_(basename),
    rollSize_(rollSize),
    thread_(std::bind(&AsyncLogging::threadFunc, this), "Logging"),
    latch_(1),
    mutex_(),
    cond_(mutex_),
    currentBuffer_(new Buffer),
    nextBuffer_(new Buffer),
    buffers_()
{
  currentBuffer_->bzero();
  nextBuffer_->bzero();
  buffers_.reserve(16);
}

void AsyncLogging::append(const char* logline, int len)
{
  muduo::MutexLockGuard lock(mutex_);
  if (currentBuffer_->avail() > len)
  {
    currentBuffer_->append(logline, len);
  }
  else
  {
    buffers_.push_back(std::move(currentBuffer_));

    if (nextBuffer_)
    {
      currentBuffer_ = std::move(nextBuffer_);
    }
    else
    {
      //HACK何时nextBuffer_为NULL
      //前端写的极快，调用该函数时后端线程还在执行临界区外代码
      currentBuffer_.reset(new Buffer); // Rarely happens
    }
    //此处对于绝大部分场景应该都不用缓冲区可用大小检查,len大于新缓冲区整体大小的情况应该不允许出现
    currentBuffer_->append(logline, len);
    cond_.notify();//当前缓冲区满时才会主动唤醒
  }
}

void AsyncLogging::threadFunc()
{
  assert(running_ == true);
  latch_.countDown();
  LogFile output(basename_, rollSize_, false);
  BufferPtr newBuffer1(new Buffer);
  BufferPtr newBuffer2(new Buffer);
  newBuffer1->bzero();
  newBuffer2->bzero();
  BufferVector buffersToWrite;
  buffersToWrite.reserve(16);
  while (running_)
  {
    assert(newBuffer1 && newBuffer1->length() == 0);
    assert(newBuffer2 && newBuffer2->length() == 0);
    assert(buffersToWrite.empty());

    {
      muduo::MutexLockGuard lock(mutex_);
      if (buffers_.empty())  // unusual usage!
      {
        cond_.waitForSeconds(flushInterval_);
      }
      //HACK 条件变量超时返回，此时currentBuffer_有可能为空
      //buffers_此时有可能也为空；虽然对于一个系统3s不产生日志很少见;这种情形也可以优化
      buffers_.push_back(std::move(currentBuffer_));
      currentBuffer_ = std::move(newBuffer1);
      //buffers_交换,减小加锁范围
      buffersToWrite.swap(buffers_);
      if (!nextBuffer_)
      {
        nextBuffer_ = std::move(newBuffer2);
      }
    }

    //临界区之外，实际写文件代码
    assert(!buffersToWrite.empty());

    //缓冲区flushInterval_时间间隔内大于100M,写的过于频繁,上层应用有可能有问题,正常写硬盘速度已跟不上
    //HACK buffers_什么时候size会大于4,没看到何处new,只有std::move来来去去
    //实际大部分场景,最多同时存在4个缓冲区而已
    if (buffersToWrite.size() > 25)
    {
      char buf[256];
      snprintf(buf, sizeof buf, "Dropped log messages at %s, %zd larger buffers\n",
               Timestamp::now().toFormattedString().c_str(),
               buffersToWrite.size()-2);
      fputs(buf, stderr);
      output.append(buf, static_cast<int>(strlen(buf)));//日志中记录日志丢弃信息
      buffersToWrite.erase(buffersToWrite.begin()+2, buffersToWrite.end());//隐含要求buffers_的size最小是2
    }

    for (const auto& buffer : buffersToWrite)
    {
      // FIXME: use unbuffered stdio FILE ? or use ::writev ?
      output.append(buffer->data(), buffer->length());
    }

    if (buffersToWrite.size() > 2)
    {
      // drop non-bzero-ed buffers, avoid trashing
      buffersToWrite.resize(2);
    }

    if (!newBuffer1)
    {
      assert(!buffersToWrite.empty());
      newBuffer1 = std::move(buffersToWrite.back());
      buffersToWrite.pop_back();
      newBuffer1->reset();
    }

    if (!newBuffer2)
    {
      assert(!buffersToWrite.empty());
      newBuffer2 = std::move(buffersToWrite.back());
      buffersToWrite.pop_back();
      newBuffer2->reset();
    }

    buffersToWrite.clear();
    output.flush();
  }
  output.flush();
}

